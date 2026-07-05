#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


NLP3_PANEL_DESKTOP_LAUNCHER_SIGNATURE = "nlp3-panel-desktop-launcher-v1"

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_UI_PORT = 18913
PORT_FALLBACK_ATTEMPTS = 20
LOCK_ROOT = Path(tempfile.gettempdir()) / "NisojeStudio"
LOCK_PATH = LOCK_ROOT / "desktop_launcher.lock"
STATE_PATH = LOCK_ROOT / "desktop_launcher_state.json"


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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Launch Nisoje Studio for desktop users without requiring terminal interaction.",
    )
    parser.add_argument(
        "--ui-port",
        type=int,
        default=DEFAULT_UI_PORT,
        help="Panel UI port. Defaults to 18913.",
    )
    parser.add_argument(
        "--panel-exe",
        default="",
        help="Optional override for NisojeStudio.exe.",
    )
    parser.add_argument(
        "--games-root",
        default=str(DEFAULT_GAMES_ROOT),
        help="Root folder containing external games discovered by the panel.",
    )
    parser.add_argument(
        "--startup-timeout",
        type=int,
        default=25,
        help="Seconds to wait for /health after launching the panel.",
    )
    parser.add_argument(
        "--wait-until-ready",
        action="store_true",
        help="Wait until the panel /health endpoint responds before exiting.",
    )
    parser.add_argument(
        "--restart-if-running",
        action="store_true",
        help="Terminate an existing panel instance from this repo before launching a fresh one.",
    )
    parser.add_argument(
        "--shutdown-timeout",
        type=int,
        default=10,
        help="Seconds to wait for the old panel process to release the port when restart-if-running is used.",
    )
    return parser.parse_args()


def json_request(url: str) -> dict:
    request = urllib.request.Request(url=url, headers={"Accept": "application/json"}, method="GET")
    with urllib.request.urlopen(request, timeout=2.0) as response:
        raw = response.read().decode("utf-8")
    return json.loads(raw) if raw.strip() else {}


def health_is_ready(port: int) -> bool:
    try:
        return json_request(f"http://127.0.0.1:{port}/health").get("ok") is True
    except Exception:
        return False


def port_is_idle(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.3)
        return sock.connect_ex(("127.0.0.1", port)) != 0


