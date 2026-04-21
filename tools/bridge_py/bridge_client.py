from __future__ import annotations

import asyncio
import json
from pathlib import Path
from typing import Any


Payload = dict[str, Any]


def _actor_payload(
    user_id: str,
    username: str,
    display_name: str,
    avatar_url: str = "",
    is_follower: bool = False,
    is_subscriber: bool = False,
    is_moderator: bool = False,
) -> Payload:
    return {
        "id": user_id,
        "username": username,
        "display_name": display_name,
        "avatar_url": avatar_url,
        "is_follower": bool(is_follower),
        "is_subscriber": bool(is_subscriber),
        "is_moderator": bool(is_moderator),
    }


def _metadata_payload(
    event_id: str,
    room_id: str,
    source_event_type: str,
    timestamp_ms: int,
) -> Payload:
    return {
        "event_id": event_id,
        "room_id": room_id,
        "source_event_type": source_event_type,
        "timestamp_ms": timestamp_ms,
    }


def build_session_status(
    *,
    target_user: str,
    connection_state: str,
    room_id: str = "",
    message: str = "",
    timestamp_ms: int = 0,
) -> Payload:
    return {
        "message_type": "session_status",
        "target_user": target_user,
        "room_id": room_id,
        "connection_state": connection_state,
        "message": message,
        "timestamp_ms": timestamp_ms,
    }


def _base_event_payload(
    *,
    kind: str,
    user_id: str,
    username: str,
    display_name: str,
    event_id: str,
    room_id: str,
    source_event_type: str,
    timestamp_ms: int,
    avatar_url: str = "",
    is_follower: bool = False,
    is_subscriber: bool = False,
    is_moderator: bool = False,
    text: str = "",
    gift: Payload | None = None,
    viewer_count: int = 0,
) -> Payload:
    return {
        "kind": kind,
        "actor": _actor_payload(
            user_id,
            username,
            display_name,
            avatar_url,
            is_follower=is_follower,
            is_subscriber=is_subscriber,
            is_moderator=is_moderator,
        ),
        "metadata": _metadata_payload(event_id, room_id, source_event_type, timestamp_ms),
        "text": text,
        "gift": gift,
        "viewer_count": viewer_count,
    }


def build_chat_event(
    *,
    user_id: str,
    username: str,
    display_name: str,
    text: str,
    event_id: str,
    room_id: str,
    timestamp_ms: int,
    avatar_url: str = "",
    viewer_count: int = 0,
    is_follower: bool = False,
    is_subscriber: bool = False,
    is_moderator: bool = False,
) -> Payload:
    return _base_event_payload(
        kind="chat",
        user_id=user_id,
        username=username,
        display_name=display_name,
        event_id=event_id,
        room_id=room_id,
        source_event_type="comment",
        timestamp_ms=timestamp_ms,
        avatar_url=avatar_url,
        is_follower=is_follower,
        is_subscriber=is_subscriber,
        is_moderator=is_moderator,
        text=text,
        gift=None,
        viewer_count=viewer_count,
    )


def build_gift_event(
    *,
    user_id: str,
    username: str,
    display_name: str,
    event_id: str,
    room_id: str,
    timestamp_ms: int,
    gift_id: str,
    gift_name: str,
    quantity: int,
    diamond_count: int,
    avatar_url: str = "",
    viewer_count: int = 0,
    is_follower: bool = False,
    is_subscriber: bool = False,
    is_moderator: bool = False,
) -> Payload:
    return _base_event_payload(
        kind="gift",
        user_id=user_id,
        username=username,
        display_name=display_name,
        event_id=event_id,
        room_id=room_id,
        source_event_type="gift",
        timestamp_ms=timestamp_ms,
        avatar_url=avatar_url,
        is_follower=is_follower,
        is_subscriber=is_subscriber,
        is_moderator=is_moderator,
        text="",
        gift={
            "gift_id": gift_id,
            "gift_name": gift_name,
            "quantity": quantity,
            "diamond_count": diamond_count,
        },
        viewer_count=viewer_count,
    )


def build_follow_event(
    *,
    user_id: str,
    username: str,
    display_name: str,
    event_id: str,
    room_id: str,
    timestamp_ms: int,
    avatar_url: str = "",
    viewer_count: int = 0,
    is_follower: bool = False,
    is_subscriber: bool = False,
    is_moderator: bool = False,
) -> Payload:
    return _base_event_payload(
        kind="follow",
        user_id=user_id,
        username=username,
        display_name=display_name,
        event_id=event_id,
        room_id=room_id,
        source_event_type="follow",
        timestamp_ms=timestamp_ms,
        avatar_url=avatar_url,
        is_follower=is_follower,
        is_subscriber=is_subscriber,
        is_moderator=is_moderator,
        text="",
        gift=None,
        viewer_count=viewer_count,
    )


def build_like_event(
    *,
    user_id: str,
    username: str,
    display_name: str,
    event_id: str,
    room_id: str,
    timestamp_ms: int,
    like_count: int,
    avatar_url: str = "",
    viewer_count: int = 0,
    is_follower: bool = False,
    is_subscriber: bool = False,
    is_moderator: bool = False,
) -> Payload:
    payload = _base_event_payload(
        kind="like",
        user_id=user_id,
        username=username,
        display_name=display_name,
        event_id=event_id,
        room_id=room_id,
        source_event_type="like",
        timestamp_ms=timestamp_ms,
        avatar_url=avatar_url,
        is_follower=is_follower,
        is_subscriber=is_subscriber,
        is_moderator=is_moderator,
        text="",
        gift=None,
        viewer_count=viewer_count,
    )
    payload["like_count"] = max(0, int(like_count))
    return payload


