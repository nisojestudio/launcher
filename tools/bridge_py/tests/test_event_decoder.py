from __future__ import annotations

import unittest

from bridge_client import (
    build_chat_event,
    build_custom_raw_event,
    build_moderation_event,
    build_session_status,
    build_viewer_count_event,
)
from event_decoder import decode_canonical_event, decode_session_status
from event_models import CanonicalEventType, ConnectionState


class EventDecoderTests(unittest.TestCase):
    def test_decode_chat_payload(self) -> None:
        payload = build_chat_event(
            user_id="user-01",
            username="alice",
            display_name="Alice",
            text="Hola bridge",
            event_id="evt-chat-001",
            room_id="room-001",
            timestamp_ms=1710000001000,
        )

        event = decode_canonical_event(payload)

        self.assertEqual(event.event_type, CanonicalEventType.CHAT)
        self.assertEqual(event.actor.display_name, "Alice")
        self.assertEqual(event.metadata.event_id, "evt-chat-001")
        self.assertEqual(event.metadata.room_id, "room-001")
        self.assertEqual(event.text, "Hola bridge")

    def test_decode_special_payloads(self) -> None:
        viewer_count_event = decode_canonical_event(
            build_viewer_count_event(
                event_id="evt-viewers-001",
                room_id="room-002",
                timestamp_ms=1710000002000,
                viewer_count=321,
            )
        )
        self.assertEqual(viewer_count_event.event_type, CanonicalEventType.VIEWER_COUNT)
        self.assertEqual(viewer_count_event.viewer_count, 321)

        moderation_event = decode_canonical_event(
            build_moderation_event(
                user_id="mod-01",
                username="moderator",
                display_name="Moderator",
                event_id="evt-mod-001",
                room_id="room-003",
                timestamp_ms=1710000003000,
                moderation_action="mute",
            )
        )
        self.assertEqual(moderation_event.event_type, CanonicalEventType.MODERATION)
        self.assertEqual(moderation_event.metadata.moderation_action, "mute")
        self.assertEqual(moderation_event.text, "mute")

        custom_raw_event = decode_canonical_event(
            build_custom_raw_event(
                user_id="raw-01",
                username="raw-user",
                display_name="Raw User",
                event_id="evt-raw-001",
                room_id="room-004",
                timestamp_ms=1710000004000,
                source_event_type="control",
                text="custom payload",
                raw_payload={"foo": "bar", "count": 7},
            )
        )
        self.assertEqual(custom_raw_event.event_type, CanonicalEventType.CUSTOM_RAW)
        self.assertIsInstance(custom_raw_event.raw_payload, dict)
        self.assertEqual(custom_raw_event.raw_payload["foo"], "bar")

    def test_decode_session_status(self) -> None:
        status = decode_session_status(
            build_session_status(
                target_user="alice",
                connection_state="connected",
                room_id="room-005",
                message="Connected",
                timestamp_ms=1710000005000,
            )
        )

        self.assertEqual(status.target_user, "alice")
        self.assertEqual(status.connection_state, ConnectionState.CONNECTED)
        self.assertEqual(status.room_id, "room-005")
        self.assertEqual(status.message, "Connected")


if __name__ == "__main__":
    unittest.main()
