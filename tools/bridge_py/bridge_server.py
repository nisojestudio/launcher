from __future__ import annotations

import asyncio
import ipaddress
import json
import re
from dataclasses import dataclass
from typing import Any, Awaitable, Callable
from urllib.parse import urlparse

from metrics_registry import MetricsRegistry


JsonFactory = Callable[[], dict[str, Any]]
ReplayStartHandler = Callable[[dict[str, Any]], Awaitable[dict[str, Any]]]
ReplayStopHandler = Callable[[], Awaitable[dict[str, Any]]]
ShutdownHandler = Callable[[], Awaitable[dict[str, Any]]]

LOOPBACK_ORIGIN_PATTERN = re.compile(r"^https?://(?:localhost|127\.0\.0\.1|\[::1\])(?::\d+)?$", re.IGNORECASE)


def _is_loopback_host(host: str) -> bool:
    normalized = str(host or "").strip().lower()
    if not normalized:
        return False
    if normalized == "localhost":
        return True
    try:
        return ipaddress.ip_address(normalized).is_loopback
    except ValueError:
        return False


def _is_loopback_origin(origin: str | None) -> bool:
    if not origin:
        return True
    try:
        parsed = urlparse(origin)
    except Exception:
        return False
    if parsed.scheme not in {"http", "https"}:
        return False
    return _is_loopback_host(parsed.hostname or "")


def _validate_loopback_server_hosts(config: "BridgeServerConfig") -> None:
    if not _is_loopback_host(config.host):
        raise ValueError("BridgeServer requires a loopback control_host")
    if config.enable_ws_broadcast and not _is_loopback_host(config.ws_host):
        raise ValueError("BridgeServer requires a loopback broadcast_ws_host")


@dataclass(slots=True)
class BridgeServerConfig:
    host: str = "127.0.0.1"
    port: int = 8770
    ws_host: str = "127.0.0.1"
    ws_port: int = 8766
    enable_ws_broadcast: bool = True


