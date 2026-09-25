from __future__ import annotations

import asyncio
import json
import tempfile
import unittest
from pathlib import Path

from bridge_client import build_chat_event
from event_decoder import decode_canonical_event
from event_dispatcher import AsyncEventDispatcher, PanelWsSink
from event_models import ConnectionState, SessionStatus
from metrics_registry import MetricsRegistry


class _FakePanelConnection:
    def __init__(self) -> None:
        self.sent: list[str] = []
        self.closed = False

    async def send(self, payload: str) -> None:
        self.sent.append(payload)

    async def close(self) -> None:
        self.closed = True


async def _connect_to_fake(_url: str) -> _FakePanelConnection:
    return _FakePanelConnection()


async def _connect_refused(_url: str) -> _FakePanelConnection:
    raise ConnectionRefusedError("no hay panel escuchando")


class PanelWsSinkAttachmentTests(unittest.IsolatedAsyncioTestCase):
    """`is_attached` es lo que permite silenciar las alertas sin panel."""

    async def test_assumes_attached_before_any_failure(self) -> None:
        """En arranque no hay evidencia de ausencia: no silenciar por defecto."""
        sink = PanelWsSink("ws://127.0.0.1:8765", _connect_refused)

        self.assertTrue(sink.is_attached)

    async def test_detaches_when_send_fails(self) -> None:
        """Un envio fallido prueba que el panel no esta."""
        sink = PanelWsSink("ws://127.0.0.1:8765", _connect_refused)

        with self.assertRaises(ConnectionRefusedError):
            await sink.send_json({"message_type": "canonical_event"})

        self.assertFalse(sink.is_attached)

    async def test_reattaches_after_a_successful_send(self) -> None:
        """Si el panel vuelve, las alertas vuelven a tener destinatario."""
        sink = PanelWsSink("ws://127.0.0.1:8765", _connect_to_fake)

        await sink.send_json({"message_type": "canonical_event"})

        self.assertTrue(sink.is_attached)


class PanelWsSinkCooldownTests(unittest.IsolatedAsyncioTestCase):
    """Un rechazo por cooldown NO es un fallo nuevo.

    Antes de la correccion, cada envio rechazado por el cooldown refrescaba el
    reloj del backoff y sumaba un fallo mas. Con trafico constante (latido cada
    30s + eventos del vivo) la ventana nunca llegaba a expirar: el sink quedaba
    desconectado para siempre y las alertas sonoras se quedaban silenciadas,
    aunque el panel hubiera vuelto.
    """

    async def test_cooldown_rejection_does_not_extend_the_backoff(self) -> None:
        attempts: list[str] = []

        async def factory(url: str) -> _FakePanelConnection:
            attempts.append(url)
            if len(attempts) == 1:
                raise ConnectionRefusedError("el panel estaba abajo")
            return _FakePanelConnection()

        sink = PanelWsSink("ws://127.0.0.1:8765", factory)

        with self.assertRaises(ConnectionRefusedError):
            await sink.send_json({"message_type": "session_status"})
        self.assertEqual(len(attempts), 1)
        self.assertFalse(sink.is_attached)

        # Trafico durante el cooldown: no reconecta todavia, pero tampoco
        # deberia sumar fallos ni correr el reloj de la espera.
        for _ in range(3):
            with self.assertRaises(RuntimeError):
                await sink.send_json({"message_type": "session_status"})
        self.assertEqual(len(attempts), 1)
        self.assertEqual(
            sink._consecutive_failures,
            1,
            "los rechazos por cooldown no deben contar como fallos",
        )

        # Cumplido el cooldown, el siguiente envio reconecta aunque haya
        # seguido habiendo trafico en el medio.
        sink._last_failure_monotonic -= 10_000.0
        await sink.send_json({"message_type": "session_status"})
        self.assertEqual(len(attempts), 2)
        self.assertTrue(sink.is_attached)


class DispatcherStatusTests(unittest.IsolatedAsyncioTestCase):
    """El status viaja por el canal directo con todo lo que el panel necesita."""

    async def test_emit_status_delivers_the_diagnostic_payload(self) -> None:
        connection = _FakePanelConnection()

        async def factory(_url: str) -> _FakePanelConnection:
            return connection

        dispatcher = AsyncEventDispatcher(
            metrics=MetricsRegistry(),
            queue_size=4,
            batch_size=1,
            overflow_policy="drop_oldest",
            panel_ws_sink=PanelWsSink("ws://127.0.0.1:8765", factory),
        )

        await dispatcher.emit_status(
            SessionStatus(
                target_user="alice",
                connection_state=ConnectionState.RECONNECTING,
                message="Rotando de API key",
                timestamp_ms=1710000001000,
                phase="waiting",
                severity="warn",
                alert_code="API_KEY_ROTATION_REQUESTED",
                alert_action="rotate_key",
                retry_in_sec=30.0,
                daily_budget_total=50,
                daily_budget_remaining=12,
                daily_budget_manual_reserve=10,
                daily_budget_remaining_auto=2,
            )
        )
        payload = json.loads(connection.sent[0])
        self.assertEqual(payload["message_type"], "session_status")
        self.assertEqual(payload["connection_state"], "reconnecting")
        self.assertEqual(payload["phase"], "waiting")
        self.assertEqual(payload["alert_code"], "API_KEY_ROTATION_REQUESTED")
        self.assertEqual(payload["alert_action"], "rotate_key")
        self.assertEqual(payload["daily_budget_total"], 50)
        self.assertEqual(payload["daily_budget_remaining"], 12)

        # Sin tope ni alerta los campos no viajan: el panel no debe ver ceros
        # que parezcan "hoy no queda nada" ni una accion inventada.
        await dispatcher.emit_status(SessionStatus(target_user="alice"))
        payload = json.loads(connection.sent[1])
        self.assertNotIn("daily_budget_total", payload)
        self.assertNotIn("alert_action", payload)
        self.assertNotIn("alert_code", payload)


class DispatcherTests(unittest.IsolatedAsyncioTestCase):
    async def test_dispatcher_writes_jsonl_and_inbox(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            jsonl_path = temp_path / "bridge.jsonl"
            inbox_path = temp_path / "inbox"
            metrics = MetricsRegistry()
            dispatcher = AsyncEventDispatcher(
                metrics=metrics,
                queue_size=16,
                batch_size=4,
                overflow_policy="drop_oldest",
                jsonl_path=str(jsonl_path),
                inbox_dir=str(inbox_path),
                session_name="dispatch-test",
            )
            await dispatcher.start()
            try:
                for index in range(3):
                    accepted = await dispatcher.publish(
                        decode_canonical_event(
                            build_chat_event(
                                user_id=f"user-{index}",
                                username=f"user{index}",
                                display_name=f"User {index}",
                                text=f"message {index}",
                                event_id=f"evt-dispatch-{index}",
                                room_id="room-dispatch",
                                timestamp_ms=1710000001000 + index,
                            )
                        )
                    )
                    self.assertTrue(accepted)

                for _ in range(50):
                    if metrics.snapshot().counters.get("events_dispatched_total", 0) >= 3:
                        break
                    await asyncio.sleep(0.02)
            finally:
                await dispatcher.stop()

            self.assertTrue(jsonl_path.exists())
            self.assertEqual(len(jsonl_path.read_text(encoding="utf-8").strip().splitlines()), 3)
            self.assertEqual(len(list(inbox_path.glob("*.json"))), 3)
            self.assertEqual(metrics.snapshot().counters.get("events_dispatched_total"), 3)


if __name__ == "__main__":
    unittest.main()
