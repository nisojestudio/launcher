from __future__ import annotations

import json
import time
from typing import Any

from event_models import (
    CanonicalActor,
    CanonicalEvent,
    CanonicalEventType,
    CanonicalGift,
    CanonicalMetadata,
)
from tiktok_adapter import (
    _actor_fields,
    _timestamp_ms,
    clamp_int,
    clamp_text,
    extract_chat_message,
    extract_tiktok_event_identity,
    extract_viewer_count,
    resolve_room_id,
    safe_attr,
    safe_user,
    tiktok_source_event_type,
)


def canonical_event_to_panel_payload(event: CanonicalEvent) -> dict[str, Any]:
    payload = event.to_dict()
    payload["kind"] = event.event_type.value
    payload["text"] = event.text
    payload["gift"] = event.gift.to_dict() if event.gift is not None else None
    payload["viewer_count"] = event.viewer_count
    payload["like_count"] = event.like_count
    return payload


def _base_event(
    *,
    event_type: CanonicalEventType,
    actor: CanonicalActor,
    metadata: CanonicalMetadata,
    text: str = "",
    gift: CanonicalGift | None = None,
    viewer_count: int = 0,
    like_count: int = 0,
    raw_payload: dict[str, Any] | None = None,
) -> CanonicalEvent:
    now_ms = int(time.time() * 1000)
    latency_ms = 0
    if metadata.timestamp_ms > 0 and now_ms >= metadata.timestamp_ms:
        latency_ms = now_ms - metadata.timestamp_ms
    return CanonicalEvent(
        event_type=event_type,
        actor=actor,
        metadata=metadata,
        text=text,
        gift=gift,
        viewer_count=max(0, viewer_count),
        like_count=max(0, like_count),
        raw_payload=raw_payload,
        latency_ms=latency_ms,
    )


def _actor_from_user(event: Any) -> CanonicalActor:
    user = safe_user(event)
    actor_id, username, display_name, avatar_url, is_follower, is_subscriber, is_moderator = _actor_fields(user)
    return CanonicalActor(
        user_id=actor_id,
        username=username,
        display_name=display_name,
        avatar_url=avatar_url,
        is_follower=is_follower,
        is_subscriber=is_subscriber,
        is_moderator=is_moderator,
    )


def _actor_payload_flag(actor_payload: dict[str, Any], snake_key: str, camel_key: str) -> bool:
    value = actor_payload.get(snake_key)
    if value is None:
        value = actor_payload.get(camel_key)
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        normalized = value.strip().lower()
        if normalized in ("true", "1", "yes", "on"):
            return True
        if normalized in ("false", "0", "no", "off", ""):
            return False
    return False


def _metadata_from_tiktok_event(
    event_type: CanonicalEventType,
    event: Any,
    actor_id: str,
    *,
    room_id: Any = "",
    session_id: str | int | None = None,
    payload_data: dict[str, Any] | None = None,
    source_event_type: str = "",
    moderation_action: str = "",
) -> CanonicalMetadata:
    identity = extract_tiktok_event_identity(
        event_type.value,
        event,
        actor_id,
        data=payload_data or {},
        session_id=session_id,
    )
    return CanonicalMetadata(
        event_id=identity["event_id"],
        room_id=resolve_room_id(room_id, event),
        source_event_type=source_event_type or event_type.value,
        timestamp_ms=_timestamp_ms(identity),
        moderation_action=moderation_action,
    )


def _custom_raw_payload(event: Any) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "event_class": getattr(getattr(event, "__class__", None), "__name__", "UnknownEvent"),
    }
    for field_name in (
        "action",
        "action_type",
        "status",
        "count",
        "total",
        "total_likes",
        "viewer_count",
        "room_user_count",
        "msg_type",
        "message",
        "comment",
        "content",
        "text",
        "action_content",
    ):
        field_value = safe_attr(event, field_name, None)
        if field_value is not None:
            payload[field_name] = field_value if isinstance(field_value, (str, int, float, bool)) else str(field_value)
    question = safe_attr(event, "question", None)
    question_content = safe_attr(question, "content", None)
    if question_content is not None:
        payload["question_content"] = (
            question_content if isinstance(question_content, (str, int, float, bool)) else str(question_content)
        )
    return payload


