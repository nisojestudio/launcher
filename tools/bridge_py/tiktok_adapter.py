from __future__ import annotations

import hashlib
import json
import time
from typing import Any

from bridge_client import (
    Payload,
    build_chat_event,
    build_follow_event,
    build_gift_event,
    build_share_event,
    build_viewer_join_event,
)

MAX_EVENT_TIMESTAMP_MS = 9_999_999_999_999

CHAT_EVENT_NAMES = frozenset(
    (
        "CommentEvent",
        "CommentsEvent",
        "EmoteChatEvent",
        "QuestionNewEvent",
        "ScreenChatEvent",
    )
)

CHAT_SOURCE_EVENT_TYPES = {
    "CommentEvent": "comment",
    "CommentsEvent": "comments",
    "EmoteChatEvent": "emote_chat",
    "QuestionNewEvent": "question_new",
    "ScreenChatEvent": "screen_chat",
}


def clamp_int(value: Any, default: int = 0, min_value: int = 0, max_value: int = 1_000_000) -> int:
    try:
        parsed = int(value)
    except Exception:
        return default
    if parsed < min_value:
        return min_value
    if parsed > max_value:
        return max_value
    return parsed


def clamp_text(value: Any, default: str = "", max_len: int = 128) -> str:
    try:
        text = str(value if value is not None else default)
    except Exception:
        text = str(default)
    text = text.replace("\x00", "").strip()
    if len(text) > max_len:
        return text[:max_len]
    return text


def safe_attr(value: Any, name: str, default: Any = None) -> Any:
    if isinstance(value, dict):
        return value.get(name, default)
    try:
        return getattr(value, name, default)
    except Exception:
        return default


def first_string_from_urls(urls: Any) -> str:
    try:
        if isinstance(urls, (list, tuple)) and urls:
            return clamp_text(urls[0], "", 4096)
    except Exception:
        return ""
    return ""


def first_string_from_image_model(image_like: Any) -> str:
    if image_like is None:
        return ""
    for candidate in (
        safe_attr(image_like, "m_urls", None),
        safe_attr(image_like, "urls", None),
        safe_attr(image_like, "url_list", None),
    ):
        extracted = first_string_from_urls(candidate)
        if extracted:
            return extracted
    single_url = (
        safe_attr(image_like, "m_uri", "")
        or safe_attr(image_like, "uri", "")
        or safe_attr(image_like, "url", "")
    )
    return clamp_text(single_url, "", 4096)


def normalize_event_time(value: Any) -> int | None:
    try:
        parsed = int(value)
    except Exception:
        return None
    if parsed <= 0:
        return None
    if parsed < 10_000_000_000:
        return parsed * 1000
    while parsed > MAX_EVENT_TIMESTAMP_MS:
        parsed //= 1000
    return parsed if parsed > 0 else None


def tiktok_event_class_name(event: Any) -> str:
    return clamp_text(safe_attr(safe_attr(event, "__class__", None), "__name__", ""), "", 80)


def tiktok_source_event_type(event: Any) -> str:
    event_name = tiktok_event_class_name(event)
    if event_name in CHAT_SOURCE_EVENT_TYPES:
        return CHAT_SOURCE_EVENT_TYPES[event_name]
    if event_name.endswith("Event"):
        event_name = event_name[:-5]
    return clamp_text(event_name.lower(), "comment", 64)


def text_from_tiktok_model(value: Any, *, max_len: int = 300) -> str:
    if value is None:
        return ""
    if isinstance(value, str):
        return clamp_text(value, "", max_len)
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return clamp_text(str(value), "", max_len)

    pieces = safe_attr(value, "pieces", None)
    if isinstance(pieces, (list, tuple)) and pieces:
        joined = "".join(
            clamp_text(safe_attr(piece, "string_value", ""), "", max_len)
            for piece in pieces
        )
        if joined:
            return clamp_text(joined, "", max_len)

    for field_name in ("text", "content", "string_value", "default_pattern", "action_content", "message"):
        text = clamp_text(safe_attr(value, field_name, ""), "", max_len)
        if text:
            return text
    return ""


