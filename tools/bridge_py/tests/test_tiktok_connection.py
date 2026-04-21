from __future__ import annotations

import unittest

from tiktok_connection import classify_tiktok_connect_error


class TikTokConnectionClassificationTests(unittest.TestCase):
    def test_user_not_found_error_classifies_legacy_exception_name(self) -> None:
        code, message = classify_tiktok_connect_error("TikTokLive v6.6.5 -> UserNotFoundError")
        self.assertEqual(code, "USER_NOT_FOUND")
        self.assertIn("No se encontro", message)

    def test_stream_disconnected_classifies_closed_stream(self) -> None:
        code, message = classify_tiktok_connect_error("stream disconnected")
        self.assertEqual(code, "STREAM_DISCONNECTED")
        self.assertIn("conexion con TikTok", message)


if __name__ == "__main__":
    unittest.main()
