from __future__ import annotations

import asyncio
import json
import time
from typing import Any, Awaitable, Callable
from urllib.parse import urlencode

from event_models import CanonicalActor, CanonicalEvent, CanonicalEventType, CanonicalGift, CanonicalMetadata, ConnectionState, SessionStatus
from structured_logging import log_json, utc_now_ms

try:
    import websockets
except ImportError:
    websockets = None  # type: ignore[assignment]

TIKTOOLS_WS_BASE = "wss://api.tik.tools"

# tik.tools close codes
_NOT_LIVE_CLOSE_CODES = {4005, 4006, 4404, 4555}
_SESSION_LIMIT_CLOSE_CODE = 4429
_SERVER_INITIATED_CLOSE_CODES = {4404, 4429, 4555, 4500, 4556}


class TikToolsConnectionError(RuntimeError):
    def __init__(self, code: str, message: str, *, raw_error: str = "") -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.raw_error = raw_error


def classify_tiktools_error(raw_error: str) -> tuple[str, str]:
    lower = str(raw_error or "").lower()
    if "4429" in lower or "demo session ended" in lower or "concurrent websocket" in lower:
        return "API_SESSION_ENDED", "tik.tools cerro la sesion por limite de plan o de WebSockets."
    if "invalid" in lower and ("key" in lower or "api" in lower):
        return "INVALID_API_KEY", "La API key de tik.tools es invalida."
    if "not found" in lower or "user not found" in lower:
        return "USER_NOT_FOUND", "No se encontro ese usuario en TikTok."
    if "not live" in lower or "not currently live" in lower or "offline" in lower:
        return "NOT_LIVE", "El usuario no esta en vivo en este momento."
    if "4404" in lower and ("live" in lower or "not" in lower):
        return "NOT_LIVE", "El usuario no esta en vivo en este momento."
    if "rate" in lower and "limit" in lower:
        return "RATE_LIMIT", "Se agoto el limite de la API de tik.tools."
    if "timeout" in lower or "network" in lower or "connection" in lower:
        return "NETWORK_ERROR", "No se pudo conectar por un problema de red."
    return "UNKNOWN", "No se pudo completar la conexion con tik.tools."


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
    ) -> None:
        self._logger = logger
        self._legacy_bridge_root = legacy_bridge_root
        self._connect_timeout_sec = connect_timeout_sec
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
        # Session metrics
        self._session_started_at: float = 0.0
        self._session_events_received: int = 0
        self._session_gifts_received: int = 0
        self._session_chat_received: int = 0

    @property
    def room_id(self) -> str:
        return self._room_id

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
                message="Connecting via tik.tools WebSocket",
                timestamp_ms=utc_now_ms(),
            )
        )

        self._session_started_at = time.monotonic()
        self._session_events_received = 0
        self._session_gifts_received = 0
        self._session_chat_received = 0
        self._ws_task = asyncio.create_task(self._ws_loop(), name="tiktools-ws")

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

                        await self._status_callback(
                            SessionStatus(
                                target_user=self._target_user,
                                connection_state=ConnectionState.CONNECTED,
                                room_id=self._room_id,
                                message="Connected via tik.tools",
                                timestamp_ms=utc_now_ms(),
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
                            "tik.tools cerro la sesion por limite de plan o de WebSockets.",
                            raw_error=f"close code {close_code}: {close_reason}",
                        )
                    if close_code in _NOT_LIVE_CLOSE_CODES:
                        raise TikToolsConnectionError(
                            "NOT_LIVE",
                            "El usuario no esta en vivo en este momento.",
                            raw_error=f"close code {close_code}: {close_reason}",
                        )
                    raise TikToolsConnectionError(
                        "STREAM_DISCONNECTED",
                        "La conexion con tik.tools se cerro.",
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
