"""Load test for the TikTok LIVE Bridge.

Simulates different activity levels to test bridge stability under load.
Uses the existing simulate_burst mechanism plus synthetic event generation.

Usage:
    python tools/bridge_py/test_load.py --level 1
    python tools/bridge_py/test_load.py --level 2 --provider tiktools --user rewrap --api-key YOUR_KEY
    python tools/bridge_py/test_load.py --level 3 --duration 300
"""

from __future__ import annotations

import argparse
import asyncio
import json
import time
from typing import Any

from bridge_config import BridgeConfig, load_bridge_config
from event_stream import TikTokBridgeService
from structured_logging import log_json, utc_now_ms


# Load test levels
LOAD_LEVELS = {
    1: {
        "name": "Baseline (low activity)",
        "description": "5 comments/s, 1 gift every 10s, 1 join every 30s",
        "chat_events_per_sec": 5,
        "gift_events_per_sec": 0.1,
        "join_events_per_sec": 0.03,
        "like_events_per_sec": 1,
        "duration_sec": 300,
    },
    2: {
        "name": "Medium activity",
        "description": "20 comments/s, 5 gifts/s, 3 joins/s",
        "chat_events_per_sec": 20,
        "gift_events_per_sec": 5,
        "join_events_per_sec": 3,
        "like_events_per_sec": 10,
        "duration_sec": 300,
    },
    3: {
        "name": "High activity",
        "description": "50 comments/s, 20 gifts/s, 10 joins/s",
        "chat_events_per_sec": 50,
        "gift_events_per_sec": 20,
        "join_events_per_sec": 10,
        "like_events_per_sec": 30,
        "duration_sec": 300,
    },
    4: {
        "name": "Stress test",
        "description": "100 comments/s, 50 gifts/s, 20 joins/s",
        "chat_events_per_sec": 100,
        "gift_events_per_sec": 50,
        "join_events_per_sec": 20,
        "like_events_per_sec": 50,
        "duration_sec": 300,
    },
    5: {
        "name": "Realistic LIVE simulation",
        "description": "Burst of 200 gifts in 10s + 30 comments/s sustained",
        "chat_events_per_sec": 30,
        "gift_events_per_sec": 5,
        "gift_burst_count": 200,
        "gift_burst_duration_sec": 10,
        "join_events_per_sec": 5,
        "like_events_per_sec": 15,
        "duration_sec": 900,
    },
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Bridge load test.")
    parser.add_argument("--level", type=int, required=True, choices=range(1, 6), help="Load test level (1-5).")
    parser.add_argument("--provider", default="tiktools", choices=("tiktools", "direct", "euler"), help="Provider to test.")
    parser.add_argument("--user", default="", help="TikTok username for live connection test.")
    parser.add_argument("--api-key", default="", help="API key for tiktools/euler.")
    parser.add_argument("--config", default="tools/bridge_py/bridge_config.yaml", help="Config file path.")
    parser.add_argument("--output", default="", help="JSONL output path.")
    parser.add_argument("--ws", default="", help="Panel WebSocket URL.")
    parser.add_argument("--duration", type=int, default=0, help="Override duration in seconds.")
    parser.add_argument("--json", action="store_true", help="Output results as JSON.")
    return parser.parse_args()


def generate_event(event_type: str, index: int, room_id: str = "load-test") -> dict[str, Any]:
    """Generate a synthetic event for load testing."""
    ts = utc_now_ms()
    base = {
        "message_type": "event",
        "schema_version": "1.1",
        "platform": "tiktok-live",
        "event_type": event_type,
        "kind": event_type,
        "actor": {
            "id": f"load-user-{index % 1000}",
            "username": f"loaduser{index % 1000}",
            "display_name": f"Load User {index % 1000}",
            "avatar_url": "",
            "is_follower": False,
            "is_subscriber": False,
            "is_moderator": False,
        },
        "metadata": {
            "event_id": f"load:{event_type}:{ts}:{index}",
            "room_id": room_id,
            "source_event_type": event_type,
            "timestamp_ms": ts,
            "schema_version": "1.1",
            "platform": "tiktok-live",
        },
        "viewer_count": 100 + (index % 500),
        "like_count": 0,
        "latency_ms": 0,
    }
    if event_type == "chat":
        base["text"] = f"Load test message {index}"
    elif event_type == "gift":
        base["gift"] = {
            "gift_id": str(5000 + (index % 10)),
            "gift_name": f"TestGift{index % 10}",
            "quantity": 1 + (index % 5),
            "diamond_count": 10 + (index % 100),
        }
    elif event_type == "like":
        base["like_count"] = 1 + (index % 100)
    return base


async def run_load_test(args: argparse.Namespace) -> dict[str, Any]:
    level_config = LOAD_LEVELS[args.level]
    duration = args.duration or level_config["duration_sec"]

    config = load_bridge_config(args.config)
    if args.provider:
        config.connection_mode = args.provider
    if args.user:
        config.connection.username = args.user
    if args.api_key:
        config.connection.api_key = args.api_key
    if args.output:
        config.output.output_jsonl = args.output
    if args.ws:
        config.output.panel_ws_url = args.ws

    # Disable file I/O for pure load testing unless specifically enabled
    if not args.output:
        config.output.output_jsonl = ""
        config.output.inbox_dir = ""

    service = TikTokBridgeService(config)
    await service.start()

    results: dict[str, Any] = {
        "level": args.level,
        "name": level_config["name"],
        "description": level_config["description"],
        "duration_sec": duration,
        "start_time_ms": utc_now_ms(),
        "events_sent": 0,
        "events_accepted": 0,
        "queue_overflows": 0,
        "errors": [],
    }

    logger = service.logger
    log_json(logger, "info", "load_test", "starting load test",
             level=args.level, name=level_config["name"], duration_sec=duration)

    start = time.monotonic()
    index = 0

    try:
        while (time.monotonic() - start) < duration:
            elapsed = time.monotonic() - start
            remaining = duration - elapsed
            if remaining <= 0:
                break

            # Send chat events
            chat_interval = 1.0 / max(0.01, level_config["chat_events_per_sec"])
            chat_count = int(min(level_config["chat_events_per_sec"] * 0.5, 20))
            for i in range(chat_count):
                ev = generate_event("chat", index, "load-test")
                index += 1
                results["events_sent"] += 1
                try:
                    from event_decoder import decode_canonical_event
                    canonical = decode_canonical_event(ev)
                    accepted = await service._publish_event(canonical)
                    if accepted:
                        results["events_accepted"] += 1
                except Exception as e:
                    results["errors"].append(str(e)[:200])

            # Send gift events
            gift_interval = 1.0 / max(0.01, level_config["gift_events_per_sec"])
            gift_count = int(min(level_config["gift_events_per_sec"] * 0.5, 10))
            for i in range(gift_count):
                ev = generate_event("gift", index, "load-test")
                index += 1
                results["events_sent"] += 1
                try:
                    from event_decoder import decode_canonical_event
                    canonical = decode_canonical_event(ev)
                    accepted = await service._publish_event(canonical)
                    if accepted:
                        results["events_accepted"] += 1
                except Exception as e:
                    results["errors"].append(str(e)[:200])

            # Send join events
            join_count = int(min(level_config["join_events_per_sec"] * 0.5, 5))
            for i in range(join_count):
                ev = generate_event("viewer_join", index, "load-test")
                index += 1
                results["events_sent"] += 1
                try:
                    from event_decoder import decode_canonical_event
                    canonical = decode_canonical_event(ev)
                    accepted = await service._publish_event(canonical)
                    if accepted:
                        results["events_accepted"] += 1
                except Exception as e:
                    results["errors"].append(str(e)[:200])

            # Send like events
            like_count = int(min(level_config["like_events_per_sec"] * 0.5, 15))
            for i in range(like_count):
                ev = generate_event("like", index, "load-test")
                index += 1
                results["events_sent"] += 1
                try:
                    from event_decoder import decode_canonical_event
                    canonical = decode_canonical_event(ev)
                    accepted = await service._publish_event(canonical)
                    if accepted:
                        results["events_accepted"] += 1
                except Exception as e:
                    results["errors"].append(str(e)[:200])

            # Handle gift burst (level 5)
            if level_config.get("gift_burst_count") and elapsed < level_config.get("gift_burst_duration_sec", 10):
                burst_count = min(20, level_config["gift_burst_count"] // 10)
                for i in range(burst_count):
                    ev = generate_event("gift", index, "load-test")
                    index += 1
                    results["events_sent"] += 1
                    try:
                        from event_decoder import decode_canonical_event
                        canonical = decode_canonical_event(ev)
                        accepted = await service._publish_event(canonical)
                        if accepted:
                            results["events_accepted"] += 1
                    except Exception as e:
                        results["errors"].append(str(e)[:200])

            # Sleep between batches
            await asyncio.sleep(0.5)

            # Periodic status
            if index % 100 == 0:
                metrics = service.metrics.snapshot()
                log_json(logger, "info", "load_test", "progress",
                         events_sent=results["events_sent"],
                         events_accepted=results["events_accepted"],
                         queue_size=metrics.gauges.get("queue_size", 0),
                         throughput=metrics.throughput_events_per_sec,
                         elapsed_sec=round(elapsed, 1))

    except KeyboardInterrupt:
        results["interrupted"] = True
    finally:
        results["end_time_ms"] = utc_now_ms()
        results["actual_duration_sec"] = round(time.monotonic() - start, 2)

        # Collect final metrics
        metrics = service.metrics.snapshot()
        results["final_metrics"] = {
            "counters": dict(metrics.counters),
            "gauges": dict(metrics.gauges),
            "throughput_events_per_sec": metrics.throughput_events_per_sec,
            "median_ingest_latency_ms": metrics.median_ingest_latency_ms,
        }
        results["unique_errors"] = list(set(results["errors"]))[:10]

        await service.stop()

        log_json(logger, "info", "load_test", "load test completed",
                 events_sent=results["events_sent"],
                 events_accepted=results["events_accepted"],
                 actual_duration_sec=results["actual_duration_sec"],
                 errors=len(results["errors"]))

    return results


def main() -> int:
    args = parse_args()
    try:
        results = asyncio.run(run_load_test(args))
    except KeyboardInterrupt:
        return 0
    except Exception as exc:
        print(f"error: {exc}", flush=True)
        return 1

    if args.json:
        print(json.dumps(results, ensure_ascii=False, indent=2))
    else:
        print(f"\n{'='*60}")
        print(f"  LOAD TEST RESULTS - Level {results['level']}: {results['name']}")
        print(f"{'='*60}")
        print(f"  Duration:     {results['actual_duration_sec']}s")
        print(f"  Events sent:  {results['events_sent']}")
        print(f"  Accepted:     {results['events_accepted']}")
        print(f"  Dropped:      {results['events_sent'] - results['events_accepted']}")
        print(f"  Errors:       {len(results['errors'])}")
        if results.get("unique_errors"):
            print(f"  Unique errors:")
            for err in results["unique_errors"][:5]:
                print(f"    - {err[:100]}")
        metrics = results.get("final_metrics", {})
        counters = metrics.get("counters", {})
        gauges = metrics.get("gauges", {})
        print(f"\n  Throughput:   {metrics.get('throughput_events_per_sec', 0):.1f} evt/s")
        print(f"  Median latency: {metrics.get('median_ingest_latency_ms', 0):.1f}ms")
        print(f"  Queue size:   {gauges.get('queue_size', 0):.0f}")
        print(f"  WS failures:  {counters.get('panel_ws_send_failures_total', 0)}")
        print(f"  Dropped:      {counters.get('events_dropped_total', 0)}")
        print(f"  Dispatched:   {counters.get('events_dispatched_total', 0)}")
        print(f"{'='*60}\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
