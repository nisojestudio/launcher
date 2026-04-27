#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path


NLP3_PANEL_GAME_LAUNCHER_SIGNATURE = "nlp3-panel-game-launcher-v1"

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_UI_PORT = 18913


def resolve_default_games_root() -> Path:
    user_profile = os.environ.get("USERPROFILE", "").strip()
    if user_profile:
        return Path(user_profile) / "Desktop" / "Juegos"
    return Path.home() / "Desktop" / "Juegos"


DEFAULT_GAMES_ROOT = resolve_default_games_root()


def choose_newest_existing_path(candidates: list[Path]) -> Path | None:
    ranked: list[tuple[float, int, Path]] = []
    for index, candidate in enumerate(candidates):
        if not candidate.exists():
            continue
        try:
            modified = candidate.stat().st_mtime
        except OSError:
            modified = 0.0
        ranked.append((modified, -index, candidate.resolve()))

    if not ranked:
        return None

    return max(ranked)[2]


def discover_versioned_panel_executables() -> list[Path]:
    candidates: list[Path] = []

    releases_root = REPO_ROOT / "dist" / "releases"
    if releases_root.exists():
        candidates.extend(releases_root.glob("*/NisojeStudio/NisojeStudio.exe"))

    build_root = REPO_ROOT / "build"
    if build_root.exists():
        candidates.extend(build_root.glob("release-*/src/platform/NisojeStudio.exe"))

    return candidates


@dataclass
class SelectedGame:
    game_root: Path
    game_id: str
    display_name: str
    release_dir: Path
    entry_executable: Path
    webview_loader: Path
    manifest_path: Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Launch Nisoje Studio and activate a selected external game.",
    )
    parser.add_argument("--ui-port", type=int, required=True, help="Panel UI port.")
    parser.add_argument("--game-root", required=True, help="Absolute path to the selected game root.")
    parser.add_argument("--panel-exe", default="", help="Optional override for NisojeStudio.exe.")
    parser.add_argument(
        "--startup-timeout",
        type=int,
        default=25,
        help="Seconds to wait for panel health and game activation.",
    )
    parser.add_argument(
        "--exit-after-activate",
        action="store_true",
        help="Exit after activation succeeds instead of waiting on the panel process.",
    )
    return parser.parse_args()


