from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any


SCHEMA_VERSION = "1.1"
PLATFORM_NAME = "tiktok-live"


class CanonicalEventType(str, Enum):
    CHAT = "chat"
    LIKE = "like"
    GIFT = "gift"
    FOLLOW = "follow"
    SHARE = "share"
    VIEWER_JOIN = "viewer_join"
    VIEWER_COUNT = "viewer_count"
    LIVE_START = "live_start"
    LIVE_END = "live_end"
    MODERATION = "moderation"
    CUSTOM_RAW = "custom_raw"


class ConnectionState(str, Enum):
    IDLE = "idle"
    PREPARING = "preparing"
    RESOLVING_ROOM = "resolving_room"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    RECONNECTING = "reconnecting"
    DISCONNECTED = "disconnected"
    FAULTED = "faulted"
    STOPPED = "stopped"


@dataclass(slots=True)
class CanonicalActor:
    user_id: str = ""
    username: str = ""
    display_name: str = ""
    avatar_url: str = ""

    def to_dict(self) -> dict[str, Any]:
        return {
            "id": self.user_id,
            "username": self.username,
            "display_name": self.display_name,
            "avatar_url": self.avatar_url,
        }


@dataclass(slots=True)
class CanonicalGift:
    gift_id: str = ""
    gift_name: str = ""
    quantity: int = 0
    diamond_count: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "gift_id": self.gift_id,
            "gift_name": self.gift_name,
            "quantity": self.quantity,
            "diamond_count": self.diamond_count,
        }


@dataclass(slots=True)
class CanonicalMetadata:
    event_id: str = ""
    room_id: str = ""
    source_event_type: str = ""
    timestamp_ms: int = 0
    schema_version: str = SCHEMA_VERSION
    platform: str = PLATFORM_NAME
    moderation_action: str = ""
    retry_count: int = 0

    def to_dict(self) -> dict[str, Any]:
        payload = {
            "event_id": self.event_id,
            "room_id": self.room_id,
            "source_event_type": self.source_event_type,
            "timestamp_ms": self.timestamp_ms,
            "schema_version": self.schema_version,
            "platform": self.platform,
        }
        if self.moderation_action:
            payload["moderation_action"] = self.moderation_action
        if self.retry_count > 0:
            payload["retry_count"] = self.retry_count
        return payload


@dataclass(slots=True)
class CanonicalEvent:
    event_type: CanonicalEventType
    actor: CanonicalActor = field(default_factory=CanonicalActor)
    metadata: CanonicalMetadata = field(default_factory=CanonicalMetadata)
    text: str = ""
    gift: CanonicalGift | None = None
    viewer_count: int = 0
    like_count: int = 0
    raw_payload: dict[str, Any] | None = None
    latency_ms: int = 0

    def to_dict(self) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "message_type": "event",
            "schema_version": self.metadata.schema_version,
            "platform": self.metadata.platform,
            "event_type": self.event_type.value,
            "kind": self.event_type.value,
            "actor": self.actor.to_dict(),
            "metadata": self.metadata.to_dict(),
            "text": self.text,
            "gift": self.gift.to_dict() if self.gift is not None else None,
            "viewer_count": self.viewer_count,
            "like_count": self.like_count,
            "latency_ms": self.latency_ms,
        }
        if self.raw_payload is not None:
            payload["raw_payload"] = self.raw_payload
        return payload


@dataclass(slots=True)
class SessionStatus:
    target_user: str = ""
    connection_state: ConnectionState = ConnectionState.IDLE
    room_id: str = ""
    message: str = ""
    timestamp_ms: int = 0
    retry_count: int = 0
    uptime_ms: int = 0
    last_event_timestamp_ms: int = 0

    def to_panel_payload(self) -> dict[str, Any]:
        payload = {
            "message_type": "session_status",
            "target_user": self.target_user,
            "room_id": self.room_id,
            "connection_state": self.connection_state.value,
            "message": self.message,
            "timestamp_ms": self.timestamp_ms,
        }
        if self.retry_count > 0:
            payload["retry_count"] = self.retry_count
        if self.uptime_ms > 0:
            payload["uptime_ms"] = self.uptime_ms
        if self.last_event_timestamp_ms > 0:
            payload["last_event_timestamp_ms"] = self.last_event_timestamp_ms
        return payload


@dataclass(slots=True)
class ConnectionHealth:
    connected: bool = False
    connection_state: str = ConnectionState.IDLE.value
    heartbeat_age_ms: int = 0
    last_event_age_ms: int = 0
    retry_count: int = 0
    connected_since_ms: int = 0
    last_error: str = ""

    def to_dict(self) -> dict[str, Any]:
        return {
            "connected": self.connected,
            "connection_state": self.connection_state,
            "heartbeat_age_ms": self.heartbeat_age_ms,
            "last_event_age_ms": self.last_event_age_ms,
            "retry_count": self.retry_count,
            "connected_since_ms": self.connected_since_ms,
            "last_error": self.last_error,
        }
