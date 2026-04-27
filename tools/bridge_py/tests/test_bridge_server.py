from __future__ import annotations

import asyncio
import json
import unittest

import websockets

from bridge_server import BridgeServer, BridgeServerConfig
from metrics_registry import MetricsRegistry


async def http_request(port: int, request: str) -> tuple[str, str]:
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    writer.write(request.encode("utf-8"))
    await writer.drain()
    response = await reader.read()
    writer.close()
    await writer.wait_closed()
    raw = response.decode("utf-8", errors="replace")
    header_text, _, body = raw.partition("\r\n\r\n")
    return header_text, body


class BridgeServerTests(unittest.IsolatedAsyncioTestCase):
    async def _make_server(self, **config_overrides) -> BridgeServer:
        metrics = MetricsRegistry()

        async def replay_start(payload):
            return {"ok": True, "payload": payload, "started": True}

        async def replay_stop():
            return {"ok": True, "stopped": True}

        async def shutdown():
            return {"ok": True, "stopping": True}

        server = BridgeServer(
            config=BridgeServerConfig(**config_overrides),
            metrics=metrics,
            health_factory=lambda: {"ok": True, "status": "running"},
            status_factory=lambda: {"status": "ready"},
            replay_start_handler=replay_start,
            replay_stop_handler=replay_stop,
            shutdown_handler=shutdown,
        )
        await server.start()
        return server

    async def test_health_status_metrics_and_replay_endpoints(self) -> None:
        server = await self._make_server(port=0, enable_ws_broadcast=False)
        try:
            port = server._http_server.sockets[0].getsockname()[1]

            header, body = await http_request(port, "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
            self.assertIn("200 OK", header)
            self.assertTrue(json.loads(body)["ok"])

            header, body = await http_request(port, "GET /status HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
            self.assertIn("200 OK", header)
            self.assertEqual(json.loads(body)["status"], "ready")

            header, body = await http_request(port, "GET /metrics HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
            self.assertIn("200 OK", header)
            self.assertIn("livepanel_bridge_uptime_ms", body)

            replay_payload = json.dumps({"path": "session.jsonl"})
            header, body = await http_request(
                port,
                "POST /replay/start HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "Content-Type: application/json\r\n"
                f"Content-Length: {len(replay_payload.encode('utf-8'))}\r\n"
                "Connection: close\r\n\r\n"
                f"{replay_payload}",
            )
            self.assertIn("200 OK", header)
            self.assertTrue(json.loads(body)["started"])

            header, body = await http_request(port, "POST /replay/stop HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
            self.assertIn("200 OK", header)
            self.assertTrue(json.loads(body)["stopped"])

            header, body = await http_request(port, "POST /shutdown HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
            self.assertIn("200 OK", header)
            self.assertTrue(json.loads(body)["stopping"])
        finally:
            await server.stop()

    async def test_post_control_endpoints_reject_non_loopback_origin(self) -> None:
        server = await self._make_server(port=0, enable_ws_broadcast=False)
        try:
            port = server._http_server.sockets[0].getsockname()[1]

            header, body = await http_request(
                port,
                "POST /replay/stop HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "Origin: https://example.invalid\r\n"
                "Connection: close\r\n\r\n",
            )
            self.assertIn("403 Forbidden", header)
            self.assertEqual(json.loads(body)["error"], "origin_not_allowed")

            header, body = await http_request(
                port,
                "POST /replay/stop HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "Origin: http://127.0.0.1:18913\r\n"
                "Connection: close\r\n\r\n",
            )
            self.assertIn("200 OK", header)
            self.assertTrue(json.loads(body)["stopped"])
        finally:
            await server.stop()

    async def test_broadcast_ws_rejects_non_loopback_origin(self) -> None:
        server = await self._make_server(port=0, ws_port=0, enable_ws_broadcast=True)
        try:
            ws_port = server._ws_server.sockets[0].getsockname()[1]

            async with websockets.connect(
                f"ws://127.0.0.1:{ws_port}",
                origin="http://127.0.0.1:18913",
            ) as websocket:
                payload = json.loads(await websocket.recv())
                self.assertEqual(payload["message_type"], "bridge_server_status")

            with self.assertRaises((websockets.InvalidStatus, websockets.InvalidHandshake)):
                async with websockets.connect(
                    f"ws://127.0.0.1:{ws_port}",
                    origin="https://example.invalid",
                ):
                    self.fail("websocket handshake should have been rejected")
        finally:
            await server.stop()

    async def test_non_loopback_hosts_are_rejected(self) -> None:
        with self.assertRaises(ValueError):
            await self._make_server(host="0.0.0.0", enable_ws_broadcast=False)

        with self.assertRaises(ValueError):
            await self._make_server(ws_host="0.0.0.0", enable_ws_broadcast=True)


if __name__ == "__main__":
    unittest.main()
