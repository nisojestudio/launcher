from __future__ import annotations

import asyncio
import unittest
from unittest import mock

from bridge_config import BridgeConfig
from bridge_client import build_chat_event
from connection_manager import ConnectionManager, compute_retry_delay
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


if __name__ == "__main__":
    unittest.main()
