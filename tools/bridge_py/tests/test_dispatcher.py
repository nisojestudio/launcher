from __future__ import annotations

import asyncio
import tempfile
import unittest
from pathlib import Path

from bridge_client import build_chat_event
from event_decoder import decode_canonical_event
from event_dispatcher import AsyncEventDispatcher
from metrics_registry import MetricsRegistry


class DispatcherTests(unittest.IsolatedAsyncioTestCase):
    async def test_dispatcher_writes_jsonl_and_inbox(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            jsonl_path = temp_path / "bridge.jsonl"
            inbox_path = temp_path / "inbox"
            metrics = MetricsRegistry()
            dispatcher = AsyncEventDispatcher(
                metrics=metrics,
                queue_size=16,
                batch_size=4,
                overflow_policy="drop_oldest",
                jsonl_path=str(jsonl_path),
                inbox_dir=str(inbox_path),
                session_name="dispatch-test",
            )
            await dispatcher.start()
            try:
                for index in range(3):
                    accepted = await dispatcher.publish(
                        decode_canonical_event(
                            build_chat_event(
                                user_id=f"user-{index}",
                                username=f"user{index}",
                                display_name=f"User {index}",
                                text=f"message {index}",
                                event_id=f"evt-dispatch-{index}",
                                room_id="room-dispatch",
                                timestamp_ms=1710000001000 + index,
                            )
                        )
                    )
                    self.assertTrue(accepted)

                for _ in range(50):
                    if metrics.snapshot().counters.get("events_dispatched_total", 0) >= 3:
                        break
                    await asyncio.sleep(0.02)
            finally:
                await dispatcher.stop()

            self.assertTrue(jsonl_path.exists())
            self.assertEqual(len(jsonl_path.read_text(encoding="utf-8").strip().splitlines()), 3)
            self.assertEqual(len(list(inbox_path.glob("*.json"))), 3)
            self.assertEqual(metrics.snapshot().counters.get("events_dispatched_total"), 3)


if __name__ == "__main__":
    unittest.main()
