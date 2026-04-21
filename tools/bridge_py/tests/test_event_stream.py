from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from bridge_config import BridgeConfig
from event_stream import TikTokBridgeService


class EventStreamTests(unittest.IsolatedAsyncioTestCase):
    async def test_simulate_burst_flushes_before_stop(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "burst.jsonl"
            config = BridgeConfig()
            config.output.panel_ws_url = ""
            config.output.output_jsonl = str(output_path)
            config.output.broadcast_ws_enabled = False
            config.output.control_port = 0
            config.logging.log_path = str(Path(temp_dir) / "bridge.log.jsonl")

            service = TikTokBridgeService(config)
            await service.start()
            try:
                result = await service.simulate_burst(count=120)
            finally:
                await service.stop()

            self.assertEqual(result["events_emitted"], 120)
            self.assertTrue(output_path.exists())
            self.assertEqual(len(output_path.read_text(encoding="utf-8").strip().splitlines()), 120)

    async def test_file_outputs_survive_panel_ws_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "burst_with_ws_failure.jsonl"
            config = BridgeConfig()
            config.output.panel_ws_url = "ws://127.0.0.1:9"
            config.output.output_jsonl = str(output_path)
            config.output.broadcast_ws_enabled = False
            config.output.control_port = 0
            config.logging.log_path = str(Path(temp_dir) / "bridge-with-ws-failure.log.jsonl")

            service = TikTokBridgeService(config)
            await service.start()
            try:
                result = await service.simulate_burst(count=50)
            finally:
                await service.stop()

            self.assertEqual(result["events_emitted"], 50)
            self.assertTrue(output_path.exists())
            self.assertEqual(len(output_path.read_text(encoding="utf-8").strip().splitlines()), 50)


if __name__ == "__main__":
    unittest.main()
