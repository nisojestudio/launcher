from __future__ import annotations

import argparse
import asyncio
import importlib
import importlib.metadata as importlib_metadata
import json
import os
import platform
import ssl
import sys
import traceback
from pathlib import Path
from typing import Any

from bridge_config import load_bridge_config
from structured_logging import close_logger, configure_logger, describe_log_destination, log_json, utc_now_ms
from tiktok_connection import TikTokConnection, TikTokConnectionError, normalize_tiktok_user


EXIT_CODES = {
    "SUCCESS": 0,
    "INVALID_RUNTIME": 10,
    "INVALID_USERNAME": 11,
    "USER_NOT_FOUND": 12,
    "NOT_LIVE": 13,
    "AGE_RESTRICTED": 14,
    "ACCESS_BLOCKED": 15,
    "NETWORK_ERROR": 16,
    "STREAM_DISCONNECTED": 17,
    "BOOTSTRAP_FAILED": 18,
    "UNKNOWN": 19,
    "UNEXPECTED": 20,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Prueba forense de conexion TikTok para Panel Live."
    )
    parser.add_argument("--config", default="tools/bridge_py/bridge_config.yaml", help="Ruta a bridge_config.yaml.")
    parser.add_argument("--user", required=True, help="Username real de TikTok, con o sin @.")
    parser.add_argument("--room-id", default="", help="Room id numerico opcional.")
    parser.add_argument("--max-seconds", type=int, default=20, help="Ventana maxima de observacion.")
    parser.add_argument("--connect-timeout-sec", type=float, default=20.0, help="Timeout del handshake.")
    parser.add_argument("--report-path", default="", help="Ruta JSON para guardar el informe.")
    parser.add_argument("--log-path", default="", help="Ruta JSONL de log del probe.")
    parser.add_argument(
        "--require-event",
        action="store_true",
        help="Falla si no llega al menos un evento durante la ventana observada.",
    )
    return parser.parse_args()


def module_version(name: str) -> str:
    try:
        return importlib_metadata.version(name)
    except Exception:
        try:
            module = importlib.import_module(name)
            version_value = getattr(module, "__version__", "")
            if isinstance(version_value, str) and version_value.strip():
                return version_value.strip()
        except Exception:
            pass
    return "unknown"


def env_presence() -> dict[str, bool]:
    keys = (
        "LIVEPANEL_TIKTOK_PYTHON_EXE",
        "LIVEPANEL_TIKTOK_RUNNER_SCRIPT",
        "LIVEPANEL_LEGACY_BRIDGE_ROOT",
        "LIVEPANEL_BRIDGE_LOG_PATH",
        "LIVEPANEL_PANEL_WS_URL",
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "NO_PROXY",
    )
    return {key: bool(str(os.getenv(key, "") or "").strip()) for key in keys}


def default_report_path(log_path: str) -> Path:
    log_destination = Path(log_path)
    timestamp = utc_now_ms()
    return log_destination.parent / f"tiktok-connection-report-{timestamp}.json"


