from __future__ import annotations

import asyncio
import time
import unittest

from event_models import ConnectionState, SessionStatus
from session_manager import HeartbeatMonitor


def make_monitor() -> HeartbeatMonitor:
    """Latido rapido: intervalo minimo y silencio declarado a los 2 segundos."""
    return HeartbeatMonitor(
        warning_after_sec=60.0,
        interval_sec=1.0,
        silence_timeout_sec=2.0,
    )


class HeartbeatPhaseHonestyTests(unittest.IsolatedAsyncioTestCase):
    async def _collect_statuses(
        self,
        monitor: HeartbeatMonitor,
        *,
        phase_provider,
        expected: int,
    ) -> list[SessionStatus]:
        statuses: list[SessionStatus] = []
        stop_event = asyncio.Event()

        async def callback(status: SessionStatus) -> None:
            statuses.append(status)
            if len(statuses) >= expected:
                stop_event.set()

        await monitor.run(
            target_user="tester",
            room_id_provider=lambda: "room-1",
            status_callback=callback,
            stop_event=stop_event,
            phase_provider=phase_provider,
        )
        return statuses

    async def test_silence_emits_error_phase_not_connected(self) -> None:
        """Al declarar DISCONNECTED por silencio la fase debe ser 'error'.

        phase_provider() trae "connected" congelado en connection_manager
        (current_phase): arrastrarlo era el bug que dejaba la franja en verde.
        """
        monitor = make_monitor()
        monitor.set_state(ConnectionState.CONNECTED)
        # Silencio de 30s: el monitor lo declara caido en el primer tick.
        monitor._last_event_monotonic = time.monotonic() - 30

        statuses = await self._collect_statuses(
            monitor,
            phase_provider=lambda: "connected",
            expected=1,
        )

        self.assertEqual(len(statuses), 1)
        self.assertEqual(statuses[0].connection_state, ConnectionState.DISCONNECTED)
        self.assertEqual(statuses[0].phase, "error")
        self.assertEqual(statuses[0].severity, "error")
        self.assertIn("sin eventos", statuses[0].message.lower())

    async def test_phase_stays_error_on_later_ticks(self) -> None:
        """Mientras siga declarada caida, ningun tick posterior vuelve a verde."""
        monitor = make_monitor()
        monitor.set_state(ConnectionState.CONNECTED)
        monitor._last_event_monotonic = time.monotonic() - 30

        statuses = await self._collect_statuses(
            monitor,
            phase_provider=lambda: "connected",
            expected=2,
        )

        self.assertEqual(len(statuses), 2)
        self.assertEqual([status.phase for status in statuses], ["error", "error"])
        self.assertEqual(
            [status.connection_state for status in statuses],
            [ConnectionState.DISCONNECTED, ConnectionState.DISCONNECTED],
        )

    async def test_waiting_phase_is_not_overridden(self) -> None:
        """Sin silencio declarado, la fase sigue viniendo del connection_manager."""
        monitor = make_monitor()
        monitor.set_state(ConnectionState.RECONNECTING, retry_count=1)

        statuses = await self._collect_statuses(
            monitor,
            phase_provider=lambda: "waiting",
            expected=1,
        )

        self.assertEqual(len(statuses), 1)
        self.assertEqual(statuses[0].phase, "waiting")
        self.assertNotEqual(statuses[0].phase, monitor._phase_for_state())

    async def test_connected_heartbeat_uses_phase_provider(self) -> None:
        """Sesion viva sin silencio: el latido respeta la fase del manager."""
        monitor = make_monitor()
        monitor.set_state(ConnectionState.CONNECTED)

        statuses = await self._collect_statuses(
            monitor,
            phase_provider=lambda: "connected",
            expected=1,
        )

        self.assertEqual(len(statuses), 1)
        self.assertEqual(statuses[0].connection_state, ConnectionState.CONNECTED)
        self.assertEqual(statuses[0].phase, "connected")


