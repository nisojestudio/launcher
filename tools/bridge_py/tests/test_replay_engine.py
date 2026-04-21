from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from bridge_client import append_jsonl, build_chat_event, build_follow_event
from replay_engine import ReplayEngine


class ReplayEngineTests(unittest.IsolatedAsyncioTestCase):
    async def test_replay_jsonl_preserves_events(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            jsonl_path = Path(temp_dir) / "session.jsonl"
            append_jsonl(
                jsonl_path,
                build_chat_event(
                    user_id="user-01",
                    username="alice",
                    display_name="Alice",
                    text="Hola replay",
                    event_id="evt-replay-chat-001",
                    room_id="room-r1",
                    timestamp_ms=1710000001000,
                ),
            )
            append_jsonl(
                jsonl_path,
                build_follow_event(
                    user_id="user-02",
                    username="bob",
                    display_name="Bob",
                    event_id="evt-replay-follow-001",
                    room_id="room-r1",
                    timestamp_ms=1710000002000,
                ),
            )

            seen_ids: list[str] = []

            async def callback(event) -> bool:
                seen_ids.append(event.metadata.event_id)
                return True

            result = await ReplayEngine().replay_jsonl(
                jsonl_path,
                callback,
                preserve_timing=False,
                speed=1.0,
                loop=False,
            )

        self.assertEqual(result.events_seen, 2)
        self.assertEqual(result.events_emitted, 2)
        self.assertEqual(result.invalid_lines, 0)
        self.assertEqual(seen_ids, ["evt-replay-chat-001", "evt-replay-follow-001"])

    async def test_simulate_burst_handles_high_volume(self) -> None:
        accepted = 0

        async def callback(_event) -> bool:
            nonlocal accepted
            accepted += 1
            return True

        result = await ReplayEngine().simulate_burst(callback, count=500)

        self.assertEqual(result.events_seen, 500)
        self.assertEqual(result.events_emitted, 500)
        self.assertEqual(accepted, 500)


if __name__ == "__main__":
    unittest.main()
