from __future__ import annotations

import json
from typing import Any

from event_models import CanonicalEvent, ConnectionState, SessionStatus
from event_normalizer import normalize_custom_raw_event


def decode_json_payload(payload: str) -> dict[str, Any]:
    parsed = json.loads(payload)
    if not isinstance(parsed, dict):
        raise ValueError("payload must be a JSON object")
    return parsed


def decode_canonical_event(payload: str | dict[str, Any]) -> CanonicalEvent:
    parsed = payload if isinstance(payload, dict) else decode_json_payload(payload)
    return normalize_custom_raw_event(parsed)


def decode_session_status(payload: str | dict[str, Any]) -> SessionStatus:
    parsed = payload if isinstance(payload, dict) else decode_json_payload(payload)
    if parsed.get("message_type") != "session_status":
        raise ValueError("payload is not a session_status message")
    state_value = str(parsed.get("connection_state") or ConnectionState.IDLE.value)
    try:
        connection_state = ConnectionState(state_value)
    except ValueError:
        connection_state = ConnectionState.IDLE
    return SessionStatus(
        target_user=str(parsed.get("target_user") or ""),
        connection_state=connection_state,
        room_id=str(parsed.get("room_id") or ""),
        message=str(parsed.get("message") or ""),
        timestamp_ms=int(parsed.get("timestamp_ms") or 0),
        retry_count=int(parsed.get("retry_count") or 0),
        uptime_ms=int(parsed.get("uptime_ms") or 0),
        last_event_timestamp_ms=int(parsed.get("last_event_timestamp_ms") or 0),
    )
