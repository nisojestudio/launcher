from __future__ import annotations

import asyncio
import json
import time
from typing import Any, Awaitable, Callable
from urllib.parse import urlencode

from event_models import (
    CanonicalActor, CanonicalEvent, CanonicalEventType,
    CanonicalGift, CanonicalMetadata, ConnectionState, SessionStatus,
)
from structured_logging import log_json, utc_now_ms

try:
    import websockets
except ImportError:
    websockets = None  # type: ignore[assignment]

EULER_WS_BASE = "wss://ws.eulerstream.com"
EULER_API_BASE = "https://api.eulerstream.com"
_EULER_NOT_LIVE_CLOSE_CODES = {4404, 4555}
_EULER_SESSION_LIMIT_CLOSE_CODE = 4429


class EulerConnectionError(RuntimeError):
    def __init__(self, code: str, message: str, *, raw_error: str = "") -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.raw_error = raw_error


def classify_euler_error(raw_error: str) -> tuple[str, str]:
    lower = str(raw_error or "").lower()
    if "4429" in lower or "too many connections" in lower:
        return "API_SESSION_ENDED", "Euler cerro la sesion por limite de conexiones."
    if "invalid" in lower and ("key" in lower or "api" in lower or "jwt" in lower):
        return "INVALID_API_KEY", "La API key o JWT de Euler es invalida."
    if "not found" in lower or "user not found" in lower:
        return "USER_NOT_FOUND", "No se encontro ese usuario en TikTok."
    if "not live" in lower or "not currently live" in lower or "offline" in lower:
        return "NOT_LIVE", "El usuario no esta en vivo."
    if "4404" in lower:
        return "NOT_LIVE", "El usuario no esta en vivo."
    if "rate" in lower and "limit" in lower:
        return "RATE_LIMIT", "Se agoto el limite de la API de Euler."
    if "timeout" in lower or "network" in lower or "connection" in lower:
        return "NETWORK_ERROR", "No se pudo conectar por un problema de red."
    return "UNKNOWN", "No se pudo completar la conexion con Euler."


_EULER_EVENT_MAP: dict[str, CanonicalEventType] = {
    "chatevent": CanonicalEventType.CHAT,
    "commentevent": CanonicalEventType.CHAT,
    "commentsevent": CanonicalEventType.CHAT,
    "emotechatevent": CanonicalEventType.CHAT,
    "questionnewevent": CanonicalEventType.CHAT,
    "giftevent": CanonicalEventType.GIFT,
    "likeevent": CanonicalEventType.LIKE,
    "followevent": CanonicalEventType.FOLLOW,
    "shareevent": CanonicalEventType.SHARE,
    "jointevent": CanonicalEventType.VIEWER_JOIN,
    "roomuserseqevent": CanonicalEventType.VIEWER_COUNT,
    "connectevent": CanonicalEventType.LIVE_START,
    "roominfo": CanonicalEventType.LIVE_START,
    "end": CanonicalEventType.LIVE_END,
    "offline": CanonicalEventType.LIVE_END,
    "disconnectevent": CanonicalEventType.LIVE_END,
    "roommessageevent": CanonicalEventType.MODERATION,
}


def _extract_event_type(event: Any) -> CanonicalEventType | None:
    class_name = getattr(getattr(event, "__class__", None), "__name__", "")
    if class_name:
        et = _EULER_EVENT_MAP.get(class_name.lower())
        if et is not None:
            return et
    raw = str(getattr(event, "event", "") or getattr(event, "event_type", "")).lower()
    return _EULER_EVENT_MAP.get(raw) if raw else None


def _extract_user(event: Any) -> dict[str, str]:
    user = getattr(event, "user", None) or getattr(event, "user_info", None)
    if user is None:
        return {"user_id": "", "username": "", "display_name": "", "avatar_url": ""}
    return {
        "user_id": str(getattr(user, "id", "") or getattr(user, "user_id", "") or getattr(user, "uniqueId", "")),
        "username": str(getattr(user, "uniqueId", "") or getattr(user, "username", "")),
        "display_name": str(getattr(user, "nickname", "") or getattr(user, "display_name", "")),
        "avatar_url": str(getattr(user, "avatarUrl", "") or getattr(user, "avatar_url", "")),
    }


