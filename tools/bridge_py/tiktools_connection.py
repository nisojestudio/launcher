from __future__ import annotations

import asyncio
import json
import time
from typing import Any, Awaitable, Callable
from urllib.parse import urlencode

from event_models import CanonicalActor, CanonicalEvent, CanonicalEventType, CanonicalGift, CanonicalMetadata, ConnectionState, SessionStatus
from error_catalog import classify_close_code, classify_error_text, message_for
from structured_logging import log_json, utc_now_ms

try:
    import websockets
except ImportError:
    websockets = None  # type: ignore[assignment]

TIKTOOLS_WS_BASE = "wss://api.tik.tools"

# Codigos de cierre que el catalogo de errores reconoce. Se mantienen aqui por
# compatibilidad de lectura; la clasificacion vive en error_catalog.
_NOT_LIVE_CLOSE_CODES = {4005, 4006, 4404}
_SESSION_LIMIT_CLOSE_CODE = 4429
_SERVER_INITIATED_CLOSE_CODES = {4404, 4429, 4555, 4556, 4500, 1012}


class TikToolsConnectionError(RuntimeError):
    def __init__(self, code: str, message: str, *, raw_error: str = "") -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.raw_error = raw_error


def classify_tiktools_error(raw_error: str) -> tuple[str, str]:
    code = classify_error_text(raw_error)
    return code, message_for(code)


_WS_EVENT_MAP: dict[str, CanonicalEventType] = {
    "chat": CanonicalEventType.CHAT,
    "comment": CanonicalEventType.CHAT,
    "gift": CanonicalEventType.GIFT,
    "like": CanonicalEventType.LIKE,
    "follow": CanonicalEventType.FOLLOW,
    "share": CanonicalEventType.SHARE,
    "join": CanonicalEventType.VIEWER_JOIN,
    "member": CanonicalEventType.VIEWER_JOIN,
    "roomuserseq": CanonicalEventType.VIEWER_COUNT,
    "roominfo": CanonicalEventType.LIVE_START,
    "live": CanonicalEventType.LIVE_START,
    "end": CanonicalEventType.LIVE_END,
    "offline": CanonicalEventType.LIVE_END,
    "moderation": CanonicalEventType.MODERATION,
}


def _ws_event_to_canonical(raw: dict[str, Any], *, room_id: str, session_id: int, target_user: str) -> CanonicalEvent | None:
    event_name = str(raw.get("event", "")).lower()
    event_type = _WS_EVENT_MAP.get(event_name)
    if event_type is None:
        return None

    # tik.tools: roomInfo has data at top level, others nest in "data"
    data = raw.get("data", raw) if isinstance(raw.get("data"), dict) else raw

    user = data.get("user", {}) if isinstance(data.get("user"), dict) else {}
    actor = CanonicalActor(
        user_id=str(user.get("id", user.get("userId", ""))),
        username=str(user.get("uniqueId", user.get("username", ""))),
        display_name=str(user.get("nickname", user.get("display_name", ""))),
        avatar_url=str(user.get("avatarUrl", user.get("avatar_url", ""))),
    )

    metadata = CanonicalMetadata(
        event_id=f"tiktools:{session_id}:{utc_now_ms()}",
        room_id=room_id,
        source_event_type=event_name,
        timestamp_ms=utc_now_ms(),
    )

    text = ""
    gift = None
    viewer_count = 0
    raw_payload = None

    if event_type == CanonicalEventType.CHAT:
        text = str(data.get("comment", data.get("text", "")))
    elif event_type == CanonicalEventType.GIFT:
        gift = CanonicalGift(
            gift_id=str(data.get("giftId", "")),
            gift_name=str(data.get("giftName", data.get("gift_name", ""))),
            quantity=int(data.get("repeatCount", data.get("gift_count", 1))),
            diamond_count=int(data.get("diamondCount", data.get("diamond_count", 0))),
        )
    elif event_type == CanonicalEventType.VIEWER_COUNT:
        viewer_count = int(data.get("totalViewers", data.get("viewerCount", data.get("viewer_count", 0))))
    elif event_type == CanonicalEventType.LIVE_END:
        text = str(data.get("message", "live ended"))

    return CanonicalEvent(
        event_type=event_type,
        actor=actor,
        metadata=metadata,
        text=text,
        gift=gift,
        viewer_count=viewer_count,
        raw_payload=raw_payload,
    )


