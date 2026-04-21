from __future__ import annotations

import asyncio
import importlib
import re
import sys
from pathlib import Path
from types import ModuleType
from typing import Any, Awaitable, Callable
from urllib.parse import urlparse

from event_models import CanonicalEvent, CanonicalEventType, ConnectionState, SessionStatus
from event_normalizer import (
    normalize_chat_event,
    normalize_custom_raw_event,
    normalize_follow_event,
    normalize_gift_event,
    normalize_like_event,
    normalize_live_end_event,
    normalize_live_start_event,
    normalize_moderation_event,
    normalize_share_event,
    normalize_viewer_count_event,
    normalize_viewer_join_event,
)
from structured_logging import log_json, utc_now_ms


TIKTOK_USER_RE = re.compile(r"^[a-z0-9._-]{2,64}$")
TIKTOK_CHAT_EVENT_NAMES = (
    "CommentEvent",
    "CommentsEvent",
    "EmoteChatEvent",
    "QuestionNewEvent",
    "ScreenChatEvent",
)


class TikTokConnectionError(RuntimeError):
    def __init__(self, code: str, message: str, *, raw_error: str = "") -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.raw_error = raw_error


def normalize_tiktok_user(value: str) -> str:
    if value is None:
        return ""
    raw_value = str(value).strip()
    if not raw_value:
        return ""

    candidate = raw_value
    lower_value = raw_value.lower()
    if "tiktok.com/" in lower_value:
        url_value = raw_value
        if "://" not in url_value:
            url_value = f"https://{url_value.lstrip('/')}"
        parsed = urlparse(url_value)
        host = (parsed.netloc or "").strip().lower()
        path = (parsed.path or "").strip()
        if host.endswith("tiktok.com"):
            match = re.search(r"/@([^/?#]+)", path, flags=re.IGNORECASE)
            if match:
                candidate = match.group(1)
            else:
                candidate = path.strip("/").split("/", 1)[0]

    candidate = candidate.strip().split("?", 1)[0].split("#", 1)[0].strip()
    if candidate.startswith("@"):
        candidate = candidate[1:]
    candidate = candidate.strip().strip("/")
    if "/" in candidate:
        candidate = candidate.split("/", 1)[0]
    return candidate.lower()


def is_valid_tiktok_user(value: str) -> bool:
    normalized = normalize_tiktok_user(value)
    if not normalized:
        return False
    return bool(TIKTOK_USER_RE.fullmatch(normalized))


def _site_packages_candidates(legacy_bridge_root: str | Path) -> list[Path]:
    bridge_root = Path(__file__).resolve().parent
    candidates = [
        bridge_root / "python_runtime" / "Lib" / "site-packages",
        bridge_root / ".venv" / "Lib" / "site-packages",
    ]

    legacy_root_text = str(legacy_bridge_root or "").strip()
    if legacy_root_text:
        candidates.append(Path(legacy_root_text) / ".venv" / "Lib" / "site-packages")

    unique_candidates: list[Path] = []
    seen: set[str] = set()
    for candidate in candidates:
        key = str(candidate)
        if key in seen:
            continue
        seen.add(key)
        unique_candidates.append(candidate)

    return unique_candidates


def bootstrap_legacy_bridge_environment(legacy_bridge_root: str | Path) -> None:
    try:
        importlib.import_module("TikTokLive")
        return
    except ImportError:
        pass

    for site_packages in _site_packages_candidates(legacy_bridge_root):
        if not site_packages.exists():
            continue
        site_packages_text = str(site_packages)
        if site_packages_text not in sys.path:
            sys.path.insert(0, site_packages_text)
        try:
            importlib.import_module("TikTokLive")
            return
        except ImportError:
            continue

    searched = ", ".join(str(path) for path in _site_packages_candidates(legacy_bridge_root))
    raise RuntimeError(
        "TikTokLive is not available. Install it in the active environment, "
        "create tools/bridge_py/.venv with requirements.txt, or point LIVEPANEL_LEGACY_BRIDGE_ROOT/--legacy-bridge-root to another compatible bridge runtime. "
        f"Searched: {searched}"
    )


