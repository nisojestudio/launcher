from __future__ import annotations

import unittest
from types import SimpleNamespace

from event_models import CanonicalEventType
from event_normalizer import (
    normalize_chat_event,
    normalize_gift_event,
    normalize_viewer_join_event,
)


def make_user(
    *,
    user_id: str,
    username: str,
    display_name: str,
    avatar_url: str,
    is_follower: bool = False,
    is_subscriber: bool = False,
    is_moderator: bool = False,
) -> SimpleNamespace:
    return SimpleNamespace(
        user_id=user_id,
        unique_id=username,
        nickname=display_name,
        avatar_url=avatar_url,
        isFollower=is_follower,
        isSubscriber=is_subscriber,
        isModerator=is_moderator,
    )


class EventNormalizerTests(unittest.TestCase):
    def test_normalize_chat_preserves_actor_fields(self) -> None:
        event = SimpleNamespace(
            user=make_user(
                user_id="chat-user",
                username="chatuser",
                display_name="Chat User",
                avatar_url="https://example.com/chat.png",
            ),
            comment="hola",
        )

        normalized = normalize_chat_event(event, room_id="room-001", session_id="session-001")

        self.assertEqual(normalized.event_type, CanonicalEventType.CHAT)
        self.assertEqual(normalized.actor.user_id, "chat-user")
        self.assertEqual(normalized.actor.display_name, "Chat User")
        self.assertEqual(normalized.actor.avatar_url, "https://example.com/chat.png")
        self.assertEqual(normalized.text, "hola")

    def test_normalize_viewer_join_preserves_avatar(self) -> None:
        event = SimpleNamespace(
            user=make_user(
                user_id="join-user",
                username="joinuser",
                display_name="Join User",
                avatar_url="https://example.com/join.png",
            ),
            viewer_count=701,
        )

        normalized = normalize_viewer_join_event(event, room_id="room-002", session_id="session-002")

        self.assertEqual(normalized.event_type, CanonicalEventType.VIEWER_JOIN)
        self.assertEqual(normalized.actor.user_id, "join-user")
        self.assertEqual(normalized.actor.avatar_url, "https://example.com/join.png")
        self.assertEqual(normalized.viewer_count, 701)

    def test_normalize_gift_preserves_quantity_and_value(self) -> None:
        event = SimpleNamespace(
            user=make_user(
                user_id="gift-user",
                username="giftuser",
                display_name="Gift User",
                avatar_url="https://example.com/gift.png",
            ),
            gift=SimpleNamespace(
                id="gift-rose",
                name="Rose",
                count=2,
                diamond_count=1,
            ),
            repeat_count=2,
            viewer_count=702,
        )

        normalized = normalize_gift_event(event, room_id="room-003", session_id="session-003")

        self.assertEqual(normalized.event_type, CanonicalEventType.GIFT)
        self.assertEqual(normalized.actor.user_id, "gift-user")
        self.assertEqual(normalized.actor.avatar_url, "https://example.com/gift.png")
        self.assertIsNotNone(normalized.gift)
        self.assertEqual(normalized.gift.gift_name, "Rose")
        self.assertEqual(normalized.gift.quantity, 2)
        self.assertEqual(normalized.gift.diamond_count, 1)


if __name__ == "__main__":
    unittest.main()
