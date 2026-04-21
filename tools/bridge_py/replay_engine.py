from __future__ import annotations

import asyncio
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Awaitable, Callable

from event_decoder import decode_canonical_event
from event_models import CanonicalEvent, CanonicalEventType


ReplayCallback = Callable[[CanonicalEvent], Awaitable[bool]]


@dataclass(slots=True)
class ReplayResult:
    events_seen: int = 0
    events_emitted: int = 0
    invalid_lines: int = 0
    stopped: bool = False


class ReplayEngine:
    def __init__(self) -> None:
        self._stop_requested = False

    def stop(self) -> None:
        self._stop_requested = True

    async def replay_jsonl(
        self,
        path: str | Path,
        callback: ReplayCallback,
        *,
        preserve_timing: bool = True,
        speed: float = 1.0,
        loop: bool = False,
    ) -> ReplayResult:
        self._stop_requested = False
        target_path = Path(path)
        result = ReplayResult()
        if not target_path.exists():
            return result

        speed = max(0.1, float(speed))
        while True:
            previous_timestamp_ms = None
            with target_path.open("r", encoding="utf-8") as handle:
                for raw_line in handle:
                    if self._stop_requested:
                        result.stopped = True
                        return result

                    line = raw_line.strip()
                    if not line:
                        continue

                    result.events_seen += 1
                    try:
                        event = decode_canonical_event(json.loads(line))
                    except Exception:
                        result.invalid_lines += 1
                        continue

                    if preserve_timing and previous_timestamp_ms is not None:
                        delta_ms = max(0, event.metadata.timestamp_ms - previous_timestamp_ms)
                        if delta_ms > 0:
                            await asyncio.sleep(delta_ms / 1000.0 / speed)

                    previous_timestamp_ms = event.metadata.timestamp_ms
                    if await callback(event):
                        result.events_emitted += 1

            if not loop or self._stop_requested:
                break

        return result

    async def simulate_burst(
        self,
        callback: ReplayCallback,
        *,
        count: int,
        event_type: CanonicalEventType = CanonicalEventType.CHAT,
        room_id: str = "synthetic-room",
    ) -> ReplayResult:
        self._stop_requested = False
        result = ReplayResult()
        for index in range(max(0, count)):
            if self._stop_requested:
                result.stopped = True
                return result

            timestamp_ms = 1_710_000_000_000 + index
            event = decode_canonical_event(
                {
                    "event_type": event_type.value,
                    "kind": event_type.value,
                    "actor": {
                        "id": f"synthetic-{index}",
                        "username": f"synthetic-{index}",
                        "display_name": f"Synthetic {index}",
                        "avatar_url": "",
                    },
                    "metadata": {
                        "event_id": f"synthetic-{event_type.value}-{index}",
                        "room_id": room_id,
                        "source_event_type": event_type.value,
                        "timestamp_ms": timestamp_ms,
                    },
                    "text": f"synthetic burst {index}",
                    "viewer_count": 0,
                    "like_count": 1 if event_type == CanonicalEventType.LIKE else 0,
                    "raw_payload": {"synthetic": True, "index": index},
                }
            )
            result.events_seen += 1
            if await callback(event):
                result.events_emitted += 1
        return result
