from __future__ import annotations

import asyncio
import time
from typing import Awaitable, Callable

from event_models import ConnectionHealth, ConnectionState, SessionStatus
from structured_logging import utc_now_ms


StatusCallback = Callable[[SessionStatus], Awaitable[None]]


class HeartbeatMonitor:
    def __init__(self, *, warning_after_sec: float, interval_sec: float) -> None:
        self._warning_after_sec = max(1.0, warning_after_sec)
        self._interval_sec = max(1.0, interval_sec)
        self._connected_since_monotonic: float | None = None
        self._last_status_monotonic = time.monotonic()
        self._last_event_monotonic = time.monotonic()
        self._last_error = ""
        self._retry_count = 0
        self._connection_state = ConnectionState.IDLE

    def set_state(self, state: ConnectionState, *, retry_count: int = 0, error: str = "") -> None:
        self._connection_state = state
        self._retry_count = retry_count
        self._last_status_monotonic = time.monotonic()
        if error:
            self._last_error = error
        if state == ConnectionState.CONNECTED:
            self._connected_since_monotonic = time.monotonic()
            self._last_event_monotonic = self._connected_since_monotonic
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
    ) -> None:
        while not stop_event.is_set():
            snapshot = self.snapshot()
            message = "heartbeat"
            if snapshot.last_event_age_ms > int(self._warning_after_sec * 1000):
                message = "heartbeat warning: no recent events"

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
                )
            )
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=self._interval_sec)
            except asyncio.TimeoutError:
                continue


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
