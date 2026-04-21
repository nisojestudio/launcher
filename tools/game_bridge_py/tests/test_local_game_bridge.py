from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
BRIDGE_SCRIPT = REPO_ROOT / "tools" / "game_bridge_py" / "run_local_game_bridge.py"

try:
    from tools.game_bridge_py.run_local_game_bridge import GameContract
    from tools.game_bridge_py.run_local_game_bridge import launch_command
    from tools.game_bridge_py.run_local_game_bridge import load_game_contract
except ModuleNotFoundError:
    sys.path.insert(0, str(REPO_ROOT / "tools" / "game_bridge_py"))
    from run_local_game_bridge import GameContract
    from run_local_game_bridge import launch_command
    from run_local_game_bridge import load_game_contract


def wait_for(predicate, timeout: float = 8.0, interval: float = 0.05) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(interval)
    return predicate()


def read_json_if_ready(path: Path) -> dict | None:
    try:
        raw = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return None
    if not raw.strip():
        return None
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return None


class LocalGameBridgeTest(unittest.TestCase):
    def test_exe_launch_command_can_use_bundle_launch_root(self) -> None:
        with tempfile.TemporaryDirectory(prefix="nlp3_local_game_launch_root_") as temp_dir:
            game_root = Path(temp_dir) / "Arena Live"
            launch_root = game_root / "build" / "Release"
            launch_root.mkdir(parents=True, exist_ok=True)
            (launch_root / "web").mkdir(parents=True, exist_ok=True)
            (launch_root / "WebView2Loader.dll").write_bytes(b"loader")

            contract = GameContract(
                game_id="arena_live",
                display_name="Arena Live",
                description="",
                module_root=game_root,
                entry_path=launch_root / "ArenaLive.exe",
                launch_root=launch_root,
                working_directory=game_root,
                pass_module_root_arg=True,
                launch_args=[
                    "--config-file",
                    "config/live_config.json",
                    "--inbox-file",
                    "runtime/inbox/events.jsonl",
                    "--status-file",
                    "runtime/status.json",
                    "--log-file",
                    "runtime/host.log.jsonl",
                ],
                detected_type="exe",
                config_file=game_root / "config" / "live_config.json",
                inbox_file=game_root / "runtime" / "inbox" / "events.jsonl",
                status_file=game_root / "runtime" / "status.json",
                log_file=game_root / "runtime" / "host.log.jsonl",
            )

            command = launch_command(contract)
            self.assertEqual(command[0], str(launch_root / "ArenaLive.exe"))
            self.assertEqual(command[1:3], ["--module-root", str(launch_root)])
            self.assertIn(str(game_root / "config" / "live_config.json"), command)
            self.assertIn(str(game_root / "runtime" / "inbox" / "events.jsonl"), command)
            self.assertIn(str(game_root / "runtime" / "status.json"), command)
            self.assertIn(str(game_root / "runtime" / "host.log.jsonl"), command)

    def test_manifest_can_disable_module_root_arg_and_switch_working_directory(self) -> None:
        with tempfile.TemporaryDirectory(prefix="nlp3_local_game_launch_policy_") as temp_dir:
            game_root = Path(temp_dir) / "Arena Legacy"
            launch_root = game_root / "build" / "Release"
            launch_root.mkdir(parents=True, exist_ok=True)
            (launch_root / "ArenaLegacy.exe").write_bytes(b"exe")
            (launch_root / "web").mkdir(parents=True, exist_ok=True)
            (launch_root / "WebView2Loader.dll").write_bytes(b"loader")

            manifest = {
                "id": "arena_legacy",
                "displayName": "Arena Legacy",
                "description": "Bridge launch policy test.",
                "type": "external-webview-game",
                "entryExecutable": "ArenaLegacy.exe",
                "launchArgs": [
                    "--config-file",
                    "config/live_config.json",
                    "--inbox-file",
                    "runtime/inbox/events.jsonl",
                ],
                "communication": {
                    "configFile": "config/live_config.json",
                    "inboxFile": "runtime/inbox/events.jsonl",
                    "statusFile": "runtime/status.json",
                    "logFile": "runtime/host.log.jsonl",
                },
                "bridge": {
                    "launch": {
                        "passModuleRootArg": False,
                        "workingDirectory": "launch_root",
                    }
                },
            }
            (game_root / "module_manifest.json").parent.mkdir(parents=True, exist_ok=True)
            (game_root / "module_manifest.json").write_text(
                json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )

            contract = load_game_contract(game_root)
            command = launch_command(contract)

            self.assertEqual(contract.entry_path, launch_root / "ArenaLegacy.exe")
            self.assertEqual(contract.working_directory, launch_root)
            self.assertFalse(contract.pass_module_root_arg)
            self.assertEqual(command[0], str(launch_root / "ArenaLegacy.exe"))
            self.assertNotIn("--module-root", command)
            self.assertIn(str(game_root / "config" / "live_config.json"), command)
            self.assertIn(str(game_root / "runtime" / "inbox" / "events.jsonl"), command)

    def test_bridge_resets_runtime_and_forwards_join_chat_follow_share_like_avatar_and_gift(self) -> None:
        with tempfile.TemporaryDirectory(prefix="nlp3_local_game_bridge_") as temp_dir:
            game_root = Path(temp_dir) / "Arena Live Test"
            (game_root / "config").mkdir(parents=True, exist_ok=True)
            (game_root / "runtime" / "inbox").mkdir(parents=True, exist_ok=True)
            (game_root / "runtime" / "panel_bridge" / "inbox").mkdir(parents=True, exist_ok=True)
            (game_root / "runtime" / "panel_bridge" / "control").mkdir(parents=True, exist_ok=True)

            stale_game_inbox = game_root / "runtime" / "inbox" / "events.jsonl"
            stale_status = game_root / "runtime" / "status.json"
            stale_host_log = game_root / "runtime" / "host.log.jsonl"
            stale_panel_inbox = game_root / "runtime" / "panel_bridge" / "inbox" / "panel_events.jsonl"
            stale_bridge_state = game_root / "runtime" / "panel_bridge" / "state.json"
            stale_bridge_log = game_root / "runtime" / "panel_bridge" / "bridge.log.jsonl"
            for stale_file in (
                stale_game_inbox,
                stale_status,
                stale_host_log,
                stale_panel_inbox,
                stale_bridge_state,
                stale_bridge_log,
            ):
                stale_file.parent.mkdir(parents=True, exist_ok=True)
                stale_file.write_text("stale-data\n", encoding="utf-8")

            fake_game = game_root / "fake_game.py"
            fake_game.write_text(
                textwrap.dedent(
                    """
                    import argparse
                    import json
                    import time
                    from pathlib import Path


                    def write_json(path: Path, payload: dict) -> None:
                        path.parent.mkdir(parents=True, exist_ok=True)
                        tmp = path.with_suffix(path.suffix + ".tmp")
                        tmp.write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")
                        tmp.replace(path)


                    parser = argparse.ArgumentParser()
                    parser.add_argument("--module-root", default="")
                    parser.add_argument("--config-file", required=True)
                    parser.add_argument("--inbox-file", required=True)
                    parser.add_argument("--status-file", required=True)
                    parser.add_argument("--log-file", required=True)
                    args = parser.parse_args()

                    inbox_path = Path(args.inbox_file)
                    status_path = Path(args.status_file)
                    log_path = Path(args.log_file)
                    log_path.parent.mkdir(parents=True, exist_ok=True)
                    log_path.write_text("", encoding="utf-8")

                    with log_path.open("a", encoding="utf-8", newline="\\n") as handle:
                        handle.write(json.dumps({"message": "fake_game_ready"}) + "\\n")

                    seen = []
                    last_avatar = ""
                    processed = 0
                    deadline = time.monotonic() + 6.0

                    while time.monotonic() < deadline:
                        lines = inbox_path.read_text(encoding="utf-8").splitlines() if inbox_path.exists() else []
                        fresh = lines[processed:]
                        for line in fresh:
                            if not line.strip():
                                continue
                            event = json.loads(line)
                            kind = str(event.get("kind") or "")
                            seen.append(kind)
                            user = event.get("user") or {}
                            if kind == "join":
                                last_avatar = str(user.get("avatarUrl") or user.get("avatar") or "")
                            payload = {
                                "type": "GAME_STATE_CHANGE",
                                "payload": {
                                    "ts": int(time.time() * 1000),
                                    "data": {
                                        "roundState": "live",
                                        "mode": "arena-live",
                                        "canonical": {
                                            "ranking": [
                                                {
                                                    "rank": 1,
                                                    "id": "bridge-user",
                                                    "name": "Bridge Pilot",
                                                    "score": len(seen) * 10,
                                                    "avatarUrl": last_avatar,
                                                }
                                            ],
                                            "feed": seen[-8:],
                                        },
                                        "achievements": ["avatar-updated"] if last_avatar else [],
                                    },
                                },
                            }
                            write_json(status_path, payload)
                            with log_path.open("a", encoding="utf-8", newline="\\n") as handle:
                                handle.write(json.dumps({"message": f"handled:{kind}"}) + "\\n")
                        processed = len(lines)
                        if {"join", "chat", "follow", "share", "like", "gift"}.issubset(set(seen)):
                            break
                        time.sleep(0.05)
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )

            manifest = {
                "id": "arena_live_test",
                "displayName": "Arena Live Test",
                "description": "Fake external game used by the bridge smoke test.",
                "type": "external-webview-game",
                "entryExecutable": "fake_game.py",
                "launchArgs": [
                    "--config-file",
                    "config/live_config.json",
                    "--inbox-file",
                    "runtime/inbox/events.jsonl",
                    "--status-file",
                    "runtime/status.json",
                    "--log-file",
                    "runtime/host.log.jsonl",
                ],
                "communication": {
                    "configFile": "config/live_config.json",
                    "inboxFile": "runtime/inbox/events.jsonl",
                    "statusFile": "runtime/status.json",
                    "logFile": "runtime/host.log.jsonl",
                },
            }
            (game_root / "module_manifest.json").write_text(
                json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            (game_root / "config" / "live_config.json").write_text("{}\n", encoding="utf-8")

            bridge_process = subprocess.Popen(
                [sys.executable, str(BRIDGE_SCRIPT), "--game-root", str(game_root)],
                cwd=str(REPO_ROOT),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )

            panel_bridge_inbox = game_root / "runtime" / "panel_bridge" / "inbox" / "panel_events.jsonl"
            panel_bridge_state = game_root / "runtime" / "panel_bridge" / "state.json"
            game_inbox = game_root / "runtime" / "inbox" / "events.jsonl"

            try:
                self.assertTrue(
                    wait_for(
                        lambda: (read_json_if_ready(panel_bridge_state) or {}).get("gameId") == "arena_live_test"
                    ),
                    "bridge state file was not initialized",
                )

                events = [
                    {
                        "kind": "join",
                        "user": {
                            "id": "bridge-join",
                            "name": "Bridge Join",
                            "avatarUrl": "https://example.com/join.png",
                        },
                        "data": {"message": "viewer_join"},
                        "ts": 1,
                    },
                    {
                        "kind": "chat",
                        "user": {
                            "id": "bridge-chat",
                            "name": "Bridge Chat",
                            "avatarUrl": "https://example.com/chat.png",
                        },
                        "data": {"message": "hola arena"},
                        "ts": 2,
                    },
                    {
                        "kind": "follow",
                        "user": {
                            "id": "bridge-follow",
                            "name": "Bridge Follow",
                            "avatarUrl": "https://example.com/follow.png",
                        },
                        "data": {"count": 1},
                        "ts": 3,
                    },
                    {
                        "kind": "share",
                        "user": {
                            "id": "bridge-share",
                            "name": "Bridge Share",
                            "avatarUrl": "https://example.com/share.png",
                        },
                        "data": {"count": 1},
                        "ts": 4,
                    },
                    {
                        "kind": "like",
                        "user": {
                            "id": "bridge-like",
                            "name": "Bridge Like",
                            "avatarUrl": "https://example.com/like.png",
                        },
                        "data": {"count": 5},
                        "ts": 5,
                    },
                    {
                        "kind": "avatar",
                        "user": {
                            "id": "bridge-avatar",
                            "name": "Bridge Avatar",
                            "avatarUrl": "https://example.com/avatar.png",
                        },
                        "data": {"message": "avatar update"},
                        "ts": 6,
                    },
                    {
                        "kind": "gift",
                        "user": {
                            "id": "bridge-gift",
                            "name": "Bridge Gift",
                            "avatarUrl": "https://example.com/gift.png",
                        },
                        "data": {"giftName": "Rose", "count": 3, "diamond": 30},
                        "ts": 7,
                    },
                ]

                with panel_bridge_inbox.open("a", encoding="utf-8", newline="\n") as handle:
                    for event in events:
                        handle.write(json.dumps(event, ensure_ascii=False) + "\n")

                self.assertTrue(wait_for(game_inbox.exists), "game inbox file was not created")
                self.assertTrue(
                    wait_for(lambda: len(game_inbox.read_text(encoding="utf-8").splitlines()) >= 7),
                    "game inbox did not receive the forwarded events",
                )
                self.assertTrue(
                    wait_for(
                        lambda: panel_bridge_state.exists()
                        and json.loads(panel_bridge_state.read_text(encoding="utf-8")).get("runtime", {}).get("ranking")
                    ),
                    "bridge state did not expose runtime ranking data",
                )
            finally:
                stop_flag = game_root / "runtime" / "panel_bridge" / "control" / "stop.flag"
                stop_flag.write_text("stop\n", encoding="utf-8")
                bridge_process.wait(timeout=8)
                if bridge_process.stdout is not None:
                    bridge_process.stdout.close()

            forwarded_lines = [
                json.loads(line)
                for line in game_inbox.read_text(encoding="utf-8").splitlines()
                if line.strip()
            ]
            forwarded_kinds = [entry["kind"] for entry in forwarded_lines]
            self.assertEqual(
                forwarded_kinds,
                ["join", "chat", "follow", "share", "like", "join", "gift"],
            )
            self.assertEqual(forwarded_lines[0]["user"]["avatarUrl"], "https://example.com/join.png")
            self.assertEqual(forwarded_lines[5]["user"]["avatarUrl"], "https://example.com/avatar.png")
            self.assertTrue(forwarded_lines[5]["data"]["avatarUpdate"])
            self.assertEqual(forwarded_lines[6]["data"]["giftName"], "Rose")
            self.assertFalse(any("stale-data" in json.dumps(entry, ensure_ascii=False) for entry in forwarded_lines))

            bridge_state = json.loads(panel_bridge_state.read_text(encoding="utf-8"))
            self.assertTrue(bridge_state["ok"])
            self.assertEqual(bridge_state["gameId"], "arena_live_test")
            self.assertEqual(bridge_state["detectedType"], "python")
            self.assertEqual(bridge_state["runtime"]["lastStatusType"], "GAME_STATE_CHANGE")
            self.assertEqual(bridge_state["runtime"]["ranking"][0]["score"], 70)
            self.assertIn("avatar-updated", bridge_state["runtime"]["achievements"])
            self.assertIn("info: forwarded_event", " ".join(bridge_state["recentBridgeLogs"]))


if __name__ == "__main__":
    unittest.main()