def classify_tiktok_connect_error(raw_error: str) -> tuple[str, str]:
    raw_text = str(raw_error or "").strip()
    lower = raw_text.lower()

    if any(token in lower for token in ("invalid unique_id", "invalid username", "invalid user", "bad username")):
        return "INVALID_USERNAME", "El username de TikTok es invalido."
    if any(token in lower for token in (
        "user_not_found",
        "user not found",
        "usernotfounderror",
        "profile not found",
        "does not exist",
        "cannot find user",
    )):
        return "USER_NOT_FOUND", "No se encontro ese usuario en TikTok."
    if any(token in lower for token in ("rate_limit", "too many connections", "free api key")):
        return "RATE_LIMIT", "Se agoto el limite del servicio de firmado."
    if any(token in lower for token in ("age restricted", "sessionid", "session id")):
        return "AGE_RESTRICTED", "El live requiere sesion autenticada de TikTok."
    if any(token in lower for token in ("detected by tiktok", "blocked by tiktok", "empty request", "rejected by tiktok", "sign api", "sign server")):
        return "ACCESS_BLOCKED", "TikTok rechazo o bloqueo temporalmente el acceso automatico al live."
    if any(token in lower for token in ("not live", "isn't live", "is not live", "offline", "live has ended", "user is not live")):
        return "NOT_LIVE", "El usuario no esta en vivo en este momento."
    if any(token in lower for token in ("stream disconnected", "stream_disconnected", "connection closed", "websocket closed")):
        return "STREAM_DISCONNECTED", "La conexion con TikTok se cerro poco despues de abrirse."
    if any(token in lower for token in ("timed out", "timeout", "network", "connection", "dns")):
        return "NETWORK_ERROR", "No se pudo conectar por un problema de red."
    return "UNKNOWN", "No se pudo completar la conexion con TikTok."


async def resolve_room_id_with_fallback(client: Any) -> tuple[str, str]:
    web_client = getattr(client, "_web", None)
    unique_id = getattr(client, "unique_id", "")
    if web_client is None or not unique_id:
        return "", ""

    try:
        room_id = int(await web_client.fetch_room_id_from_html(unique_id))
        return str(room_id), "html"
    except Exception:
        pass

    try:
        room_id = int(await web_client.fetch_room_id_from_api(unique_id))
        return str(room_id), "api"
    except Exception:
        return "", ""


