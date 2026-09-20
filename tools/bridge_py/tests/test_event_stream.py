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


class BridgeServiceSoundAlertWiringTests(unittest.IsolatedAsyncioTestCase):
    async def test_sound_alerts_follow_the_panel_attachment(self) -> None:
        """Cableado: al perder el panel, el manager deja de tener destinatario.

        Se accede a `_connection_manager` a proposito: lo que se verifica es
        justamente el cableado entre el sink del dispatcher y el manager, que
        los tests unitarios del manager (que pasan el predicado a mano) no
        pueden cubrir.
        """
        with tempfile.TemporaryDirectory() as temp_dir:
            config = BridgeConfig()
            config.output.panel_ws_url = "ws://127.0.0.1:9"  # nadie escucha
            config.output.broadcast_ws_enabled = False
            config.output.control_port = 0
            config.logging.log_path = str(Path(temp_dir) / "bridge.log.jsonl")

            service = TikTokBridgeService(config)
            await service.start()
            try:
                # Un evento fuerza el envio al panel: al fallar, el sink se
                # marca como no adjunto y el manager debe verlo.
                await service.simulate_burst(count=5)
                # El dispatcher procesa en segundo plano: hay que esperar a que
                # drene para que el envio al panel se haya intentado de verdad.
                await service.dispatcher.flush()

                self.assertFalse(service._connection_manager._panel_allows_sound())
            finally:
                await service.stop()


if __name__ == "__main__":
    unittest.main()