async def run_probe(args: argparse.Namespace) -> tuple[int, dict[str, Any]]:
    config = load_bridge_config(args.config)
    target_user = normalize_tiktok_user(args.user)
    room_id = str(args.room_id or "").strip()
    if args.log_path:
        config.logging.log_path = args.log_path
    config.connection.username = target_user
    config.connection.room_id = room_id
    config.connection.connect_timeout_sec = max(5.0, float(args.connect_timeout_sec))

    log_destination = describe_log_destination(config.logging.log_path)
    logger = configure_logger(name="livepanel.bridge.probe", log_path=config.logging.log_path)

    report: dict[str, Any] = {
        "generatedAtMs": utc_now_ms(),
        "targetUser": target_user,
        "requestedRoomId": room_id,
        "maxSeconds": max(1, int(args.max_seconds)),
        "requireEvent": bool(args.require_event),
        "runtime": {
            "pythonExecutable": sys.executable,
            "pythonVersion": sys.version.split()[0],
            "platform": platform.platform(),
            "cwd": str(Path.cwd()),
            "opensslVersion": ssl.OPENSSL_VERSION,
            "moduleVersions": {
                "TikTokLive": module_version("TikTokLive"),
                "websockets": module_version("websockets"),
                "PyYAML": module_version("PyYAML"),
                "certifi": module_version("certifi"),
            },
            "logPath": log_destination["resolvedPath"],
            "logPathRequested": log_destination["requestedPath"],
            "logPathUsesLocalFallback": bool(log_destination["usesLocalFallback"]),
            "logDirectoryWritable": bool(log_destination["directoryWritable"]),
        },
        "browserFlowRequired": False,
        "redirectUriExpected": "",
        "environmentPresence": env_presence(),
        "statusTimeline": [],
        "eventCount": 0,
        "eventSamples": [],
        "result": "pending",
        "errorCode": "",
        "errorMessage": "",
        "rawError": "",
        "traceback": "",
    }

    if not report["runtime"]["logDirectoryWritable"]:
        report["result"] = "invalid_runtime"
        report["errorCode"] = "INVALID_RUNTIME"
        report["errorMessage"] = "La ruta efectiva de logs no es escribible para este usuario."
        close_logger(logger)
        return EXIT_CODES["INVALID_RUNTIME"], report

    connected_event = asyncio.Event()
    first_event = asyncio.Event()
    stream_closed_event = asyncio.Event()
    observed_room_id = room_id
    connection: TikTokConnection | None = None

    async def on_event(event: Any) -> None:
        nonlocal observed_room_id
        report["eventCount"] += 1
        metadata = getattr(event, "metadata", None)
        room_value = getattr(metadata, "room_id", "") if metadata is not None else ""
        if room_value:
            observed_room_id = str(room_value)
        if len(report["eventSamples"]) < 5:
            report["eventSamples"].append(
                {
                    "eventType": getattr(getattr(event, "event_type", None), "value", ""),
                    "roomId": room_value,
                    "timestampMs": getattr(metadata, "timestamp_ms", 0) if metadata is not None else 0,
                }
            )
        first_event.set()

    async def on_status(status: Any) -> None:
        nonlocal observed_room_id
        room_value = str(getattr(status, "room_id", "") or "")
        if room_value:
            observed_room_id = room_value
        payload = {
            "timestampMs": getattr(status, "timestamp_ms", utc_now_ms()),
            "state": getattr(getattr(status, "connection_state", None), "value", ""),
            "message": getattr(status, "message", ""),
            "roomId": room_value,
            "retryCount": getattr(status, "retry_count", 0),
        }
        report["statusTimeline"].append(payload)
        if payload["state"] == "connected":
            connected_event.set()

    async def wait_for_stream_close() -> None:
        assert connection is not None
        try:
            await connection.wait_closed()
        finally:
            stream_closed_event.set()

    try:
        log_json(
            logger,
            "info",
            "tiktok_probe",
            "starting tiktok connection probe",
            target_user=target_user,
            requested_room_id=room_id,
            python_executable=sys.executable,
            openssl_version=ssl.OPENSSL_VERSION,
            effective_log_path=log_destination["resolvedPath"],
            require_event=bool(args.require_event),
        )

        connection = TikTokConnection(
            logger=logger,
            legacy_bridge_root=config.legacy_bridge_root,
            connect_timeout_sec=config.connection.connect_timeout_sec,
            event_callback=on_event,
            status_callback=on_status,
            target_user=target_user,
            room_id=room_id,
            session_id=utc_now_ms(),
        )
        await connection.open()

        stream_task = asyncio.create_task(wait_for_stream_close(), name="tiktok-probe-stream")
        try:
            connected_timeout = min(max(5, int(args.max_seconds)), 15)
            await asyncio.wait_for(connected_event.wait(), timeout=connected_timeout)

            if args.require_event:
                await asyncio.wait_for(first_event.wait(), timeout=max(1, int(args.max_seconds)))
                report["result"] = "connected_with_event"
            else:
                report["result"] = "connected"
                try:
                    await asyncio.wait_for(stream_closed_event.wait(), timeout=max(1, int(args.max_seconds)))
                    report["streamClosedDuringWindow"] = True
                except asyncio.TimeoutError:
                    report["streamClosedDuringWindow"] = False
        finally:
            stream_task.cancel()
            try:
                await stream_task
            except asyncio.CancelledError:
                pass
            except Exception:
                pass

        report["observedRoomId"] = observed_room_id
        log_json(
            logger,
            "info",
            "tiktok_probe",
            "tiktok connection probe finished",
            result=report["result"],
            observed_room_id=observed_room_id,
            event_count=report["eventCount"],
            stream_closed_during_window=report.get("streamClosedDuringWindow", False),
        )
        return EXIT_CODES["SUCCESS"], report
    except TikTokConnectionError as exc:
        report["result"] = "failed"
        report["errorCode"] = exc.code
        report["errorMessage"] = exc.message
        report["rawError"] = exc.raw_error
        report["observedRoomId"] = observed_room_id
        exit_code = EXIT_CODES.get(exc.code, EXIT_CODES["UNKNOWN"])
        log_json(
            logger,
            "error",
            "tiktok_probe",
            "tiktok connection probe failed",
            code=exc.code,
            error_message=exc.message,
            raw_error=exc.raw_error,
            observed_room_id=observed_room_id,
        )
        return exit_code, report
    except asyncio.TimeoutError as exc:
        report["result"] = "failed"
        report["errorCode"] = "NETWORK_ERROR"
        report["errorMessage"] = f"TikTok no completo el handshake dentro del CONNECT_TIMEOUT_SEC configurado."
        report["rawError"] = str(exc)
        report["observedRoomId"] = observed_room_id
        log_json(
            logger,
            "error",
            "tiktok_probe",
            "tiktok connection timed out waiting for connect event",
            error_message=report["errorMessage"],
            raw_error=report["rawError"],
            observed_room_id=observed_room_id,
        )
        return EXIT_CODES["NETWORK_ERROR"], report
    except Exception as exc:
        report["result"] = "failed"
        report["errorCode"] = "UNEXPECTED"
        report["errorMessage"] = str(exc)
        report["traceback"] = traceback.format_exc()
        report["observedRoomId"] = observed_room_id
        log_json(
            logger,
            "error",
            "tiktok_probe",
            "tiktok connection probe crashed",
            error_message=str(exc),
            observed_room_id=observed_room_id,
            traceback=report["traceback"],
        )
        return EXIT_CODES["UNEXPECTED"], report
    finally:
        if connection is not None:
            try:
                await connection.close()
            except BaseException:
                pass
        try:
            close_logger(logger)
        except Exception:
            pass


