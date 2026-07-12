from __future__ import annotations

import asyncio
import json
import time
from pathlib import Path
from typing import Any, Awaitable, Callable

from bridge_client import append_jsonl, write_json
from event_models import CanonicalEvent, SessionStatus
from event_normalizer import canonical_event_to_json, canonical_event_to_panel_payload
from metrics_registry import MetricsRegistry


DispatchCallback = Callable[[CanonicalEvent], Awaitable[None]]
StatusCallback = Callable[[SessionStatus], Awaitable[None]]
BroadcastCallback = Callable[[dict[str, Any]], Awaitable[None]]


class PanelWsSink:
    def __init__(self, ws_url: str, connect_factory: Callable[[str], Awaitable[Any]]) -> None:
        self._ws_url = ws_url
        self._connect_factory = connect_factory
        self._connection: Any = None
        self._last_failure_monotonic = 0.0
        self._retry_cooldown_sec = 0.5

    async def _ensure_connection(self) -> Any:
        if self._connection is None:
            now = time.monotonic()
            if self._last_failure_monotonic > 0 and (now - self._last_failure_monotonic) < self._retry_cooldown_sec:
                raise RuntimeError("panel ws reconnect cooldown")
            self._connection = await self._connect_factory(self._ws_url)
        return self._connection

    async def send_json(self, payload: dict[str, Any]) -> None:
        serialized = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
        try:
            connection = await self._ensure_connection()
            await connection.send(serialized)
        except Exception:
            self._last_failure_monotonic = time.monotonic()
            await self.close()
            raise

    async def close(self) -> None:
        if self._connection is None:
            return
        try:
            await self._connection.close()
        except Exception:
            pass
        self._connection = None


class AsyncEventDispatcher:
    def __init__(
        self,
        *,
        metrics: MetricsRegistry,
        queue_size: int,
        batch_size: int,
        overflow_policy: str,
        panel_ws_sink: PanelWsSink | None = None,
        jsonl_path: str = "",
        inbox_dir: str = "",
        session_name: str = "tiktok-live",
        broadcast_callback: BroadcastCallback | None = None,
    ) -> None:
        self._metrics = metrics
        self._queue: asyncio.Queue[CanonicalEvent] = asyncio.Queue(maxsize=max(1, queue_size))
        self._batch_size = max(1, batch_size)
        self._overflow_policy = overflow_policy or "drop_oldest"
        self._panel_ws_sink = panel_ws_sink
        self._jsonl_path = Path(jsonl_path) if jsonl_path else None
        self._inbox_dir = Path(inbox_dir) if inbox_dir else None
        self._session_name = session_name or "tiktok-live"
        self._broadcast_callback = broadcast_callback
        self._worker_task: asyncio.Task[None] | None = None
        self._sequence = 0
        self._running = False
        self._idle_event = asyncio.Event()
        self._idle_event.set()

    async def start(self) -> None:
        if self._worker_task is not None:
            return
        self._running = True
        self._worker_task = asyncio.create_task(self._worker_loop(), name="bridge-dispatcher")

    async def stop(self) -> None:
        await self.flush()
        self._running = False
        if self._worker_task is not None:
            self._worker_task.cancel()
            try:
                await self._worker_task
            except asyncio.CancelledError:
                pass
            self._worker_task = None
        if self._panel_ws_sink is not None:
            await self._panel_ws_sink.close()

    async def flush(self, timeout_sec: float = 10.0) -> bool:
        try:
            await asyncio.wait_for(self._idle_event.wait(), timeout=timeout_sec)
            return True
        except asyncio.TimeoutError:
            return False

    async def publish(self, event: CanonicalEvent) -> bool:
        self._metrics.set_gauge("queue_size", float(self._queue.qsize()))
        try:
            self._idle_event.clear()
            self._queue.put_nowait(event)
            self._metrics.increment("events_buffered_total")
            self._metrics.set_gauge("queue_size", float(self._queue.qsize()))
            return True
        except asyncio.QueueFull:
            self._metrics.increment("events_dropped_total")
            if self._overflow_policy == "drop_oldest":
                try:
                    _ = self._queue.get_nowait()
                except asyncio.QueueEmpty:
                    return False
                self._queue.put_nowait(event)
                self._idle_event.clear()
                self._metrics.increment("events_buffer_recovered_total")
                self._metrics.set_gauge("queue_size", float(self._queue.qsize()))
                return True
            return False

    async def emit_status(self, status: SessionStatus) -> None:
        if self._panel_ws_sink is not None:
            try:
                await self._panel_ws_sink.send_json(status.to_panel_payload())
            except Exception:
                self._metrics.increment("panel_ws_send_failures_total")
        if self._broadcast_callback is not None:
            try:
                await self._broadcast_callback(
                    {
                        "message_type": "session_status",
                        **status.to_panel_payload(),
                    }
                )
            except Exception:
                self._metrics.increment("broadcast_send_failures_total")

    async def _worker_loop(self) -> None:
        while self._running:
            event = await self._queue.get()
            batch = [event]
            while len(batch) < self._batch_size:
                try:
                    batch.append(self._queue.get_nowait())
                except asyncio.QueueEmpty:
                    break

            for item in batch:
                try:
                    await self._dispatch_one(item)
                except Exception:
                    self._metrics.increment("events_dispatch_failures_total")
            self._metrics.set_gauge("queue_size", float(self._queue.qsize()))
            if self._queue.empty():
                self._idle_event.set()

    async def _dispatch_one(self, event: CanonicalEvent) -> None:
        self._metrics.record_event(event.event_type.value, event.latency_ms)
        payload = canonical_event_to_panel_payload(event)
        dispatched = False

        if self._jsonl_path is not None:
            append_jsonl(self._jsonl_path, payload)
            dispatched = True

        if self._inbox_dir is not None:
            self._sequence += 1
            file_name = f"{self._session_name}-{self._sequence:06d}-{event.event_type.value}.json"
            write_json(self._inbox_dir / file_name, payload)
            dispatched = True

        if self._panel_ws_sink is not None:
            try:
                await self._panel_ws_sink.send_json(payload)
                dispatched = True
            except Exception:
                self._metrics.increment("panel_ws_send_failures_total")

        if self._broadcast_callback is not None:
            try:
                await self._broadcast_callback(
                    {
                        "message_type": "canonical_event",
                        "event": event.to_dict(),
                    }
                )
                dispatched = True
            except Exception:
                self._metrics.increment("broadcast_send_failures_total")

        if dispatched:
            self._metrics.increment("events_dispatched_total")


async def connect_websocket(url: str) -> Any:
    import websockets  # type: ignore

    return await websockets.connect(
        url,
        open_timeout=5.0,
        close_timeout=1.0,
        ping_interval=None,
    )
