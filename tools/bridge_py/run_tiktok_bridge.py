from __future__ import annotations

import argparse
import asyncio
import importlib
import sys

from bridge_config import BridgeConfig, load_bridge_config
from event_stream import TikTokBridgeService
from structured_logging import log_json
from tiktok_connection import is_valid_tiktok_user, normalize_tiktok_user


DEFAULT_PANEL_WS_URL = "ws://127.0.0.1:8765"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Production TikTok Live bridge for Nisoje Studio."
    )
    parser.add_argument("--config", default="tools/bridge_py/bridge_config.yaml", help="Path to bridge_config.yaml.")
    parser.add_argument("--user", default="", help="TikTok username, @username or TikTok profile URL.")
    parser.add_argument("--room-id", default="", help="Optional numeric room id override.")
    parser.add_argument("--output", default="", help="Optional JSONL output path.")
    parser.add_argument("--inbox", default="", help="Optional inbox directory with one JSON file per event.")
    parser.add_argument("--ws", default="", help="Panel WebSocket URL. Defaults to ws://127.0.0.1:8765.")
    parser.add_argument("--session-name", default="", help="Base name for inbox files.")
    parser.add_argument("--max-events", type=int, default=-1, help="Stop after N accepted events. Use 0 for unlimited.")
    parser.add_argument("--max-seconds", type=int, default=-1, help="Stop after N seconds. Use 0 for unlimited.")
    parser.add_argument("--legacy-bridge-root", default="", help="Legacy bridge root used to reuse TikTokLive dependencies.")
    parser.add_argument("--status-port", type=int, default=0, help="Local control HTTP port exposing /health, /status and /metrics.")
    parser.add_argument("--broadcast-ws-port", type=int, default=0, help="Optional local broadcast WebSocket port for canonical events.")
    parser.add_argument("--replay", default="", help="Replay a JSONL file instead of connecting to TikTok live.")
    parser.add_argument("--replay-speed", type=float, default=0.0, help="Replay speed multiplier.")
    parser.add_argument("--replay-loop", action="store_true", help="Loop replay until stopped.")
    parser.add_argument("--simulate-burst", type=int, default=0, help="Emit a synthetic burst of chat events.")
    parser.add_argument("--no-broadcast-ws", action="store_true", help="Disable the bridge broadcast WebSocket server.")
    return parser.parse_args()


def apply_cli_overrides(config: BridgeConfig, args: argparse.Namespace) -> BridgeConfig:
    output_destination_overridden = False
    if args.user:
        config.connection.username = normalize_tiktok_user(args.user)
    if args.room_id:
        config.connection.room_id = str(args.room_id).strip()
    if args.output:
        config.output.output_jsonl = args.output
        output_destination_overridden = True
    if args.inbox:
        config.output.inbox_dir = args.inbox
        output_destination_overridden = True
    if args.ws:
        config.output.panel_ws_url = args.ws
        output_destination_overridden = True
    elif output_destination_overridden:
        config.output.panel_ws_url = ""
    if args.session_name:
        config.output.session_name = args.session_name
    if args.max_events >= 0:
        config.max_events = args.max_events
    if args.max_seconds >= 0:
        config.max_seconds = args.max_seconds
    if args.legacy_bridge_root:
        config.legacy_bridge_root = args.legacy_bridge_root
    if args.status_port > 0:
        config.output.control_port = args.status_port
    if args.broadcast_ws_port > 0:
        config.output.broadcast_ws_port = args.broadcast_ws_port
    if args.replay:
        config.replay.enabled = True
        config.replay.jsonl_path = args.replay
    if args.replay_speed > 0:
        config.replay.speed = args.replay_speed
    if args.replay_loop:
        config.replay.loop = True
    if args.no_broadcast_ws:
        config.output.broadcast_ws_enabled = False
    if not config.output.panel_ws_url and not config.output.output_jsonl and not config.output.inbox_dir:
        config.output.panel_ws_url = DEFAULT_PANEL_WS_URL
    return config


