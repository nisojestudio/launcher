from __future__ import annotations

import json
import logging
import os
import sys
import time
from pathlib import Path
from typing import Any


class JsonLogFormatter(logging.Formatter):
    def format(self, record: logging.LogRecord) -> str:
        payload: dict[str, Any] = {
            "timestamp": int(record.created * 1000),
            "severity": record.levelname.lower(),
            "component": getattr(record, "component", "bridge"),
            "message": record.getMessage(),
        }

        extra_fields = getattr(record, "fields", None)
        if isinstance(extra_fields, dict):
            payload.update(extra_fields)

        return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


def _default_local_log_root() -> Path:
    local_app_data = os.getenv("LOCALAPPDATA", "").strip()
    if local_app_data:
        return Path(local_app_data) / "NisojeStudio" / "logs"
    return Path(tempfile.gettempdir()) / "NisojeStudio" / "logs"


def _nearest_existing_parent(path: Path) -> Path:
    candidate = path.parent
    while not candidate.exists() and candidate != candidate.parent:
        candidate = candidate.parent
    return candidate


def _is_under_protected_windows_root(path: Path) -> bool:
    protected_roots = [
        os.getenv("ProgramFiles", "").strip(),
        os.getenv("ProgramFiles(x86)", "").strip(),
        os.getenv("SystemRoot", "").strip(),
        os.getenv("WINDIR", "").strip(),
    ]
    normalized_path = str(path.resolve()).lower()
    for root in protected_roots:
        if not root:
            continue
        normalized_root = str(Path(root).resolve()).lower().rstrip("\\/")
        if normalized_path == normalized_root or normalized_path.startswith(normalized_root + "\\"):
            return True
    return False


def _is_existing_parent_writable(path: Path) -> bool:
    existing_parent = _nearest_existing_parent(path)
    if _is_under_protected_windows_root(existing_parent):
        return False
    return os.access(existing_parent, os.W_OK)


def resolve_log_path(default_path: str) -> Path:
    env_value = os.getenv("LIVEPANEL_BRIDGE_LOG_PATH", "").strip()
    requested_text = env_value or default_path
    requested_path = Path(requested_text)
    if requested_path.is_absolute():
        return requested_path

    candidate = (Path.cwd() / requested_path).resolve()
    if _is_existing_parent_writable(candidate):
        return candidate

    return (_default_local_log_root() / requested_path.name).resolve()


def describe_log_destination(default_path: str) -> dict[str, Any]:
    env_value = os.getenv("LIVEPANEL_BRIDGE_LOG_PATH", "").strip()
    requested_text = env_value or default_path
    requested_path = Path(requested_text)
    resolved_path = resolve_log_path(default_path)
    used_fallback = (
        not requested_path.is_absolute()
        and resolved_path != (Path.cwd() / requested_path).resolve()
    )
    return {
        "requestedPath": requested_text,
        "resolvedPath": str(resolved_path),
        "directoryWritable": _is_existing_parent_writable(resolved_path),
        "usesLocalFallback": used_fallback,
    }


def configure_logger(
    *,
    name: str = "livepanel.bridge",
    level: str = "INFO",
    log_path: str = "tools/bridge_py/logs/bridge.jsonl",
) -> logging.Logger:
    logger = logging.getLogger(name)
    logger.setLevel(getattr(logging, level.upper(), logging.INFO))
    logger.handlers.clear()
    logger.propagate = False

    formatter = JsonLogFormatter()

    stream_handler = logging.StreamHandler(sys.stdout)
    stream_handler.setFormatter(formatter)
    logger.addHandler(stream_handler)

    resolved_log_path = resolve_log_path(log_path)
    resolved_log_path.parent.mkdir(parents=True, exist_ok=True)
    file_handler = logging.FileHandler(resolved_log_path, encoding="utf-8")
    file_handler.setFormatter(formatter)
    logger.addHandler(file_handler)

    return logger


def log_json(
    logger: logging.Logger,
    severity: str,
    component: str,
    message: str,
    **fields: Any,
) -> None:
    level = getattr(logging, severity.upper(), logging.INFO)
    logger.log(level, message, extra={"component": component, "fields": fields})


def close_logger(logger: logging.Logger) -> None:
    handlers = list(logger.handlers)
    for handler in handlers:
        try:
            handler.flush()
        except Exception:
            pass
        try:
            handler.close()
        except Exception:
            pass
        logger.removeHandler(handler)


def utc_now_ms() -> int:
    return int(time.time() * 1000)
