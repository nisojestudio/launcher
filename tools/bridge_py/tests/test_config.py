from __future__ import annotations

import tempfile
import textwrap
import unittest
from unittest.mock import patch
from pathlib import Path

from bridge_config import load_bridge_config


class BridgeConfigTests(unittest.TestCase):
    def test_safe_config_redacts_tiktools_api_key(self) -> None:
        config = load_bridge_config()
        config.connection.api_key = "secret-value"

        self.assertEqual(config.to_safe_dict()["connection"]["api_key"], "***")

    def test_load_bridge_config_from_yaml(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            config_path = Path(temp_dir) / "bridge_config.yaml"
            config_path.write_text(
                textwrap.dedent(
                    """
                    connection_mode: tiktok_live
                    connection:
                      username: alice
                      connect_timeout_sec: 33
                    retry_policy:
                      enabled: true
                      base_delay_sec: 5
                    buffer:
                      size: 8192
                      batch_size: 128
                    output:
                      panel_ws_url: ws://127.0.0.1:9123
                      control_port: 9911
                    replay:
                      enabled: true
                      jsonl_path: session.jsonl
                    logging:
                      level: DEBUG
                    max_events: 17
                    max_seconds: 90
                    """
                ).strip(),
                encoding="utf-8",
            )

            config = load_bridge_config(config_path)

        self.assertEqual(config.connection.username, "alice")
        self.assertEqual(config.connection.connect_timeout_sec, 33.0)
        self.assertTrue(config.retry_policy.enabled)
        self.assertEqual(config.retry_policy.base_delay_sec, 5.0)
        self.assertEqual(config.buffer.size, 8192)
        self.assertEqual(config.buffer.batch_size, 128)
        self.assertEqual(config.output.panel_ws_url, "ws://127.0.0.1:9123")
        self.assertEqual(config.output.control_port, 9911)
        self.assertTrue(config.replay.enabled)
        self.assertEqual(config.replay.jsonl_path, "session.jsonl")
        self.assertEqual(config.logging.level, "DEBUG")
        self.assertEqual(config.max_events, 17)
        self.assertEqual(config.max_seconds, 90)

    def test_load_bridge_config_reads_legacy_root_from_env(self) -> None:
        with patch.dict("os.environ", {"LIVEPANEL_LEGACY_BRIDGE_ROOT": r"C:\bridge-runtime"}, clear=False):
            config = load_bridge_config(Path("missing-bridge-config.yaml"))

        self.assertEqual(config.legacy_bridge_root, r"C:\bridge-runtime")

    def test_load_bridge_config_normalizes_direct_provider(self) -> None:
        with patch.dict("os.environ", {"LIVEPANEL_TIKTOK_PROVIDER": "tiktok_live"}, clear=False):
            config = load_bridge_config(Path("missing-bridge-config.yaml"))

        self.assertEqual(config.connection_mode, "direct")

    def test_sound_alerts_default_to_requiring_a_panel(self) -> None:
        """Por defecto se silencian sin panel: es el pitido con el panel cerrado."""
        with patch.dict("os.environ", {}, clear=True):
            config = load_bridge_config(Path("missing-bridge-config.yaml"))

        self.assertTrue(config.sound_alerts.enabled)
        self.assertTrue(config.sound_alerts.require_panel)

    def test_load_bridge_config_reads_sound_alerts_section(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            config_path = Path(temp_dir) / "bridge_config.yaml"
            config_path.write_text(
                textwrap.dedent(
                    """
                    sound_alerts:
                      enabled: false
                      require_panel: false
                    """
                ).strip(),
                encoding="utf-8",
            )
            config = load_bridge_config(config_path)

        self.assertFalse(config.sound_alerts.enabled)
        self.assertFalse(config.sound_alerts.require_panel)

    def test_sound_alerts_env_override_wins_over_yaml(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            config_path = Path(temp_dir) / "bridge_config.yaml"
            config_path.write_text("sound_alerts:\n  enabled: true\n", encoding="utf-8")
            with patch.dict("os.environ", {"LIVEPANEL_BRIDGE_SOUND_ALERTS_ENABLED": "false"}, clear=True):
                config = load_bridge_config(config_path)

        self.assertFalse(config.sound_alerts.enabled)


if __name__ == "__main__":
    unittest.main()
