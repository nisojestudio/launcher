#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Any


def now_ms() -> int:
    return int(time.time() * 1000)


def coerce_int(value: Any, fallback: int = 0) -> int:
    try:
        if value is None or value == "":
            return fallback
        return int(float(value))
    except (TypeError, ValueError):
        return fallback


def safe_mkdir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def safe_read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return ""
    except OSError:
        return ""


def safe_write_text(path: Path, content: str) -> None:
    safe_mkdir(path.parent)
    temp_path = path.with_suffix(path.suffix + ".tmp")
    try:
        temp_path.write_text(content, encoding="utf-8")
        for _ in range(6):
            try:
                temp_path.replace(path)
                return
            except PermissionError:
                # Windows readers can temporarily hold the previous file open while
                # the panel is polling it. Retry the atomic replace briefly before
                # falling back to an in-place rewrite.
                time.sleep(0.02)
    except PermissionError:
        pass
    finally:
        try:
            if temp_path.exists():
                temp_path.unlink()
        except OSError:
            pass
    # Final fallback: keep the bridge alive even if the atomic swap cannot win.
    path.write_text(content, encoding="utf-8")


def safe_reset_file(path: Path) -> None:
    safe_mkdir(path.parent)
    try:
        path.write_text("", encoding="utf-8")
    except FileNotFoundError:
        path.write_text("", encoding="utf-8")
    except OSError:
        # Keep the bridge resilient if Windows still has a transient handle open.
        temp_path = path.with_suffix(path.suffix + ".reset")
        temp_path.write_text("", encoding="utf-8")
        temp_path.replace(path)


def resolve_relative(root: Path, value: str | None, fallback: str) -> Path:
    raw = (value or fallback).strip()
    candidate = Path(raw)
    if candidate.is_absolute():
        return candidate
    return root / candidate


def resolve_entry_path(game_root: Path, entry_value: str) -> Path:
    direct_path = resolve_relative(game_root, entry_value, entry_value)
    if direct_path.exists():
        return direct_path

    entry_name = Path(entry_value).name
    if not entry_name:
        return direct_path

    common_candidates = [
        game_root / "build" / "Release" / entry_name,
        game_root / "build" / "Debug" / entry_name,
        game_root / "build" / "RelWithDebInfo" / entry_name,
        game_root / "dist" / entry_name,
        game_root / "bin" / entry_name,
    ]
    for candidate in common_candidates:
        if candidate.exists():
            return candidate

    for candidate in game_root.rglob(entry_name):
        if candidate.is_file():
            return candidate

    return direct_path


def should_resolve_launch_argument(value: str) -> bool:
    raw = value.strip()
    if not raw:
        return False
    if raw.startswith("-"):
        return False
    if "/" in raw or "\\" in raw:
        return True
    if "." in Path(raw).name:
        return True
    return False