def extract_chat_message(event: Any) -> str:
    for field_name in ("comment", "text", "content", "message", "action_content"):
        text = text_from_tiktok_model(safe_attr(event, field_name, None))
        if text:
            return text

    question = safe_attr(event, "question", None)
    question_text = text_from_tiktok_model(safe_attr(question, "content", None))
    if question_text:
        return question_text

    content_list = safe_attr(event, "content_list", None)
    if isinstance(content_list, (list, tuple)) and content_list:
        parts: list[str] = []
        for item in content_list:
            text = text_from_tiktok_model(safe_attr(item, "text", None) or safe_attr(item, "content", None))
            if text:
                parts.append(text)
        if parts:
            return clamp_text(" ".join(parts), "", 300)

    emote_list = safe_attr(event, "emote_list", None)
    if isinstance(emote_list, (list, tuple)) and emote_list:
        return "emote" if len(emote_list) == 1 else f"{len(emote_list)} emotes"

    return ""


def normalize_event_identity_value(value: Any, max_len: int = 160) -> str | None:
    if value is None or isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        try:
            parsed = int(value)
        except Exception:
            return None
        return str(parsed) if parsed > 0 else None
    text = clamp_text(value, "", max_len)
    return text or None


def build_stable_event_id(
    kind: str,
    user_id: str,
    data: dict[str, Any] | None = None,
    *,
    session_id: str | int | None = None,
    message_id: Any = None,
    log_id: Any = None,
    create_time: Any = None,
) -> str:
    safe_kind = clamp_text(kind, "unknown", 32)
    safe_user_id = clamp_text(user_id, "unknown", 64)
    safe_session = normalize_event_identity_value(session_id, 32) or "0"
    safe_message_id = normalize_event_identity_value(message_id, 80)
    safe_log_id = normalize_event_identity_value(log_id, 120)
    safe_create_time = normalize_event_time(create_time)

    if safe_message_id:
        return f"tt:{safe_session}:{safe_kind}:msg:{safe_message_id}"
    if safe_log_id:
        return f"tt:{safe_session}:{safe_kind}:log:{safe_log_id}"

    fingerprint_payload = {
        "kind": safe_kind,
        "userId": safe_user_id,
        "sessionId": safe_session,
        "createTime": safe_create_time,
        "data": data if isinstance(data, dict) else {},
    }
    digest = hashlib.sha1(
        json.dumps(
            fingerprint_payload,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=True,
        ).encode("utf-8")
    ).hexdigest()[:20]
    return f"tt:{safe_session}:{safe_kind}:fp:{digest}"


def extract_tiktok_event_identity(
    kind: str,
    event: Any,
    user_id: str,
    data: dict[str, Any] | None = None,
    *,
    session_id: str | int | None = None,
) -> dict[str, Any]:
    base_message = safe_attr(event, "base_message", None)
    message_id = (
        safe_attr(base_message, "message_id", None)
        or safe_attr(event, "message_id", None)
        or safe_attr(event, "msg_id", None)
    )
    log_id = (
        safe_attr(base_message, "log_id", None)
        or safe_attr(event, "log_id", None)
    )
    question = safe_attr(event, "question", None)
    create_time = (
        safe_attr(base_message, "create_time", None)
        or safe_attr(event, "create_time", None)
        or safe_attr(question, "create_time", None)
        or safe_attr(event, "timestamp", None)
    )
    return {
        "event_id": build_stable_event_id(
            kind,
            user_id,
            data=data,
            session_id=session_id,
            message_id=message_id,
            log_id=log_id,
            create_time=create_time,
        ),
        "message_id": normalize_event_identity_value(message_id, 80),
        "log_id": normalize_event_identity_value(log_id, 120),
        "timestamp_ms": normalize_event_time(create_time),
    }


