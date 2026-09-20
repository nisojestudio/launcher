from __future__ import annotations

import asyncio
import random
import time
from typing import Any, Awaitable, Callable

from bridge_config import BridgeConfig
from error_catalog import (
    ACTION_ROTATE_KEY,
    ACTION_WAIT_FOR_LIVE,
    action_for,
    message_for,
    severity_for,
    spec_for,
)
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


def compute_retry_delay(
    config: BridgeConfig,
    code: str,
    attempt_index: int,
    waiting_for_live_seconds: float = 0.0,
) -> float | None:
    """Delay antes del siguiente intento. None = no reintentar.

    La politica vive en `error_catalog`: aca solo se aplica el tiempo.
    - `wait_for_live` (la cuenta no esta en vivo) espera `not_live_delay_sec`
      durante `waiting_for_live_max_minutes`, sin consumir el presupuesto de
      intentos ni el limite de reconexiones: el objetivo es conectar cuando el
      vivo empiece, no rendirse.
    - El resto de errores reintentables usa backoff exponencial con jitter.
    """
    if not config.retry_policy.enabled:
        return None

    spec = spec_for(code)

    if action_for(code) == ACTION_WAIT_FOR_LIVE:
        waiting_budget_seconds = float(
            getattr(config.retry_policy, "waiting_for_live_max_minutes", 0) or 0
        ) * 60.0
        if waiting_budget_seconds > 0 and waiting_for_live_seconds >= waiting_budget_seconds:
            return None
        return max(1.0, config.retry_policy.not_live_delay_sec)

    if not spec.retryable:
        return None

    if config.retry_policy.max_attempts > 0 and attempt_index >= config.retry_policy.max_attempts:
        return None

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
        # Momento en que empezo la espera por el vivo (0 = no estamos esperando).
        waiting_for_live_since = 0.0
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
                        message="Preparando la conexion con TikTok...",
                        timestamp_ms=utc_now_ms(),
                        retry_count=attempt,
                        phase="starting",
                        provider=self._config.connection_mode,
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

                # Solo se declara conectado cuando el proveedor confirmo la sala.
                # Antes de eso la sesion esta abierta pero el panel debe seguir
                # mostrando "Conectando".
                handshake_ok = bool(getattr(connection, "handshake_complete", True))
                if handshake_ok:
                    # La sesion quedo confirmada: la espera por el vivo se reinicia.
                    waiting_for_live_since = 0.0
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
                    await emit_status(
                        SessionStatus(
                            target_user=target_user,
                            connection_state=ConnectionState.CONNECTED,
                            room_id=connection.room_id,
                            message="Conectado al live de TikTok.",
                            timestamp_ms=utc_now_ms(),
                            retry_count=attempt,
                            severity="info",
                            phase="connected",
                            provider=self._config.connection_mode,
                        )
                    )
                else:
                    log_json(
                        self._logger,
                        "info",
                        "connection_manager",
                        "session open, waiting for live room confirmation",
                        target_user=target_user,
                        provider=self._config.connection_mode,
                        attempt=attempt,
                    )
                    await emit_status(
                        SessionStatus(
                            target_user=target_user,
                            connection_state=ConnectionState.CONNECTING,
                            room_id=connection.room_id,
                            message="Conectando: esperando que la sala del live quede disponible.",
                            timestamp_ms=utc_now_ms(),
                            retry_count=attempt,
                            severity="info",
                            phase="connecting",
                            provider=self._config.connection_mode,
                        )
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

                provider = self._config.connection_mode
                error_action = action_for(exc.code)
                waiting_for_live = error_action == ACTION_WAIT_FOR_LIVE
                if waiting_for_live and waiting_for_live_since <= 0:
                    waiting_for_live_since = time.monotonic()
                waiting_for_live_seconds = (
                    time.monotonic() - waiting_for_live_since if waiting_for_live_since > 0 else 0.0
                )

                await emit_status(
                    SessionStatus(
                        target_user=target_user,
                        connection_state=ConnectionState.FAULTED,
                        room_id=connection.room_id if connection is not None else room_id,
                        message=exc.message,
                        timestamp_ms=utc_now_ms(),
                        retry_count=attempt,
                        severity=severity_for(exc.code),
                        alert_code=exc.code,
                        alert_action=error_action,
                        phase="waiting" if waiting_for_live else "error",
                        provider=provider,
                    )
                )
                log_json(
                    self._logger,
                    "error",
                    "connection_manager",
                    "session failed",
                    code=exc.code,
                    error_message=exc.message,
                    error_action=error_action,
                    retry_count=attempt,
                    target_user=target_user,
                    raw_error=exc.raw_error,
                    provider=provider,
                    waiting_for_live_seconds=round(waiting_for_live_seconds, 1),
                    reconnects_remaining=self._reconnect_limiter.remaining(provider),
                )

                # Check if we should retry
                retry_delay = compute_retry_delay(
                    self._config,
                    exc.code,
                    attempt,
                    waiting_for_live_seconds=waiting_for_live_seconds,
                )
                if retry_delay is None:
                    log_json(
                        self._logger,
                        "warning",
                        "connection_manager",
                        "no more retries for this error code",
                        code=exc.code,
                        attempt=attempt,
                        waiting_for_live_seconds=round(waiting_for_live_seconds, 1),
                    )
                    final_message = f"permanent failure: {exc.message}"
                    exit_code = 1
                    break

                # El limite de reconexiones por hora protege la cuota del
                # proveedor, pero esperar a que empiece el vivo no la consume.
                if not waiting_for_live and not self._reconnect_limiter.can_reconnect(provider):
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
                if not waiting_for_live:
                    self._reconnect_limiter.record_reconnect(provider)
                self._metrics.increment("reconnect_total")
                self._metrics.set_gauge("reconnects_remaining", float(self._reconnect_limiter.remaining(provider)))
                heartbeat_monitor.set_state(ConnectionState.RECONNECTING, retry_count=attempt, error=exc.message)
                if waiting_for_live:
                    retry_message = (
                        f"Esperando a que @{target_user} empiece el vivo. "
                        f"Nuevo intento en {retry_delay:.0f}s."
                    )
                    retry_phase = "waiting"
                else:
                    retry_message = f"Reintentando conexion ({attempt}) en {retry_delay:.0f}s..."
                    retry_phase = "connecting"
                await emit_status(
                    SessionStatus(
                        target_user=target_user,
                        connection_state=ConnectionState.RECONNECTING,
                        room_id=connection.room_id if connection is not None else room_id,
                        message=retry_message,
                        timestamp_ms=utc_now_ms(),
                        retry_count=attempt,
                        severity=severity_for(exc.code),
                        alert_code=exc.code,
                        alert_action=error_action,
                        phase=retry_phase,
                        provider=provider,
                        retry_in_sec=retry_delay,
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