class TikTokConnection:
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
    ) -> None:
        self._logger = logger
        self._legacy_bridge_root = legacy_bridge_root
        self._connect_timeout_sec = connect_timeout_sec
        self._event_callback = event_callback
        self._status_callback = status_callback
        self._target_user = normalize_tiktok_user(target_user)
        self._room_id = str(room_id or "").strip()
        self._room_id_source = "cli" if self._room_id else ""
        self._session_id = session_id or utc_now_ms()
        self._client: Any = None
        self._stream_task: asyncio.Task[Any] | None = None
        self._events_module: ModuleType | None = None

    @property
    def room_id(self) -> str:
        return self._room_id

    async def open(self) -> None:
        if not is_valid_tiktok_user(self._target_user):
            raise TikTokConnectionError("INVALID_USERNAME", "El username de TikTok es invalido.")

        bootstrap_legacy_bridge_environment(self._legacy_bridge_root)
        try:
            tiktok_module = importlib.import_module("TikTokLive")
            self._events_module = importlib.import_module("TikTokLive.events")
        except Exception as exc:
            raise TikTokConnectionError("BOOTSTRAP_FAILED", "No se pudo cargar TikTokLive.", raw_error=str(exc)) from exc

        client_class = getattr(tiktok_module, "TikTokLiveClient", None)
        if client_class is None:
            raise TikTokConnectionError("BOOTSTRAP_FAILED", "TikTokLiveClient no esta disponible.")

        self._client = client_class(unique_id=self._target_user)
        self._register_handlers()

        if not self._room_id:
            await self._status_callback(
                SessionStatus(
                    target_user=self._target_user,
                    connection_state=ConnectionState.RESOLVING_ROOM,
                    room_id="",
                    message="Resolving room id",
                    timestamp_ms=utc_now_ms(),
                )
            )
            self._room_id, self._room_id_source = await resolve_room_id_with_fallback(self._client)

        await self._status_callback(
            SessionStatus(
                target_user=self._target_user,
                connection_state=ConnectionState.CONNECTING,
                room_id=self._room_id,
                message="Opening TikTok live connection",
                timestamp_ms=utc_now_ms(),
            )
        )

        try:
            if self._room_id:
                self._stream_task = await asyncio.wait_for(
                    self._client.start(fetch_live_check=False, room_id=int(self._room_id)),
                    timeout=self._connect_timeout_sec,
                )
            else:
                self._stream_task = await asyncio.wait_for(
                    self._client.start(),
                    timeout=self._connect_timeout_sec,
                )
        except Exception as exc:
            code, message = classify_tiktok_connect_error(str(exc))
            raise TikTokConnectionError(code, message, raw_error=str(exc)) from exc

    async def wait_closed(self) -> None:
        if self._stream_task is None:
            return
        try:
            await self._stream_task
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            code, message = classify_tiktok_connect_error(str(exc))
            raise TikTokConnectionError(code, message, raw_error=str(exc)) from exc

    async def close(self) -> None:
        if self._client is None:
            return
        try:
            await self._client.disconnect()
        except Exception:
            pass

    def _register_handler(self, event_name: str, callback: Callable[[Any], Awaitable[None]]) -> None:
        if self._events_module is None or self._client is None:
            return
        event_class = getattr(self._events_module, event_name, None)
        if event_class is None:
            return

        @self._client.on(event_class)
        async def _handler(event: Any) -> None:
            await callback(event)

    def _register_handlers(self) -> None:
        assert self._client is not None

        async def on_connect(event: Any) -> None:
            runtime_room_id = getattr(self._client, "room_id", None)
            if runtime_room_id:
                self._room_id = str(runtime_room_id)
            await self._status_callback(
                SessionStatus(
                    target_user=self._target_user,
                    connection_state=ConnectionState.CONNECTED,
                    room_id=self._room_id,
                    message="Connected to TikTok live",
                    timestamp_ms=utc_now_ms(),
                )
            )
            await self._event_callback(
                normalize_live_start_event(
                    event,
                    target_user=self._target_user,
                    room_id=self._room_id,
                    session_id=self._session_id,
                )
            )

        async def on_comment(event: Any) -> None:
            await self._event_callback(normalize_chat_event(event, room_id=self._room_id, session_id=self._session_id))

        async def on_gift(event: Any) -> None:
            await self._event_callback(normalize_gift_event(event, room_id=self._room_id, session_id=self._session_id))

        async def on_like(event: Any) -> None:
            await self._event_callback(normalize_like_event(event, room_id=self._room_id, session_id=self._session_id))

        async def on_follow(event: Any) -> None:
            await self._event_callback(normalize_follow_event(event, room_id=self._room_id, session_id=self._session_id))

        async def on_share(event: Any) -> None:
            await self._event_callback(normalize_share_event(event, room_id=self._room_id, session_id=self._session_id))

        async def on_join(event: Any) -> None:
            await self._event_callback(normalize_viewer_join_event(event, room_id=self._room_id, session_id=self._session_id))

        async def on_viewer_count(event: Any) -> None:
            await self._event_callback(normalize_viewer_count_event(event, room_id=self._room_id, session_id=self._session_id))

        async def on_moderation(event: Any) -> None:
            await self._event_callback(normalize_moderation_event(event, room_id=self._room_id, session_id=self._session_id))

        async def on_control(event: Any) -> None:
            action = getattr(event, "action", None)
            if str(action) in {"3", "4", "END", "end"}:
                await self._event_callback(
                    normalize_live_end_event(
                        target_user=self._target_user,
                        room_id=self._room_id,
                        session_id=self._session_id,
                        message="live end control event",
                        raw_payload={"event_class": "ControlEvent", "action": action},
                    )
                )
                return

            await self._event_callback(
                normalize_custom_raw_event(
                    {
                        "event_type": CanonicalEventType.CUSTOM_RAW.value,
                        "actor": {
                            "id": self._target_user,
                            "username": self._target_user,
                            "display_name": self._target_user,
                            "avatar_url": "",
                        },
                        "metadata": {
                            "event_id": f"tt:{self._session_id}:control:{utc_now_ms()}",
                            "room_id": self._room_id,
                            "source_event_type": "control",
                            "timestamp_ms": utc_now_ms(),
                        },
                        "raw_payload": {
                            "event_class": getattr(getattr(event, '__class__', None), '__name__', 'ControlEvent'),
                            "action": action,
                        },
                    },
                    room_id=self._room_id,
                    target_user=self._target_user,
                )
            )

        self._register_handler("ConnectEvent", on_connect)
        for event_name in TIKTOK_CHAT_EVENT_NAMES:
            self._register_handler(event_name, on_comment)
        self._register_handler("GiftEvent", on_gift)
        self._register_handler("LikeEvent", on_like)
        self._register_handler("FollowEvent", on_follow)
        self._register_handler("ShareEvent", on_share)
        self._register_handler("JoinEvent", on_join)
        self._register_handler("RoomUserSeqEvent", on_viewer_count)
        self._register_handler("ControlEvent", on_control)
        self._register_handler("RoomMessageEvent", on_moderation)

        log_json(
            self._logger,
            "info",
            "tiktok_connection",
            "registered event handlers",
            target_user=self._target_user,
        )
