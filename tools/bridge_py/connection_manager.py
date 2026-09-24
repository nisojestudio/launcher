from __future__ import annotations

import asyncio
import random
import time
from datetime import datetime, timedelta, timezone
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
        panel_attached: Callable[[], bool] | None = None,
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
        self._sound = SoundAlerts(enabled=config.sound_alerts.enabled)
        self._sound_requires_panel = config.sound_alerts.require_panel
        self._panel_attached = panel_attached
        self._last_played_state = ""
        # Pool de credenciales: se rota cuando el proveedor agota la cuota.
        self._api_keys = self._config.connection.effective_api_keys()
        self._key_index = 0
        # Indice de key -> instante (monotonic) hasta el que queda en cuarentena.
        self._key_cooldown_until: dict[int, float] = {}

    def _panel_allows_sound(self) -> bool:
        """False cuando no hay panel a quien avisar.

        El bridge puede sobrevivir al panel a proposito (resiliencia ante
        reinicios), pero entonces las alertas sonoras no tienen destinatario y
        solo molestan. Ante la duda se permite sonar: nunca silenciar por un
        fallo del propio predicado.
        """
        if not self._sound_requires_panel:
            return True
        if self._panel_attached is None:
            return True
        try:
            return bool(self._panel_attached())
        except Exception:
            return True

    def _key_cooldown_seconds(self) -> float:
        configured_minutes = float(getattr(self._config.retry_policy, "key_cooldown_minutes", 0) or 0)
        if configured_minutes > 0:
            return configured_minutes * 60.0
        # Sin configuracion: esperar al reinicio diario del proveedor.
        now = datetime.now(timezone.utc)
        tomorrow = (now + timedelta(days=1)).replace(hour=0, minute=0, second=0, microsecond=0)
        return max(60.0, (tomorrow - now).total_seconds())

    def _select_api_key(self) -> str:
        """Primera key disponible del pool (respeta cuarentenas)."""
        if not self._api_keys:
            return ""
        now = time.monotonic()
        for offset in range(len(self._api_keys)):
            index = (self._key_index + offset) % len(self._api_keys)
            if self._key_cooldown_until.get(index, 0.0) <= now:
                self._key_index = index
                return self._api_keys[index]
        return ""

    def _quarantine_current_key(self) -> None:
        if not self._api_keys:
            return
        self._key_cooldown_until[self._key_index] = time.monotonic() + self._key_cooldown_seconds()

    def _next_available_key_delay(self) -> float | None:
        """Segundos hasta que alguna key salga de cuarentena (None = ninguna)."""
        if not self._api_keys:
            return None
        now = time.monotonic()
        pending = [
            self._key_cooldown_until.get(index, 0.0)
            for index in range(len(self._api_keys))
            if self._key_cooldown_until.get(index, 0.0) > now
        ]
        if len(pending) < len(self._api_keys):
            return 0.0
        if not pending:
            return 0.0
        return max(0.0, min(pending) - now)

    def _current_key_label(self) -> str:
        if not self._api_keys:
            return ""
        return self._config.connection.label_for_key(self._api_keys[self._key_index])

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
        # Ultima fase emitida: el latido la reutiliza para no pisar "waiting".
        current_phase = "starting"
        heartbeat_monitor = HeartbeatMonitor(
            warning_after_sec=self._config.connection.heartbeat_warning_after_sec,
            interval_sec=self._config.connection.heartbeat_interval_sec,
            silence_timeout_sec=self._config.connection.silence_timeout_sec,
        )
        supervisor = SessionSupervisor(heartbeat_monitor=heartbeat_monitor)

        async def emit_status(status: SessionStatus) -> None:
            nonlocal current_phase
            if status.phase:
                current_phase = status.phase
            # Play sound on meaningful state transitions
            state_key = status.connection_state.value
            if state_key != self._last_played_state and self._sound.should_alert(state_key):
                if not self._panel_allows_sound():
                    self._metrics.increment("sound_alerts_suppressed_total")
                elif state_key == "connected":
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
                # Credencial de este intento: la primera key no agotada del pool.
                selected_api_key = self._select_api_key()
                if self._config.connection_mode == "direct":
                    connection = TikTokConnection(**connection_args)
                elif self._config.connection_mode == "euler":
                    from euler_connection import EulerConnection
                    if not selected_api_key:
                        raise EulerConnectionError(
                            "INVALID_API_KEY",
                            "Euler requiere API key (JWT). Proporcione --api-key o LIVEPANEL_BRIDGE_API_KEY.",
                        )
                    connection = EulerConnection(
                        **connection_args,
                        api_key=selected_api_key,
                        heartbeat_interval_sec=self._config.connection.heartbeat_interval_sec,
                        heartbeat_warning_after_sec=self._config.connection.heartbeat_warning_after_sec,
                        silence_timeout_sec=self._config.connection.silence_timeout_sec,
                    )
                else:
                    if not selected_api_key:
                        raise TikToolsConnectionError(
                            "INVALID_API_KEY",
                            "tik.tools requiere API key. Proporcione --api-key o LIVEPANEL_BRIDGE_API_KEY.",
                        )
                    connection = TikToolsConnection(
                        **connection_args,
                        api_key=selected_api_key,
                    )
                if selected_api_key:
                    log_json(
                        self._logger,
                        "info",
                        "connection_manager",
                        "using api key from pool",
                        key_label=self._current_key_label(),
                        pool_size=len(self._api_keys),
                        target_user=target_user,
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
                            alert_code="WAITING_ROOM",
                            alert_action=ACTION_WAIT_FOR_LIVE,
                            phase="waiting",
                            provider=self._config.connection_mode,
                        )
                    )
                await supervisor.start_heartbeat(
                    target_user=target_user,
                    room_id_provider=lambda: connection.room_id,
                    status_callback=emit_status,
                    phase_provider=lambda: current_phase,
                )
                wait_task = asyncio.create_task(connection.wait_closed(), name="bridge-connection-wait")
                try:
                    # El relay del proveedor puede confirmar la sala DESPUES del
                    # timeout del handshake: hay que declarar la conexion cuando
                    # llega, o el panel se queda mostrando "Conectando".
                    late_connect_declared = handshake_ok
                    while True:
                        remaining_seconds = remaining_runtime_seconds(started_at, max_seconds)
                        if remaining_seconds is not None and remaining_seconds <= 0:
                            final_message = f"max_seconds reached ({max_seconds})"
                            supervisor.stop()
                            await connection.close()
                        if self._stop_requested or supervisor.stop_requested:
                            await connection.close()

                        if not late_connect_declared and bool(
                            getattr(connection, "handshake_complete", False)
                        ):
                            late_connect_declared = True
                            heartbeat_monitor.set_state(ConnectionState.CONNECTED, retry_count=attempt)
                            self._metrics.increment("session_starts_total")
                            self._metrics.set_gauge("connected", 1)
                            log_json(
                                self._logger,
                                "info",
                                "connection_manager",
                                "session connected after delayed room confirmation",
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

                # Cuota agotada: la key actual entra en cuarentena y se sigue con
                # la proxima del pool, sin reiniciar el proceso.
                rotating_key = error_action == ACTION_ROTATE_KEY and len(self._api_keys) > 1
                key_delay: float | None = None
                key_label = self._current_key_label()
                if rotating_key:
                    self._quarantine_current_key()
                    key_delay = self._next_available_key_delay()
                    key_label = self._current_key_label()
                    log_json(
                        self._logger,
                        "warning",
                        "connection_manager",
                        "api key quota exhausted, rotating",
                        exhausted_key_label=key_label,
                        pool_size=len(self._api_keys),
                        next_key_delay_sec=None if key_delay is None else round(key_delay, 1),
                        target_user=target_user,
                    )

                error_message = exc.message
                provider_label = {"tiktools": "tik.tools", "euler": "Euler Stream"}.get(provider, provider)
                if rotating_key:
                    if key_delay is not None and key_delay > 0:
                        error_message = (
                            f"{provider_label} rechazo todas las API keys del pool. "
                            f"Se reintenta cuando alguna se libere (en {key_delay / 60.0:.0f} min)."
                        )
                    else:
                        error_message = (
                            f"{provider_label} rechazo la credencial en uso ({key_label}, cuota o plan "
                            "agotado). Se rota automaticamente a la siguiente."
                        )
                elif error_action == ACTION_ROTATE_KEY:
                    # El catalogo pide rotar pero no hay pool: no prometer una
                    # rotacion que no puede pasar (deja al usuario sin pista).
                    error_message = (
                        f"{provider_label} rechazo la unica API key configurada (cuota o plan "
                        "agotado). Agrega una credencial nueva en Cuentas y API keys."
                    )

                await emit_status(
                    SessionStatus(
                        target_user=target_user,
                        connection_state=ConnectionState.FAULTED,
                        room_id=connection.room_id if connection is not None else room_id,
                        message=error_message,
                        timestamp_ms=utc_now_ms(),
                        retry_count=attempt,
                        severity=severity_for(exc.code),
                        alert_code=exc.code,
                        alert_action=error_action,
                        phase="waiting" if (waiting_for_live or rotating_key) else "error",
                        provider=provider,
                        key_label=key_label,
                    )
                )
                log_json(
                    self._logger,
                    "error",
                    "connection_manager",
                    "session failed",
                    code=exc.code,
                    error_message=error_message,
                    error_action=error_action,
                    retry_count=attempt,
                    target_user=target_user,
                    raw_error=exc.raw_error,
                    provider=provider,
                    key_label=key_label,
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
                # Motivo por el que la sesion cortaria ahora ("" = sigue reintentando).
                stop_reason = ""
                if rotating_key and key_delay is not None:
                    # Rotar de credencial no es un fallo: se reintenta enseguida
                    # (o cuando la cuota se libere) sin gastar el presupuesto de
                    # intentos ni el limite de reconexiones por hora.
                    max_wait_minutes = float(
                        getattr(self._config.retry_policy, "all_keys_cooldown_max_minutes", 30) or 0
                    )
                    if max_wait_minutes > 0 and key_delay > max_wait_minutes * 60.0:
                        retry_delay = None
                        stop_reason = "all_keys_busy"
                    else:
                        retry_delay = max(1.0, key_delay)
                elif error_action == ACTION_ROTATE_KEY:
                    # El catalogo pide rotar pero no hay otra key: reintentar con
                    # la misma credencial no cambia nada, no vale la pena quemar
                    # max_attempts esperando un resultado identico.
                    retry_delay = None
                    stop_reason = "rotate_without_pool"
                if retry_delay is None:
                    log_json(
                        self._logger,
                        "warning",
                        "connection_manager",
                        "no more retries for this error code",
                        code=exc.code,
                        attempt=attempt,
                        waiting_for_live_seconds=round(waiting_for_live_seconds, 1),
                        stop_reason=stop_reason,
                    )
                    # Mensaje final visible en el monitor del live. Debe decir lo
                    # que realmente paso: nunca "se reintenta" cuando ya no va a
                    # haber reintento, y siempre dejar la accion del usuario.
                    if stop_reason == "rotate_without_pool":
                        final_message = error_message
                    elif stop_reason == "all_keys_busy":
                        final_message = (
                            f"{provider_label} rechazo todas las API keys del pool y ninguna se "
                            "libera pronto. Revisa las credenciales en Cuentas y API keys."
                        )
                    elif not spec_for(exc.code).retryable or attempt <= 0:
                        final_message = exc.message
                    else:
                        final_message = f"Se dejo de reintentar tras {attempt} reintentos. {exc.message}"
                    exit_code = 1
                    break

                # El limite de reconexiones por hora protege la cuota del
                # proveedor, pero esperar el vivo o rotar de credencial no la
                # consumen.
                if not waiting_for_live and not rotating_key and not self._reconnect_limiter.can_reconnect(provider):
                    log_json(
                        self._logger,
                        "warning",
                        "connection_manager",
                        "reconnect rate limit reached",
                        max_per_hour=self._config.retry_policy.max_reconnect_per_hour,
                        attempt=attempt,
                        provider=provider,
                    )
                    final_message = (
                        "Se alcanzo el limite de reconexiones por hora. "
                        "Espera unos minutos y vuelve a conectar."
                    )
                    exit_code = 1
                    break

                remaining_seconds = remaining_runtime_seconds(started_at, max_seconds)
                if remaining_seconds is not None and remaining_seconds <= 0:
                    final_message = f"max_seconds reached ({max_seconds})"
                    break

                attempt += 1
                if not waiting_for_live and not rotating_key:
                    self._reconnect_limiter.record_reconnect(provider)
                self._metrics.increment("reconnect_total")
                self._metrics.set_gauge("reconnects_remaining", float(self._reconnect_limiter.remaining(provider)))
                heartbeat_monitor.set_state(ConnectionState.RECONNECTING, retry_count=attempt, error=exc.message)
                if rotating_key:
                    retry_message = (
                        f"Rotando de API key ({self._current_key_label()}). "
                        f"Reconectando en {retry_delay:.0f}s."
                    )
                    retry_phase = "waiting"
                elif waiting_for_live:
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
                        key_label=self._current_key_label(),
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