def safe_user(event: Any) -> dict[str, str]:
    question = safe_attr(event, "question", None)
    user = (
        safe_attr(event, "user", None)
        or safe_attr(event, "user_info", None)
        or safe_attr(question, "user", None)
    )
    if user is None:
        return {
            "user_id": "",
            "unique_id": "",
            "nickname": "Unknown",
            "avatar_url": "",
            "is_follower": False,
            "is_subscriber": False,
            "is_moderator": False,
        }

    unique_id = clamp_text(
        safe_attr(user, "unique_id", "")
        or safe_attr(user, "username", "")
        or safe_attr(user, "sec_uid", ""),
        "",
        64,
    )
    user_id = clamp_text(
        safe_attr(user, "user_id", "")
        or safe_attr(user, "id", "")
        or unique_id,
        "",
        64,
    )
    nickname = clamp_text(
        safe_attr(user, "nickname", "")
        or safe_attr(user, "nick_name", "")
        or safe_attr(user, "name", "")
        or safe_attr(user, "username", "")
        or "Unknown",
        "Unknown",
        80,
    ) or "Unknown"

    avatar_url = (
        clamp_text(safe_attr(user, "avatar_url", "") or safe_attr(user, "avatarUrl", ""), "", 4096)
        or first_string_from_image_model(safe_attr(user, "avatar_large", None))
        or first_string_from_image_model(safe_attr(user, "avatar_medium", None))
        or first_string_from_image_model(safe_attr(user, "avatar_thumb", None))
        or first_string_from_image_model(safe_attr(user, "avatar", None))
        or first_string_from_image_model(safe_attr(user, "profile_picture", None))
        or ""
    )

    is_follower = bool(
        safe_attr(user, "isFollower", False)
        or safe_attr(event, "isFollower", False)
        or safe_attr(event, "follower", False)
    )
    is_subscriber = bool(
        safe_attr(user, "isSubscriber", False)
        or safe_attr(user, "isMember", False)
        or safe_attr(event, "isSubscriber", False)
        or safe_attr(event, "subscriber", False)
        or safe_attr(event, "member", False)
    )
    is_moderator = bool(
        safe_attr(user, "isModerator", False)
        or safe_attr(user, "moderator", False)
        or safe_attr(event, "isModerator", False)
        or safe_attr(event, "moderator", False)
    )

    return {
        "user_id": user_id,
        "unique_id": unique_id,
        "nickname": nickname,
        "avatar_url": avatar_url,
        "is_follower": is_follower,
        "is_subscriber": is_subscriber,
        "is_moderator": is_moderator,
    }


def resolve_room_id(room_id: Any, event: Any = None) -> str:
    candidate = room_id
    if candidate in (None, ""):
        base_message = safe_attr(event, "base_message", None)
        candidate = (
            safe_attr(event, "room_id", None)
            or safe_attr(event, "roomId", None)
            or safe_attr(base_message, "room_id", None)
            or safe_attr(base_message, "roomId", None)
        )
    normalized = normalize_event_identity_value(candidate, 64)
    return normalized or ""


def extract_viewer_count(event: Any) -> int:
    return clamp_int(
        safe_attr(event, "viewer_count", None)
        or safe_attr(event, "room_user_count", None)
        or safe_attr(event, "member_count", None)
        or safe_attr(event, "total_user", None)
        or 0,
        default=0,
        min_value=0,
        max_value=100_000_000,
    )


def _timestamp_ms(identity: dict[str, Any]) -> int:
    return clamp_int(
        identity.get("timestamp_ms"),
        default=int(time.time() * 1000),
        min_value=1,
        max_value=MAX_EVENT_TIMESTAMP_MS,
    )


def _actor_fields(user: dict[str, Any]) -> tuple[str, str, str, str, bool, bool, bool]:
    actor_id = user["user_id"] or user["unique_id"] or "unknown"
    username = user["unique_id"] or user["user_id"] or actor_id
    display_name = user["nickname"] or username or actor_id
    avatar_url = user["avatar_url"] or ""
    return (
        actor_id,
        username,
        display_name,
        avatar_url,
        bool(user.get("is_follower", False)),
        bool(user.get("is_subscriber", False)),
        bool(user.get("is_moderator", False)),
    )


def to_external_chat_event(event: Any, *, room_id: Any = "", session_id: str | int | None = None) -> Payload:
    user = safe_user(event)
    actor_id, username, display_name, avatar_url, is_follower, is_subscriber, is_moderator = _actor_fields(user)
    message = extract_chat_message(event)
    data = {"message": message, "comment": message}
    identity = extract_tiktok_event_identity("chat", event, actor_id, data=data, session_id=session_id)
    return build_chat_event(
        user_id=actor_id,
        username=username,
        display_name=display_name,
        avatar_url=avatar_url,
        text=message,
        event_id=identity["event_id"],
        room_id=resolve_room_id(room_id, event),
        timestamp_ms=_timestamp_ms(identity),
        viewer_count=extract_viewer_count(event),
        is_follower=is_follower,
        is_subscriber=is_subscriber,
        is_moderator=is_moderator,
    )