def normalize_chat_event(event: Any, *, room_id: Any = "", session_id: str | int | None = None) -> CanonicalEvent:
    actor = _actor_from_user(event)
    message = extract_chat_message(event)
    metadata = _metadata_from_tiktok_event(
        CanonicalEventType.CHAT,
        event,
        actor.user_id or actor.username or "unknown",
        room_id=room_id,
        session_id=session_id,
        payload_data={"message": message},
        source_event_type=tiktok_source_event_type(event),
    )
    return _base_event(
        event_type=CanonicalEventType.CHAT,
        actor=actor,
        metadata=metadata,
        text=message,
        viewer_count=extract_viewer_count(event),
        raw_payload=_custom_raw_payload(event),
    )


def normalize_like_event(event: Any, *, room_id: Any = "", session_id: str | int | None = None) -> CanonicalEvent:
    actor = _actor_from_user(event)
    like_count = clamp_int(
        safe_attr(event, "count", None) or safe_attr(event, "like_count", None) or 1,
        default=1,
        min_value=1,
        max_value=10_000_000,
    )
    viewer_count = extract_viewer_count(event)
    metadata = _metadata_from_tiktok_event(
        CanonicalEventType.LIKE,
        event,
        actor.user_id or actor.username or "unknown",
        room_id=room_id,
        session_id=session_id,
        payload_data={"count": like_count},
        source_event_type="like",
    )
    return _base_event(
        event_type=CanonicalEventType.LIKE,
        actor=actor,
        metadata=metadata,
        like_count=like_count,
        viewer_count=viewer_count,
        raw_payload=_custom_raw_payload(event),
    )


def normalize_gift_event(event: Any, *, room_id: Any = "", session_id: str | int | None = None) -> CanonicalEvent:
    actor = _actor_from_user(event)
    gift = safe_attr(event, "gift", None)
    gift_payload = CanonicalGift(
        gift_id=clamp_text(safe_attr(gift, "id", None), "", 80),
        gift_name=clamp_text(safe_attr(gift, "name", "gift"), "gift", 120),
        quantity=clamp_int(
            safe_attr(gift, "count", None)
            or safe_attr(event, "repeat_count", None)
            or safe_attr(event, "count", None)
            or 1,
            default=1,
            min_value=1,
            max_value=10_000,
        ),
        diamond_count=clamp_int(
            safe_attr(gift, "diamond_count", 0),
            default=0,
            min_value=0,
            max_value=1_000_000,
        ),
    )
    metadata = _metadata_from_tiktok_event(
        CanonicalEventType.GIFT,
        event,
        actor.user_id or actor.username or "unknown",
        room_id=room_id,
        session_id=session_id,
        payload_data=gift_payload.to_dict(),
        source_event_type="gift",
    )
    return _base_event(
        event_type=CanonicalEventType.GIFT,
        actor=actor,
        metadata=metadata,
        gift=gift_payload,
        viewer_count=extract_viewer_count(event),
        raw_payload=_custom_raw_payload(event),
    )


def normalize_follow_event(event: Any, *, room_id: Any = "", session_id: str | int | None = None) -> CanonicalEvent:
    actor = _actor_from_user(event)
    metadata = _metadata_from_tiktok_event(
        CanonicalEventType.FOLLOW,
        event,
        actor.user_id or actor.username or "unknown",
        room_id=room_id,
        session_id=session_id,
        source_event_type="follow",
    )
    return _base_event(
        event_type=CanonicalEventType.FOLLOW,
        actor=actor,
        metadata=metadata,
        viewer_count=extract_viewer_count(event),
        raw_payload=_custom_raw_payload(event),
    )


