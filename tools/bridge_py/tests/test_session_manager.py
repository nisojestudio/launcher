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


if __name__ == "__main__":
    unittest.main()