def to_external_gift_event(event: Any, *, room_id: Any = "", session_id: str | int | None = None) -> Payload:
    user = safe_user(event)
    actor_id, username, display_name, avatar_url, is_follower, is_subscriber, is_moderator = _actor_fields(user)
    gift = safe_attr(event, "gift", None)
    gift_id = normalize_event_identity_value(safe_attr(gift, "id", None), 80) or ""
    gift_name = clamp_text(safe_attr(gift, "name", "gift"), "gift", 120)
    quantity = clamp_int(
        safe_attr(gift, "count", None)
        or safe_attr(event, "repeat_count", None)
        or safe_attr(event, "count", None)
        or 1,
        default=1,
        min_value=1,
        max_value=1000,
    )
    diamond_count = clamp_int(
        safe_attr(gift, "diamond_count", 0),
        default=0,
        min_value=0,
        max_value=1_000_000,
    )
    data = {
        "giftId": gift_id or None,
        "giftName": gift_name,
        "count": quantity,
        "diamond": diamond_count,
    }
    identity = extract_tiktok_event_identity("gift", event, actor_id, data=data, session_id=session_id)
    return build_gift_event(
        user_id=actor_id,
        username=username,
        display_name=display_name,
        avatar_url=avatar_url,
        event_id=identity["event_id"],
        room_id=resolve_room_id(room_id, event),
        timestamp_ms=_timestamp_ms(identity),
        gift_id=gift_id,
        gift_name=gift_name,
        quantity=quantity,
        diamond_count=diamond_count,
        viewer_count=extract_viewer_count(event),
        is_follower=is_follower,
        is_subscriber=is_subscriber,
        is_moderator=is_moderator,
    )


def to_external_follow_event(event: Any, *, room_id: Any = "", session_id: str | int | None = None) -> Payload:
    user = safe_user(event)
    actor_id, username, display_name, avatar_url, is_follower, is_subscriber, is_moderator = _actor_fields(user)
    identity = extract_tiktok_event_identity("follow", event, actor_id, data={}, session_id=session_id)
    return build_follow_event(
        user_id=actor_id,
        username=username,
        display_name=display_name,
        avatar_url=avatar_url,
        event_id=identity["event_id"],
        room_id=resolve_room_id(room_id, event),
        timestamp_ms=_timestamp_ms(identity),
        viewer_count=extract_viewer_count(event),
        is_follower=is_follower,
        is_subscriber=is_subscriber,
        is_moderator=is_moderator,
    )


def to_external_share_event(event: Any, *, room_id: Any = "", session_id: str | int | None = None) -> Payload:
    user = safe_user(event)
    actor_id, username, display_name, avatar_url, is_follower, is_subscriber, is_moderator = _actor_fields(user)
    identity = extract_tiktok_event_identity("share", event, actor_id, data={}, session_id=session_id)
    return build_share_event(
        user_id=actor_id,
        username=username,
        display_name=display_name,
        avatar_url=avatar_url,
        event_id=identity["event_id"],
        room_id=resolve_room_id(room_id, event),
        timestamp_ms=_timestamp_ms(identity),
        viewer_count=extract_viewer_count(event),
        is_follower=is_follower,
        is_subscriber=is_subscriber,
        is_moderator=is_moderator,
    )


def to_external_viewer_join_event(event: Any, *, room_id: Any = "", session_id: str | int | None = None) -> Payload:
    user = safe_user(event)
    actor_id, username, display_name, avatar_url, is_follower, is_subscriber, is_moderator = _actor_fields(user)
    identity = extract_tiktok_event_identity("viewer_join", event, actor_id, data={}, session_id=session_id)
    return build_viewer_join_event(
        user_id=actor_id,
        username=username,
        display_name=display_name,
        avatar_url=avatar_url,
        event_id=identity["event_id"],
        room_id=resolve_room_id(room_id, event),
        timestamp_ms=_timestamp_ms(identity),
        viewer_count=extract_viewer_count(event),
        is_follower=is_follower,
        is_subscriber=is_subscriber,
        is_moderator=is_moderator,
    )


def adapt_known_tiktok_event(event: Any, *, room_id: Any = "", session_id: str | int | None = None) -> Payload | None:
    event_name = safe_attr(safe_attr(event, "__class__", None), "__name__", "")
    adapters = {name: to_external_chat_event for name in CHAT_EVENT_NAMES}
    adapters.update({
        "GiftEvent": to_external_gift_event,
        "FollowEvent": to_external_follow_event,
        "ShareEvent": to_external_share_event,
        "JoinEvent": to_external_viewer_join_event,
    })
    adapter = adapters.get(str(event_name))
    if adapter is None:
        return None
    return adapter(event, room_id=room_id, session_id=session_id)
