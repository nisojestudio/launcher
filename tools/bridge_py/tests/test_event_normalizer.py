from __future__ import annotations

import unittest
from types import SimpleNamespace

from event_models import CanonicalEventType
from event_normalizer import (
    normalize_chat_event,
    normalize_custom_raw_event,
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

    def test_normalize_chat_preserves_and_serializes_actor_flags(self) -> None:
        event = SimpleNamespace(
            user=make_user(
                user_id="flag-user",
                username="flaguser",
                display_name="Flag User",
                avatar_url="https://example.com/flag.png",
                is_follower=True,
                is_subscriber=True,
                is_moderator=True,
            ),
            comment="hola flags",
        )

        normalized = normalize_chat_event(event, room_id="room-001", session_id="session-001")
        payload = normalized.to_dict()

        self.assertTrue(normalized.actor.is_follower)
        self.assertTrue(normalized.actor.is_subscriber)
        self.assertTrue(normalized.actor.is_moderator)
        self.assertEqual(
            payload["actor"],
            {
                "id": "flag-user",
                "username": "flaguser",
                "display_name": "Flag User",
                "avatar_url": "https://example.com/flag.png",
                "is_follower": True,
                "is_subscriber": True,
                "is_moderator": True,
            },
        )

    def test_normalize_custom_raw_preserves_actor_flags_from_snake_and_camel_case(self) -> None:
        cases = (
            {
                "is_follower": True,
                "is_subscriber": True,
                "is_moderator": True,
            },
            {
                "isFollower": True,
                "isSubscriber": True,
                "isModerator": True,
            },
        )

        for actor_flags in cases:
            with self.subTest(actor_flags=actor_flags):
                normalized = normalize_custom_raw_event(
                    {
                        "event_type": "chat",
                        "actor": {
                            "id": "custom-user",
                            "username": "customuser",
                            "display_name": "Custom User",
                            **actor_flags,
                        },
                        "text": "custom flags",
                    },
                    room_id="room-001",
                    target_user="fallback-user",
                    timestamp_ms=1_776_700_000_000,
                )

                self.assertTrue(normalized.actor.is_follower)
                self.assertTrue(normalized.actor.is_subscriber)
                self.assertTrue(normalized.actor.is_moderator)
                self.assertTrue(normalized.to_dict()["actor"]["is_follower"])
                self.assertTrue(normalized.to_dict()["actor"]["is_subscriber"])
                self.assertTrue(normalized.to_dict()["actor"]["is_moderator"])

    def test_normalize_custom_raw_parses_string_and_numeric_false_actor_flags(self) -> None:
        normalized = normalize_custom_raw_event(
            {
                "event_type": "chat",
                "actor": {
                    "id": "custom-user",
                    "username": "customuser",
                    "display_name": "Custom User",
                    "isFollower": "false",
                    "isSubscriber": "0",
                    "isModerator": 0,
                },
                "text": "custom flags",
            },
            room_id="room-001",
            target_user="fallback-user",
            timestamp_ms=1_776_700_000_000,
        )

        self.assertFalse(normalized.actor.is_follower)
        self.assertFalse(normalized.actor.is_subscriber)
        self.assertFalse(normalized.actor.is_moderator)
        self.assertFalse(normalized.to_dict()["actor"]["is_follower"])
        self.assertFalse(normalized.to_dict()["actor"]["is_subscriber"])
        self.assertFalse(normalized.to_dict()["actor"]["is_moderator"])

    def test_normalize_chat_preserves_millisecond_timestamp(self) -> None:
        event = SimpleNamespace(
            user=make_user(
                user_id="chat-user",
                username="chatuser",
                display_name="Chat User",
                avatar_url="",
            ),
            comment="hola",
            base_message=SimpleNamespace(create_time=1_776_700_000_000, message_id="msg-001"),
        )

        normalized = normalize_chat_event(event, room_id="room-001", session_id="session-001")

        self.assertEqual(normalized.metadata.timestamp_ms, 1_776_700_000_000)

    def test_normalize_chat_converts_second_timestamp_to_milliseconds(self) -> None:
        event = SimpleNamespace(
            user=make_user(
                user_id="chat-user",
                username="chatuser",
                display_name="Chat User",
                avatar_url="",
            ),
            comment="hola",
            base_message=SimpleNamespace(create_time=1_776_700_000, message_id="msg-002"),
        )

        normalized = normalize_chat_event(event, room_id="room-001", session_id="session-001")

        self.assertEqual(normalized.metadata.timestamp_ms, 1_776_700_000_000)

    def test_normalize_screen_chat_uses_user_info_and_content(self) -> None:
        screen_chat_event = type("ScreenChatEvent", (), {})()
        screen_chat_event.user_info = make_user(
            user_id="screen-user",
            username="screenuser",
            display_name="Screen User",
            avatar_url="https://example.com/screen.png",
        )
        screen_chat_event.content = "mensaje en pantalla"
        screen_chat_event.base_message = SimpleNamespace(create_time=1_776_700_000_000, message_id="screen-001")

        normalized = normalize_chat_event(screen_chat_event, room_id="room-001", session_id="session-001")

        self.assertEqual(normalized.event_type, CanonicalEventType.CHAT)
        self.assertEqual(normalized.actor.user_id, "screen-user")
        self.assertEqual(normalized.text, "mensaje en pantalla")
        self.assertEqual(normalized.metadata.source_event_type, "screen_chat")

    def test_normalize_question_chat_uses_nested_question_user_and_content(self) -> None:
        question_event = type("QuestionNewEvent", (), {})()
        question_event.question = SimpleNamespace(
            user=make_user(
                user_id="question-user",
                username="questionuser",
                display_name="Question User",
                avatar_url="https://example.com/question.png",
            ),
            content="pregunta del chat",
            create_time=1_776_700_000,
        )

        normalized = normalize_chat_event(question_event, room_id="room-001", session_id="session-001")

        self.assertEqual(normalized.event_type, CanonicalEventType.CHAT)
        self.assertEqual(normalized.actor.user_id, "question-user")
        self.assertEqual(normalized.text, "pregunta del chat")
        self.assertEqual(normalized.metadata.timestamp_ms, 1_776_700_000_000)
        self.assertEqual(normalized.metadata.source_event_type, "question_new")

    def test_normalize_emote_chat_records_placeholder_text(self) -> None:
        emote_event = type("EmoteChatEvent", (), {})()
        emote_event.user = make_user(
            user_id="emote-user",
            username="emoteuser",
            display_name="Emote User",
            avatar_url="",
        )
        emote_event.emote_list = [SimpleNamespace(emote_id="smile")]

        normalized = normalize_chat_event(emote_event, room_id="room-001", session_id="session-001")

        self.assertEqual(normalized.event_type, CanonicalEventType.CHAT)
        self.assertEqual(normalized.actor.user_id, "emote-user")
        self.assertEqual(normalized.text, "emote")
        self.assertEqual(normalized.metadata.source_event_type, "emote_chat")

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