class TikToolsConnection:
    def __init__(
        self,
        *,
        logger: Any,
        legacy_bridge_root: str,
        connect_timeout_sec: float,
        event_callback: Callable[[CanonicalEvent], Awaitable[None]],
        status_callback: Callable[[SessionStatus], Awaitable[None]],
        target_user: str,
        room_id: str = "",
        session_id: int = 0,
        api_key: str = "",
        handshake_timeout_sec: float = 0.0,
    ) -> None:
        self._logger = logger
        self._legacy_bridge_root = legacy_bridge_root
        self._connect_timeout_sec = connect_timeout_sec
        self._handshake_timeout_sec = float(handshake_timeout_sec or 0.0)
        self._event_callback = event_callback
        self._status_callback = status_callback
        self._target_user = target_user.strip().lstrip("@").lower()
        self._room_id = str(room_id or "").strip()
        self._session_id = session_id or utc_now_ms()
        self._api_key = api_key.strip()
        self._stop = False
        self._remote_live_ended = False
        self._ws_task: asyncio.Task[None] | None = None
        self._websocket: Any | None = None
        self._handshake_complete = False
        self._handshake_event: asyncio.Event | None = None
        # Session metrics
        self._session_started_at: float = 0.0
        self._session_events_received: int = 0
        self._session_gifts_received: int = 0
        self._session_chat_received: int = 0

    @property
    def room_id(self) -> str:
        return self._room_id

    @property
    def handshake_complete(self) -> bool:
        """True solo cuando el proveedor ya confirmo la sala del live.

        Mientras sea False la sesion esta abierta pero NO conectada: el panel
        debe mostrar 'Conectando', no 'Conectado'.
        """
        return self._handshake_complete

    async def open(self) -> None:
        if websockets is None:
            raise TikToolsConnectionError("BOOTSTRAP_FAILED", "websockets no esta instalado. Ejecuta: pip install websockets")
        if not self._api_key:
            raise TikToolsConnectionError("INVALID_API_KEY", "No se proporciono una API key de tik.tools.")

        await self._status_callback(
            SessionStatus(
                target_user=self._target_user,
                connection_state=ConnectionState.CONNECTING,
                room_id=self._room_id,
                message="Conectando con el WebSocket de tik.tools...",
                timestamp_ms=utc_now_ms(),
                severity="info",
                phase="connecting",
            )
        )

        self._session_started_at = time.monotonic()
        self._session_events_received = 0
        self._session_gifts_received = 0
        self._session_chat_received = 0
        self._handshake_complete = False
        self._handshake_event = asyncio.Event()
        self._ws_task = asyncio.create_task(self._ws_loop(), name="tiktools-ws")
        await self._wait_for_handshake()

    async def _wait_for_handshake(self) -> bool:
        """Espera la confirmacion de sala (evento roomInfo) del proveedor.

        Devuelve True si el live quedo confirmado. Si el proveedor todavia no
        responde la sala, devuelve False sin cortar la sesion: seguir conectando
        es justamente lo que se espera cuando el vivo esta por empezar.
        """
        if self._handshake_event is None:
            return False

        loop = asyncio.get_running_loop()
        # El relay de tik.tools puede tardar en confirmar la sala (medido: hasta
        # ~45s). Nunca menos de 45s para no declarar un falso "waiting"; el
        # parametro handshake_timeout_sec permite ajustarlo (tests).
        timeout_sec = self._handshake_timeout_sec or max(float(self._connect_timeout_sec or 0.0), 45.0)
        deadline = loop.time() + timeout_sec

        while True:
            if self._handshake_complete:
                return True
            if self._ws_task is not None and self._ws_task.done():
                # Propaga el error real de la sesion (cierre, cuota, no live).
                await self._ws_task
                return self._handshake_complete
            if loop.time() >= deadline:
                log_json(
                    self._logger,
                    "warning",
                    "tiktools_connection",
                    "handshake timeout waiting for room info",
                    target_user=self._target_user,
                    timeout_sec=timeout_sec,
                    session_uptime_ms=self._session_uptime_ms(),
                )
                await self._status_callback(
                    SessionStatus(
                        target_user=self._target_user,
                        connection_state=ConnectionState.CONNECTING,
                        room_id=self._room_id,
                        message="Esperando que TikTok confirme la sala del live...",
                        timestamp_ms=utc_now_ms(),
                        severity="info",
                        phase="connecting",
                    )
                )
                return False
            await asyncio.sleep(0.05)

    def _session_uptime_ms(self) -> int:
        if self._session_started_at <= 0:
            return 0
        return int((time.monotonic() - self._session_started_at) * 1000)

    async def _ws_loop(self) -> None:
        if websockets is None:
            return

        query = {"uniqueId": self._target_user, "apiKey": self._api_key}
        if self._room_id:
            query["roomId"] = self._room_id
        uri = f"{TIKTOOLS_WS_BASE}?{urlencode(query)}"

        try:
            ws = await websockets.connect(
                uri,
                open_timeout=self._connect_timeout_sec,
                ping_interval=20,
                ping_timeout=20,
            )
            self._websocket = ws
            log_json(
                self._logger,
                "info",
                "tiktools_connection",
                "websocket connected",
                target_user=self._target_user,
                session_uptime_ms=self._session_uptime_ms(),
            )

            try:
                async for raw_msg in ws:
                    if self._stop:
                        break

                    try:
                        msg = json.loads(raw_msg)
                    except (json.JSONDecodeError, TypeError):
                        continue

                    event_name = str(msg.get("event", "")).lower()

                    # Handle connection lifecycle
                    if event_name == "roominfo":
                        top_level_room_id = str(msg.get("roomId", ""))
                        if top_level_room_id and not self._room_id:
                            self._room_id = top_level_room_id

                        # Recien aca la sala existe: hasta este punto el panel
                        # debe mostrar "Conectando".
                        self._handshake_complete = True
                        if self._handshake_event is not None:
                            self._handshake_event.set()

                        await self._status_callback(
                            SessionStatus(
                                target_user=self._target_user,
                                connection_state=ConnectionState.CONNECTED,
                                room_id=self._room_id,
                                message="Conectado via tik.tools",
                                timestamp_ms=utc_now_ms(),
                                severity="info",
                                phase="connected",
                            )
                        )
                        log_json(
                            self._logger,
                            "info",
                            "tiktools_connection",
                            "tiktools connected",
                            target_user=self._target_user,
                            room_id=self._room_id,
                            session_uptime_ms=self._session_uptime_ms(),
                        )

                        canonical = _ws_event_to_canonical(
                            msg, room_id=self._room_id, session_id=self._session_id, target_user=self._target_user
                        )
                        if canonical is not None:
                            self._session_events_received += 1
                            await self._event_callback(canonical)
                        continue

                    if event_name in ("end", "offline"):
                        canonical = _ws_event_to_canonical(
                            msg, room_id=self._room_id, session_id=self._session_id, target_user=self._target_user
                        )
                        if canonical is not None:
                            self._session_events_received += 1
                            await self._event_callback(canonical)
                        self._remote_live_ended = True
                        log_json(
                            self._logger,
                            "info",
                            "tiktools_connection",
                            "remote live ended",
                            event=event_name,
                            session_events=self._session_events_received,
                            session_uptime_ms=self._session_uptime_ms(),
                        )
                        break

                    # Convert and emit canonical event
                    canonical = _ws_event_to_canonical(
                        msg, room_id=self._room_id, session_id=self._session_id, target_user=self._target_user
                    )
                    if canonical is not None:
                        self._session_events_received += 1
                        if canonical.event_type == CanonicalEventType.GIFT:
                            self._session_gifts_received += 1
                        elif canonical.event_type == CanonicalEventType.CHAT:
                            self._session_chat_received += 1
                        await self._event_callback(canonical)

                # WebSocket loop ended
                if self._remote_live_ended:
                    raise TikToolsConnectionError(
                        "NOT_LIVE",
                        "El live de TikTok termino.",
                        raw_error="provider event: live ended",
                    )
                if not self._stop:
                    close_code = getattr(ws, "close_code", None)
                    close_reason = str(getattr(ws, "close_reason", "") or "")

                    log_json(
                        self._logger,
                        "warning",
                        "tiktools_connection",
                        "websocket closed unexpectedly",
                        close_code=close_code,
                        close_reason=close_reason,
                        session_events=self._session_events_received,
                        session_gifts=self._session_gifts_received,
                        session_chat=self._session_chat_received,
                        session_uptime_ms=self._session_uptime_ms(),
                    )

                    if close_code == _SESSION_LIMIT_CLOSE_CODE:
                        raise TikToolsConnectionError(
                            "API_SESSION_ENDED",
                            message_for("API_SESSION_ENDED"),
                            raw_error=f"close code {close_code}: {close_reason}",
                        )

                    # El catalogo decide: 4404 -> NOT_LIVE (transitorio),
                    # 4555 -> cuota agotada, 4556 -> relay, 1012 -> reinicio.
                    classified_code = classify_close_code(close_code, close_reason)
                    raise TikToolsConnectionError(
                        classified_code,
                        message_for(classified_code),
                        raw_error=f"close code {close_code}: {close_reason}",
                    )
            except Exception as exc:
                log_json(self._logger, "error", "tiktools_connection", "ws_loop_inner_error", error=str(exc), type=type(exc).__name__)
                raise
            finally:
                self._websocket = None
                log_json(
                    self._logger,
                    "info",
                    "tiktools_connection",
                    "ws_loop cleanup",
                    session_events=self._session_events_received,
                    session_gifts=self._session_gifts_received,
                    session_chat=self._session_chat_received,
                    session_uptime_ms=self._session_uptime_ms(),
                )
                try:
                    await ws.close()
                except Exception:
                    pass

        except asyncio.CancelledError:
            return
        except TikToolsConnectionError:
            raise
        except Exception as exc:
            code, message = classify_tiktools_error(str(exc))
            log_json(self._logger, "error", "tiktools_connection", "ws_loop_error", error=str(exc), code=code)
            raise TikToolsConnectionError(code, message, raw_error=str(exc)) from exc

    async def wait_closed(self) -> None:
        if self._ws_task is None:
            return
        try:
            await self._ws_task
        except asyncio.CancelledError:
            raise
        except TikToolsConnectionError:
            raise
        except Exception as exc:
            code, message = classify_tiktools_error(str(exc))
            raise TikToolsConnectionError(code, message, raw_error=str(exc)) from exc

    async def close(self) -> None:
        self._stop = True
        if self._websocket is not None:
            try:
                await self._websocket.close(code=1000, reason="bridge shutdown")
            except Exception:
                pass
        if self._ws_task is not None and not self._ws_task.done():
            self._ws_task.cancel()
            try:
                await self._ws_task
            except asyncio.CancelledError:
                pass
