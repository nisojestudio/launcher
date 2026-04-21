from __future__ import annotations

import argparse
import importlib
from pathlib import Path

from bridge_client import (
    append_jsonl,
    build_chat_event,
    build_follow_event,
    build_gift_event,
    emit_ws_sync,
    write_json,
)


def build_sample_events() -> list[dict]:
    return [
        build_chat_event(
            user_id="user-01",
            username="alice",
            display_name="Alice",
            avatar_url="",
            text="Hello host",
            event_id="evt-001",
            room_id="room-001",
            timestamp_ms=1710000001000,
        ),
        build_gift_event(
            user_id="user-02",
            username="bob",
            display_name="Bob",
            avatar_url="",
            event_id="evt-002",
            room_id="room-001",
            timestamp_ms=1710000002000,
            gift_id="gift-rose",
            gift_name="Rose",
            quantity=2,
            diamond_count=150,
        ),
        build_follow_event(
            user_id="user-03",
            username="carol",
            display_name="Carol",
            avatar_url="",
            event_id="evt-003",
            room_id="room-001",
            timestamp_ms=1710000003000,
        ),
    ]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate local sample external bridge events for Nisoje Studio."
    )
    parser.add_argument(
        "--output",
        default=str(Path(__file__).with_name("sample_session.jsonl")),
        help="Target JSONL file path.",
    )
    parser.add_argument(
        "--inbox",
        default="",
        help="Optional inbox directory where each event is written as one .json file.",
    )
    parser.add_argument(
        "--session-name",
        default="sample-session",
        help="Base name used for inbox event files.",
    )
    parser.add_argument(
        "--ws",
        default="",
        help="Optional websocket URL for future local tests, for example ws://127.0.0.1:8765.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.ws:
        try:
            importlib.import_module("websockets")
        except ImportError as exc:
            raise SystemExit(
                "error: --ws requires the optional Python package 'websockets'. "
                "Use --output/--inbox or install websockets first."
            ) from exc

    events = build_sample_events()

    output_path = Path(args.output)
    if output_path.exists():
        output_path.unlink()

    for payload in events:
        append_jsonl(output_path, payload)

    print(f"wrote {len(events)} events to {output_path}")

    if args.inbox:
        inbox_path = Path(args.inbox)
        inbox_path.mkdir(parents=True, exist_ok=True)
        for index, payload in enumerate(events, start=1):
            event_kind = str(payload.get("kind", "event"))
            target_name = f"{args.session_name}-{index:03d}-{event_kind}.json"
            write_json(inbox_path / target_name, payload)
        print(f"wrote {len(events)} inbox events to {inbox_path}")

    if args.ws:
        sent_count = 0
        for payload in events:
            emit_ws_sync(args.ws, payload)
            sent_count += 1
        print(f"sent {sent_count} events to {args.ws}")
        print("panel tip: bridge demo ws await 3 50 0")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
