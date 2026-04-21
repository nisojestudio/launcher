#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

try:
    from tools.game_bridge_py.run_local_game_bridge import build_bridge_paths
    from tools.game_bridge_py.run_local_game_bridge import launch_command
    from tools.game_bridge_py.run_local_game_bridge import load_game_contract
except ModuleNotFoundError:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from run_local_game_bridge import build_bridge_paths
    from run_local_game_bridge import launch_command
    from run_local_game_bridge import load_game_contract


@dataclass
class AuditItem:
    level: str
    code: str
    message: str
    details: dict[str, Any]


def is_inside_root(root: Path, candidate: Path) -> bool:
    try:
        candidate.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def add_item(
    items: list[AuditItem],
    level: str,
    code: str,
    message: str,
    **details: Any,
) -> None:
    items.append(AuditItem(level=level, code=code, message=message, details=details))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Audit an external game contract for Nisoje Studio.")
    parser.add_argument("--game-root", required=True, help="Absolute or relative path to the external game root.")
    parser.add_argument("--json", action="store_true", help="Print the audit report as JSON.")
    return parser.parse_args()


def audit_game_root(game_root: Path) -> tuple[dict[str, Any], int]:
    items: list[AuditItem] = []
    manifest_path = game_root / "module_manifest.json"

    if not game_root.exists():
        add_item(items, "fail", "game_root_missing", "Game root does not exist.", path=str(game_root))
        report = {
            "ok": False,
            "gameRoot": str(game_root),
            "items": [item.__dict__ for item in items],
        }
        return report, 1

    if not game_root.is_dir():
        add_item(items, "fail", "game_root_not_directory", "Game root is not a directory.", path=str(game_root))
        report = {
            "ok": False,
            "gameRoot": str(game_root),
            "items": [item.__dict__ for item in items],
        }
        return report, 1

    add_item(items, "ok", "game_root_found", "Game root resolved.", path=str(game_root))

    if not manifest_path.exists():
        add_item(items, "fail", "manifest_missing", "module_manifest.json is missing.", path=str(manifest_path))
        report = {
            "ok": False,
            "gameRoot": str(game_root),
            "items": [item.__dict__ for item in items],
        }
        return report, 1

    add_item(items, "ok", "manifest_found", "module_manifest.json found.", path=str(manifest_path))

    raw_manifest: dict[str, Any]
    try:
        raw_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        add_item(items, "ok", "manifest_parsed", "module_manifest.json parsed correctly.")
    except Exception as exc:
        add_item(items, "fail", "manifest_invalid_json", "module_manifest.json could not be parsed.", error=str(exc))
        report = {
            "ok": False,
            "gameRoot": str(game_root),
            "items": [item.__dict__ for item in items],
        }
        return report, 1

    for key in ("id", "displayName", "entryExecutable"):
        value = raw_manifest.get(key)
        if isinstance(value, str) and value.strip():
            add_item(items, "ok", f"{key}_present", f"Manifest field '{key}' is present.", value=value.strip())
        else:
            add_item(items, "fail", f"{key}_missing", f"Manifest field '{key}' is missing or empty.")

    communication = raw_manifest.get("communication") if isinstance(raw_manifest.get("communication"), dict) else {}
    for key, fallback in (
        ("configFile", "config/live_config.json"),
        ("inboxFile", "runtime/inbox/events.jsonl"),
        ("statusFile", "runtime/status.json"),
        ("logFile", "runtime/host.log.jsonl"),
    ):
        if isinstance(communication.get(key), str) and str(communication.get(key)).strip():
            add_item(items, "ok", f"{key}_explicit", f"Manifest field communication.{key} is explicit.", value=str(communication.get(key)).strip())
        else:
            add_item(
                items,
                "warn",
                f"{key}_defaulted",
                f"communication.{key} is not explicit; the bridge will fall back to {fallback}.",
            )

    try:
        contract = load_game_contract(game_root)
        bridge_paths = build_bridge_paths(game_root)
    except Exception as exc:
        add_item(items, "fail", "contract_load_failed", "The bridge could not resolve the game contract.", error=str(exc))
        report = {
            "ok": False,
            "gameRoot": str(game_root),
            "items": [item.__dict__ for item in items],
        }
        return report, 1

    if contract.entry_path.exists():
        add_item(items, "ok", "entry_resolved", "The game entry executable/script was resolved.", entryPath=str(contract.entry_path))
    else:
        add_item(items, "fail", "entry_missing", "The resolved game entry does not exist.", entryPath=str(contract.entry_path))

    if contract.working_directory.exists():
        add_item(items, "ok", "working_directory_ok", "The working directory exists.", workingDirectory=str(contract.working_directory))
    else:
        add_item(items, "fail", "working_directory_missing", "The working directory does not exist.", workingDirectory=str(contract.working_directory))

    for label, path in (
        ("configFile", contract.config_file),
        ("inboxFile", contract.inbox_file),
        ("statusFile", contract.status_file),
        ("logFile", contract.log_file),
        ("bridgeInboxFile", bridge_paths.inbox_file),
        ("bridgeStateFile", bridge_paths.state_file),
        ("bridgeLogFile", bridge_paths.log_file),
    ):
        parent = path.parent
        if is_inside_root(game_root, path):
            add_item(items, "ok", f"{label}_scoped", f"{label} stays inside the game root.", path=str(path))
        else:
            add_item(items, "warn", f"{label}_outside_root", f"{label} resolves outside the game root.", path=str(path))

        if parent.exists():
            add_item(items, "ok", f"{label}_parent_ready", f"Parent directory exists for {label}.", parent=str(parent))
        else:
            add_item(items, "warn", f"{label}_parent_missing", f"Parent directory is missing for {label}; the bridge will create it on launch.", parent=str(parent))

    detected_type = contract.detected_type
    add_item(items, "ok", "detected_type", "The bridge detected a launch type.", detectedType=detected_type)

    if detected_type in {"exe", "unity"} and contract.pass_module_root_arg:
        add_item(
            items,
            "warn",
            "module_root_arg_enabled",
            "The bridge will append --module-root to the launch command. Disable it in module_manifest.json if the legacy executable does not accept that flag.",
            launchRoot=str(contract.launch_root),
        )
    else:
        add_item(
            items,
            "ok",
            "module_root_arg_policy",
            "The bridge launch policy for --module-root is explicit.",
            enabled=contract.pass_module_root_arg,
        )

    if (
        detected_type in {"exe", "unity"}
        and not contract.pass_module_root_arg
        and contract.entry_path.parent != game_root
        and contract.working_directory == game_root
    ):
        add_item(
            items,
            "warn",
            "launch_root_policy_risk",
            "The executable lives outside the module root and will launch without --module-root while keeping cwd=module_root. Confirm that the game resolves assets correctly.",
            entryParent=str(contract.entry_path.parent),
            workingDirectory=str(contract.working_directory),
        )

    try:
        command = launch_command(contract)
        add_item(items, "ok", "launch_command_resolved", "The bridge resolved a concrete launch command.", command=command)
    except Exception as exc:
        add_item(items, "fail", "launch_command_failed", "The bridge could not resolve a launch command.", error=str(exc))

    failures = [item for item in items if item.level == "fail"]
    warnings = [item for item in items if item.level == "warn"]
    report = {
        "ok": not failures,
        "gameRoot": str(game_root),
        "gameId": contract.game_id,
        "displayName": contract.display_name,
        "detectedType": contract.detected_type,
        "summary": {
            "failures": len(failures),
            "warnings": len(warnings),
            "checks": len(items),
        },
        "items": [item.__dict__ for item in items],
    }
    return report, 0 if not failures else 1


def print_text_report(report: dict[str, Any]) -> None:
    summary = report.get("summary") or {}
    print(f"AUDIT {report.get('displayName') or report.get('gameRoot')}")
    print(f"game_root: {report.get('gameRoot')}")
    if report.get("gameId"):
        print(f"game_id: {report.get('gameId')}")
    if report.get("detectedType"):
        print(f"detected_type: {report.get('detectedType')}")
    print(
        "summary: "
        f"{summary.get('checks', 0)} checks, "
        f"{summary.get('failures', 0)} failures, "
        f"{summary.get('warnings', 0)} warnings"
    )
    print("")
    for item in report.get("items") or []:
        level = str(item.get("level") or "").upper().ljust(4)
        print(f"[{level}] {item.get('code')}: {item.get('message')}")
        details = item.get("details") if isinstance(item.get("details"), dict) else {}
        for key, value in details.items():
            print(f"       {key}: {value}")


def main() -> int:
    args = parse_args()
    game_root = Path(args.game_root).expanduser().resolve()
    report, exit_code = audit_game_root(game_root)
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        print_text_report(report)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