def ensure_runtime_dependencies(config: BridgeConfig) -> None:
    if config.output.panel_ws_url or config.output.broadcast_ws_enabled:
        try:
            importlib.import_module("websockets")
        except ImportError as exc:
            raise SystemExit(
                "error: websockets no esta instalado en este entorno. Instala requirements.txt o usa LIVEPANEL_LEGACY_BRIDGE_ROOT/--legacy-bridge-root con otro runtime compatible."
            ) from exc


def validate_runtime_args(config: BridgeConfig, args: argparse.Namespace) -> None:
    if config.max_events < 0:
        raise SystemExit("error: --max-events must be >= 0")
    if config.max_seconds < 0:
        raise SystemExit("error: --max-seconds must be >= 0")

    if args.simulate_burst > 0 or config.replay.enabled:
        return

    if not config.connection.username:
        raise SystemExit("error: --user is required unless --replay or --simulate-burst is used")
    if not is_valid_tiktok_user(config.connection.username):
        raise SystemExit(
            "error: --user debe ser el username real de TikTok, por ejemplo alice, @alice o https://www.tiktok.com/@alice"
        )


def print_user_not_found_help() -> None:
    print("error: No se encontro el usuario de TikTok.", flush=True)
    print("Revisa que --user sea el username real exacto de TikTok.", flush=True)
    print("Para validar que el panel funciona, puedes probar primero con eventos simulados usando sample_events.py.", flush=True)
    print("Ejemplo:", flush=True)
    print("  python tools/bridge_py/sample_events.py --inbox tools/bridge_py/live_inbox --session-name demo", flush=True)


async def run() -> int:
    args = parse_args()
    config = apply_cli_overrides(load_bridge_config(args.config), args)
    validate_runtime_args(config, args)
    ensure_runtime_dependencies(config)

    service = TikTokBridgeService(config)
    await service.start()

    logger = service.logger
    log_json(
        logger,
        "info",
        "runner",
        "bridge runtime prepared",
        target_user=config.connection.username,
        panel_ws_url=config.output.panel_ws_url,
        inbox_dir=config.output.inbox_dir,
        output_jsonl=config.output.output_jsonl,
        control_port=config.output.control_port,
        broadcast_ws_port=config.output.broadcast_ws_port if config.output.broadcast_ws_enabled else 0,
        max_events=config.max_events,
        max_seconds=config.max_seconds,
    )

    exit_code = 0
    try:
        if args.simulate_burst > 0:
            result = await service.simulate_burst(count=args.simulate_burst)
            log_json(logger, "info", "runner", "synthetic burst completed", **result)
        elif config.replay.enabled and config.replay.jsonl_path:
            result = await service.run_replay(
                jsonl_path=config.replay.jsonl_path,
                preserve_timing=config.replay.preserve_timing,
                speed=config.replay.speed,
                loop=config.replay.loop,
            )
            log_json(logger, "info", "runner", "replay completed", **result)
        else:
            exit_code = await service.run_connection(
                target_user=config.connection.username,
                room_id=config.connection.room_id,
                max_events=config.max_events,
                max_seconds=config.max_seconds,
            )
            if exit_code != 0:
                status = service.status_payload()
                last_message = str(status.get("last_status_message") or "")
                if "No se encontro ese usuario" in last_message:
                    print_user_not_found_help()
            else:
                status = service.status_payload()
                last_session = status.get("last_session_status") or {}
                connection_state = str(last_session.get("connection_state") or "unknown")
                if connection_state != "connected":
                    log_json(
                        logger,
                        "warning",
                        "runner",
                        "TikTok connection ended without reaching connected state",
                        connection_state=connection_state,
                        last_status_message=status.get("last_status_message"),
                        last_session_status=last_session,
                    )
    except KeyboardInterrupt:
        exit_code = 0
    finally:
        log_json(logger, "info", "runner", "bridge runtime stopping", exit_code=exit_code)
        await service.stop()

    return exit_code


def main() -> int:
    try:
        return asyncio.run(run())
    except KeyboardInterrupt:
        return 0
    except SystemExit:
        raise
    except Exception as exc:
        print(f"error: {exc}", flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