class SilenceReconnectTests(unittest.IsolatedAsyncioTestCase):
    """A3: sesion abierta sin eventos -> hay que reconectar, no quedarse quieto."""

    @staticmethod
    def monitor(*, reconnect_sec: float) -> HeartbeatMonitor:
        return HeartbeatMonitor(
            warning_after_sec=60.0,
            interval_sec=1.0,
            silence_timeout_sec=2.0,
            silence_reconnect_sec=reconnect_sec,
        )

    def test_disabled_never_requests_a_reconnect(self) -> None:
        monitor = self.monitor(reconnect_sec=0.0)
        monitor.set_state(ConnectionState.CONNECTED)
        monitor._last_event_monotonic = time.monotonic() - 3600

        self.assertFalse(monitor.reconnect_due)

    def test_not_due_before_the_session_reaches_connected(self) -> None:
        """Esperando la sala del live no hay eventos esperables: no reconecta."""
        monitor = self.monitor(reconnect_sec=5.0)
        monitor.set_state(ConnectionState.CONNECTING)
        monitor._last_event_monotonic = time.monotonic() - 3600

        self.assertFalse(monitor.reconnect_due)

    def test_not_due_below_the_threshold(self) -> None:
        monitor = self.monitor(reconnect_sec=300.0)
        monitor.set_state(ConnectionState.CONNECTED)
        monitor.mark_event()

        self.assertFalse(monitor.reconnect_due)

    def test_due_when_an_open_session_goes_silent(self) -> None:
        monitor = self.monitor(reconnect_sec=300.0)
        monitor.set_state(ConnectionState.CONNECTED)
        monitor._last_event_monotonic = time.monotonic() - 301

        self.assertTrue(monitor.reconnect_due)
        self.assertGreater(monitor.last_event_age_sec, 300.0)

    def test_stays_due_after_the_silence_was_declared(self) -> None:
        """El aviso de silencio (120s) no puede desactivar la reconexion (300s).

        Al declarar, el estado pasa a DISCONNECTED y el connected_since se
        borra: si la condicion dependiera de estar "conectado", el socket a
        medias se quedaria abierto para siempre.
        """
        monitor = self.monitor(reconnect_sec=300.0)
        monitor.set_state(ConnectionState.CONNECTED)
        # Asi queda justo despues de que el latido declare la caida por silencio.
        monitor._silence_declared = True
        monitor._connected_since_monotonic = None
        monitor.set_state(ConnectionState.DISCONNECTED)
        monitor._last_event_monotonic = time.monotonic() - 301

        self.assertTrue(monitor.silence_declared)
        self.assertTrue(monitor.reconnect_due)

    def test_a_new_connection_clears_the_debt(self) -> None:
        monitor = self.monitor(reconnect_sec=300.0)
        monitor.set_state(ConnectionState.CONNECTED)
        monitor._last_event_monotonic = time.monotonic() - 301
        self.assertTrue(monitor.reconnect_due)

        monitor.set_state(ConnectionState.CONNECTED, retry_count=1)

        self.assertFalse(monitor.reconnect_due)
        self.assertFalse(monitor.silence_declared)

    def test_events_alone_clear_the_debt(self) -> None:
        monitor = self.monitor(reconnect_sec=300.0)
        monitor.set_state(ConnectionState.CONNECTED)
        monitor._last_event_monotonic = time.monotonic() - 301
        self.assertTrue(monitor.reconnect_due)

        monitor.mark_event()

        self.assertFalse(monitor.reconnect_due)

    async def test_declared_silence_announces_the_reconnect_countdown(self) -> None:
        """Despues de declarar la caida, el aviso dice que va a reconectar."""
        monitor = self.monitor(reconnect_sec=60.0)
        monitor.set_state(ConnectionState.CONNECTED)
        monitor._last_event_monotonic = time.monotonic() - 30

        statuses = await self._collect_statuses(monitor, expected=2)

        self.assertEqual(len(statuses), 2)
        self.assertIn("se ha perdido", statuses[0].message)
        self.assertIn("Reconectando automaticamente en", statuses[1].message)
        self.assertEqual(statuses[1].severity, "error")
        self.assertEqual(statuses[1].phase, "error")

    async def _collect_statuses(self, monitor: HeartbeatMonitor, *, expected: int) -> list[SessionStatus]:
        statuses: list[SessionStatus] = []
        stop_event = asyncio.Event()

        async def callback(status: SessionStatus) -> None:
            statuses.append(status)
            if len(statuses) >= expected:
                stop_event.set()

        await monitor.run(
            target_user="tester",
            room_id_provider=lambda: "room-1",
            status_callback=callback,
            stop_event=stop_event,
            phase_provider=lambda: "connected",
        )
        return statuses


if __name__ == "__main__":
    unittest.main()
