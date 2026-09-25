from __future__ import annotations

import asyncio
import time
import unittest
from unittest import mock

from bridge_config import BridgeConfig
from bridge_client import build_chat_event
from connection_manager import (
    ConnectionManager,
    ReconnectRateLimiter,
    compute_retry_delay,
)
from daily_budget import KIND_AUTO, KIND_MANUAL, DailyConnectionBudget
from error_catalog import action_for, classify_close_code, classify_error_text
from event_decoder import decode_canonical_event
from metrics_registry import MetricsRegistry
from structured_logging import configure_logger
from tiktools_connection import TikToolsConnectionError


async def accepted_event(_event) -> bool:
    return True


async def ignored_status(_status) -> None:
    return None


class RecordingSoundAlerts:
    """Sustituye a SoundAlerts para observar que suena y que no."""

    played: list[str] = []

    def __init__(self, **_kwargs) -> None:
        pass

    def should_alert(self, _new_state: str) -> bool:
        return True

    async def play_connected(self) -> None:
        type(self).played.append("connected")

    async def play_disconnected(self) -> None:
        type(self).played.append("disconnected")

    async def play_reconnecting(self) -> None:
        type(self).played.append("reconnecting")


class FakeConnection:
    attempts = 0
    fail_first = False
    user_not_found = False
    hang_until_closed = False

    def __init__(
        self,
        *,
        event_callback,
        status_callback,
        target_user: str,
        room_id: str = "",
        **_kwargs,
    ) -> None:
        self._event_callback = event_callback
        self._status_callback = status_callback
        self.room_id = room_id or "room-fake"
        self._target_user = target_user
        self._wait_task: asyncio.Task[None] | None = None
        self._closed_event = asyncio.Event()

    async def open(self) -> None:
        type(self).attempts += 1
        if type(self).user_not_found:
            raise TikToolsConnectionError("USER_NOT_FOUND", "No se encontro ese usuario en TikTok.")
        if type(self).fail_first and type(self).attempts == 1:
            raise TikToolsConnectionError("NETWORK_ERROR", "Fallo temporal de red.")

        if type(self).hang_until_closed:
            async def _wait() -> None:
                await self._closed_event.wait()

            self._wait_task = asyncio.create_task(_wait())
            return

        async def _emit() -> None:
            await self._event_callback(
                decode_canonical_event(
                    build_chat_event(
                        user_id="user-01",
                        username=self._target_user,
                        display_name=self._target_user,
                        text="hola",
                        event_id=f"evt-fake-{type(self).attempts}",
                        room_id=self.room_id,
                        timestamp_ms=1710000001000,
                    )
                )
            )

        self._wait_task = asyncio.create_task(_emit())

    async def wait_closed(self) -> None:
        if self._wait_task is not None:
            await self._wait_task

    async def close(self) -> None:
        self._closed_event.set()
        return None


def bridge_config_with_api_key() -> BridgeConfig:
    """Config del provider tik.tools con credencial: el manager exige una key.

    Sin key, `ConnectionManager.run` corta en INVALID_API_KEY antes de abrir la
    sesion, que es justamente lo que estos tests no quieren medir.
    """
    config = BridgeConfig()
    config.connection.api_key = "test-key"
    return config


