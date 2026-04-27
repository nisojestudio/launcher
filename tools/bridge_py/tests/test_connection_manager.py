from __future__ import annotations

import asyncio
import unittest
from unittest import mock

from bridge_config import BridgeConfig
from bridge_client import build_chat_event
from connection_manager import ConnectionManager
from event_decoder import decode_canonical_event
from metrics_registry import MetricsRegistry
from structured_logging import configure_logger
from tiktok_connection import TikTokConnectionError


class FakeConnection:
    attempts = 0
    fail_first = False
    user_not_found = False
    hang_until_closed = False

    def __init__(
        self,
        *,
        event_callback,
        status_callback,
        target_user: str,
        room_id: str = "",
        **_kwargs,
    ) -> None:
        self._event_callback = event_callback
        self._status_callback = status_callback
        self.room_id = room_id or "room-fake"
        self._target_user = target_user
        self._wait_task: asyncio.Task[None] | None = None
        self._closed_event = asyncio.Event()

    async def open(self) -> None:
        type(self).attempts += 1
        if type(self).user_not_found:
            raise TikTokConnectionError("USER_NOT_FOUND", "No se encontro ese usuario en TikTok.")
        if type(self).fail_first and type(self).attempts == 1:
            raise TikTokConnectionError("NETWORK_ERROR", "Fallo temporal de red.")

        if type(self).hang_until_closed:
            async def _wait() -> None:
                await self._closed_event.wait()

            self._wait_task = asyncio.create_task(_wait())
            return

        async def _emit() -> None:
            await self._event_callback(
                decode_canonical_event(
                    build_chat_event(
                        user_id="user-01",
                        username=self._target_user,
                        display_name=self._target_user,
                        text="hola",
                        event_id=f"evt-fake-{type(self).attempts}",
                        room_id=self.room_id,
                        timestamp_ms=1710000001000,
                    )
                )
            )

        self._wait_task = asyncio.create_task(_emit())

    async def wait_closed(self) -> None:
        if self._wait_task is not None:
            await self._wait_task

    async def close(self) -> None:
        self._closed_event.set()
        return None


class ConnectionManagerTests(unittest.IsolatedAsyncioTestCase):
    async def test_reconnects_after_transient_failure(self) -> None:
        FakeConnection.attempts = 0
        FakeConnection.fail_first = True
        FakeConnection.user_not_found = False
        FakeConnection.hang_until_closed = False

        config = BridgeConfig()
        config.retry_policy.enabled = True
        config.retry_policy.max_attempts = 3
        metrics = MetricsRegistry()
        received_ids: list[str] = []
        statuses: list[str] = []
        messages: list[str] = []

        async def event_callback(event) -> bool:
            received_ids.append(event.metadata.event_id)
            return True

        async def status_callback(status) -> None:
            statuses.append(status.connection_state.value)
            messages.append(status.message)

        async def fast_sleep(_seconds: float) -> None:
            return None

        manager = ConnectionManager(
            config=config,
            logger=configure_logger(name="livepanel.bridge.test.connection.retry", log_path="tools/bridge_py/logs/test_connection_retry.jsonl"),
            metrics=metrics,
            event_callback=event_callback,
            status_callback=status_callback,
        )

        with mock.patch("connection_manager.TikTokConnection", FakeConnection), mock.patch(
            "connection_manager.asyncio.sleep",
            side_effect=fast_sleep,
        ):
            exit_code = await manager.run(target_user="alice", max_events=1, max_seconds=0)

        self.assertEqual(exit_code, 0)
        self.assertEqual(FakeConnection.attempts, 2)
        self.assertEqual(len(received_ids), 1)
        self.assertIn("reconnecting", statuses)
        self.assertIn("max_events reached (1)", messages)
        self.assertGreaterEqual(metrics.snapshot().counters.get("reconnect_total", 0), 1)

    async def test_stops_without_retry_on_user_not_found(self) -> None:
        FakeConnection.attempts = 0
        FakeConnection.fail_first = False
        FakeConnection.user_not_found = True
        FakeConnection.hang_until_closed = False

        metrics = MetricsRegistry()

        async def event_callback(_event) -> bool:
            return True

        messages: list[str] = []

        async def status_callback(status) -> None:
            messages.append(status.message)

        manager = ConnectionManager(
            config=BridgeConfig(),
            logger=configure_logger(name="livepanel.bridge.test.connection.not_found", log_path="tools/bridge_py/logs/test_connection_not_found.jsonl"),
            metrics=metrics,
            event_callback=event_callback,
            status_callback=status_callback,
        )

        with mock.patch("connection_manager.TikTokConnection", FakeConnection):
            exit_code = await manager.run(target_user="missing-user", max_events=1, max_seconds=0)

        self.assertEqual(exit_code, 1)
        self.assertEqual(FakeConnection.attempts, 1)
        self.assertEqual(metrics.snapshot().counters.get("reconnect_total", 0), 0)

    async def test_max_seconds_stops_even_without_events(self) -> None:
        FakeConnection.attempts = 0
        FakeConnection.fail_first = False
        FakeConnection.user_not_found = False
        FakeConnection.hang_until_closed = True

        metrics = MetricsRegistry()

        async def event_callback(_event) -> bool:
            return True

        messages: list[str] = []

        async def status_callback(status) -> None:
            messages.append(status.message)

        manager = ConnectionManager(
            config=BridgeConfig(),
            logger=configure_logger(name="livepanel.bridge.test.connection.max_seconds", log_path="tools/bridge_py/logs/test_connection_max_seconds.jsonl"),
            metrics=metrics,
            event_callback=event_callback,
            status_callback=status_callback,
        )

        with mock.patch("connection_manager.TikTokConnection", FakeConnection):
            exit_code = await manager.run(target_user="alice", max_events=0, max_seconds=1)

        self.assertEqual(exit_code, 0)
        self.assertEqual(FakeConnection.attempts, 1)
        self.assertIn("max_seconds reached (1)", messages)


if __name__ == "__main__":
    unittest.main()
