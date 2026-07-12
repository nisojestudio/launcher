from __future__ import annotations

import asyncio
import time
from typing import Any, Awaitable, Callable

from bridge_config import BridgeConfig
from event_models import CanonicalEvent, ConnectionState, SessionStatus
from metrics_registry import MetricsRegistry
from session_manager import HeartbeatMonitor, SessionSupervisor
from structured_logging import log_json, utc_now_ms
from tiktok_connection import TikTokConnection, TikTokConnectionError


EventCallback = Callable[[CanonicalEvent], Awaitable[bool]]
StatusCallback = Callable[[SessionStatus], Awaitable[None]]


def compute_retry_delay(config: BridgeConfig, code: str, attempt_index: int) -> float | None:
    if not config.retry_policy.enabled:
        return None
    if code in ("INVALID_USERNAME", "USER_NOT_FOUND", "AGE_RESTRICTED", "ACCESS_BLOCKED", "RATE_LIMIT"):
        return None
    if config.retry_policy.max_attempts > 0 and attempt_index >= config.retry_policy.max_attempts:
        return None
    if code == "NOT_LIVE":
        return max(1.0, config.retry_policy.not_live_delay_sec)
    delay = config.retry_policy.base_delay_sec * (2 ** max(0, attempt_index))
    return max(1.0, min(config.retry_policy.max_delay_sec, delay))


def remaining_runtime_seconds(started_at: float, max_seconds: int) -> float | None:
    if max_seconds <= 0:
        return None
    return float(max_seconds) - (time.monotonic() - started_at)