class ConnectionManagerTests(unittest.IsolatedAsyncioTestCase):
    async def test_reconnects_after_transient_failure(self) -> None:
        FakeConnection.attempts = 0
        FakeConnection.fail_first = True
        FakeConnection.user_not_found = False
        FakeConnection.hang_until_closed = False

        config = bridge_config_with_api_key()
        config.retry_policy.enabled = True
        config.retry_policy.max_attempts = 3
        metrics = MetricsRegistry()
        received_ids: list[str] = []
        statuses: list[str] = []
        messages: list[str] = []

        async def event_callback(event) -> bool:
            received_ids.append(event.metadata.event_id)
            return True

        async def status_callback(status) -> None:
            statuses.append(status.connection_state.value)
            messages.append(status.message)

        async def fast_sleep(_seconds: float) -> None:
            return None

        manager = ConnectionManager(
            config=config,
            logger=configure_logger(name="livepanel.bridge.test.connection.retry", log_path="tools/bridge_py/logs/test_connection_retry.jsonl"),
            metrics=metrics,
            event_callback=event_callback,
            status_callback=status_callback,
        )

        with mock.patch("connection_manager.TikToolsConnection", FakeConnection), mock.patch(
            "connection_manager.asyncio.sleep",
            side_effect=fast_sleep,
        ):
            exit_code = await manager.run(target_user="alice", max_events=1, max_seconds=0)

        self.assertEqual(exit_code, 0)
        self.assertEqual(FakeConnection.attempts, 2)
        self.assertEqual(len(received_ids), 1)
        self.assertIn("reconnecting", statuses)
        self.assertIn("max_events reached (1)", messages)
        self.assertGreaterEqual(metrics.snapshot().counters.get("reconnect_total", 0), 1)

    async def test_stops_without_retry_on_user_not_found(self) -> None:
        FakeConnection.attempts = 0
        FakeConnection.fail_first = False
        FakeConnection.user_not_found = True
        FakeConnection.hang_until_closed = False

        metrics = MetricsRegistry()

        async def event_callback(_event) -> bool:
            return True

        messages: list[str] = []

        async def status_callback(status) -> None:
            messages.append(status.message)

        manager = ConnectionManager(
            config=bridge_config_with_api_key(),
            logger=configure_logger(name="livepanel.bridge.test.connection.not_found", log_path="tools/bridge_py/logs/test_connection_not_found.jsonl"),
            metrics=metrics,
            event_callback=event_callback,
            status_callback=status_callback,
        )

        with mock.patch("connection_manager.TikToolsConnection", FakeConnection):
            exit_code = await manager.run(target_user="missing-user", max_events=1, max_seconds=0)

        self.assertEqual(exit_code, 1)
        self.assertEqual(FakeConnection.attempts, 1)
        self.assertEqual(metrics.snapshot().counters.get("reconnect_total", 0), 0)

    async def test_max_seconds_stops_even_without_events(self) -> None:
        FakeConnection.attempts = 0
        FakeConnection.fail_first = False
        FakeConnection.user_not_found = False
        FakeConnection.hang_until_closed = True

        metrics = MetricsRegistry()

        async def event_callback(_event) -> bool:
            return True

        messages: list[str] = []

        async def status_callback(status) -> None:
            messages.append(status.message)

        manager = ConnectionManager(
            config=bridge_config_with_api_key(),
            logger=configure_logger(name="livepanel.bridge.test.connection.max_seconds", log_path="tools/bridge_py/logs/test_connection_max_seconds.jsonl"),
            metrics=metrics,
            event_callback=event_callback,
            status_callback=status_callback,
        )

        with mock.patch("connection_manager.TikToolsConnection", FakeConnection):
            exit_code = await manager.run(target_user="alice", max_events=0, max_seconds=1)

        self.assertEqual(exit_code, 0)
        self.assertEqual(FakeConnection.attempts, 1)
        self.assertIn("max_seconds reached (1)", messages)

    async def test_not_live_keeps_waiting_instead_of_failing_permanently(self) -> None:
        """4404 (cuenta no en vivo) debe esperar y reintentar, no abandonar."""

        class NotLiveConnection(FakeConnection):
            async def open(self) -> None:
                type(self).attempts += 1
                raise TikToolsConnectionError("NOT_LIVE", "La cuenta no esta en vivo todavia.")

        NotLiveConnection.attempts = 0
        config = bridge_config_with_api_key()
        config.retry_policy.max_attempts = 2
        config.retry_policy.not_live_delay_sec = 1
        config.retry_policy.waiting_for_live_max_minutes = 0  # sin limite

        messages: list[str] = []
        phases: list[str] = []

        async def status_callback(status) -> None:
            messages.append(status.message)
            phases.append(status.phase)

        manager = ConnectionManager(
            config=config,
            logger=configure_logger(
                name="livepanel.bridge.test.connection.not_live",
                log_path="tools/bridge_py/logs/test_connection_not_live.jsonl",
            ),
            metrics=MetricsRegistry(),
            event_callback=accepted_event,
            status_callback=status_callback,
        )

        loop = asyncio.get_running_loop()
        with mock.patch("connection_manager.TikToolsConnection", NotLiveConnection):
            run_task = asyncio.create_task(manager.run(target_user="alice", max_events=1))
            deadline = loop.time() + 12
            while NotLiveConnection.attempts < 3 and loop.time() < deadline:
                await asyncio.sleep(0.05)
            manager.stop()
            await asyncio.wait_for(run_task, timeout=10)

        # Supero max_attempts (2) porque esperar el vivo no consume ese
        # presupuesto, y el panel recibe la fase "waiting".
        self.assertGreaterEqual(NotLiveConnection.attempts, 3)
        self.assertIn("waiting", phases)
        self.assertTrue(any("empiece el vivo" in message for message in messages))

    async def test_quota_exhausted_is_reported_as_key_rotation(self) -> None:
        config = bridge_config_with_api_key()
        delay = compute_retry_delay(config, "API_SESSION_ENDED", 0)

        # La cuota agotada es reintentable (con otra key) y nunca cae en NOT_LIVE.
        self.assertIsNotNone(delay)
        self.assertEqual(action_for("API_SESSION_ENDED"), "rotate_key")
        self.assertEqual(action_for("NOT_LIVE"), "wait_for_live")

    async def test_daily_demo_limit_close_code_is_quota_not_not_live(self) -> None:
        self.assertEqual(classify_close_code(4555, "Daily Demo Limit Reached"), "API_SESSION_ENDED")
        self.assertEqual(classify_close_code(4429, "Daily Demo Limit Reached"), "API_SESSION_ENDED")
        self.assertEqual(classify_close_code(4404, "Creator is not currently live"), "NOT_LIVE")
        self.assertEqual(classify_close_code(4556, "Relay connection error"), "RELAY_ERROR")
        self.assertEqual(classify_close_code(1012, "service restart"), "SERVER_RESTART")

    async def test_evaluation_period_close_is_key_rotation_not_unknown(self) -> None:
        """4401 'Evaluation period ended' = plan vencido: hay que rotar de key.

        Regresión real (2026-09-24): el 4401 caía como UNKNOWN, no se rotaba
        la credencial y el runner agotaba max_attempts con la misma key muerta.
        """
        reason = (
            "Evaluation period ended. Upgrade at https://tik.tools/pricing "
            "to continue using TikTools."
        )
        # Texto completo tal como lo arma websockets al reportar el cierre.
        raw_error = f"received 4401 (private use) {reason}; then sent 4401 (private use) {reason}"

        self.assertEqual(classify_close_code(4401, reason), "API_SESSION_ENDED")
        self.assertEqual(classify_error_text(raw_error), "API_SESSION_ENDED")
        # Sin rotación no hay forma de llegar a la key sana del pool.
        self.assertEqual(action_for("API_SESSION_ENDED"), "rotate_key")

    async def test_late_room_confirmation_is_declared_as_connected(self) -> None:
        """Si la sala se confirma despues del timeout, el panel debe ver 'connected'."""

        class LateHandshakeConnection(FakeConnection):
            def __init__(self, **kwargs) -> None:
                super().__init__(**kwargs)
                self.room_id = "room-late"
                self.handshake_complete = False

            async def open(self) -> None:
                type(self).attempts += 1
                # open() vuelve sin handshake: la sala todavia no esta confirmada.
                return

            async def wait_closed(self) -> None:
                # La sesion sigue abierta; la sala se confirma unos instantes
                # despues (como el relay de tik.tools).
                async def confirm_room_later() -> None:
                    await asyncio.sleep(0.3)
                    self.handshake_complete = True

                flipper = asyncio.create_task(confirm_room_later())
                try:
                    await self._closed_event.wait()
                finally:
                    flipper.cancel()

        LateHandshakeConnection.attempts = 0
        config = bridge_config_with_api_key()
        statuses: list[tuple[str, str]] = []

        async def status_callback(status) -> None:
            statuses.append((status.connection_state.value, status.phase))

        manager = ConnectionManager(
            config=config,
            logger=configure_logger(
                name="livepanel.bridge.test.connection.late_handshake",
                log_path="tools/bridge_py/logs/test_connection_late_handshake.jsonl",
            ),
            metrics=MetricsRegistry(),
            event_callback=accepted_event,
            status_callback=status_callback,
        )

        loop = asyncio.get_running_loop()
        with mock.patch("connection_manager.TikToolsConnection", LateHandshakeConnection):
            run_task = asyncio.create_task(manager.run(target_user="alice", max_events=1))
            deadline = loop.time() + 10
            while not any(state == "connected" for state, _ in statuses) and loop.time() < deadline:
                await asyncio.sleep(0.05)
            manager.stop()
            await asyncio.wait_for(run_task, timeout=10)

        self.assertIn("connecting", [state for state, _ in statuses])
        self.assertIn(("connected", "connected"), statuses)

    async def test_quota_exhausted_rotates_to_the_next_api_key(self) -> None:
        """4429/4555 deben rotar de credencial en vez de morir."""

        used_keys: list[str] = []

        class QuotaConnection(FakeConnection):
            def __init__(self, **kwargs) -> None:
                super().__init__(**kwargs)
                used_keys.append(str(kwargs.get("api_key") or ""))
                self.handshake_complete = False
                self.room_id = "room-quota"

            async def open(self) -> None:
                type(self).attempts += 1
                if type(self).attempts <= 2:
                    raise TikToolsConnectionError(
                        "API_SESSION_ENDED",
                        "tik.tools corto la sesion por limite del plan.",
                    )
                return

            async def wait_closed(self) -> None:
                await self._closed_event.wait()

        QuotaConnection.attempts = 0
        config = bridge_config_with_api_key()
        config.connection.api_keys = ["key-uno", "key-dos", "key-tres"]
        config.connection.api_key = ""
        config.connection.api_key_labels = ["cuenta-1", "cuenta-2", "cuenta-3"]
        config.retry_policy.max_attempts = 1  # la rotacion no debe consumirlo

        statuses: list[tuple[str, str]] = []
        key_labels: list[str] = []

        async def status_callback(status) -> None:
            statuses.append((status.connection_state.value, status.phase))
            if status.key_label:
                key_labels.append(status.key_label)

        manager = ConnectionManager(
            config=config,
            logger=configure_logger(
                name="livepanel.bridge.test.connection.rotate",
                log_path="tools/bridge_py/logs/test_connection_rotate.jsonl",
            ),
            metrics=MetricsRegistry(),
            event_callback=accepted_event,
            status_callback=status_callback,
        )

        loop = asyncio.get_running_loop()
        with mock.patch("connection_manager.TikToolsConnection", QuotaConnection):
            run_task = asyncio.create_task(manager.run(target_user="alice", max_events=1))
            deadline = loop.time() + 15
            while QuotaConnection.attempts < 3 and loop.time() < deadline:
                await asyncio.sleep(0.05)
            manager.stop()
            await asyncio.wait_for(run_task, timeout=10)

        # Tres intentos: dos con cuota agotada y el tercero con otra key.
        self.assertGreaterEqual(QuotaConnection.attempts, 3)
        self.assertEqual(used_keys[:3], ["key-uno", "key-dos", "key-tres"])
        self.assertTrue(any("cuenta-" in label for label in key_labels))

    async def test_rotate_request_without_pool_stops_immediately(self) -> None:
        """Con una sola key no hay a quien rotar: no quema max_attempts ni promete rotacion."""

        class SingleKeyQuotaConnection(FakeConnection):
            async def open(self) -> None:
                type(self).attempts += 1
                raise TikToolsConnectionError(
                    "API_SESSION_ENDED",
                    "tik.tools cerro la sesion: se agoto la cuota o vencio el plan de la API key.",
                )

        SingleKeyQuotaConnection.attempts = 0
        config = bridge_config_with_api_key()  # una unica credencial
        config.retry_policy.max_attempts = 5

        messages: list[str] = []
        actions: list[str] = []
        metrics = MetricsRegistry()

        async def status_callback(status) -> None:
            messages.append(status.message)
            if status.alert_action:
                actions.append(status.alert_action)

        manager = ConnectionManager(
            config=config,
            logger=configure_logger(
                name="livepanel.bridge.test.connection.single_key",
                log_path="tools/bridge_py/logs/test_connection_single_key.jsonl",
            ),
            metrics=metrics,
            event_callback=accepted_event,
            status_callback=status_callback,
        )

        with mock.patch("connection_manager.TikToolsConnection", SingleKeyQuotaConnection):
            exit_code = await manager.run(target_user="alice", max_events=1, max_seconds=5)

        self.assertEqual(exit_code, 1)
        # Reintentar con la misma credencial muerta no cambia el resultado.
        self.assertEqual(SingleKeyQuotaConnection.attempts, 1)
        self.assertEqual(metrics.snapshot().counters.get("reconnect_total", 0), 0)
        # El codigo sigue pidiendo rotacion y el usuario ve la accion real.
        self.assertIn("rotate_key", actions)
        self.assertTrue(
            any("Cuentas y API keys" in message for message in messages),
            f"mensaje final esperado con la accion del usuario, obtuve: {messages!r}",
        )
        self.assertFalse(
            any("Se rota automaticamente" in message for message in messages),
            f"no debe prometer una rotacion que no puede pasar: {messages!r}",
        )
        self.assertFalse(
            any("Se reintenta" in message for message in messages),
            f"no debe anunciar reintentos cuando ya no va a haber ninguno: {messages!r}",
        )

    async def test_selects_direct_tiktoklive_provider(self) -> None:
        FakeConnection.attempts = 0
        FakeConnection.fail_first = False
        FakeConnection.user_not_found = False
        FakeConnection.hang_until_closed = False
        config = bridge_config_with_api_key()
        config.connection_mode = "direct"

        async def event_callback(_event) -> bool:
            return True

        async def status_callback(_status) -> None:
            return None

        manager = ConnectionManager(
            config=config,
            logger=configure_logger(name="livepanel.bridge.test.connection.direct", log_path="tools/bridge_py/logs/test_connection_direct.jsonl"),
            metrics=MetricsRegistry(),
            event_callback=event_callback,
            status_callback=status_callback,
        )

        with mock.patch("connection_manager.TikTokConnection", FakeConnection):
            exit_code = await manager.run(target_user="alice", max_events=1)

        self.assertEqual(exit_code, 0)
        self.assertEqual(FakeConnection.attempts, 1)


    async def test_sound_alerts_are_suppressed_when_panel_is_not_connected(self) -> None:
        """El bug reportado: el bridge sobrevive al panel y seguia pitando."""
        metrics = MetricsRegistry()

        await self._run_session_recording_sounds(
            panel_attached=lambda: False,
            metrics=metrics,
        )

        self.assertEqual(RecordingSoundAlerts.played, [])
        self.assertGreaterEqual(
            metrics.snapshot().counters.get("sound_alerts_suppressed_total", 0), 1
        )

    async def test_sound_alerts_play_when_panel_is_connected(self) -> None:
        """Control positivo: con panel presente las alertas siguen sonando."""
        metrics = MetricsRegistry()

        await self._run_session_recording_sounds(
            panel_attached=lambda: True,
            metrics=metrics,
        )

        self.assertIn("reconnecting", RecordingSoundAlerts.played)
        self.assertEqual(metrics.snapshot().counters.get("sound_alerts_suppressed_total", 0), 0)

    async def test_sound_alerts_play_without_predicate_for_legacy_callers(self) -> None:
        """Sin predicado se conserva el comportamiento anterior."""
        await self._run_session_recording_sounds(panel_attached=None, metrics=MetricsRegistry())

        self.assertIn("reconnecting", RecordingSoundAlerts.played)

    async def test_sound_alerts_play_when_predicate_raises(self) -> None:
        """Ante la duda no se silencia: un predicado roto no debe comer la alerta."""

        def broken_predicate() -> bool:
            raise RuntimeError("panel ws sink unavailable")

        await self._run_session_recording_sounds(
            panel_attached=broken_predicate,
            metrics=MetricsRegistry(),
        )

        self.assertIn("reconnecting", RecordingSoundAlerts.played)

    async def _run_session_recording_sounds(self, *, panel_attached, metrics: MetricsRegistry) -> None:
        """Sesion con un fallo transitorio: produce la transicion a 'reconnecting'."""
        FakeConnection.attempts = 0
        FakeConnection.fail_first = True
        FakeConnection.user_not_found = False
        FakeConnection.hang_until_closed = False

        config = bridge_config_with_api_key()
        config.retry_policy.max_attempts = 3

        async def fast_sleep(_seconds: float) -> None:
            return None

        RecordingSoundAlerts.played = []
        # El manager debe construirse DENTRO del patch: SoundAlerts se instancia
        # en __init__, asi que parchearlo despues no tendria efecto.
        with mock.patch("connection_manager.TikToolsConnection", FakeConnection), mock.patch(
            "connection_manager.SoundAlerts", RecordingSoundAlerts
        ), mock.patch("connection_manager.asyncio.sleep", side_effect=fast_sleep):
            manager = ConnectionManager(
                config=config,
                logger=configure_logger(
                    name="livepanel.bridge.test.connection.sound",
                    log_path="tools/bridge_py/logs/test_connection_sound.jsonl",
                ),
                metrics=metrics,
                event_callback=accepted_event,
                status_callback=ignored_status,
                panel_attached=panel_attached,
            )
            await manager.run(target_user="alice", max_events=1, max_seconds=0)


