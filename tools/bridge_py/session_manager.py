from __future__ import annotations

import asyncio
import time
from typing import Awaitable, Callable

from event_models import ConnectionHealth, ConnectionState, SessionStatus
from structured_logging import utc_now_ms


StatusCallback = Callable[[SessionStatus], Awaitable[None]]


class HeartbeatMonitor:
    def __init__(
        self,
        *,
        warning_after_sec: float,
        interval_sec: float,
        silence_timeout_sec: float = 0.0,
    ) -> None:
        self._warning_after_sec = max(1.0, warning_after_sec)
        self._interval_sec = max(1.0, interval_sec)
        # silence_timeout_sec: after this many seconds without events, declare DISCONNECTED.
        # Default 0 = disabled (use warning_after_sec * 2 as fallback).
        self._silence_timeout_sec = (
            max(2.0, silence_timeout_sec)
            if silence_timeout_sec > 0
            else max(2.0, self._warning_after_sec * 2)
        )
        self._connected_since_monotonic: float | None = None
        self._last_status_monotonic = time.monotonic()
        self._last_event_monotonic = time.monotonic()
        self._last_error = ""
        self._retry_count = 0
        self._connection_state = ConnectionState.IDLE
        self._silence_declared = False  # True once we've emitted DISCONNECTED due to silence

    def set_state(self, state: ConnectionState, *, retry_count: int = 0, error: str = "") -> None:
        self._connection_state = state
        self._retry_count = retry_count
        self._last_status_monotonic = time.monotonic()
        if error:
            self._last_error = error
        if state == ConnectionState.CONNECTED:
            self._connected_since_monotonic = time.monotonic()
            self._last_event_monotonic = self._connected_since_monotonic
            self._silence_declared = False  # reset on new connection
        elif state in (ConnectionState.DISCONNECTED, ConnectionState.FAULTED, ConnectionState.STOPPED):
            self._connected_since_monotonic = None

    def mark_event(self) -> None:
        self._last_event_monotonic = time.monotonic()

    def snapshot(self) -> ConnectionHealth:
        now = time.monotonic()
        connected_since_ms = 0
        if self._connected_since_monotonic is not None:
            connected_since_ms = int((now - self._connected_since_monotonic) * 1000)
        return ConnectionHealth(
            connected=self._connection_state == ConnectionState.CONNECTED,
            connection_state=self._connection_state.value,
            heartbeat_age_ms=int((now - self._last_status_monotonic) * 1000),
            last_event_age_ms=int((now - self._last_event_monotonic) * 1000),
            retry_count=self._retry_count,
            connected_since_ms=connected_since_ms,
            last_error=self._last_error,
        )

    async def run(
        self,
        *,
        target_user: str,
        room_id_provider: Callable[[], str],
        status_callback: StatusCallback,
        stop_event: asyncio.Event,
        phase_provider: Callable[[], str] | None = None,
    ) -> None:
        while not stop_event.is_set():
            snapshot = self.snapshot()
            now = time.monotonic()

            # Detect silence: connected but no events for too long
            silence_detected = (
                self._connection_state == ConnectionState.CONNECTED
                and not self._silence_declared
                and snapshot.last_event_age_ms > int(self._silence_timeout_sec * 1000)
            )

            if silence_detected:
                # Declare disconnected due to silence
                self._silence_declared = True
                self._connection_state = ConnectionState.DISCONNECTED
                self._connected_since_monotonic = None
                message = (
                    f"DESCONECTADO: sin eventos durante {snapshot.last_event_age_ms // 1000}s. "
                    f"La conexion con TikTok se ha perdido."
                )
                severity = "error"
            elif snapshot.last_event_age_ms > int(self._warning_after_sec * 1000):
                seconds_idle = snapshot.last_event_age_ms // 1000
                message = f"Advertencia: sin eventos durante {seconds_idle}s. Verificando conexion..."
                severity = "warn"
            else:
                # Latido normal: el mensaje describe el estado real para que el
                # monitor no muestre una palabra tecnica suelta.
                if self._connection_state == ConnectionState.CONNECTED:
                    message = "Escuchando el live de TikTok."
                elif self._connection_state in (
                    ConnectionState.CONNECTING,
                    ConnectionState.PREPARING,
                    ConnectionState.RECONNECTING,
                ):
                    message = "Conectando con TikTok..."
                else:
                    message = "Esperando para reconectar con TikTok..."
                severity = "info"

            await status_callback(
                SessionStatus(
                    target_user=target_user,
                    connection_state=self._connection_state,
                    room_id=room_id_provider(),
                    message=message,
                    timestamp_ms=utc_now_ms(),
                    retry_count=snapshot.retry_count,
                    uptime_ms=snapshot.connected_since_ms,
                    last_event_timestamp_ms=0,
                    severity=severity,
                    # La fase la decide el connection_manager: si esta esperando
                    # el vivo, el latido no debe decir "conectando".
                    phase=(phase_provider() if phase_provider is not None else self._phase_for_state()),
                )
            )
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=self._interval_sec)
            except asyncio.TimeoutError:
                continue

    def _phase_for_state(self) -> str:
        """Fase del monitor del live derivada del estado de conexion."""
        if self._connection_state == ConnectionState.CONNECTED:
            return "connected"
        if self._connection_state in (
            ConnectionState.CONNECTING,
            ConnectionState.PREPARING,
            ConnectionState.RECONNECTING,
        ):
            return "connecting"
        if self._connection_state == ConnectionState.DISCONNECTED:
            return "error"
        return "starting"


class SessionSupervisor:
    def __init__(self, *, heartbeat_monitor: HeartbeatMonitor) -> None:
        self._heartbeat_monitor = heartbeat_monitor
        self._heartbeat_stop_event = asyncio.Event()
        self._heartbeat_task: asyncio.Task[None] | None = None
        self._stop_requested = False

    @property
    def stop_requested(self) -> bool:
        return self._stop_requested

    def stop(self) -> None:
        self._stop_requested = True
        self._heartbeat_stop_event.set()

    async def start_heartbeat(
        self,
        *,
        target_user: str,
        room_id_provider: Callable[[], str],
        status_callback: StatusCallback,
        phase_provider: Callable[[], str] | None = None,
    ) -> None:
        if self._heartbeat_task is not None:
            return
        self._heartbeat_stop_event = asyncio.Event()
        self._heartbeat_task = asyncio.create_task(
            self._heartbeat_monitor.run(
                target_user=target_user,
                room_id_provider=room_id_provider,
                status_callback=status_callback,
                stop_event=self._heartbeat_stop_event,
                phase_provider=phase_provider,
            ),
            name="bridge-heartbeat",
        )

    async def stop_heartbeat(self) -> None:
        self._heartbeat_stop_event.set()
        if self._heartbeat_task is not None:
            try:
                await self._heartbeat_task
            except asyncio.CancelledError:
                pass
            self._heartbeat_task = None