def normalize_share_event(event: Any, *, room_id: Any = "", session_id: str | int | None = None) -> CanonicalEvent:
    actor = _actor_from_user(event)
    metadata = _metadata_from_tiktok_event(
        CanonicalEventType.SHARE,
        event,
        actor.user_id or actor.username or "unknown",
        room_id=room_id,
        session_id=session_id,
        source_event_type="share",
    )
    return _base_event(
        event_type=CanonicalEventType.SHARE,
        actor=actor,
        metadata=metadata,
        viewer_count=extract_viewer_count(event),
        raw_payload=_custom_raw_payload(event),
    )


def normalize_viewer_join_event(event: Any, *, room_id: Any = "", session_id: str | int | None = None) -> CanonicalEvent:
    actor = _actor_from_user(event)
    metadata = _metadata_from_tiktok_event(
        CanonicalEventType.VIEWER_JOIN,
        event,
        actor.user_id or actor.username or "unknown",
        room_id=room_id,
        session_id=session_id,
        source_event_type="viewer_join",
    )
    return _base_event(
        event_type=CanonicalEventType.VIEWER_JOIN,
        actor=actor,
        metadata=metadata,
        viewer_count=extract_viewer_count(event),
        raw_payload=_custom_raw_payload(event),
    )


def normalize_viewer_count_event(
    event: Any,
    *,
    room_id: Any = "",
    session_id: str | int | None = None,
) -> CanonicalEvent:
    actor = CanonicalActor()
    viewer_count = clamp_int(
        safe_attr(event, "viewer_count", None)
        or safe_attr(event, "room_user_count", None)
        or safe_attr(event, "total", None)
        or safe_attr(event, "total_user", None)
        or extract_viewer_count(event),
        default=0,
        min_value=0,
        max_value=100_000_000,
    )
    metadata = _metadata_from_tiktok_event(
        CanonicalEventType.VIEWER_COUNT,
        event,
        "viewer-count",
        room_id=room_id,
        session_id=session_id,
        payload_data={"viewer_count": viewer_count},
        source_event_type="viewer_count",
    )
    return _base_event(
        event_type=CanonicalEventType.VIEWER_COUNT,
        actor=actor,
        metadata=metadata,
        viewer_count=viewer_count,
        raw_payload=_custom_raw_payload(event),
    )


def normalize_live_start_event(
    event: Any,
    *,
    target_user: str,
    room_id: Any = "",
    session_id: str | int | None = None,
) -> CanonicalEvent:
    actor = CanonicalActor(user_id=target_user, username=target_user, display_name=target_user)
    metadata = _metadata_from_tiktok_event(
        CanonicalEventType.LIVE_START,
        event,
        actor.user_id or "live",
        room_id=room_id,
        session_id=session_id,
        source_event_type="live_start",
    )
    return _base_event(
        event_type=CanonicalEventType.LIVE_START,
        actor=actor,
        metadata=metadata,
        raw_payload=_custom_raw_payload(event),
    )


def normalize_live_end_event(
    *,
    target_user: str,
    room_id: str = "",
    session_id: str | int | None = None,
    message: str = "stream ended",
    timestamp_ms: int | None = None,
    raw_payload: dict[str, Any] | None = None,
) -> CanonicalEvent:
    actor = CanonicalActor(user_id=target_user, username=target_user, display_name=target_user)
    metadata = CanonicalMetadata(
        event_id=f"tt:{session_id or '0'}:live_end:{timestamp_ms or int(time.time() * 1000)}",
        room_id=room_id,
        source_event_type="live_end",
        timestamp_ms=timestamp_ms or int(time.time() * 1000),
    )
    return _base_event(
        event_type=CanonicalEventType.LIVE_END,
        actor=actor,
        metadata=metadata,
        text=message,
        raw_payload=raw_payload or {"reason": message},
    )