class BridgeServer:
    def __init__(
        self,
        *,
        config: BridgeServerConfig,
        metrics: MetricsRegistry,
        health_factory: JsonFactory,
        status_factory: JsonFactory,
        replay_start_handler: ReplayStartHandler,
        replay_stop_handler: ReplayStopHandler,
        shutdown_handler: ShutdownHandler,
    ) -> None:
        self._config = config
        self._metrics = metrics
        self._health_factory = health_factory
        self._status_factory = status_factory
        self._replay_start_handler = replay_start_handler
        self._replay_stop_handler = replay_stop_handler
        self._shutdown_handler = shutdown_handler
        self._http_server: asyncio.AbstractServer | None = None
        self._ws_server: Any = None
        self._subscribers: set[Any] = set()

    async def start(self) -> None:
        _validate_loopback_server_hosts(self._config)
        self._http_server = await asyncio.start_server(
            self._handle_http_client,
            self._config.host,
            self._config.port,
        )
        if self._config.enable_ws_broadcast:
            import websockets  # type: ignore

            self._ws_server = await websockets.serve(
                self._handle_ws_client,
                self._config.ws_host,
                self._config.ws_port,
                origins=[None, LOOPBACK_ORIGIN_PATTERN],
            )

    async def stop(self) -> None:
        if self._http_server is not None:
            self._http_server.close()
            await self._http_server.wait_closed()
            self._http_server = None
        if self._ws_server is not None:
            self._ws_server.close()
            await self._ws_server.wait_closed()
            self._ws_server = None
        for subscriber in list(self._subscribers):
            try:
                await subscriber.close()
            except Exception:
                pass
        self._subscribers.clear()

    async def broadcast(self, payload: dict[str, Any]) -> None:
        if not self._subscribers:
            return
        serialized = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
        stale: list[Any] = []
        for subscriber in list(self._subscribers):
            try:
                await subscriber.send(serialized)
            except Exception:
                stale.append(subscriber)
        for subscriber in stale:
            self._subscribers.discard(subscriber)
        self._metrics.set_gauge("broadcast_subscribers", float(len(self._subscribers)))

    async def _handle_ws_client(self, websocket: Any) -> None:
        self._subscribers.add(websocket)
        self._metrics.set_gauge("broadcast_subscribers", float(len(self._subscribers)))
        try:
            await websocket.send(
                json.dumps(
                    {
                        "message_type": "bridge_server_status",
                        "status": self._status_factory(),
                    },
                    ensure_ascii=False,
                    separators=(",", ":"),
                )
            )
            async for _message in websocket:
                await websocket.send(
                    json.dumps(
                        {
                            "message_type": "bridge_server_ack",
                            "ok": True,
                        },
                        ensure_ascii=False,
                        separators=(",", ":"),
                    )
                )
        finally:
            self._subscribers.discard(websocket)
            self._metrics.set_gauge("broadcast_subscribers", float(len(self._subscribers)))

    async def _handle_http_client(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
    ) -> None:
        try:
            header_bytes = await reader.readuntil(b"\r\n\r\n")
        except Exception:
            writer.close()
            await writer.wait_closed()
            return

        header_text = header_bytes.decode("utf-8", errors="replace")
        lines = header_text.split("\r\n")
        request_line = lines[0].split(" ", 2)
        if len(request_line) < 2:
            await self._write_response(writer, 400, {"error": "bad request"})
            return
        method = request_line[0].upper()
        path = request_line[1]
        headers: dict[str, str] = {}
        for line in lines[1:]:
            if not line or ":" not in line:
                continue
            name, value = line.split(":", 1)
            headers[name.strip().lower()] = value.strip()

        body = b""
        content_length = int(headers.get("content-length", "0") or "0")
        if content_length > 0:
            body = await reader.readexactly(content_length)

        if method == "POST" and not _is_loopback_origin(headers.get("origin")):
            await self._write_response(writer, 403, {"error": "origin_not_allowed"})
            return

        if method == "GET" and path == "/health":
            await self._write_response(writer, 200, self._health_factory())
            return
        if method == "GET" and path == "/status":
            await self._write_response(writer, 200, self._status_factory())
            return
        if method == "GET" and path == "/metrics":
            await self._write_text_response(writer, 200, self._metrics.render_prometheus(), "text/plain; charset=utf-8")
            return
        if method == "POST" and path == "/replay/start":
            payload = json.loads(body.decode("utf-8") or "{}") if body else {}
            if not isinstance(payload, dict):
                payload = {}
            result = await self._replay_start_handler(payload)
            await self._write_response(writer, 200, result)
            return
        if method == "POST" and path == "/replay/stop":
            result = await self._replay_stop_handler()
            await self._write_response(writer, 200, result)
            return
        if method == "POST" and path == "/shutdown":
            result = await self._shutdown_handler()
            await self._write_response(writer, 200, result)
            return

        await self._write_response(writer, 404, {"error": "not found"})

    async def _write_response(self, writer: asyncio.StreamWriter, status: int, payload: dict[str, Any]) -> None:
        serialized = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        await self._write_text_response(writer, status, serialized.decode("utf-8"), "application/json; charset=utf-8")

    async def _write_text_response(
        self,
        writer: asyncio.StreamWriter,
        status: int,
        body: str,
        content_type: str,
    ) -> None:
        reason = {
            200: "OK",
            400: "Bad Request",
            403: "Forbidden",
            404: "Not Found",
        }.get(status, "OK")
        encoded = body.encode("utf-8")
        writer.write(
            (
                f"HTTP/1.1 {status} {reason}\r\n"
                f"Content-Type: {content_type}\r\n"
                f"Content-Length: {len(encoded)}\r\n"
                "Connection: close\r\n\r\n"
            ).encode("utf-8")
            + encoded
        )
        await writer.drain()
        writer.close()
        await writer.wait_closed()