class ConnectionManager:
    def __init__(
        self,
        *,
        config: BridgeConfig,
        logger: Any,
        metrics: MetricsRegistry,
        event_callback: EventCallback,
        status_callback: StatusCallback,
    ) -> None:
        self._config = config
        self._logger = logger
        self._metrics = metrics
        self._event_callback = event_callback
        self._status_callback = status_callback
        self._stop_requested = False

    def stop(self) -> None:
        self._stop_requested = True

    async def run(self, *, target_user: str, room_id: str = "", max_events: int = 0, max_seconds: int = 0) -> int:
        attempt = 0
        accepted_events = 0
        started_at = time.monotonic()
        final_message = "Bridge stopped"
        heartbeat_monitor = HeartbeatMonitor(
            warning_after_sec=self._config.connection.heartbeat_warning_after_sec,
            interval_sec=self._config.connection.heartbeat_interval_sec,
        )
        supervisor = SessionSupervisor(heartbeat_monitor=heartbeat_monitor)

        async def emit_status(status: SessionStatus) -> None:
            await self._status_callback(status)

        while not self._stop_requested:
            connection: TikTokConnection | None = None

            async def emit_event(event: CanonicalEvent) -> None:
                nonlocal accepted_events, connection, final_message
                heartbeat_monitor.mark_event()
                if await self._event_callback(event):
                    accepted_events += 1

                if max_events > 0 and accepted_events >= max_events:
                    final_message = f"max_events reached ({max_events})"
                    supervisor.stop()
                    if connection is not None:
                        try:
                            await connection.close()
                        except BaseException:
                            pass
                    return

                remaining_seconds = remaining_runtime_seconds(started_at, max_seconds)
                if remaining_seconds is not None and remaining_seconds <= 0:
                    final_message = f"max_seconds reached ({max_seconds})"
                    supervisor.stop()
                    if connection is not None:
                        try:
                            await connection.close()
                        except BaseException:
                            pass

            try:
                remaining_seconds = remaining_runtime_seconds(started_at, max_seconds)
                if remaining_seconds is not None and remaining_seconds <= 0:
                    final_message = f"max_seconds reached ({max_seconds})"
                    break

                heartbeat_monitor.set_state(ConnectionState.PREPARING, retry_count=attempt)
                await emit_status(
                    SessionStatus(
                        target_user=target_user,
                        connection_state=ConnectionState.PREPARING,
                        room_id=room_id,
                        message="Preparing TikTok connection",
                        timestamp_ms=utc_now_ms(),
                        retry_count=attempt,
                    )
                )
                connection = TikTokConnection(
                    logger=self._logger,
                    legacy_bridge_root=self._config.legacy_bridge_root,
                    connect_timeout_sec=self._config.connection.connect_timeout_sec,
                    event_callback=emit_event,
                    status_callback=emit_status,
                    target_user=target_user,
                    room_id=room_id,
                    session_id=utc_now_ms(),
                )

                heartbeat_monitor.set_state(ConnectionState.CONNECTING, retry_count=attempt)
                await connection.open()
                heartbeat_monitor.set_state(ConnectionState.CONNECTED, retry_count=attempt)
                self._metrics.increment("session_starts_total")
                self._metrics.set_gauge("connected", 1)
                await supervisor.start_heartbeat(
                    target_user=target_user,
                    room_id_provider=lambda: connection.room_id,
                    status_callback=emit_status,
                )
                wait_task = asyncio.create_task(connection.wait_closed(), name="bridge-connection-wait")
                try:
                    while True:
                        remaining_seconds = remaining_runtime_seconds(started_at, max_seconds)
                        if remaining_seconds is not None and remaining_seconds <= 0:
                            final_message = f"max_seconds reached ({max_seconds})"
                            supervisor.stop()
                            await connection.close()
                        if self._stop_requested or supervisor.stop_requested:
                            await connection.close()
                        if wait_task.done():
                            try:
                                await wait_task
                            except BaseException:
                                pass
                            break

                        wait_timeout = 0.5
                        if remaining_seconds is not None:
                            wait_timeout = max(0.05, min(wait_timeout, remaining_seconds))

                        done, _pending = await asyncio.wait({wait_task}, timeout=wait_timeout)
                        if done:
                            try:
                                await wait_task
                            except BaseException:
                                pass
                            break
                finally:
                    if not wait_task.done():
                        wait_task.cancel()
                        try:
                            await wait_task
                        except BaseException:
                            pass

                if supervisor.stop_requested or self._stop_requested:
                    break

                raise TikTokConnectionError(
                    "STREAM_DISCONNECTED",
                    "La conexion con TikTok se cerro.",
                )
            except TikTokConnectionError as exc:
                self._metrics.increment("session_failures_total")
                self._metrics.set_gauge("connected", 0)
                heartbeat_monitor.set_state(ConnectionState.FAULTED, retry_count=attempt, error=exc.message)
                await emit_status(
                    SessionStatus(
                        target_user=target_user,
                        connection_state=ConnectionState.FAULTED,
                        room_id=connection.room_id if connection is not None else room_id,
                        message=exc.message,
                        timestamp_ms=utc_now_ms(),
                        retry_count=attempt,
                    )
                )
                log_json(
                    self._logger,
                    "error",
                    "connection_manager",
                    "TikTok session failed",
                    code=exc.code,
                    error_message=exc.message,
                    retry_count=attempt,
                    target_user=target_user,
                    raw_error=exc.raw_error,
                )
                retry_delay = compute_retry_delay(self._config, exc.code, attempt)
                if retry_delay is None:
                    return 1
                remaining_seconds = remaining_runtime_seconds(started_at, max_seconds)
                if remaining_seconds is not None and remaining_seconds <= 0:
                    final_message = f"max_seconds reached ({max_seconds})"
                    break
                attempt += 1
                self._metrics.increment("reconnect_total")
                heartbeat_monitor.set_state(ConnectionState.RECONNECTING, retry_count=attempt, error=exc.message)
                await emit_status(
                    SessionStatus(
                        target_user=target_user,
                        connection_state=ConnectionState.RECONNECTING,
                        room_id=connection.room_id if connection is not None else room_id,
                        message=f"Retrying in {retry_delay:.1f}s",
                        timestamp_ms=utc_now_ms(),
                        retry_count=attempt,
                    )
                )
                await supervisor.stop_heartbeat()
                if connection is not None:
                    try:
                        await connection.close()
                    except BaseException:
                        pass
                remaining_seconds = remaining_runtime_seconds(started_at, max_seconds)
                sleep_delay = retry_delay if remaining_seconds is None else min(retry_delay, max(0.0, remaining_seconds))
                if sleep_delay <= 0:
                    final_message = f"max_seconds reached ({max_seconds})"
                    break
                await asyncio.sleep(sleep_delay)
                continue
            finally:
                self._metrics.set_gauge("connected", 0)
                await supervisor.stop_heartbeat()
                if connection is not None:
                    try:
                        await connection.close()
                    except BaseException:
                        pass

        await emit_status(
            SessionStatus(
                target_user=target_user,
                connection_state=ConnectionState.STOPPED if self._stop_requested else ConnectionState.DISCONNECTED,
                room_id=room_id,
                message=final_message,
                timestamp_ms=utc_now_ms(),
            )
        )
        return 0
