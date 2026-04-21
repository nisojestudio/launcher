from __future__ import annotations

from dataclasses import asdict
from typing import Any

from bridge_config import BridgeConfig
from bridge_server import BridgeServer, BridgeServerConfig
from connection_manager import ConnectionManager
from event_dispatcher import AsyncEventDispatcher, PanelWsSink, connect_websocket
from event_models import CanonicalEvent, SessionStatus
from metrics_registry import MetricsRegistry
from replay_engine import ReplayEngine
from structured_logging import close_logger, configure_logger, log_json, utc_now_ms


class TikTokBridgeService:
    def __init__(self, config: BridgeConfig) -> None:
        self.config = config
        self.metrics = MetricsRegistry()
        self.logger = configure_logger(level=config.logging.level, log_path=config.logging.log_path)
        self.dispatcher = AsyncEventDispatcher(
            metrics=self.metrics,
            queue_size=config.buffer.size,
            batch_size=config.buffer.batch_size,
            overflow_policy=config.buffer.overflow_policy,
            panel_ws_sink=PanelWsSink(config.output.panel_ws_url, connect_websocket)
            if config.output.panel_ws_url
            else None,
            jsonl_path=config.output.output_jsonl,
            inbox_dir=config.output.inbox_dir,
            session_name=config.output.session_name,
            broadcast_callback=self._broadcast_payload,
        )
        self.replay_engine = ReplayEngine()
        self._server = BridgeServer(
            config=BridgeServerConfig(
                host=config.output.control_host,
                port=config.output.control_port,
                ws_host=config.output.broadcast_ws_host,
                ws_port=config.output.broadcast_ws_port,
                enable_ws_broadcast=config.output.broadcast_ws_enabled,
            ),
            metrics=self.metrics,
            health_factory=self.health_payload,
            status_factory=self.status_payload,
            replay_start_handler=self._handle_replay_start,
            replay_stop_handler=self._handle_replay_stop,
            shutdown_handler=self._handle_shutdown,
        )
        self._connection_manager = ConnectionManager(
            config=config,
            logger=self.logger,
            metrics=self.metrics,
            event_callback=self._publish_event,
            status_callback=self._publish_status,
        )
        self._connection_task = None
        self._replay_task = None
        self._last_replay_result: dict[str, Any] = {}
        self._last_status_message = "idle"
        self._last_session_status: dict[str, Any] = {}
        self._shutdown_requested = False

    async def start(self) -> None:
        await self.dispatcher.start()
        await self._server.start()
        self.metrics.set_gauge("service_running", 1)
        log_json(self.logger, "info", "bridge_service", "service started", config=self.config.to_dict())

    async def stop(self) -> None:
        self._shutdown_requested = True
        self._connection_manager.stop()
        self.replay_engine.stop()
        if self._connection_task is not None:
            self._connection_task.cancel()
            try:
                await self._connection_task
            except Exception:
                pass
            self._connection_task = None
        if self._replay_task is not None:
            self._replay_task.cancel()
            try:
                await self._replay_task
            except Exception:
                pass
            self._replay_task = None
        await self._server.stop()
        await self.dispatcher.stop()
        self.metrics.set_gauge("service_running", 0)
        log_json(self.logger, "info", "bridge_service", "service stopped")
        close_logger(self.logger)

    async def run_connection(self, *, target_user: str, room_id: str = "", max_events: int = 0, max_seconds: int = 0) -> int:
        import asyncio

        self._connection_task = asyncio.create_task(
            self._connection_manager.run(
                target_user=target_user,
                room_id=room_id,
                max_events=max_events,
                max_seconds=max_seconds,
            ),
            name="bridge-connection-manager",
        )
        return await self._connection_task

    async def run_replay(
        self,
        *,
        jsonl_path: str,
        preserve_timing: bool,
        speed: float,
        loop: bool,
    ) -> dict[str, Any]:
        result = await self.replay_engine.replay_jsonl(
            jsonl_path,
            self._publish_event,
            preserve_timing=preserve_timing,
            speed=speed,
            loop=loop,
        )
        self._last_replay_result = asdict(result)
        return self._last_replay_result

    async def simulate_burst(self, *, count: int) -> dict[str, Any]:
        result = await self.replay_engine.simulate_burst(self._publish_event, count=count)
        self._last_replay_result = asdict(result)
        return self._last_replay_result

    async def _publish_event(self, event: CanonicalEvent) -> bool:
        accepted = await self.dispatcher.publish(event)
        if accepted:
            self._last_status_message = f"dispatched {event.event_type.value}"
        return accepted

    async def _publish_status(self, status: SessionStatus) -> None:
        self._last_session_status = status.to_panel_payload()
        self._last_status_message = status.message or status.connection_state.value
        self.metrics.set_gauge("connected", 1 if status.connection_state.value == "connected" else 0)
        await self.dispatcher.emit_status(status)

    async def _broadcast_payload(self, payload: dict[str, Any]) -> None:
        await self._server.broadcast(payload)

    def health_payload(self) -> dict[str, Any]:
        snapshot = self.metrics.snapshot()
        return {
            "ok": True,
            "status": "running",
            "uptime_ms": snapshot.uptime_ms,
            "throughput_events_per_sec": snapshot.throughput_events_per_sec,
            "median_ingest_latency_ms": snapshot.median_ingest_latency_ms,
            "last_status_message": self._last_status_message,
            "session_status": self._last_session_status,
        }

    def status_payload(self) -> dict[str, Any]:
        return {
            "timestamp_ms": utc_now_ms(),
            "config": self.config.to_dict(),
            "metrics": self.metrics.snapshot().to_dict(),
            "last_replay_result": self._last_replay_result,
            "last_status_message": self._last_status_message,
            "last_session_status": self._last_session_status,
            "shutdown_requested": self._shutdown_requested,
        }

    async def _handle_replay_start(self, payload: dict[str, Any]) -> dict[str, Any]:
        import asyncio

        path = str(payload.get("path") or self.config.replay.jsonl_path or "").strip()
        if not path:
            return {"ok": False, "error": "replay path missing"}
        preserve_timing = bool(payload.get("preserve_timing", self.config.replay.preserve_timing))
        speed = float(payload.get("speed", self.config.replay.speed or 1.0))
        loop = bool(payload.get("loop", self.config.replay.loop))
        if self._replay_task is not None and not self._replay_task.done():
            return {"ok": False, "error": "replay already running"}

        async def _runner() -> None:
            self._last_replay_result = await self.run_replay(
                jsonl_path=path,
                preserve_timing=preserve_timing,
                speed=speed,
                loop=loop,
            )

        self.replay_engine = ReplayEngine()
        self._replay_task = asyncio.create_task(_runner(), name="bridge-replay")
        return {"ok": True, "started": True, "path": path}

    async def _handle_replay_stop(self) -> dict[str, Any]:
        self.replay_engine.stop()
        if self._replay_task is not None:
            try:
                await self._replay_task
            except Exception:
                pass
            self._replay_task = None
        return {"ok": True, "stopped": True, "result": self._last_replay_result}

    async def _handle_shutdown(self) -> dict[str, Any]:
        self._shutdown_requested = True
        self._last_status_message = "shutdown requested"
        self._connection_manager.stop()
        self.replay_engine.stop()
        return {"ok": True, "stopping": True}