def build_share_event(
    *,
    user_id: str,
    username: str,
    display_name: str,
    event_id: str,
    room_id: str,
    timestamp_ms: int,
    avatar_url: str = "",
    viewer_count: int = 0,
    is_follower: bool = False,
    is_subscriber: bool = False,
    is_moderator: bool = False,
) -> Payload:
    return _base_event_payload(
        kind="share",
        user_id=user_id,
        username=username,
        display_name=display_name,
        event_id=event_id,
        room_id=room_id,
        source_event_type="share",
        timestamp_ms=timestamp_ms,
        avatar_url=avatar_url,
        is_follower=is_follower,
        is_subscriber=is_subscriber,
        is_moderator=is_moderator,
        text="",
        gift=None,
        viewer_count=viewer_count,
    )


def build_viewer_count_event(
    *,
    event_id: str,
    room_id: str,
    timestamp_ms: int,
    viewer_count: int,
    source_event_type: str = "viewer_count",
) -> Payload:
    return _base_event_payload(
        kind="viewer_count",
        user_id="viewer-count",
        username="viewer-count",
        display_name="Viewer Count",
        event_id=event_id,
        room_id=room_id,
        source_event_type=source_event_type,
        timestamp_ms=timestamp_ms,
        avatar_url="",
        text="",
        gift=None,
        viewer_count=viewer_count,
    )


def build_live_start_event(
    *,
    target_user: str,
    event_id: str,
    room_id: str,
    timestamp_ms: int,
    text: str = "live_start",
) -> Payload:
    return _base_event_payload(
        kind="live_start",
        user_id=target_user,
        username=target_user,
        display_name=target_user,
        event_id=event_id,
        room_id=room_id,
        source_event_type="live_start",
        timestamp_ms=timestamp_ms,
        avatar_url="",
        text=text,
        gift=None,
        viewer_count=0,
    )


def build_live_end_event(
    *,
    target_user: str,
    event_id: str,
    room_id: str,
    timestamp_ms: int,
    text: str = "live_end",
) -> Payload:
    return _base_event_payload(
        kind="live_end",
        user_id=target_user,
        username=target_user,
        display_name=target_user,
        event_id=event_id,
        room_id=room_id,
        source_event_type="live_end",
        timestamp_ms=timestamp_ms,
        avatar_url="",
        text=text,
        gift=None,
        viewer_count=0,
    )


def build_moderation_event(
    *,
    user_id: str,
    username: str,
    display_name: str,
    event_id: str,
    room_id: str,
    timestamp_ms: int,
    moderation_action: str,
    avatar_url: str = "",
) -> Payload:
    payload = _base_event_payload(
        kind="moderation",
        user_id=user_id,
        username=username,
        display_name=display_name,
        event_id=event_id,
        room_id=room_id,
        source_event_type="moderation",
        timestamp_ms=timestamp_ms,
        avatar_url=avatar_url,
        text=moderation_action,
        gift=None,
        viewer_count=0,
    )
    payload["metadata"]["moderation_action"] = moderation_action
    payload["moderation_action"] = moderation_action
    return payload


def build_custom_raw_event(
    *,
    user_id: str,
    username: str,
    display_name: str,
    event_id: str,
    room_id: str,
    timestamp_ms: int,
    source_event_type: str,
    text: str = "",
    raw_payload: Payload | None = None,
    avatar_url: str = "",
) -> Payload:
    payload = _base_event_payload(
        kind="custom_raw",
        user_id=user_id,
        username=username,
        display_name=display_name,
        event_id=event_id,
        room_id=room_id,
        source_event_type=source_event_type,
        timestamp_ms=timestamp_ms,
        avatar_url=avatar_url,
        text=text,
        gift=None,
        viewer_count=0,
    )
    if raw_payload is not None:
        payload["raw_payload"] = raw_payload
    return payload


def build_viewer_join_event(
    *,
    user_id: str,
    username: str,
    display_name: str,
    event_id: str,
    room_id: str,
    timestamp_ms: int,
    avatar_url: str = "",
    viewer_count: int = 0,
) -> Payload:
    return _base_event_payload(
        kind="viewer_join",
        user_id=user_id,
        username=username,
        display_name=display_name,
        event_id=event_id,
        room_id=room_id,
        source_event_type="join",
        timestamp_ms=timestamp_ms,
        avatar_url=avatar_url,
        text="",
        gift=None,
        viewer_count=viewer_count,
    )


def payload_to_json(payload: Payload) -> str:
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


def append_jsonl(path: str | Path, payload: Payload) -> None:
    target_path = Path(path)
    target_path.parent.mkdir(parents=True, exist_ok=True)
    with target_path.open("a", encoding="utf-8", newline="\n") as output:
        output.write(payload_to_json(payload))
        output.write("\n")


def write_json(path: str | Path, payload: Payload) -> None:
    target_path = Path(path)
    target_path.parent.mkdir(parents=True, exist_ok=True)
    target_path.write_text(payload_to_json(payload), encoding="utf-8")


async def emit_ws(url: str, payload: Payload) -> None:
    try:
        import websockets  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "Optional dependency 'websockets' is not installed. "
            "Use JSONL mode now or install it later for WS tests."
        ) from exc

    async with websockets.connect(url) as websocket:
        await websocket.send(payload_to_json(payload))


def emit_ws_sync(url: str, payload: Payload) -> None:
    asyncio.run(emit_ws(url, payload))