def _euler_event_to_canonical(
    event: Any, *, room_id: str, session_id: int, target_user: str,
) -> CanonicalEvent | None:
    event_type = _extract_event_type(event)
    if event_type is None:
        return None
    user = _extract_user(event)
    actor = CanonicalActor(
        user_id=user["user_id"], username=user["username"],
        display_name=user["display_name"], avatar_url=user["avatar_url"],
    )
    metadata = CanonicalMetadata(
        event_id=f"euler:{session_id}:{utc_now_ms()}",
        room_id=room_id or str(getattr(event, "room_id", "") or getattr(event, "roomId", "")),
        source_event_type=event_type.value,
        timestamp_ms=utc_now_ms(),
    )
    text = ""
    gift = None
    viewer_count = 0
    if event_type == CanonicalEventType.CHAT:
        text = str(getattr(event, "comment", "") or getattr(event, "text", "") or getattr(event, "message", ""))
    elif event_type == CanonicalEventType.GIFT:
        g = getattr(event, "gift", None)
        gift = CanonicalGift(
            gift_id=str(getattr(g, "id", "") if g else getattr(event, "giftId", "")),
            gift_name=str(getattr(g, "name", "gift") if g else getattr(event, "giftName", "gift")),
            quantity=int(getattr(g, "count", 1) if g else getattr(event, "repeatCount", 1)),
            diamond_count=int(getattr(g, "diamond_count", 0) if g else getattr(event, "diamondCount", 0)),
        )
    elif event_type == CanonicalEventType.VIEWER_COUNT:
        viewer_count = int(getattr(event, "viewer_count", 0) or getattr(event, "totalViewers", 0))
    elif event_type == CanonicalEventType.LIVE_END:
        text = str(getattr(event, "message", "live ended"))
    return CanonicalEvent(
        event_type=event_type, actor=actor, metadata=metadata,
        text=text, gift=gift, viewer_count=viewer_count,
    )