def json_request(url: str, method: str = "GET", payload: dict | None = None) -> dict:
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        headers["Content-Type"] = "application/json; charset=utf-8"

    request = urllib.request.Request(url=url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(request, timeout=2.0) as response:
        raw = response.read().decode("utf-8")
    return json.loads(raw) if raw.strip() else {}


def check_local_port_idle(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.3)
        return sock.connect_ex(("127.0.0.1", port)) != 0


def find_panel_executable(override_path: str) -> Path:
    if override_path.strip():
        candidate = Path(override_path).expanduser().resolve()
        if candidate.exists():
            return candidate
        raise RuntimeError(f"Panel executable not found at {candidate}")

    versioned_match = choose_newest_existing_path(discover_versioned_panel_executables())
    if versioned_match is not None:
        return versioned_match

    candidates = [
        REPO_ROOT / "NisojeStudio.exe",
        REPO_ROOT / "build" / "release" / "src" / "platform" / "NisojeStudio.exe",
        REPO_ROOT / "build" / "Release" / "src" / "platform" / "NisojeStudio.exe",
        REPO_ROOT / "build" / "src" / "platform" / "NisojeStudio.exe",
        REPO_ROOT / "dist" / "NisojeStudio" / "NisojeStudio.exe",
    ]
    preferred_match = choose_newest_existing_path(candidates)
    if preferred_match is not None:
        return preferred_match

    for base in (REPO_ROOT / "dist", REPO_ROOT / "build"):
        if not base.exists():
            continue
        fallback_match = choose_newest_existing_path(list(base.rglob("NisojeStudio.exe")))
        if fallback_match is not None:
            return fallback_match

    raise RuntimeError(
        "NisojeStudio.exe was not found. Build the panel first or pass --panel-exe explicitly."
    )


def load_selected_game(game_root_raw: str) -> SelectedGame:
    game_root = Path(game_root_raw).expanduser().resolve()
    if not game_root.exists():
        raise RuntimeError(f"Game root does not exist: {game_root}")

    release_dir = game_root / "build" / "Release"
    if not release_dir.exists():
        raise RuntimeError(f"Selected game is missing build\\Release: {game_root}")

    executables = sorted(release_dir.glob("*.exe"))
    if not executables:
        raise RuntimeError(f"Selected game is missing a .exe in build\\Release: {game_root}")

    webview_loader = release_dir / "WebView2Loader.dll"
    if not webview_loader.exists():
        raise RuntimeError(
            f"Selected game is missing WebView2Loader.dll in build\\Release: {game_root}"
        )

    manifest_path = game_root / "module_manifest.json"
    if not manifest_path.exists():
        raise RuntimeError(
            f"Selected game is missing module_manifest.json, so the panel cannot activate it: {game_root}"
        )

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except Exception as exc:  # pragma: no cover - defensive validation
        raise RuntimeError(f"Could not read module_manifest.json: {exc}") from exc

    game_id = str(manifest.get("id") or "").strip()
    if not game_id:
        raise RuntimeError(f"module_manifest.json is missing a valid id: {manifest_path}")

    display_name = str(manifest.get("displayName") or game_root.name).strip() or game_root.name
    return SelectedGame(
        game_root=game_root,
        game_id=game_id,
        display_name=display_name,
        release_dir=release_dir,
        entry_executable=executables[0].resolve(),
        webview_loader=webview_loader.resolve(),
        manifest_path=manifest_path.resolve(),
    )


def wait_for_panel_health(port: int, panel_process: subprocess.Popen[str], timeout_seconds: int) -> None:
    deadline = time.monotonic() + timeout_seconds
    last_error = ""
    while time.monotonic() < deadline:
        if panel_process.poll() is not None:
            raise RuntimeError(
                f"Panel exited before /health became ready. Exit code: {panel_process.returncode}"
            )
        try:
            health = json_request(f"http://127.0.0.1:{port}/health")
            if health.get("ok") is True:
                return
        except Exception as exc:  # pragma: no cover - polling loop
            last_error = str(exc)
        time.sleep(0.35)

    raise RuntimeError(f"Panel /health did not become ready on port {port}. Last error: {last_error}")


def wait_for_game_catalog(port: int, game_id: str, timeout_seconds: int) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        state = json_request(f"http://127.0.0.1:{port}/api/state")
        items = (((state.get("catalog") or {}).get("items")) or [])
        if any(str(item.get("gameId") or "") == game_id for item in items if isinstance(item, dict)):
            return
        time.sleep(0.35)
    raise RuntimeError(f"Selected game {game_id!r} was not discovered in /api/state catalog.")


def activate_game(port: int, game_id: str, timeout_seconds: int) -> None:
    result = json_request(
        f"http://127.0.0.1:{port}/api/game/start",
        method="POST",
        payload={"gameId": game_id},
    )
    if result.get("ok") is not True:
        raise RuntimeError(f"/api/game/start failed for {game_id!r}: {result}")

    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        state = json_request(f"http://127.0.0.1:{port}/api/state")
        snapshot = state.get("snapshot") or {}
        external_game = snapshot.get("externalGame") or {}
        if (
            str(external_game.get("gameId") or "") == game_id
            and external_game.get("active") is True
        ):
            return
        time.sleep(0.35)

    raise RuntimeError(f"Game {game_id!r} did not become active after /api/game/start.")


def terminate_panel(process: subprocess.Popen[str] | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def main() -> int:
    args = parse_args()
    if args.ui_port <= 0 or args.ui_port > 65535:
        raise SystemExit("UI port must be between 1 and 65535.")
    if args.startup_timeout <= 0:
        raise SystemExit("startup-timeout must be > 0.")

    selected_game = load_selected_game(args.game_root)
    panel_executable = find_panel_executable(args.panel_exe)
    if not check_local_port_idle(args.ui_port):
        raise SystemExit(
            f"UI port {args.ui_port} is already in use. Close the existing process or choose another port."
        )

    panel_env = os.environ.copy()
    panel_env["NLP3_LOCAL_GAMES_ROOT"] = str(selected_game.game_root.parent)

    panel_command = [
        str(panel_executable),
        "--ui",
        "--no-browser",
        "--ui-port",
        str(args.ui_port),
    ]

    print(f"[launcher] panel exe: {panel_executable}", flush=True)
    print(f"[launcher] game root: {selected_game.game_root}", flush=True)
    print(f"[launcher] game id: {selected_game.game_id}", flush=True)
    print(f"[launcher] game exe: {selected_game.entry_executable}", flush=True)
    print(f"[launcher] loader dll: {selected_game.webview_loader}", flush=True)
    print(f"[launcher] ui port: {args.ui_port}", flush=True)
    print(f"[launcher] command: {' '.join(panel_command)}", flush=True)

    panel_process: subprocess.Popen[str] | None = None
    cleanup_panel = False
    try:
        panel_process = subprocess.Popen(
            panel_command,
            cwd=str(panel_executable.parent),
            env=panel_env,
        )
        wait_for_panel_health(args.ui_port, panel_process, args.startup_timeout)
        wait_for_game_catalog(args.ui_port, selected_game.game_id, args.startup_timeout)
        activate_game(args.ui_port, selected_game.game_id, args.startup_timeout)

        print(f"[launcher] panel url: http://127.0.0.1:{args.ui_port}/", flush=True)
        print(
            f"[launcher] game activated successfully: {selected_game.display_name} ({selected_game.game_id})",
            flush=True,
        )

        if args.exit_after_activate:
            cleanup_panel = True
            return 0

        print("[launcher] press Ctrl+C to stop the panel started by this launcher.", flush=True)
        while panel_process.poll() is None:
            time.sleep(0.5)

        return int(panel_process.returncode or 0)
    except KeyboardInterrupt:
        print("[launcher] stop requested by user.", flush=True)
        cleanup_panel = True
        return 130
    except urllib.error.URLError as exc:
        print(f"[launcher] network error while talking to the panel: {exc}", file=sys.stderr, flush=True)
        cleanup_panel = True
        return 1
    except Exception as exc:  # pragma: no cover - process wrapper
        print(f"[launcher] {exc}", file=sys.stderr, flush=True)
        cleanup_panel = True
        return 1
    finally:
        if cleanup_panel and panel_process is not None:
            terminate_panel(panel_process)


if __name__ == "__main__":
    raise SystemExit(main())