def main() -> int:
    args = parse_args()
    exit_code = EXIT_CODES["UNEXPECTED"]
    report: dict[str, Any] = {
        "result": "crashed_outside_probe",
        "errorCode": "UNEXPECTED",
        "errorMessage": "",
        "traceback": "",
        "eventCount": 0,
        "statusTimeline": [],
        "eventSamples": [],
    }
    try:
        exit_code, report = asyncio.run(run_probe(args))
    except KeyboardInterrupt:
        exit_code = 0
    except SystemExit:
        raise
    except Exception as exc:
        report = {
            "result": "crashed",
            "errorCode": "UNEXPECTED",
            "errorMessage": str(exc),
            "traceback": traceback.format_exc(),
            "eventCount": int(report.get("eventCount", 0)) if isinstance(report, dict) else 0,
            "statusTimeline": report.get("statusTimeline", []) if isinstance(report, dict) else [],
            "eventSamples": report.get("eventSamples", []) if isinstance(report, dict) else [],
        }
    finally:
        try:
            report_path = (
                Path(args.report_path)
                if args.report_path
                else default_report_path(args.log_path or "tools/bridge_py/logs/bridge.jsonl")
            )
            report_path.parent.mkdir(parents=True, exist_ok=True)
            serialized = json.dumps(report, ensure_ascii=False, indent=2, default=str) + "\n"
            report_path.write_text(serialized, encoding="utf-8")
            sys.stdout.write(serialized)
        except Exception as write_exc:
            sys.stdout.write(
                json.dumps(
                    {
                        "result": "report_write_failed",
                        "errorCode": "REPORT_WRITE_FAILED",
                        "errorMessage": str(write_exc),
                    },
                    ensure_ascii=False,
                )
                + "\n"
            )
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
