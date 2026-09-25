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
    is_follower: bool = False
    is_subscriber: bool = False
    is_moderator: bool = False

    def to_dict(self) -> dict[str, Any]:
        return {
            "id": self.user_id,
            "username": self.username,
            "display_name": self.display_name,
            "avatar_url": self.avatar_url,
            "is_follower": self.is_follower,
            "is_subscriber": self.is_subscriber,
            "is_moderator": self.is_moderator,
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
    # Campos de diagnostico visible en el panel (monitor del live).
    severity: str = ""
    alert_code: str = ""
    alert_action: str = ""
    phase: str = ""
    provider: str = ""
    key_label: str = ""
    retry_in_sec: float = 0.0
    # Presupuesto diario de conexiones (0 = sin tope configurado, no se envia).
    # El panel lo muestra para que el operador vea cuantas aperturas le quedan
    # hoy antes de que el proveedor corte la sesion.
    daily_budget_total: int = 0
    daily_budget_remaining: int = 0
    daily_budget_manual_reserve: int = 0
    daily_budget_remaining_auto: int = 0

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
        # Solo se envian los campos presentes: el panel tolera payloads viejos.
        if self.severity:
            payload["severity"] = self.severity
        if self.alert_code:
            payload["alert_code"] = self.alert_code
        if self.alert_action:
            payload["alert_action"] = self.alert_action
        if self.phase:
            payload["phase"] = self.phase
        if self.provider:
            payload["provider"] = self.provider
        if self.key_label:
            payload["key_label"] = self.key_label
        if self.retry_in_sec > 0:
            payload["retry_in_sec"] = round(float(self.retry_in_sec), 2)
        # Presupuesto diario: solo si hay tope (total > 0), para que un bridge
        # sin limite no mande ceros que el panel interpretaria como "agotado".
        if self.daily_budget_total > 0:
            payload["daily_budget_total"] = int(self.daily_budget_total)
            payload["daily_budget_remaining"] = max(0, int(self.daily_budget_remaining))
            payload["daily_budget_manual_reserve"] = max(0, int(self.daily_budget_manual_reserve))
            payload["daily_budget_remaining_auto"] = max(0, int(self.daily_budget_remaining_auto))
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
