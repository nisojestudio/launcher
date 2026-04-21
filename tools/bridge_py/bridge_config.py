from __future__ import annotations

import json
import os
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any


def _parse_bool(value: Any, default: bool) -> bool:
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"1", "true", "yes", "on"}


def _parse_int(value: Any, default: int, min_value: int = 0, max_value: int = 1_000_000) -> int:
    try:
        parsed = int(value)
    except Exception:
        return default
    if parsed < min_value:
        return min_value
    if parsed > max_value:
        return max_value
    return parsed


def _parse_float(
    value: Any,
    default: float,
    min_value: float = 0.0,
    max_value: float = 1_000_000.0,
) -> float:
    try:
        parsed = float(value)
    except Exception:
        return default
    if parsed < min_value:
        return min_value
    if parsed > max_value:
        return max_value
    return parsed


def _parse_text(value: Any, default: str = "") -> str:
    if value is None:
        return default
    text = str(value).strip()
    return text if text else default


def _load_yaml_if_available(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}

    try:
        import yaml  # type: ignore
    except ImportError:
        return {}

    with path.open("r", encoding="utf-8") as handle:
        data = yaml.safe_load(handle) or {}
    return data if isinstance(data, dict) else {}


def _env(name: str) -> str:
    return str(os.getenv(name, "") or "").strip()


@dataclass(slots=True)
class RetryPolicyConfig:
    enabled: bool = True
    base_delay_sec: float = 2.0
    max_delay_sec: float = 45.0
    not_live_delay_sec: float = 20.0
    max_attempts: int = 0


@dataclass(slots=True)
class ConnectionConfig:
    username: str = ""
    room_id: str = ""
    connect_timeout_sec: float = 20.0
    heartbeat_interval_sec: float = 15.0
    heartbeat_warning_after_sec: float = 60.0


@dataclass(slots=True)
class BufferConfig:
    size: int = 8192
    batch_size: int = 64
    overflow_policy: str = "drop_oldest"


@dataclass(slots=True)
class OutputConfig:
    panel_ws_url: str = "ws://127.0.0.1:8765"
    output_jsonl: str = ""
    inbox_dir: str = ""
    session_name: str = "tiktok-live"
    broadcast_ws_enabled: bool = True
    broadcast_ws_host: str = "127.0.0.1"
    broadcast_ws_port: int = 8766
    control_host: str = "127.0.0.1"
    control_port: int = 8770
    auto_emit_session_status: bool = True


@dataclass(slots=True)
class ReplayConfig:
    enabled: bool = False
    jsonl_path: str = ""
    preserve_timing: bool = True
    speed: float = 1.0
    loop: bool = False


@dataclass(slots=True)
class LoggingConfig:
    level: str = "INFO"
    log_path: str = "tools/bridge_py/logs/bridge.jsonl"


@dataclass(slots=True)
class BridgeConfig:
    connection_mode: str = "tiktok_live"
    connection: ConnectionConfig = field(default_factory=ConnectionConfig)
    retry_policy: RetryPolicyConfig = field(default_factory=RetryPolicyConfig)
    buffer: BufferConfig = field(default_factory=BufferConfig)
    output: OutputConfig = field(default_factory=OutputConfig)
    replay: ReplayConfig = field(default_factory=ReplayConfig)
    logging: LoggingConfig = field(default_factory=LoggingConfig)
    legacy_bridge_root: str = ""
    max_events: int = 0
    max_seconds: int = 0

    def to_dict(self) -> dict[str, Any]:
        return json.loads(json.dumps(asdict(self)))