def can_bind_local_port(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        try:
            sock.bind(("127.0.0.1", port))
        except OSError:
            return False
    return True


def find_available_loopback_port(start_port: int, attempts: int = PORT_FALLBACK_ATTEMPTS) -> int | None:
    if start_port <= 0 or start_port > 65535:
        return None

    end_port = min(65535, start_port + max(0, attempts - 1))
    for port in range(start_port, end_port + 1):
        if port_is_idle(port) and can_bind_local_port(port):
            return port
    return None


def use_fallback_ui_port(current_port: int, reason: str) -> int:
    fallback_port = find_available_loopback_port(current_port + 1)
    if fallback_port is None:
        raise RuntimeError(
            f"UI port {current_port} is unavailable and no fallback port was free. {reason}"
        )

    print(
        f"[panel-launcher] ui port {current_port} is unavailable; using fallback port {fallback_port}.",
        flush=True,
    )
    if reason.strip():
        print(f"[panel-launcher] port fallback reason: {reason.strip()}", flush=True)
    return fallback_port


def read_last_ui_port() -> int | None:
    try:
        if not STATE_PATH.exists():
            return None
        data = json.loads(STATE_PATH.read_text(encoding="utf-8"))
        port = int(data.get("ui_port", 0))
    except Exception:
        return None

    if port <= 0 or port > 65535:
        return None
    return port


def write_last_ui_port(port: int) -> None:
    try:
        LOCK_ROOT.mkdir(parents=True, exist_ok=True)
        STATE_PATH.write_text(json.dumps({"ui_port": port}, indent=2), encoding="utf-8")
    except OSError:
        pass


def ready_fallback_port(preferred_port: int) -> int | None:
    last_port = read_last_ui_port()
    if last_port is None or last_port == preferred_port:
        return None
    if health_is_ready(last_port):
        return last_port
    return None


def acquire_launcher_lock(port: int, timeout_seconds: int) -> int | None:
    LOCK_ROOT.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + timeout_seconds
    stale_after_seconds = max(timeout_seconds, 15)

    while time.monotonic() < deadline:
        try:
            return os.open(str(LOCK_PATH), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        except FileExistsError:
            if health_is_ready(port):
                return None
            try:
                age_seconds = time.time() - LOCK_PATH.stat().st_mtime
                if age_seconds >= stale_after_seconds:
                    LOCK_PATH.unlink(missing_ok=True)
                    time.sleep(0.1)
                    continue
            except OSError:
                pass
            time.sleep(0.25)

    raise RuntimeError(
        f"Another desktop launcher instance is already running and the panel did not become ready on port {port}."
    )


def release_launcher_lock(lock_fd: int | None) -> None:
    if lock_fd is None:
        return

    try:
        os.close(lock_fd)
    except OSError:
        pass
    finally:
        try:
            LOCK_PATH.unlink(missing_ok=True)
        except OSError:
            pass


def terminate_existing_repo_panel(repo_root: Path) -> list[str]:
    if os.name != "nt":
        return []

    escaped_repo_root = str(repo_root).replace("'", "''")
    powershell_script = rf"""
$repoRoot = '{escaped_repo_root}'
$normalizedRoot = [System.IO.Path]::GetFullPath($repoRoot).TrimEnd('\').ToLowerInvariant()
$targets = Get-CimInstance Win32_Process | Where-Object {{
    if (-not $_.ExecutablePath) {{ return $false }}
    if ($_.Name -notin @('NisojeStudio.exe', 'nlp3_app.exe')) {{ return $false }}
    try {{
        $normalizedPath = [System.IO.Path]::GetFullPath($_.ExecutablePath).TrimEnd('\').ToLowerInvariant()
        return $normalizedPath.StartsWith($normalizedRoot)
    }} catch {{
        return $false
    }}
}}

foreach ($target in $targets) {{
    try {{
        Stop-Process -Id $target.ProcessId -Force -ErrorAction Stop
        "{{0}}`t{{1}}`t{{2}}" -f $target.ProcessId, $target.Name, $target.ExecutablePath
    }} catch {{
        Write-Error ("Could not stop process {{0}}: {{1}}" -f $target.ProcessId, $_.Exception.Message)
        exit 1
    }}
}}
"""
    result = subprocess.run(
        ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", powershell_script],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise RuntimeError(f"Could not terminate running panel instances from this repo. {detail}")

    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def wait_for_port_to_become_idle(port: int, timeout_seconds: int) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if port_is_idle(port) and can_bind_local_port(port):
            return
        time.sleep(0.25)

    raise RuntimeError(
        f"UI port {port} did not become available for binding after waiting {timeout_seconds} seconds."
    )


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
        REPO_ROOT / "dist" / "NisojeStudio" / "NisojeStudio.exe",
        REPO_ROOT / "build" / "release" / "src" / "platform" / "NisojeStudio.exe",
        REPO_ROOT / "build" / "Release" / "src" / "platform" / "NisojeStudio.exe",
        REPO_ROOT / "build" / "src" / "platform" / "NisojeStudio.exe",
        REPO_ROOT / "build" / "release_pack" / "src" / "platform" / "NisojeStudio.exe",
    ]
    preferred_match = choose_newest_existing_path(candidates)
    if preferred_match is not None:
        return preferred_match

    for base in (REPO_ROOT / "build", REPO_ROOT / "dist"):
        if not base.exists():
            continue
        fallback_match = choose_newest_existing_path(list(base.rglob("NisojeStudio.exe")))
        if fallback_match is not None:
            return fallback_match

    raise RuntimeError(
        "NisojeStudio.exe was not found. Build the panel first or generate the portable package."
    )


def wait_for_health(port: int, panel_process: subprocess.Popen[str], timeout_seconds: int) -> None:
    deadline = time.monotonic() + timeout_seconds
    last_error = ""
    while time.monotonic() < deadline:
        if panel_process.poll() is not None:
            raise RuntimeError(
                f"Panel exited before /health became ready. Exit code: {panel_process.returncode}"
            )
        try:
            if json_request(f"http://127.0.0.1:{port}/health").get("ok") is True:
                return
        except Exception as exc:
            last_error = str(exc)
        time.sleep(0.35)

    raise RuntimeError(f"Panel /health did not become ready on port {port}. Last error: {last_error}")


def main() -> int:
    args = parse_args()
    if args.ui_port <= 0 or args.ui_port > 65535:
        raise SystemExit("UI port must be between 1 and 65535.")
    if args.startup_timeout <= 0:
        raise SystemExit("startup-timeout must be > 0.")
    if args.shutdown_timeout <= 0:
        raise SystemExit("shutdown-timeout must be > 0.")

    panel_executable = find_panel_executable(args.panel_exe)
    ui_port = args.ui_port

    fallback_port = ready_fallback_port(ui_port)
    if fallback_port is not None:
        print(
            f"[panel-launcher] Nisoje Studio already appears to be running on fallback port {fallback_port}.",
            flush=True,
        )
        return 0

    launcher_lock_fd = acquire_launcher_lock(
        ui_port,
        max(args.startup_timeout, args.shutdown_timeout) + 5,
    )
    if launcher_lock_fd is None:
        print(
            f"[panel-launcher] another launcher instance already finished startup on port {ui_port}.",
            flush=True,
        )
        return 0

    try:
        fallback_port = ready_fallback_port(ui_port)
        if fallback_port is not None:
            print(
                f"[panel-launcher] Nisoje Studio already appears to be running on fallback port {fallback_port}.",
                flush=True,
            )
            return 0

        if health_is_ready(ui_port):
            if args.restart_if_running:
                terminated = terminate_existing_repo_panel(REPO_ROOT)
                if terminated:
                    print("[panel-launcher] terminated existing panel instances:", flush=True)
                    for line in terminated:
                        print(f"[panel-launcher]   {line}", flush=True)
                    try:
                        wait_for_port_to_become_idle(ui_port, args.shutdown_timeout)
                    except RuntimeError as exc:
                        ui_port = use_fallback_ui_port(ui_port, str(exc))
                else:
                    ui_port = use_fallback_ui_port(
                        ui_port,
                        "No running panel process from this repo could be terminated.",
                    )
            else:
                print(
                    f"[panel-launcher] Nisoje Studio already appears to be running on port {ui_port}.",
                    flush=True,
                )
                write_last_ui_port(ui_port)
                return 0

        if not port_is_idle(ui_port) or not can_bind_local_port(ui_port):
            if args.restart_if_running:
                terminated = terminate_existing_repo_panel(REPO_ROOT)
                if terminated:
                    print("[panel-launcher] terminated existing panel instances:", flush=True)
                    for line in terminated:
                        print(f"[panel-launcher]   {line}", flush=True)
                    try:
                        wait_for_port_to_become_idle(ui_port, args.shutdown_timeout)
                    except RuntimeError as exc:
                        ui_port = use_fallback_ui_port(ui_port, str(exc))
                else:
                    ui_port = use_fallback_ui_port(
                        ui_port,
                        "No running panel process from this repo could be terminated.",
                    )
            else:
                ui_port = use_fallback_ui_port(
                    ui_port,
                    "The preferred port is already in use by another process.",
                )

        if health_is_ready(ui_port):
            print(
                f"[panel-launcher] Nisoje Studio already appears to be running on port {ui_port}.",
                flush=True,
            )
            write_last_ui_port(ui_port)
            return 0

        try:
            games_root = Path(args.games_root).expanduser().resolve()
            panel_env = os.environ.copy()
            panel_env["NLP3_LOCAL_GAMES_ROOT"] = str(games_root)

            panel_command = [
                str(panel_executable),
                "--ui",
                "--no-browser",
                "--ui-port",
                str(ui_port),
            ]

            print(f"[panel-launcher] panel exe: {panel_executable}", flush=True)
            print(f"[panel-launcher] games root: {games_root}", flush=True)
            print(f"[panel-launcher] ui port: {ui_port}", flush=True)
            print(f"[panel-launcher] command: {' '.join(panel_command)}", flush=True)

            creationflags = 0
            if os.name == "nt":
                creationflags = getattr(subprocess, "DETACHED_PROCESS", 0) | getattr(
                    subprocess, "CREATE_NEW_PROCESS_GROUP", 0
                )

            try:
                panel_process = subprocess.Popen(
                    panel_command,
                    cwd=str(REPO_ROOT),
                    env=panel_env,
                    creationflags=creationflags,
                )
            except OSError as exc:
                raise SystemExit(f"Could not launch NisojeStudio.exe: {exc}") from exc

            if args.wait_until_ready:
                try:
                    wait_for_health(ui_port, panel_process, args.startup_timeout)
                except Exception:
                    try:
                        if panel_process.poll() is None:
                            panel_process.terminate()
                    except OSError:
                        pass
                    raise
                write_last_ui_port(ui_port)
                print(f"[panel-launcher] panel ready at http://127.0.0.1:{ui_port}/", flush=True)

            return 0
        finally:
            release_launcher_lock(launcher_lock_fd)
    except Exception:
        release_launcher_lock(launcher_lock_fd)
        raise


if __name__ == "__main__":
    raise SystemExit(main())
