from __future__ import annotations

import asyncio
import unittest
from unittest import mock

from bridge_config import BridgeConfig
from bridge_client import build_chat_event
from connection_manager import ConnectionManager, compute_retry_delay
from error_catalog import action_for, classify_close_code
from event_decoder import decode_canonical_event
from metrics_registry import MetricsRegistry
from structured_logging import configure_logger
from tiktools_connection import TikToolsConnectionError


async def accepted_event(_event) -> bool:
    return True


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
            raise TikToolsConnectionError("USER_NOT_FOUND", "No se encontro ese usuario en TikTok.")
        if type(self).fail_first and type(self).attempts == 1:
            raise TikToolsConnectionError("NETWORK_ERROR", "Fallo temporal de red.")

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


def bridge_config_with_api_key() -> BridgeConfig:
    """Config del provider tik.tools con credencial: el manager exige una key.

    Sin key, `ConnectionManager.run` corta en INVALID_API_KEY antes de abrir la
    sesion, que es justamente lo que estos tests no quieren medir.
    """
    config = BridgeConfig()
    config.connection.api_key = "test-key"
    return config


class ConnectionManagerTests(unittest.IsolatedAsyncioTestCase):
    async def test_reconnects_after_transient_failure(self) -> None:
        FakeConnection.attempts = 0
        FakeConnection.fail_first = True
        FakeConnection.user_not_found = False
        FakeConnection.hang_until_closed = False

        config = bridge_config_with_api_key()
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

        with mock.patch("connection_manager.TikToolsConnection", FakeConnection), mock.patch(
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
            config=bridge_config_with_api_key(),
            logger=configure_logger(name="livepanel.bridge.test.connection.not_found", log_path="tools/bridge_py/logs/test_connection_not_found.jsonl"),
            metrics=metrics,
            event_callback=event_callback,
            status_callback=status_callback,
        )

        with mock.patch("connection_manager.TikToolsConnection", FakeConnection):
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
            config=bridge_config_with_api_key(),
            logger=configure_logger(name="livepanel.bridge.test.connection.max_seconds", log_path="tools/bridge_py/logs/test_connection_max_seconds.jsonl"),
            metrics=metrics,
            event_callback=event_callback,
            status_callback=status_callback,
        )

        with mock.patch("connection_manager.TikToolsConnection", FakeConnection):
            exit_code = await manager.run(target_user="alice", max_events=0, max_seconds=1)

        self.assertEqual(exit_code, 0)
        self.assertEqual(FakeConnection.attempts, 1)
        self.assertIn("max_seconds reached (1)", messages)

    async def test_not_live_keeps_waiting_instead_of_failing_permanently(self) -> None:
        """4404 (cuenta no en vivo) debe esperar y reintentar, no abandonar."""

        class NotLiveConnection(FakeConnection):
            async def open(self) -> None:
                type(self).attempts += 1
                raise TikToolsConnectionError("NOT_LIVE", "La cuenta no esta en vivo todavia.")

        NotLiveConnection.attempts = 0
        config = bridge_config_with_api_key()
        config.retry_policy.max_attempts = 2
        config.retry_policy.not_live_delay_sec = 1
        config.retry_policy.waiting_for_live_max_minutes = 0  # sin limite

        messages: list[str] = []
        phases: list[str] = []

        async def status_callback(status) -> None:
            messages.append(status.message)
            phases.append(status.phase)

        manager = ConnectionManager(
            config=config,
            logger=configure_logger(
                name="livepanel.bridge.test.connection.not_live",
                log_path="tools/bridge_py/logs/test_connection_not_live.jsonl",
            ),
            metrics=MetricsRegistry(),
            event_callback=accepted_event,
            status_callback=status_callback,
        )

        loop = asyncio.get_running_loop()
        with mock.patch("connection_manager.TikToolsConnection", NotLiveConnection):
            run_task = asyncio.create_task(manager.run(target_user="alice", max_events=1))
            deadline = loop.time() + 12
            while NotLiveConnection.attempts < 3 and loop.time() < deadline:
                await asyncio.sleep(0.05)
            manager.stop()
            await asyncio.wait_for(run_task, timeout=10)

        # Supero max_attempts (2) porque esperar el vivo no consume ese
        # presupuesto, y el panel recibe la fase "waiting".
        self.assertGreaterEqual(NotLiveConnection.attempts, 3)
        self.assertIn("waiting", phases)
        self.assertTrue(any("empiece el vivo" in message for message in messages))

    async def test_quota_exhausted_is_reported_as_key_rotation(self) -> None:
        config = bridge_config_with_api_key()
        delay = compute_retry_delay(config, "API_SESSION_ENDED", 0)

        # La cuota agotada es reintentable (con otra key) y nunca cae en NOT_LIVE.
        self.assertIsNotNone(delay)
        self.assertEqual(action_for("API_SESSION_ENDED"), "rotate_key")
        self.assertEqual(action_for("NOT_LIVE"), "wait_for_live")

    async def test_daily_demo_limit_close_code_is_quota_not_not_live(self) -> None:
        self.assertEqual(classify_close_code(4555, "Daily Demo Limit Reached"), "API_SESSION_ENDED")
        self.assertEqual(classify_close_code(4429, "Daily Demo Limit Reached"), "API_SESSION_ENDED")
        self.assertEqual(classify_close_code(4404, "Creator is not currently live"), "NOT_LIVE")
        self.assertEqual(classify_close_code(4556, "Relay connection error"), "RELAY_ERROR")
        self.assertEqual(classify_close_code(1012, "service restart"), "SERVER_RESTART")

    async def test_late_room_confirmation_is_declared_as_connected(self) -> None:
        """Si la sala se confirma despues del timeout, el panel debe ver 'connected'."""

        class LateHandshakeConnection(FakeConnection):
            def __init__(self, **kwargs) -> None:
                super().__init__(**kwargs)
                self.room_id = "room-late"
                self.handshake_complete = False

            async def open(self) -> None:
                type(self).attempts += 1
                # open() vuelve sin handshake: la sala todavia no esta confirmada.
                return

            async def wait_closed(self) -> None:
                # La sesion sigue abierta; la sala se confirma unos instantes
                # despues (como el relay de tik.tools).
                async def confirm_room_later() -> None:
                    await asyncio.sleep(0.3)
                    self.handshake_complete = True

                flipper = asyncio.create_task(confirm_room_later())
                try:
                    await self._closed_event.wait()
                finally:
                    flipper.cancel()

        LateHandshakeConnection.attempts = 0
        config = bridge_config_with_api_key()
        statuses: list[tuple[str, str]] = []

        async def status_callback(status) -> None:
            statuses.append((status.connection_state.value, status.phase))

        manager = ConnectionManager(
            config=config,
            logger=configure_logger(
                name="livepanel.bridge.test.connection.late_handshake",
                log_path="tools/bridge_py/logs/test_connection_late_handshake.jsonl",
            ),
            metrics=MetricsRegistry(),
            event_callback=accepted_event,
            status_callback=status_callback,
        )

        loop = asyncio.get_running_loop()
        with mock.patch("connection_manager.TikToolsConnection", LateHandshakeConnection):
            run_task = asyncio.create_task(manager.run(target_user="alice", max_events=1))
            deadline = loop.time() + 10
            while not any(state == "connected" for state, _ in statuses) and loop.time() < deadline:
                await asyncio.sleep(0.05)
            manager.stop()
            await asyncio.wait_for(run_task, timeout=10)

        self.assertIn("connecting", [state for state, _ in statuses])
        self.assertIn(("connected", "connected"), statuses)

    async def test_selects_direct_tiktoklive_provider(self) -> None:
        FakeConnection.attempts = 0
        FakeConnection.fail_first = False
        FakeConnection.user_not_found = False
        FakeConnection.hang_until_closed = False
        config = bridge_config_with_api_key()
        config.connection_mode = "direct"

        async def event_callback(_event) -> bool:
            return True

        async def status_callback(_status) -> None:
            return None

        manager = ConnectionManager(
            config=config,
            logger=configure_logger(name="livepanel.bridge.test.connection.direct", log_path="tools/bridge_py/logs/test_connection_direct.jsonl"),
            metrics=MetricsRegistry(),
            event_callback=event_callback,
            status_callback=status_callback,
        )

        with mock.patch("connection_manager.TikTokConnection", FakeConnection):
            exit_code = await manager.run(target_user="alice", max_events=1)

        self.assertEqual(exit_code, 0)
        self.assertEqual(FakeConnection.attempts, 1)


if __name__ == "__main__":
    unittest.main()