def normalize_moderation_event(
    event: Any,
    *,
    room_id: Any = "",
    session_id: str | int | None = None,
) -> CanonicalEvent:
    actor = _actor_from_user(event)
    moderation_action = clamp_text(
        safe_attr(event, "action", None)
        or safe_attr(event, "action_type", None)
        or safe_attr(event, "msg_type", None)
        or "moderation",
        "moderation",
        120,
    )
    metadata = _metadata_from_tiktok_event(
        CanonicalEventType.MODERATION,
        event,
        actor.user_id or actor.username or "moderation",
        room_id=room_id,
        session_id=session_id,
        payload_data={"moderation_action": moderation_action},
        source_event_type="moderation",
        moderation_action=moderation_action,
    )
    return _base_event(
        event_type=CanonicalEventType.MODERATION,
        actor=actor,
        metadata=metadata,
        text=moderation_action,
        raw_payload=_custom_raw_payload(event),
    )


def normalize_custom_raw_event(
    payload: dict[str, Any],
    *,
    room_id: str = "",
    target_user: str = "",
    timestamp_ms: int | None = None,
) -> CanonicalEvent:
    event_type = clamp_text(
        payload.get("event_type") or payload.get("kind"),
        CanonicalEventType.CUSTOM_RAW.value,
        64,
    )
    actor_payload = payload.get("actor", {}) if isinstance(payload.get("actor"), dict) else {}
    actor = CanonicalActor(
        user_id=clamp_text(actor_payload.get("id"), target_user, 64),
        username=clamp_text(actor_payload.get("username"), target_user, 64),
        display_name=clamp_text(actor_payload.get("display_name"), target_user or "custom_raw", 80),
        avatar_url=clamp_text(actor_payload.get("avatar_url"), "", 4096),
        is_follower=_actor_payload_flag(actor_payload, "is_follower", "isFollower"),
        is_subscriber=_actor_payload_flag(actor_payload, "is_subscriber", "isSubscriber"),
        is_moderator=_actor_payload_flag(actor_payload, "is_moderator", "isModerator"),
    )
    metadata_payload = payload.get("metadata", {}) if isinstance(payload.get("metadata"), dict) else {}
    final_timestamp_ms = clamp_int(
        metadata_payload.get("timestamp_ms", timestamp_ms or int(time.time() * 1000)),
        default=int(time.time() * 1000),
        min_value=1,
        max_value=9_999_999_999_999,
    )
    metadata = CanonicalMetadata(
        event_id=clamp_text(metadata_payload.get("event_id"), f"tt:custom:{final_timestamp_ms}", 160),
        room_id=clamp_text(metadata_payload.get("room_id"), room_id, 64),
        source_event_type=clamp_text(metadata_payload.get("source_event_type"), event_type, 64),
        timestamp_ms=final_timestamp_ms,
        moderation_action=clamp_text(metadata_payload.get("moderation_action"), "", 120),
    )
    gift_payload = payload.get("gift", {}) if isinstance(payload.get("gift"), dict) else {}
    gift = None
    if gift_payload:
        gift = CanonicalGift(
            gift_id=clamp_text(gift_payload.get("gift_id"), "", 80),
            gift_name=clamp_text(gift_payload.get("gift_name"), "", 120),
            quantity=clamp_int(gift_payload.get("quantity"), default=0, min_value=0, max_value=10_000),
            diamond_count=clamp_int(gift_payload.get("diamond_count"), default=0, min_value=0, max_value=1_000_000),
        )
    try:
        parsed_event_type = CanonicalEventType(event_type)
    except ValueError:
        parsed_event_type = CanonicalEventType.CUSTOM_RAW
    return _base_event(
        event_type=parsed_event_type,
        actor=actor,
        metadata=metadata,
        text=clamp_text(payload.get("text"), "", 300),
        gift=gift,
        viewer_count=clamp_int(payload.get("viewer_count"), default=0, min_value=0, max_value=100_000_000),
        like_count=clamp_int(payload.get("like_count"), default=0, min_value=0, max_value=100_000_000),
        raw_payload=payload.get("raw_payload") if isinstance(payload.get("raw_payload"), dict) else payload,
    )


def canonical_event_to_json(event: CanonicalEvent) -> str:
    return json.dumps(event.to_dict(), ensure_ascii=False, separators=(",", ":"))