class StabilityGuardTests(unittest.IsolatedAsyncioTestCase):
    """Guardias de la fase de estabilidad: presupuesto, limite horario y silencio."""

    # -- presupuesto diario ------------------------------------------------

    async def test_daily_budget_exhaustion_stops_with_a_clear_message(self) -> None:
        """Con el presupuesto agotado no se abre sesion: se explica y se para."""
        FakeConnection.attempts = 0
        FakeConnection.fail_first = True
        FakeConnection.user_not_found = False
        FakeConnection.hang_until_closed = False

        config = bridge_config_with_api_key()
        config.retry_policy.max_attempts = 0  # sin tope de intentos: manda el presupuesto
        config.retry_policy.daily_connection_budget = 1
        config.retry_policy.daily_manual_reserve = 1

        statuses = []
        messages: list[str] = []

        async def status_callback(status) -> None:
            statuses.append(status)
            messages.append(status.message)

        async def fast_sleep(_seconds: float) -> None:
            return None

        manager = ConnectionManager(
            config=config,
            logger=configure_logger(
                name="livepanel.bridge.test.connection.budget",
                log_path="tools/bridge_py/logs/test_connection_budget.jsonl",
            ),
            metrics=MetricsRegistry(),
            event_callback=accepted_event,
            status_callback=status_callback,
        )

        with mock.patch("connection_manager.TikToolsConnection", FakeConnection), mock.patch(
            "connection_manager.asyncio.sleep",
            side_effect=fast_sleep,
        ):
            exit_code = await manager.run(target_user="alice", max_events=1, max_seconds=0)

        self.assertEqual(exit_code, 1)
        # Solo llego a abrir la primera: la segunda apertura ya no cabia.
        self.assertEqual(FakeConnection.attempts, 1)
        self.assertTrue(
            any(status.alert_code == "DAILY_BUDGET_EXHAUSTED" for status in statuses),
            "el presupuesto agotado debe llegar al panel como alerta propia",
        )
        self.assertIn("presupuesto diario", messages[-1])
        # El presupuesto viaja en todos los status: el panel muestra cuanto
        # queda hoy sin tener que preguntarle al bridge.
        self.assertTrue(
            any(status.daily_budget_total == 1 for status in statuses),
            "el tope diario debe viajar en el status",
        )
        self.assertTrue(
            any(status.daily_budget_remaining == 0 for status in statuses),
            "al agotarse el status debe decir que quedan 0 conexiones",
        )

    def test_status_payload_carries_the_daily_budget(self) -> None:
        """El status lleva el presupuesto solo cuando hay tope configurado."""
        from event_models import SessionStatus

        # Sin tope no se envia: el panel no debe ver ceros que parezcan agotado.
        payload = SessionStatus(target_user="alice").to_panel_payload()
        self.assertNotIn("daily_budget_total", payload)
        self.assertNotIn("daily_budget_remaining", payload)

        payload = SessionStatus(
            target_user="alice",
            daily_budget_total=50,
            daily_budget_remaining=47,
            daily_budget_manual_reserve=10,
            daily_budget_remaining_auto=37,
        ).to_panel_payload()
        self.assertEqual(payload["daily_budget_total"], 50)
        self.assertEqual(payload["daily_budget_remaining"], 47)
        self.assertEqual(payload["daily_budget_manual_reserve"], 10)
        self.assertEqual(payload["daily_budget_remaining_auto"], 37)

        # Un contador corrupto nunca manda un negativo al panel.
        payload = SessionStatus(
            daily_budget_total=50,
            daily_budget_remaining=-3,
            daily_budget_manual_reserve=-1,
        ).to_panel_payload()
        self.assertEqual(payload["daily_budget_remaining"], 0)
        self.assertEqual(payload["daily_budget_manual_reserve"], 0)

    def test_status_payload_carries_the_alert_action(self) -> None:
        """La accion sugerida viaja en el status para mostrarla en la alerta."""
        from event_models import SessionStatus

        # Sin accion no se envia: el panel no debe inventar un "que hacer".
        payload = SessionStatus(target_user="alice", alert_code="RATE_LIMITED").to_panel_payload()
        self.assertNotIn("alert_action", payload)
        self.assertIn("alert_code", payload)

        payload = SessionStatus(
            target_user="alice",
            alert_code="API_KEY_ROTATION_REQUESTED",
            alert_action="rotate_key",
        ).to_panel_payload()
        self.assertEqual(payload["alert_action"], "rotate_key")

    async def test_reconnects_are_charged_to_the_auto_bag(self) -> None:
        """Intento 0 = manual, el resto = reconexiones automaticas."""
        FakeConnection.attempts = 0
        FakeConnection.fail_first = True
        FakeConnection.user_not_found = False
        FakeConnection.hang_until_closed = False

        config = bridge_config_with_api_key()
        config.retry_policy.max_attempts = 3

        async def fast_sleep(_seconds: float) -> None:
            return None

        manager = ConnectionManager(
            config=config,
            logger=configure_logger(
                name="livepanel.bridge.test.connection.budget_kinds",
                log_path="tools/bridge_py/logs/test_connection_budget_kinds.jsonl",
            ),
            metrics=MetricsRegistry(),
            event_callback=accepted_event,
            status_callback=ignored_status,
        )

        with mock.patch("connection_manager.TikToolsConnection", FakeConnection), mock.patch(
            "connection_manager.asyncio.sleep",
            side_effect=fast_sleep,
        ):
            exit_code = await manager.run(target_user="alice", max_events=1, max_seconds=0)

        self.assertEqual(exit_code, 0)
        self.assertEqual(FakeConnection.attempts, 2)
        budget = manager.budget
        self.assertIsNotNone(budget)
        self.assertEqual(budget.used_manual, 1)
        self.assertEqual(budget.used_auto, 1)
        self.assertEqual(budget.used_total, 2)

    # -- limite de reconexiones por hora -----------------------------------

    async def test_hourly_reconnect_limit_waits_instead_of_stopping(self) -> None:
        """Sin hueco horario se espera: el bridge no se rinde y muere."""

        class AlwaysFailingConnection(FakeConnection):
            async def open(self) -> None:
                type(self).attempts += 1
                raise TikToolsConnectionError("NETWORK_ERROR", "Fallo temporal de red.")

        AlwaysFailingConnection.attempts = 0

        config = bridge_config_with_api_key()
        config.retry_policy.max_attempts = 2
        config.retry_policy.max_reconnect_per_hour = 1
        config.retry_policy.base_delay_sec = 1.0
        config.retry_policy.jitter_sec = 0.0

        statuses = []

        async def status_callback(status) -> None:
            statuses.append(status)

        manager = ConnectionManager(
            config=config,
            logger=configure_logger(
                name="livepanel.bridge.test.connection.hourly",
                log_path="tools/bridge_py/logs/test_connection_hourly.jsonl",
            ),
            metrics=MetricsRegistry(),
            event_callback=accepted_event,
            status_callback=status_callback,
        )

        # La espera del hueco dura mas que el backoff normal: si el manager la
        # ignorara, el reintento saldria antes de liberarse el slot.
        with mock.patch("connection_manager.TikToolsConnection", AlwaysFailingConnection), mock.patch.object(
            ReconnectRateLimiter, "seconds_until_slot", return_value=3.0
        ):
            exit_code = await manager.run(target_user="alice", max_events=1, max_seconds=0)

        self.assertEqual(exit_code, 1)
        # Antes del cambio esto cortaba en el primer limite horario (1 apertura).
        self.assertEqual(AlwaysFailingConnection.attempts, 3)
        messages = [status.message for status in statuses]
        self.assertTrue(any("por hora" in message for message in messages), messages)
        # La franja del panel no puede decir "Conectando" mientras el bridge
        # se queda esperando a que se libre un hueco del limite horario.
        self.assertTrue(
            any(status.phase == "rate_limited" for status in statuses),
            f"fases vistas: {[status.phase for status in statuses]}",
        )
        # Ese mismo status trae el codigo y la accion: sin ellos el panel solo
        # podria decir "esperando" y no que esta pasando ni que hacer.
        limited = next((status for status in statuses if status.phase == "rate_limited"), None)
        self.assertIsNotNone(limited)
        self.assertEqual(limited.alert_code, "NETWORK_ERROR")
        self.assertEqual(limited.alert_action, "wait_provider")
        # El reintento espera el hueco entero, no el backoff corto.
        self.assertTrue(
            any(status.retry_in_sec >= 3.0 for status in statuses),
            "la espera del hueco horario debe llegar al status",
        )
        self.assertNotIn("limite de reconexiones por hora", messages[-1])
        self.assertIn("Se dejo de reintentar", messages[-1])

    def test_seconds_until_slot_waits_for_the_oldest_reconnect(self) -> None:
        limiter = ReconnectRateLimiter(2)
        limiter.record_reconnect("tiktools")
        limiter.record_reconnect("tiktools")

        self.assertFalse(limiter.can_reconnect("tiktools"))
        wait = limiter.seconds_until_slot("tiktools")
        self.assertGreater(wait, 0.0)
        self.assertLessEqual(wait, 3600.0)

        # La reconexion mas vieja sale de la ventana: hay hueco otra vez.
        now = time.monotonic()
        limiter._provider_timestamps["tiktools"] = [now - 3601.0, now]

        self.assertTrue(limiter.can_reconnect("tiktools"))
        self.assertEqual(limiter.seconds_until_slot("tiktools"), 0.0)

    # -- reconexion por silencio (A3) --------------------------------------

    async def test_open_session_without_events_is_reconnected(self) -> None:
        """Sesion abierta sin eventos: se cierra y se reconecta sola."""

        class SilentConnection(FakeConnection):
            async def open(self) -> None:
                type(self).attempts += 1

                async def _wait() -> None:
                    await self._closed_event.wait()

                self._wait_task = asyncio.create_task(_wait())

        SilentConnection.attempts = 0

        config = bridge_config_with_api_key()
        config.connection.silence_reconnect_sec = 0.3
        config.retry_policy.jitter_sec = 0.0
        config.retry_policy.base_delay_sec = 1.0
        config.retry_policy.daily_connection_budget = 50
        config.retry_policy.daily_manual_reserve = 10

        alert_codes: list[str] = []

        async def status_callback(status) -> None:
            if status.alert_code:
                alert_codes.append(status.alert_code)

        manager = ConnectionManager(
            config=config,
            logger=configure_logger(
                name="livepanel.bridge.test.connection.silence",
                log_path="tools/bridge_py/logs/test_connection_silence.jsonl",
            ),
            metrics=MetricsRegistry(),
            event_callback=accepted_event,
            status_callback=status_callback,
        )

        with mock.patch("connection_manager.TikToolsConnection", SilentConnection):
            await manager.run(target_user="alice", max_events=1, max_seconds=3)

        self.assertGreaterEqual(
            SilentConnection.attempts,
            2,
            "una sesion abierta sin eventos debe reconectarse, no quedarse quieta",
        )
        self.assertIn("SILENCE_TIMEOUT", alert_codes)
        # Cada apertura se cobra en el presupuesto: la primera es manual y el
        # resto son reconexiones automaticas.
        budget = manager.budget
        self.assertIsNotNone(budget)
        self.assertEqual(budget.used_manual, 1)
        self.assertEqual(budget.used_auto, SilentConnection.attempts - 1)

    # -- estiramiento de la espera del vivo --------------------------------

    def test_not_live_delay_uses_the_stretch_factor(self) -> None:
        config = bridge_config_with_api_key()
        config.retry_policy.not_live_delay_sec = 20.0

        base = compute_retry_delay(config, "NOT_LIVE", 0)

        self.assertEqual(base, 20.0)
        self.assertEqual(compute_retry_delay(config, "NOT_LIVE", 0, auto_budget_stretch=2.0), 40.0)
        self.assertEqual(compute_retry_delay(config, "NOT_LIVE", 0, auto_budget_stretch=4.0), 80.0)

    def test_stretch_factor_follows_the_auto_bag(self) -> None:
        manager = ConnectionManager(
            config=bridge_config_with_api_key(),
            logger=configure_logger(
                name="livepanel.bridge.test.connection.stretch",
                log_path="tools/bridge_py/logs/test_connection_stretch.jsonl",
            ),
            metrics=MetricsRegistry(),
            event_callback=accepted_event,
            status_callback=ignored_status,
        )

        manager._budget = DailyConnectionBudget(total_per_day=40, manual_reserve=0)
        self.assertEqual(manager._auto_budget_stretch(), 1.0)

        for _ in range(20):
            manager._budget.record(KIND_AUTO)
        self.assertEqual(manager._auto_budget_stretch(), 2.0)

        for _ in range(11):
            manager._budget.record(KIND_AUTO)
        self.assertEqual(manager._auto_budget_stretch(), 4.0)

    def test_manual_reserve_keeps_the_auto_bag_away(self) -> None:
        """La bolsa manual no la tocan las reconexiones (reserva de 10)."""
        budget = DailyConnectionBudget(total_per_day=50, manual_reserve=10)

        for _ in range(40):
            self.assertTrue(budget.can_open(KIND_AUTO))
            budget.record(KIND_AUTO)

        self.assertFalse(budget.can_open(KIND_AUTO))
        self.assertEqual(budget.remaining_total, 10)
        self.assertTrue(budget.can_open(KIND_MANUAL))


if __name__ == "__main__":
    unittest.main()