def json_compact(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


@dataclass
class GameContract:
    game_id: str
    display_name: str
    description: str
    module_root: Path
    entry_path: Path
    launch_root: Path
    working_directory: Path
    pass_module_root_arg: bool
    launch_args: list[str]
    detected_type: str
    config_file: Path
    inbox_file: Path
    status_file: Path
    log_file: Path


@dataclass
class BridgePaths:
    root: Path
    inbox_file: Path
    state_file: Path
    log_file: Path
    stop_file: Path


class JsonlTail:
    def __init__(self, path: Path) -> None:
        self._path = path
        self._offset = 0
        self._fragment = ""

    def read_lines(self) -> list[str]:
        try:
            if not self._path.exists():
                return []
            file_size = self._path.stat().st_size
            if file_size < self._offset:
                self._offset = 0
                self._fragment = ""
            if file_size == self._offset:
                return []
            with self._path.open("rb") as handle:
                handle.seek(self._offset)
                chunk = handle.read()
            self._offset += len(chunk)
            self._fragment += chunk.decode("utf-8", errors="replace")
            lines: list[str] = []
            while True:
                newline = self._fragment.find("\n")
                if newline < 0:
                    break
                line = self._fragment[:newline].strip()
                self._fragment = self._fragment[newline + 1 :]
                if line:
                    lines.append(line)
            return lines
        except OSError:
            return []


class StructuredLogger:
    def __init__(self, path: Path) -> None:
        self._path = path
        self._recent: deque[str] = deque(maxlen=24)

    @property
    def recent(self) -> list[str]:
        return list(self._recent)

    def info(self, message: str, **fields: Any) -> None:
        self._write("info", message, fields)

    def error(self, message: str, **fields: Any) -> None:
        self._write("error", message, fields)

    def _write(self, level: str, message: str, fields: dict[str, Any]) -> None:
        entry = {
            "ts": now_ms(),
            "level": level,
            "message": message,
        }
        if fields:
            entry["fields"] = fields
        line = json_compact(entry)
        safe_mkdir(self._path.parent)
        with self._path.open("a", encoding="utf-8", newline="\n") as handle:
            handle.write(line + "\n")
        self._recent.append(f"{level}: {message}")
        print(line, flush=True)


def detect_game_type(manifest_type: str, entry_path: Path) -> str:
    lowered_manifest = manifest_type.strip().lower()
    suffix = entry_path.suffix.lower()
    if "unity" in lowered_manifest:
        return "unity"
    if suffix == ".exe":
        return "unity" if entry_path.with_name("UnityPlayer.dll").exists() else "exe"
    if suffix == ".py":
        return "python"
    if suffix in {".html", ".htm"}:
        return "web"
    if "web" in lowered_manifest:
        return "web"
    return "unknown"


def resolve_working_directory(game_root: Path, launch_root: Path, raw_value: Any) -> Path:
    if not isinstance(raw_value, str):
        return game_root
    normalized = raw_value.strip()
    lowered = normalized.lower()
    if not normalized or lowered == "module_root":
        return game_root
    if lowered == "launch_root":
        return launch_root
    return resolve_relative(game_root, normalized, ".")


def load_game_contract(game_root: Path) -> GameContract:
    manifest_path = game_root / "module_manifest.json"
    raw = json.loads(manifest_path.read_text(encoding="utf-8"))
    communication = raw.get("communication") or {}
    entry_name = str(raw.get("entryExecutable") or "").strip()
    if not entry_name:
        raise RuntimeError("module_manifest missing entryExecutable")

    entry_path = resolve_entry_path(game_root, entry_name)
    detected_type = detect_game_type(str(raw.get("type") or ""), entry_path)
    launch_root = game_root
    entry_parent = entry_path.parent
    if (
        detected_type in {"exe", "unity"}
        and entry_parent != game_root
        and (entry_parent / "web").exists()
        and (entry_parent / "WebView2Loader.dll").exists()
    ):
        launch_root = entry_parent
    bridge_options = raw.get("bridge") if isinstance(raw.get("bridge"), dict) else {}
    launch_options = bridge_options.get("launch") if isinstance(bridge_options.get("launch"), dict) else {}
    raw_pass_module_root_arg = launch_options.get("passModuleRootArg")
    pass_module_root_arg = (
        raw_pass_module_root_arg
        if isinstance(raw_pass_module_root_arg, bool)
        else detected_type in {"exe", "unity"}
    )
    working_directory = resolve_working_directory(game_root, launch_root, launch_options.get("workingDirectory"))
    return GameContract(
        game_id=str(raw.get("id") or entry_path.stem).strip() or "external_game",
        display_name=str(raw.get("displayName") or entry_path.stem).strip() or entry_path.stem,
        description=str(raw.get("description") or "").strip(),
        module_root=game_root,
        entry_path=entry_path,
        launch_root=launch_root,
        working_directory=working_directory,
        pass_module_root_arg=pass_module_root_arg,
        launch_args=[str(item) for item in raw.get("launchArgs") or []],
        detected_type=detected_type,
        config_file=resolve_relative(game_root, communication.get("configFile"), "config/live_config.json"),
        inbox_file=resolve_relative(game_root, communication.get("inboxFile"), "runtime/inbox/events.jsonl"),
        status_file=resolve_relative(game_root, communication.get("statusFile"), "runtime/status.json"),
        log_file=resolve_relative(game_root, communication.get("logFile"), "runtime/host.log.jsonl"),
    )


def build_bridge_paths(game_root: Path) -> BridgePaths:
    bridge_root = game_root / "runtime" / "panel_bridge"
    return BridgePaths(
        root=bridge_root,
        inbox_file=bridge_root / "inbox" / "panel_events.jsonl",
        state_file=bridge_root / "state.json",
        log_file=bridge_root / "bridge.log.jsonl",
        stop_file=bridge_root / "control" / "stop.flag",
    )


def launch_command(contract: GameContract) -> list[str]:
    resolved_args = []
    path_keys = {
        "config/live_config.json": contract.config_file,
        "runtime/inbox/events.jsonl": contract.inbox_file,
        "runtime/status.json": contract.status_file,
        "runtime/host.log.jsonl": contract.log_file,
    }
    for item in contract.launch_args:
        if item in path_keys:
            resolved_args.append(str(path_keys[item]))
        elif should_resolve_launch_argument(item):
            resolved_args.append(str(resolve_relative(contract.module_root, item, item)))
        else:
            resolved_args.append(item)

    if contract.detected_type in {"exe", "unity"}:
        command = [str(contract.entry_path)]
        if contract.pass_module_root_arg:
            command.extend(["--module-root", str(contract.launch_root)])
        command.extend(resolved_args)
        return command
    if contract.detected_type == "python":
        return [sys.executable, str(contract.entry_path), *resolved_args]
    if contract.detected_type == "web":
        return ["cmd.exe", "/c", "start", "", str(contract.entry_path)]
    raise RuntimeError(f"unsupported game type: {contract.detected_type}")


def extract_nested_candidates(value: Any, keys: tuple[str, ...]) -> list[str]:
    hits: list[str] = []
    if isinstance(value, dict):
        for key, nested in value.items():
            lowered = str(key).strip().lower()
            if lowered in keys:
                if isinstance(nested, str):
                    text = nested.strip()
                    if text:
                        hits.append(text)
                elif isinstance(nested, list):
                    for item in nested:
                        if isinstance(item, str) and item.strip():
                            hits.append(item.strip())
            hits.extend(extract_nested_candidates(nested, keys))
    elif isinstance(value, list):
        for item in value:
            hits.extend(extract_nested_candidates(item, keys))
    return hits


def normalize_panel_event(record: dict[str, Any]) -> dict[str, Any]:
    raw_kind = str(record.get("kind") or record.get("type") or "").strip().lower()
    user = record.get("user") if isinstance(record.get("user"), dict) else {}
    data = record.get("data") if isinstance(record.get("data"), dict) else {}
    user_id = str(
        user.get("id")
        or record.get("userId")
        or record.get("actorId")
        or record.get("uniqueId")
        or record.get("sender")
        or "panel-user"
    ).strip()
    user_name = str(
        user.get("name")
        or record.get("userName")
        or record.get("actorName")
        or record.get("nickname")
        or user_id
    ).strip()
    avatar_url = str(
        user.get("avatar")
        or user.get("avatarUrl")
        or record.get("avatarUrl")
        or record.get("avatar")
        or data.get("avatarUrl")
        or ""
    ).strip()
    timestamp_ms = int(record.get("ts") or record.get("timestampMs") or now_ms())
    message = str(record.get("message") or data.get("message") or data.get("comment") or "").strip()
    count = coerce_int(data.get("count") or record.get("count") or record.get("magnitude") or 1, 1)
    gift_name = str(data.get("giftName") or record.get("giftName") or "").strip()
    gift_value = coerce_int(
        data.get("diamond")
        or data.get("coins")
        or data.get("value")
        or data.get("giftValue")
        or data.get("gift_value")
        or data.get("diamondValue")
        or data.get("price")
        or record.get("value")
        or record.get("diamond")
        or record.get("coins")
        or 0,
        0,
    )
    side_hint = str(
        data.get("sideHint")
        or data.get("voteSide")
        or data.get("side")
        or data.get("team")
        or data.get("teamId")
        or data.get("targetTeam")
        or record.get("sideHint")
        or record.get("voteSide")
        or record.get("side")
        or record.get("team")
        or record.get("teamId")
        or record.get("targetTeam")
        or ""
    ).strip()

    if raw_kind == "avatar":
        outbound_kind = "join"
        payload_data = {
            "avatarUpdate": True,
            "message": message or "avatar update",
        }
    elif raw_kind == "gift":
        outbound_kind = "gift"
        payload_data = {
            "giftName": gift_name or "Gift",
            "count": max(1, count),
            "diamond": max(0, gift_value),
            "coins": max(0, gift_value),
        }
    elif raw_kind == "like":
        outbound_kind = "like"
        payload_data = {
            "count": max(1, count),
        }
    elif raw_kind in {"chat", "comment"}:
        outbound_kind = "chat"
        payload_data = {
            "message": message or "Mensaje del panel",
            "comment": message or "Mensaje del panel",
        }
    elif raw_kind in {"follow", "share", "join"}:
        outbound_kind = raw_kind
        payload_data = {
            "count": max(1, count),
            "message": message,
        }
    else:
        outbound_kind = "chat"
        payload_data = {
            "message": message or json_compact(record),
            "comment": message or json_compact(record),
            "fallbackKind": raw_kind or "unknown",
        }

    if side_hint:
        payload_data.update({
            "sideHint": side_hint,
            "voteSide": side_hint,
            "side": side_hint,
            "team": side_hint,
        })

    return {
        "kind": outbound_kind,
        "user": {
            "id": user_id,
            "name": user_name,
            "avatar": avatar_url,
            "avatarUrl": avatar_url,
            "avatar_url": avatar_url,
        },
        "data": payload_data,
        "ts": timestamp_ms,
    }


def parse_runtime_status(raw_text: str) -> dict[str, Any]:
    if not raw_text.strip():
        return {}
    try:
        return json.loads(raw_text)
    except json.JSONDecodeError:
        return {
            "type": "INVALID_STATUS",
            "payload": {
                "raw": raw_text[:2048],
            },
        }


def summarize_runtime_state(status_obj: dict[str, Any]) -> dict[str, Any]:
    payload = status_obj.get("payload") if isinstance(status_obj.get("payload"), dict) else {}
    data = payload.get("data") if isinstance(payload.get("data"), dict) else payload
    canonical = data.get("canonical") if isinstance(data.get("canonical"), dict) else {}
    ranking_source = canonical.get("ranking") if isinstance(canonical.get("ranking"), list) else data.get("ranking")
    ranking: list[dict[str, Any]] = []
    if isinstance(ranking_source, list):
        for item in ranking_source[:8]:
            if not isinstance(item, dict):
                continue
            ranking.append(
                {
                    "rank": int(item.get("rank") or len(ranking) + 1),
                    "id": str(item.get("id") or "").strip(),
                    "name": str(item.get("name") or "Jugador").strip(),
                    "score": int(item.get("score") or item.get("points") or 0),
                    "avatarUrl": str(item.get("avatarUrl") or item.get("avatar_url") or "").strip(),
                }
            )
    feed_source = canonical.get("feed") if isinstance(canonical.get("feed"), list) else data.get("recentFeed")
    feed = [str(item).strip() for item in feed_source or [] if str(item).strip()][:8]
    achievements = []
    for text in extract_nested_candidates(status_obj, ("achievement", "achievements")):
        if text not in achievements:
            achievements.append(text)

    return {
        "lastStatusType": str(status_obj.get("type") or "").strip(),
        "lastStatusTimestampMs": int(payload.get("ts") or status_obj.get("ts") or now_ms()),
        "roundState": str(data.get("roundState") or payload.get("state") or "").strip(),
        "modeId": str(data.get("mode") or canonical.get("modeId") or "").strip(),
        "ranking": ranking,
        "feed": feed,
        "achievements": achievements[:8],
        "rawStatus": status_obj,
    }


class LocalGameBridge:
    def __init__(self, contract: GameContract, bridge_paths: BridgePaths) -> None:
        self.contract = contract
        self.bridge_paths = bridge_paths
        self.logger = StructuredLogger(bridge_paths.log_file)
        self.panel_inbox_tail = JsonlTail(bridge_paths.inbox_file)
        self.game_log_tail = JsonlTail(contract.log_file)
        self.game_process: subprocess.Popen[str] | None = None
        self.stop_requested = False
        self.last_status_mtime_ns = 0
        self.latest_runtime_summary: dict[str, Any] = {}
        self.latest_log_lines: deque[str] = deque(maxlen=24)

    def prepare(self) -> None:
        safe_mkdir(self.contract.config_file.parent)
        safe_mkdir(self.contract.inbox_file.parent)
        safe_mkdir(self.contract.status_file.parent)
        safe_mkdir(self.contract.log_file.parent)
        safe_mkdir(self.bridge_paths.inbox_file.parent)
        safe_mkdir(self.bridge_paths.state_file.parent)
        safe_mkdir(self.bridge_paths.stop_file.parent)

        # Runtime artifacts are ephemeral. Reset them on every fresh bridge launch so
        # Arena Live does not replay smoke/manual events from previous sessions.
        for artifact in (
            self.contract.inbox_file,
            self.contract.status_file,
            self.contract.log_file,
            self.bridge_paths.inbox_file,
            self.bridge_paths.state_file,
            self.bridge_paths.log_file,
        ):
            safe_reset_file(artifact)

        if self.bridge_paths.stop_file.exists():
            self.bridge_paths.stop_file.unlink()

    def launch_game(self) -> None:
        command = launch_command(self.contract)
        if not self.contract.entry_path.exists():
            raise RuntimeError(f"game entry not found: {self.contract.entry_path}")
        self.logger.info(
            "launching_game",
            game_id=self.contract.game_id,
            game_type=self.contract.detected_type,
            entry=str(self.contract.entry_path),
            cwd=str(self.contract.working_directory),
            pass_module_root_arg=self.contract.pass_module_root_arg,
        )
        self.game_process = subprocess.Popen(
            command,
            cwd=str(self.contract.working_directory),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )

    def append_game_inbox_line(self, payload: dict[str, Any]) -> None:
        line = json_compact(payload)
        with self.contract.inbox_file.open("a", encoding="utf-8", newline="\n") as handle:
            handle.write(line + "\n")

    def refresh_runtime_status(self) -> None:
        try:
            if not self.contract.status_file.exists():
                return
            stat = self.contract.status_file.stat()
            if stat.st_mtime_ns == self.last_status_mtime_ns:
                return
            self.last_status_mtime_ns = stat.st_mtime_ns
            status_obj = parse_runtime_status(safe_read_text(self.contract.status_file))
            self.latest_runtime_summary = summarize_runtime_state(status_obj)
        except OSError:
            return

    def refresh_log_tail(self) -> None:
        for line in self.game_log_tail.read_lines():
            self.latest_log_lines.append(line)

    def write_bridge_state(self) -> None:
        process_running = self.game_process is not None and self.game_process.poll() is None
        process_id = self.game_process.pid if self.game_process is not None else 0
        return_code = self.game_process.poll() if self.game_process is not None else None
        state = {
            "ok": True,
            "ts": now_ms(),
            "gameId": self.contract.game_id,
            "displayName": self.contract.display_name,
            "description": self.contract.description,
            "moduleRoot": str(self.contract.module_root),
            "detectedType": self.contract.detected_type,
            "bridgeState": "stopping" if self.stop_requested else ("running" if process_running else "stopped"),
            "bridgePaths": {
                "panelInboxFile": str(self.bridge_paths.inbox_file),
                "stateFile": str(self.bridge_paths.state_file),
                "logFile": str(self.bridge_paths.log_file),
                "stopFile": str(self.bridge_paths.stop_file),
            },
            "gamePaths": {
                "launchRoot": str(self.contract.launch_root),
                "workingDirectory": str(self.contract.working_directory),
                "entryPath": str(self.contract.entry_path),
                "configFile": str(self.contract.config_file),
                "inboxFile": str(self.contract.inbox_file),
                "statusFile": str(self.contract.status_file),
                "logFile": str(self.contract.log_file),
            },
            "launchPolicy": {
                "passModuleRootArg": self.contract.pass_module_root_arg,
            },
            "process": {
                "running": process_running,
                "processId": process_id,
                "returnCode": return_code,
            },
            "runtime": {
                **self.latest_runtime_summary,
                "recentLogs": list(self.latest_log_lines),
            },
            "recentBridgeLogs": self.logger.recent,
        }
        safe_write_text(self.bridge_paths.state_file, json_compact(state))

    def stop_game(self) -> None:
        if self.game_process is None:
            return
        if self.game_process.poll() is not None:
            return
        self.logger.info("stopping_game", game_id=self.contract.game_id, pid=self.game_process.pid)
        self.game_process.terminate()
        try:
            self.game_process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            self.game_process.kill()
            self.game_process.wait(timeout=4)

    def process_panel_events(self) -> None:
        for line in self.panel_inbox_tail.read_lines():
            try:
                raw_event = json.loads(line)
            except json.JSONDecodeError:
                self.logger.error("invalid_panel_event", raw=line[:240])
                continue
            normalized = normalize_panel_event(raw_event)
            self.append_game_inbox_line(normalized)
            self.logger.info(
                "forwarded_event",
                kind=normalized.get("kind"),
                user_id=((normalized.get("user") or {}).get("id") or ""),
            )

    def check_stop_requested(self) -> None:
        if self.bridge_paths.stop_file.exists():
            self.stop_requested = True

    def run(self) -> int:
        self.prepare()
        self.launch_game()
        self.write_bridge_state()
        try:
            while True:
                self.check_stop_requested()
                self.process_panel_events()
                self.refresh_runtime_status()
                self.refresh_log_tail()
                self.write_bridge_state()

                if self.stop_requested:
                    break

                if self.game_process is not None and self.game_process.poll() is not None:
                    break
                time.sleep(0.12)
        finally:
            self.stop_requested = True
            self.stop_game()
            self.refresh_runtime_status()
            self.refresh_log_tail()
            self.write_bridge_state()
        return 0 if self.game_process is None or (self.game_process.poll() or 0) == 0 else int(self.game_process.poll() or 0)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Local external game bridge for Nisoje Studio")
    parser.add_argument("--game-root", required=True, help="Absolute path to the external game root")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    game_root = Path(args.game_root).expanduser().resolve()
    bridge_paths = build_bridge_paths(game_root)
    logger = StructuredLogger(bridge_paths.log_file)

    bridge: LocalGameBridge | None = None

    def request_stop(_signum: int, _frame: Any) -> None:
        if bridge is not None:
            bridge.stop_requested = True
            safe_mkdir(bridge_paths.stop_file.parent)
            bridge_paths.stop_file.write_text("stop\n", encoding="utf-8")
        else:
            logger.info("signal_received_before_bridge_init")

    signal.signal(signal.SIGINT, request_stop)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, request_stop)

    try:
        contract = load_game_contract(game_root)
        bridge = LocalGameBridge(contract, bridge_paths)
        logger.info(
            "bridge_initialized",
            game_id=contract.game_id,
            game_root=str(contract.module_root),
            launch_root=str(contract.launch_root),
            game_type=contract.detected_type,
            entry=str(contract.entry_path),
        )
        return bridge.run()
    except Exception as exc:  # pragma: no cover - defensive process wrapper
        logger.error("bridge_failed", error=str(exc))
        failure_state = {
            "ok": False,
            "ts": now_ms(),
            "bridgeState": "faulted",
            "lastError": str(exc),
            "gameId": "",
        }
        safe_write_text(bridge_paths.state_file, json_compact(failure_state))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