class EulerConnection:
    def __init__(
        self, *, logger: Any, legacy_bridge_root: str, connect_timeout_sec: float,
        event_callback: Callable[[CanonicalEvent], Awaitable[None]],
        status_callback: Callable[[SessionStatus], Awaitable[None]],
        target_user: str, room_id: str = "", session_id: int = 0, api_key: str = "",
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
        self._session_started_at: float = 0.0
        self._session_events_received: int = 0
        self._session_gifts_received: int = 0
        self._session_chat_received: int = 0

    @property
    def room_id(self) -> str:
        return self._room_id

    def _session_uptime_ms(self) -> int:
        return 0 if self._session_started_at <= 0 else int((time.monotonic() - self._session_started_at) * 1000)

    async def _get_jwt_token(self) -> str:
        import urllib.request, urllib.error
        url = f"{EULER_API_BASE}/authentication/jwt?apiKey={self._api_key}"
        payload = json.dumps({
            "allowed_creators": [self._target_user],
            "expire_after": 600, "max_websockets": 1,
        }).encode("utf-8")
        req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json"}, method="POST")
        try:
            with urllib.request.urlopen(req, timeout=10) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                token = data.get("data", {}).get("token", "")
                if not token:
                    raise EulerConnectionError("INVALID_API_KEY", "Euler no devolvio un token JWT valido.")
                return token
        except urllib.error.HTTPError as exc:
            raise EulerConnectionError("INVALID_API_KEY", f"Euler JWT request failed: {exc.code}")
        except EulerConnectionError:
            raise
        except Exception as exc:
            raise EulerConnectionError("NETWORK_ERROR", f"No se pudo obtener JWT de Euler: {exc}")

    async def open(self) -> None:
        if websockets is None:
            raise EulerConnectionError("BOOTSTRAP_FAILED", "websockets no esta instalado.")
        if not self._api_key:
            raise EulerConnectionError("INVALID_API_KEY", "No se proporciono una API key de Euler.")
        await self._status_callback(SessionStatus(
            target_user=self._target_user, connection_state=ConnectionState.CONNECTING,
            room_id=self._room_id, message="Connecting via Euler Stream", timestamp_ms=utc_now_ms(),
        ))
        self._session_started_at = time.monotonic()
        self._session_events_received = 0
        self._session_gifts_received = 0
        self._session_chat_received = 0
        self._ws_task = asyncio.create_task(self._ws_loop(), name="euler-ws")

    async def _ws_loop(self) -> None:
        if websockets is None:
            return
        try:
            jwt_token = await self._get_jwt_token()
            query = {
                "jwtKey": jwt_token, "uniqueId": self._target_user,
                "schemaVersion": "v1", "features.bundleEvents": "true",
                "features.rawMessages": "false", "features.normalizeUniqueId": "true",
            }
            uri = f"{EULER_WS_BASE}?{urlencode(query)}"
            ws = await websockets.connect(uri, open_timeout=self._connect_timeout_sec, ping_interval=30, ping_timeout=15)
            self._websocket = ws
            log_json(self._logger, "info", "euler_connection", "websocket connected",
                     target_user=self._target_user, session_uptime_ms=self._session_uptime_ms())
            try:
                async for raw_msg in ws:
                    if self._stop:
                        break
                    try:
                        msg = json.loads(raw_msg)
                    except (json.JSONDecodeError, TypeError):
                        continue
                    ed = msg.get("data", msg) if isinstance(msg.get("data"), dict) else msg
                    en = str(ed.get("event", "") or ed.get("__class__", "")).lower()
                    if en in ("connectevent", "roominfo"):
                        rid = str(ed.get("roomId", "") or ed.get("room_id", ""))
                        if rid and not self._room_id:
                            self._room_id = rid
                        await self._status_callback(SessionStatus(
                            target_user=self._target_user, connection_state=ConnectionState.CONNECTED,
                            room_id=self._room_id, message="Connected via Euler Stream", timestamp_ms=utc_now_ms(),
                        ))
                        log_json(self._logger, "info", "euler_connection", "euler connected",
                                 target_user=self._target_user, room_id=self._room_id,
                                 session_uptime_ms=self._session_uptime_ms())
                        c = _euler_event_to_canonical(ed, room_id=self._room_id, session_id=self._session_id, target_user=self._target_user)
                        if c is not None:
                            self._session_events_received += 1
                            await self._event_callback(c)
                        continue
                    if en in ("end", "offline", "disconnectevent"):
                        c = _euler_event_to_canonical(ed, room_id=self._room_id, session_id=self._session_id, target_user=self._target_user)
                        if c is not None:
                            self._session_events_received += 1
                            await self._event_callback(c)
                        self._remote_live_ended = True
                        log_json(self._logger, "info", "euler_connection", "remote live ended", event=en,
                                 session_events=self._session_events_received, session_uptime_ms=self._session_uptime_ms())
                        break
                    c = _euler_event_to_canonical(ed, room_id=self._room_id, session_id=self._session_id, target_user=self._target_user)
                    if c is not None:
                        self._session_events_received += 1
                        if c.event_type == CanonicalEventType.GIFT:
                            self._session_gifts_received += 1
                        elif c.event_type == CanonicalEventType.CHAT:
                            self._session_chat_received += 1
                        await self._event_callback(c)
                if self._remote_live_ended:
                    raise EulerConnectionError("NOT_LIVE", "El live de TikTok termino.", raw_error="provider event: live ended")
                if not self._stop:
                    cc = getattr(ws, "close_code", None)
                    cr = str(getattr(ws, "close_reason", "") or "")
                    log_json(self._logger, "warning", "euler_connection", "websocket closed",
                             close_code=cc, close_reason=cr, session_events=self._session_events_received,
                             session_uptime_ms=self._session_uptime_ms())
                    if cc == _EULER_SESSION_LIMIT_CLOSE_CODE:
                        raise EulerConnectionError("API_SESSION_ENDED", "Euler cerro la sesion por limite.", raw_error=f"close code {cc}: {cr}")
                    if cc in _EULER_NOT_LIVE_CLOSE_CODES:
                        raise EulerConnectionError("NOT_LIVE", "El usuario no esta en vivo.", raw_error=f"close code {cc}: {cr}")
                    raise EulerConnectionError("STREAM_DISCONNECTED", "La conexion con Euler se cerro.", raw_error=f"close code {cc}: {cr}")
            except Exception:
                log_json(self._logger, "error", "euler_connection", "ws_loop_inner_error",
                         error=str(Exception), type=type(Exception).__name__)
                raise
            finally:
                self._websocket = None
                log_json(self._logger, "info", "euler_connection", "ws_loop cleanup",
                         session_events=self._session_events_received, session_uptime_ms=self._session_uptime_ms())
                try:
                    await ws.close()
                except Exception:
                    pass
        except asyncio.CancelledError:
            return
        except EulerConnectionError:
            raise
        except Exception as exc:
            code, message = classify_euler_error(str(exc))
            log_json(self._logger, "error", "euler_connection", "ws_loop_error", error=str(exc), code=code)
            raise EulerConnectionError(code, message, raw_error=str(exc)) from exc

    async def wait_closed(self) -> None:
        if self._ws_task is None:
            return
        try:
            await self._ws_task
        except asyncio.CancelledError:
            raise
        except EulerConnectionError:
            raise
        except Exception as exc:
            code, message = classify_euler_error(str(exc))
            raise EulerConnectionError(code, message, raw_error=str(exc)) from exc

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
