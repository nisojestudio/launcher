from __future__ import annotations

import asyncio
import logging
import unittest
from unittest import mock

from tiktools_connection import TikToolsConnection, TikToolsConnectionError, classify_tiktools_error


async def noop_callback(_value: object) -> None:
    return None


class ClosedSocket:
    close_code = 4429
    close_reason = "Demo session ended"

    def __aiter__(self):
        return self

    async def __anext__(self):
        raise StopAsyncIteration

    async def close(self, **_kwargs) -> None:
        return None


class LiveEndSocket(ClosedSocket):
    close_code = 1000
    close_reason = "normal"

    def __init__(self) -> None:
        self._messages = iter(['{"event":"end","data":{"message":"live ended"}}'])

    async def __anext__(self):
        try:
            return next(self._messages)
        except StopIteration:
            raise StopAsyncIteration


async def connect_closed_socket(*_args, **_kwargs) -> ClosedSocket:
    return ClosedSocket()


async def connect_live_end_socket(*_args, **_kwargs) -> LiveEndSocket:
    return LiveEndSocket()


class TikToolsConnectionTests(unittest.IsolatedAsyncioTestCase):
    def test_demo_session_close_is_not_reported_as_generic_network_error(self) -> None:
        code, _message = classify_tiktools_error("received 4429 Demo session ended")
        self.assertEqual(code, "API_SESSION_ENDED")

    async def test_close_code_4429_preserves_the_provider_diagnostic(self) -> None:
        connection = TikToolsConnection(
            logger=logging.getLogger("test.tiktools_connection"),
            legacy_bridge_root="",
            connect_timeout_sec=5,
            event_callback=noop_callback,
            status_callback=noop_callback,
            target_user="alice",
            api_key="test-key",
        )
        with mock.patch("tiktools_connection.websockets.connect", side_effect=connect_closed_socket):
            await connection.open()
            with self.assertRaises(TikToolsConnectionError) as raised:
                await connection.wait_closed()

        self.assertEqual(raised.exception.code, "API_SESSION_ENDED")
        self.assertIn("4429", raised.exception.raw_error)

    async def test_live_end_event_is_classified_as_not_live(self) -> None:
        connection = TikToolsConnection(
            logger=logging.getLogger("test.tiktools_connection"),
            legacy_bridge_root="",
            connect_timeout_sec=5,
            event_callback=noop_callback,
            status_callback=noop_callback,
            target_user="alice",
            api_key="test-key",
        )
        with mock.patch("tiktools_connection.websockets.connect", side_effect=connect_live_end_socket):
            await connection.open()
            with self.assertRaises(TikToolsConnectionError) as raised:
                await connection.wait_closed()

        self.assertEqual(raised.exception.code, "NOT_LIVE")


if __name__ == "__main__":
    unittest.main()
