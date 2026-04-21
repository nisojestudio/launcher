from __future__ import annotations

import logging
import unittest
from types import SimpleNamespace

from tiktok_connection import TIKTOK_CHAT_EVENT_NAMES, TikTokConnection, classify_tiktok_connect_error


async def noop_callback(_event: object) -> None:
    return None


class FakeClient:
    def __init__(self) -> None:
        self.registered_event_names: list[str] = []

    def on(self, event_class: type) -> object:
        def decorator(callback: object) -> object:
            self.registered_event_names.append(event_class.__name__)
            return callback

        return decorator


def fake_events_module(*names: str) -> SimpleNamespace:
    return SimpleNamespace(**{name: type(name, (), {}) for name in names})


class TikTokConnectionClassificationTests(unittest.TestCase):
    def test_user_not_found_error_classifies_legacy_exception_name(self) -> None:
        code, message = classify_tiktok_connect_error("TikTokLive v6.6.5 -> UserNotFoundError")
        self.assertEqual(code, "USER_NOT_FOUND")
        self.assertIn("No se encontro", message)

    def test_stream_disconnected_classifies_closed_stream(self) -> None:
        code, message = classify_tiktok_connect_error("stream disconnected")
        self.assertEqual(code, "STREAM_DISCONNECTED")
        self.assertIn("conexion con TikTok", message)

    def test_registers_alternate_chat_events(self) -> None:
        connection = TikTokConnection(
            logger=logging.getLogger("test.tiktok_connection"),
            legacy_bridge_root="",
            connect_timeout_sec=1.0,
            event_callback=noop_callback,
            status_callback=noop_callback,
            target_user="curmita01",
        )
        client = FakeClient()
        connection._client = client
        connection._events_module = fake_events_module(
            "ConnectEvent",
            *TIKTOK_CHAT_EVENT_NAMES,
            "GiftEvent",
            "LikeEvent",
            "FollowEvent",
            "ShareEvent",
            "JoinEvent",
            "RoomUserSeqEvent",
            "ControlEvent",
            "RoomMessageEvent",
        )

        connection._register_handlers()

        for event_name in TIKTOK_CHAT_EVENT_NAMES:
            self.assertIn(event_name, client.registered_event_names)


if __name__ == "__main__":
    unittest.main()