def load_bridge_config(path: str | Path | None = None) -> BridgeConfig:
    config_path = Path(path) if path else Path("tools/bridge_py/bridge_config.yaml")
    data = _load_yaml_if_available(config_path)

    connection = data.get("connection", {}) if isinstance(data.get("connection"), dict) else {}
    retry_policy = data.get("retry_policy", {}) if isinstance(data.get("retry_policy"), dict) else {}
    buffer = data.get("buffer", {}) if isinstance(data.get("buffer"), dict) else {}
    output = data.get("output", {}) if isinstance(data.get("output"), dict) else {}
    replay = data.get("replay", {}) if isinstance(data.get("replay"), dict) else {}
    logging = data.get("logging", {}) if isinstance(data.get("logging"), dict) else {}

    config = BridgeConfig(
        connection_mode=_parse_text(data.get("connection_mode"), "tiktok_live"),
        connection=ConnectionConfig(
            username=_parse_text(_env("LIVEPANEL_TIKTOK_USER") or connection.get("username"), ""),
            room_id=_parse_text(_env("LIVEPANEL_TIKTOK_ROOM_ID") or connection.get("room_id"), ""),
            connect_timeout_sec=_parse_float(
                _env("LIVEPANEL_TIKTOK_CONNECT_TIMEOUT_SEC") or connection.get("connect_timeout_sec"),
                20.0,
                min_value=5.0,
                max_value=180.0,
            ),
            heartbeat_interval_sec=_parse_float(
                _env("LIVEPANEL_BRIDGE_HEARTBEAT_INTERVAL_SEC") or connection.get("heartbeat_interval_sec"),
                15.0,
                min_value=1.0,
                max_value=600.0,
            ),
            heartbeat_warning_after_sec=_parse_float(
                _env("LIVEPANEL_BRIDGE_HEARTBEAT_WARNING_AFTER_SEC")
                or connection.get("heartbeat_warning_after_sec"),
                60.0,
                min_value=5.0,
                max_value=3600.0,
            ),
        ),
        retry_policy=RetryPolicyConfig(
            enabled=_parse_bool(
                _env("LIVEPANEL_TIKTOK_RETRY_ENABLED") or retry_policy.get("enabled"),
                True,
            ),
            base_delay_sec=_parse_float(
                _env("LIVEPANEL_TIKTOK_RETRY_BASE_SEC") or retry_policy.get("base_delay_sec"),
                2.0,
                min_value=1.0,
                max_value=120.0,
            ),
            max_delay_sec=_parse_float(
                _env("LIVEPANEL_TIKTOK_RETRY_MAX_SEC") or retry_policy.get("max_delay_sec"),
                45.0,
                min_value=2.0,
                max_value=600.0,
            ),
            not_live_delay_sec=_parse_float(
                _env("LIVEPANEL_TIKTOK_RETRY_NOT_LIVE_SEC") or retry_policy.get("not_live_delay_sec"),
                20.0,
                min_value=5.0,
                max_value=600.0,
            ),
            max_attempts=_parse_int(
                _env("LIVEPANEL_TIKTOK_RETRY_MAX_ATTEMPTS") or retry_policy.get("max_attempts"),
                0,
                min_value=0,
                max_value=1000,
            ),
        ),
        buffer=BufferConfig(
            size=_parse_int(
                _env("LIVEPANEL_BRIDGE_BUFFER_SIZE") or buffer.get("size"),
                8192,
                min_value=64,
                max_value=200000,
            ),
            batch_size=_parse_int(
                _env("LIVEPANEL_BRIDGE_BATCH_SIZE") or buffer.get("batch_size"),
                64,
                min_value=1,
                max_value=4096,
            ),
            overflow_policy=_parse_text(buffer.get("overflow_policy"), "drop_oldest"),
        ),
        output=OutputConfig(
            panel_ws_url=_parse_text(_env("LIVEPANEL_PANEL_WS_URL") or output.get("panel_ws_url"), "ws://127.0.0.1:8765"),
            output_jsonl=_parse_text(_env("LIVEPANEL_OUTPUT_JSONL") or output.get("output_jsonl"), ""),
            inbox_dir=_parse_text(_env("LIVEPANEL_OUTPUT_INBOX") or output.get("inbox_dir"), ""),
            session_name=_parse_text(_env("LIVEPANEL_SESSION_NAME") or output.get("session_name"), "tiktok-live"),
            broadcast_ws_enabled=_parse_bool(
                _env("LIVEPANEL_BROADCAST_WS_ENABLED") or output.get("broadcast_ws_enabled"),
                True,
            ),
            broadcast_ws_host=_parse_text(output.get("broadcast_ws_host"), "127.0.0.1"),
            broadcast_ws_port=_parse_int(
                _env("LIVEPANEL_BROADCAST_WS_PORT") or output.get("broadcast_ws_port"),
                8766,
                min_value=1,
                max_value=65535,
            ),
            control_host=_parse_text(output.get("control_host"), "127.0.0.1"),
            control_port=_parse_int(
                _env("LIVEPANEL_BRIDGE_CONTROL_PORT") or output.get("control_port"),
                8770,
                min_value=1,
                max_value=65535,
            ),
            auto_emit_session_status=_parse_bool(output.get("auto_emit_session_status"), True),
        ),
        replay=ReplayConfig(
            enabled=_parse_bool(_env("LIVEPANEL_REPLAY_ENABLED") or replay.get("enabled"), False),
            jsonl_path=_parse_text(_env("LIVEPANEL_REPLAY_PATH") or replay.get("jsonl_path"), ""),
            preserve_timing=_parse_bool(replay.get("preserve_timing"), True),
            speed=_parse_float(
                _env("LIVEPANEL_REPLAY_SPEED") or replay.get("speed"),
                1.0,
                min_value=0.1,
                max_value=100.0,
            ),
            loop=_parse_bool(replay.get("loop"), False),
        ),
        logging=LoggingConfig(
            level=_parse_text(_env("LIVEPANEL_BRIDGE_LOG_LEVEL") or logging.get("level"), "INFO"),
            log_path=_parse_text(_env("LIVEPANEL_BRIDGE_LOG_PATH") or logging.get("log_path"), "tools/bridge_py/logs/bridge.jsonl"),
        ),
        legacy_bridge_root=_parse_text(
            _env("LIVEPANEL_LEGACY_BRIDGE_ROOT") or data.get("legacy_bridge_root"),
            "",
        ),
        max_events=_parse_int(_env("LIVEPANEL_MAX_EVENTS") or data.get("max_events"), 0, min_value=0, max_value=100000000),
        max_seconds=_parse_int(_env("LIVEPANEL_MAX_SECONDS") or data.get("max_seconds"), 0, min_value=0, max_value=31536000),
    )
    return config
