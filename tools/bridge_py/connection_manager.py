from __future__ import annotations

import asyncio
import random
import time
from typing import Any, Awaitable, Callable

from bridge_config import BridgeConfig
from event_models import CanonicalEvent, ConnectionState, SessionStatus
from metrics_registry import MetricsRegistry
from session_manager import HeartbeatMonitor, SessionSupervisor
from sound_alerts import SoundAlerts
from structured_logging import log_json, utc_now_ms
from tiktools_connection import TikToolsConnection, TikToolsConnectionError
from tiktok_connection import TikTokConnection, TikTokConnectionError

try:
    from euler_connection import EulerConnection, EulerConnectionError
except ImportError:
    EulerConnection = None  # type: ignore[assignment,misc]
    EulerConnectionError = TikToolsConnectionError  # type: ignore[misc]


EventCallback = Callable[[CanonicalEvent], Awaitable[bool]]
StatusCallback = Callable[[SessionStatus], Awaitable[None]]

# Error codes that should NOT trigger a reconnect (permanent failures)
# Includes codes from TikTools, TikTokLive (direct), and Euler providers
_NON_RETRYABLE_CODES = frozenset({
    "INVALID_USERNAME",
    "USER_NOT_FOUND",
    "AGE_RESTRICTED",
    "ACCESS_BLOCKED",
    "RATE_LIMIT",
    "INVALID_API_KEY",
    "API_SESSION_ENDED",
    "NOT_LIVE",
    "BOOTSTRAP_FAILED",      # Euler: websockets not installed
    "INVALID_JWT",           # Euler: JWT token invalid
})

# Error codes that indicate the server deliberately closed the connection
_SERVER_CLOSE_CODES = frozenset({
    "STREAM_DISCONNECTED",
    "NOT_LIVE",
    "API_SESSION_ENDED",     # Provider ended session (limit reached)
})


def compute_retry_delay(config: BridgeConfig, code: str, attempt_index: int) -> float | None:
    """Compute delay before next reconnect attempt. Returns None if should not retry."""
    if not config.retry_policy.enabled:
        return None
    if code in _NON_RETRYABLE_CODES:
        return None
    if config.retry_policy.max_attempts > 0 and attempt_index >= config.retry_policy.max_attempts:
        return None
    if code == "NOT_LIVE":
        return max(1.0, config.retry_policy.not_live_delay_sec)

    # Exponential backoff with jitter
    base_delay = config.retry_policy.base_delay_sec * (2 ** max(0, attempt_index))
    delay = max(1.0, min(config.retry_policy.max_delay_sec, base_delay))

    # Add jitter to prevent thundering herd
    jitter = config.retry_policy.jitter_sec
    if jitter > 0:
        delay += random.uniform(0.0, jitter)

    return delay


def remaining_runtime_seconds(started_at: float, max_seconds: int) -> float | None:
    if max_seconds <= 0:
        return None
    return float(max_seconds) - (time.monotonic() - started_at)


