from __future__ import annotations

import asyncio
import logging
import unittest
from unittest import mock

from tiktools_connection import (
    TikToolsConnection,
    TikToolsConnectionError,
    _ws_event_to_canonical,
    classify_tiktools_error,
)


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
            # `open()` ahora espera el handshake: si la sesion muere antes de
            # confirmar la sala, el error sale de open() y no queda diferido.
            try:
                await connection.open()
            except TikToolsConnectionError as exc:
                raised = exc
            else:
                with self.assertRaises(TikToolsConnectionError) as context:
                    await connection.wait_closed()
                raised = context.exception

        self.assertEqual(raised.code, "API_SESSION_ENDED")
        self.assertIn("4429", raised.raw_error)

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
            try:
                await connection.open()
            except TikToolsConnectionError as exc:
                raised = exc
            else:
                with self.assertRaises(TikToolsConnectionError) as context:
                    await connection.wait_closed()
                raised = context.exception

        self.assertEqual(raised.code, "NOT_LIVE")

    async def test_connection_is_not_reported_connected_before_room_handshake(self) -> None:
        """Sin evento roomInfo la sesion sigue en CONNECTING, no en CONNECTED."""

        class SilentSocket(ClosedSocket):
            close_code = None
            close_reason = ""

            def __init__(self) -> None:
                self._release = asyncio.Event()

            async def __anext__(self):
                await self._release.wait()
                raise StopAsyncIteration

        socket_holder: dict[str, SilentSocket] = {}

        async def connect_silent_socket(*_args, **_kwargs) -> SilentSocket:
            socket_holder["socket"] = SilentSocket()
            return socket_holder["socket"]

        states: list[str] = []

        async def status_callback(status) -> None:
            states.append(status.connection_state.value)

        connection = TikToolsConnection(
            logger=logging.getLogger("test.tiktools_connection.handshake"),
            legacy_bridge_root="",
            connect_timeout_sec=1,
            event_callback=noop_callback,
            status_callback=status_callback,
            target_user="alice",
            api_key="test-key",
            handshake_timeout_sec=1,
        )

        with mock.patch("tiktools_connection.websockets.connect", side_effect=connect_silent_socket):
            handshake = await asyncio.wait_for(connection.open(), timeout=20)

        self.assertFalse(handshake is True)
        self.assertFalse(connection.handshake_complete)
        self.assertNotIn("connected", states)
        self.assertIn("connecting", states)


class GiftDiamondTotalTests(unittest.TestCase):
    """B8 — tik.tools documenta diamondCount como precio UNITARIO.

    El total del evento es diamondCount * repeatCount. El mapper canonicamente
    debe entregar diamond_count = TOTAL acreditable en este frame, y no debe
    acreditar dos veces el frame final duplicado de un combo (repeatEnd).
    Fuente: https://tik.tools/websocket — "final total = diamondCount * repeatCount".
    """

    @staticmethod
    def _gift_raw(*, repeat_count: int, diamond_count: int, repeat_end: bool,
                  group_id: str = "g-1") -> dict:
        return {
            "event": "gift",
            "data": {
                "user": {"id": "u-1", "uniqueId": "fan"},
                "giftId": 5655,
                "giftName": "Rose",
                "repeatCount": repeat_count,
                "diamondCount": diamond_count,
                "repeatEnd": repeat_end,
                "groupId": group_id,
            },
        }

    def _map(self, raw: dict, state: dict) -> object:
        return _ws_event_to_canonical(
            raw, room_id="r-1", session_id=1, target_user="alice", streak_state=state
        )

    def test_batch_x10_reports_total_not_unit_price(self) -> None:
        state: dict = {}
        event = self._map(
            self._gift_raw(repeat_count=10, diamond_count=1, repeat_end=True), state
        )
        self.assertIsNotNone(event)
        assert event is not None and event.gift is not None
        self.assertEqual(event.gift.quantity, 10)
        # Unit(1) x 10 = 10 totales; hoy devuelve 1 (unit solo) -> RED.
        self.assertEqual(event.gift.diamond_count, 10)

    def test_streak_frames_accumulate_without_double_count(self) -> None:
        state: dict = {}
        credits = []
        frames = [(1, False), (2, False), (3, True), (3, True)]
        for repeat_count, repeat_end in frames:
            event = self._map(
                self._gift_raw(
                    repeat_count=repeat_count, diamond_count=1, repeat_end=repeat_end
                ),
                state,
            )
            if event is not None and event.gift is not None:
                credits.append(event.gift.diamond_count)
        # 3 incrementos de 1 diamante = [1, 1, 1]; el frame final duplicado
        # (repeatEnd sin avance) no emite. Hoy: [1, 1, 1, 1] -> RED.
        self.assertEqual(credits, [1, 1, 1])
        self.assertEqual(sum(credits), 3)

    def test_single_streakable_gift_counted_once(self) -> None:
        # giftType 1 entrega SIEMPRE dos frames: (rC=1, end=false) y
        # (rC=1, end=true). Debe acreditar solo 1 diamante en total.
        state: dict = {}
        first = self._map(
            self._gift_raw(repeat_count=1, diamond_count=5, repeat_end=False), state
        )
        final = self._map(
            self._gift_raw(repeat_count=1, diamond_count=5, repeat_end=True), state
        )
        self.assertIsNotNone(first)
        assert first is not None and first.gift is not None
        self.assertEqual(first.gift.diamond_count, 5)
        # Frame final duplicado sin avance: no emitir (evita el doble cobro
        # que provocaria el fallback quantity en el timer).
        self.assertIsNone(final)

    def test_batch_without_streak_state_param_still_maps(self) -> None:
        # Compat: sin streak_state (llamadas legacy) sigue mapeando el total.
        event = _ws_event_to_canonical(
            self._gift_raw(repeat_count=4, diamond_count=2, repeat_end=True),
            room_id="r-1",
            session_id=1,
            target_user="alice",
        )
        self.assertIsNotNone(event)
        assert event is not None and event.gift is not None
        self.assertEqual(event.gift.diamond_count, 8)


if __name__ == "__main__":
    unittest.main()