class ReconnectRateLimiter:
    """Tracks reconnect attempts per hour per provider to avoid exhausting provider tokens."""

    def __init__(self, max_per_hour: int) -> None:
        self._max_per_hour = max(1, max_per_hour)
        self._provider_timestamps: dict[str, list[float]] = {}

    def can_reconnect(self, provider: str) -> bool:
        now = time.monotonic()
        cutoff = now - 3600.0
        timestamps = self._provider_timestamps.get(provider, [])
        timestamps = [t for t in timestamps if t > cutoff]
        self._provider_timestamps[provider] = timestamps
        return len(timestamps) < self._max_per_hour

    def record_reconnect(self, provider: str) -> None:
        now = time.monotonic()
        timestamps = self._provider_timestamps.get(provider, [])
        timestamps.append(now)
        self._provider_timestamps[provider] = timestamps

    def remaining(self, provider: str) -> int:
        now = time.monotonic()
        cutoff = now - 3600.0
        timestamps = self._provider_timestamps.get(provider, [])
        timestamps = [t for t in timestamps if t > cutoff]
        self._provider_timestamps[provider] = timestamps
        return max(0, self._max_per_hour - len(timestamps))


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
        self._reconnect_limiter = ReconnectRateLimiter(
            config.retry_policy.max_reconnect_per_hour
        )
        self._sound = SoundAlerts(enabled=True)
        self._last_played_state = ""

    def stop(self) -> None:
        self._stop_requested = True

    async def run(self, *, target_user: str, room_id: str = "", max_events: int = 0, max_seconds: int = 0) -> int:
        attempt = 0
        accepted_events = 0
        started_at = time.monotonic()
        final_message = "Bridge stopped"
        exit_code = 0
        heartbeat_monitor = HeartbeatMonitor(
            warning_after_sec=self._config.connection.heartbeat_warning_after_sec,
            interval_sec=self._config.connection.heartbeat_interval_sec,
            silence_timeout_sec=self._config.connection.silence_timeout_sec,
        )
        supervisor = SessionSupervisor(heartbeat_monitor=heartbeat_monitor)

        async def emit_status(status: SessionStatus) -> None:
            # Play sound on meaningful state transitions
            state_key = status.connection_state.value
            if state_key != self._last_played_state and self._sound.should_alert(state_key):
                if state_key == "connected":
                    await self._sound.play_connected()
                    log_json(self._logger, "info", "sound_alert", "play: connected")
                elif state_key in ("disconnected", "faulted"):
                    await self._sound.play_disconnected()
                    log_json(self._logger, "info", "sound_alert", "play: disconnected")
                elif state_key == "reconnecting":
                    await self._sound.play_reconnecting()
                    log_json(self._logger, "info", "sound_alert", "play: reconnecting")
            self._last_played_state = state_key
            await self._status_callback(status)

        while not self._stop_requested:
            connection: Any | None = None

            async def emit_event(event: CanonicalEvent) -> None:
                nonlocal accepted_events, connection, final_message
                heartbeat_monitor.mark_event()
                self._metrics.record_event(event.event_type.value, event.latency_ms)
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
                connection_args = {
                    "logger": self._logger,
                    "legacy_bridge_root": self._config.legacy_bridge_root,
                    "connect_timeout_sec": self._config.connection.connect_timeout_sec,
                    "event_callback": emit_event,
                    "status_callback": emit_status,
                    "target_user": target_user,
                    "room_id": room_id,
                    "session_id": utc_now_ms(),
                }
                if self._config.connection_mode == "direct":
                    connection = TikTokConnection(**connection_args)
                elif self._config.connection_mode == "euler":
                    from euler_connection import EulerConnection
                    api_key = self._config.connection.api_key
                    if not api_key:
                        raise EulerConnectionError(
                            "INVALID_API_KEY",
                            "Euler requiere API key (JWT). Proporcione --api-key o LIVEPANEL_BRIDGE_API_KEY.",
                        )
                    connection = EulerConnection(
                        **connection_args,
                        api_key=api_key,
                        heartbeat_interval_sec=self._config.connection.heartbeat_interval_sec,
                        heartbeat_warning_after_sec=self._config.connection.heartbeat_warning_after_sec,
                        silence_timeout_sec=self._config.connection.silence_timeout_sec,
                    )
                else:
                    api_key = self._config.connection.api_key
                    if not api_key:
                        raise TikToolsConnectionError(
                            "INVALID_API_KEY",
                            "tik.tools requiere API key. Proporcione --api-key o LIVEPANEL_BRIDGE_API_KEY.",
                        )
                    connection = TikToolsConnection(
                        **connection_args,
                        api_key=api_key,
                    )

                heartbeat_monitor.set_state(ConnectionState.CONNECTING, retry_count=attempt)
                await connection.open()
                heartbeat_monitor.set_state(ConnectionState.CONNECTED, retry_count=attempt)
                self._metrics.increment("session_starts_total")
                self._metrics.set_gauge("connected", 1)
                log_json(
                    self._logger,
                    "info",
                    "connection_manager",
                    "session connected",
                    target_user=target_user,
                    provider=self._config.connection_mode,
                    attempt=attempt,
                )
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
                            await wait_task
                            break

                        wait_timeout = 0.5
                        if remaining_seconds is not None:
                            wait_timeout = max(0.05, min(wait_timeout, remaining_seconds))

                        done, _pending = await asyncio.wait({wait_task}, timeout=wait_timeout)
                        if done:
                            await wait_task
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

                raise TikToolsConnectionError(
                    "STREAM_DISCONNECTED",
                    "DESCONECTADO: la conexion con TikTok se cerro. Reintentando...",
                )
            except (TikToolsConnectionError, TikTokConnectionError, EulerConnectionError) as exc:
                self._metrics.increment("session_failures_total")
                self._metrics.set_gauge("connected", 0)
                heartbeat_monitor.set_state(ConnectionState.FAULTED, retry_count=attempt, error=exc.message)
                await emit_status(
                    SessionStatus(
                        target_user=target_user,
                        connection_state=ConnectionState.FAULTED,
                        room_id=connection.room_id if connection is not None else room_id,
                        message=f"DESCONECTADO: {exc.message}",
                        timestamp_ms=utc_now_ms(),
                        retry_count=attempt,
                    )
                )
                provider = self._config.connection_mode
                log_json(
                    self._logger,
                    "error",
                    "connection_manager",
                    "session failed",
                    code=exc.code,
                    error_message=exc.message,
                    retry_count=attempt,
                    target_user=target_user,
                    raw_error=exc.raw_error,
                    provider=provider,
                    reconnects_remaining=self._reconnect_limiter.remaining(provider),
                )

                # Check if we should retry
                retry_delay = compute_retry_delay(self._config, exc.code, attempt)
                if retry_delay is None:
                    log_json(
                        self._logger,
                        "warning",
                        "connection_manager",
                        "no more retries for this error code",
                        code=exc.code,
                        attempt=attempt,
                    )
                    final_message = f"permanent failure: {exc.message}"
                    exit_code = 1
                    break

                # Check rate limiter (per provider)
                if not self._reconnect_limiter.can_reconnect(provider):
                    log_json(
                        self._logger,
                        "warning",
                        "connection_manager",
                        "reconnect rate limit reached",
                        max_per_hour=self._config.retry_policy.max_reconnect_per_hour,
                        attempt=attempt,
                        provider=provider,
                    )
                    final_message = "reconnect rate limit exceeded"
                    exit_code = 1
                    break

                remaining_seconds = remaining_runtime_seconds(started_at, max_seconds)
                if remaining_seconds is not None and remaining_seconds <= 0:
                    final_message = f"max_seconds reached ({max_seconds})"
                    break

                attempt += 1
                self._reconnect_limiter.record_reconnect(provider)
                self._metrics.increment("reconnect_total")
                self._metrics.set_gauge("reconnects_remaining", float(self._reconnect_limiter.remaining(provider)))
                heartbeat_monitor.set_state(ConnectionState.RECONNECTING, retry_count=attempt, error=exc.message)
                await emit_status(
                    SessionStatus(
                        target_user=target_user,
                        connection_state=ConnectionState.RECONNECTING,
                        room_id=connection.room_id if connection is not None else room_id,
                        message=f"RECONECTANDO: reintento {attempt} en {retry_delay:.0f}s...",
                        timestamp_ms=utc_now_ms(),
                        retry_count=attempt,
                    )
                )
                log_json(
                    self._logger,
                    "info",
                    "connection_manager",
                    "scheduling reconnect",
                    delay_sec=round(retry_delay, 2),
                    attempt=attempt,
                    code=exc.code,
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
        log_json(
            self._logger,
            "info",
            "connection_manager",
            "run finished",
            final_message=final_message,
            total_events=accepted_events,
            total_attempts=attempt,
        )
        return exit_code
