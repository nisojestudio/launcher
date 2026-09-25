"""Tests del presupuesto diario de conexiones contra el proveedor."""

from __future__ import annotations

import json
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

from daily_budget import (
    KIND_AUTO,
    KIND_MANUAL,
    REASON_AUTO_EXHAUSTED,
    REASON_TOTAL_EXHAUSTED,
    DailyConnectionBudget,
)


def budget(
    *,
    total: int = 50,
    reserve: int = 10,
    state_path: str | Path = "",
    now=None,
) -> DailyConnectionBudget:
    return DailyConnectionBudget(
        total_per_day=total,
        manual_reserve=reserve,
        state_path=state_path,
        now=now,
    )


class DailyConnectionBudgetTests(unittest.TestCase):
    def test_auto_cannot_consume_the_manual_reserve(self) -> None:
        """Las 40 reconexiones automaticas no se pueden comer los 10 manuales."""
        subject = budget(total=50, reserve=10)

        for _ in range(40):
            self.assertTrue(subject.can_open(KIND_AUTO))
            subject.record(KIND_AUTO)

        self.assertFalse(subject.can_open(KIND_AUTO))
        self.assertEqual(subject.denial_reason(KIND_AUTO), REASON_AUTO_EXHAUSTED)
        # El operador conserva sus 10 conexiones intactas.
        self.assertEqual(subject.used_manual, 0)
        self.assertEqual(subject.remaining_total, 10)

        for _ in range(10):
            self.assertTrue(subject.can_open(KIND_MANUAL))
            subject.record(KIND_MANUAL)

        self.assertEqual(subject.used_total, 50)
        self.assertFalse(subject.can_open(KIND_MANUAL))
        self.assertEqual(subject.denial_reason(KIND_MANUAL), REASON_TOTAL_EXHAUSTED)

    def test_auto_stops_when_the_total_is_full_even_with_reserve_left(self) -> None:
        subject = budget(total=10, reserve=5)
        for _ in range(5):
            subject.record(KIND_MANUAL)
        for _ in range(5):
            subject.record(KIND_AUTO)

        self.assertFalse(subject.can_open(KIND_AUTO))
        self.assertEqual(subject.denial_reason(KIND_AUTO), REASON_TOTAL_EXHAUSTED)
        self.assertEqual(subject.remaining_total, 0)

    def test_zero_total_disables_the_limit(self) -> None:
        subject = budget(total=0, reserve=10)

        self.assertFalse(subject.enabled)
        for _ in range(200):
            self.assertTrue(subject.can_open(KIND_AUTO))
            subject.record(KIND_AUTO)
        self.assertEqual(subject.used_total, 0)
        self.assertEqual(subject.remaining_auto_ratio(), 1.0)
        self.assertIn("sin limite", subject.describe())

    def test_manual_reserve_is_clamped_to_the_total(self) -> None:
        """Si la reserva >= total, las reconexiones automaticas no caben."""
        subject = budget(total=5, reserve=10)

        self.assertEqual(subject.auto_capacity, 0)
        self.assertFalse(subject.can_open(KIND_AUTO))
        for _ in range(5):
            self.assertTrue(subject.can_open(KIND_MANUAL))
            subject.record(KIND_MANUAL)
        self.assertFalse(subject.can_open(KIND_MANUAL))

    def test_ratio_goes_down_as_the_auto_bag_is_spent(self) -> None:
        subject = budget(total=40, reserve=0)

        self.assertEqual(subject.remaining_auto_ratio(), 1.0)
        for _ in range(20):
            subject.record(KIND_AUTO)
        self.assertAlmostEqual(subject.remaining_auto_ratio(), 0.5, places=3)
        for _ in range(11):
            subject.record(KIND_AUTO)
        self.assertLess(subject.remaining_auto_ratio(), 0.25)

    def test_counters_survive_a_restart(self) -> None:
        """Reiniciar el panel no regenera la cuota del dia."""
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "connection_budget.json"
            first = budget(total=50, reserve=10, state_path=path)
            first.record(KIND_MANUAL)
            first.record(KIND_AUTO)
            first.record(KIND_AUTO)

            second = budget(total=50, reserve=10, state_path=path)

            self.assertEqual(second.used_total, 3)
            self.assertEqual(second.used_manual, 1)
            self.assertEqual(second.used_auto, 2)
            self.assertEqual(json.loads(path.read_text(encoding="utf-8"))["auto"], 2)

    def test_state_file_from_another_day_starts_at_zero(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "connection_budget.json"
            path.write_text(
                json.dumps({"date": "2020-01-01", "manual": 9, "auto": 41}),
                encoding="utf-8",
            )

            subject = budget(total=50, reserve=10, state_path=path)

            self.assertEqual(subject.used_total, 0)
            self.assertTrue(subject.can_open(KIND_AUTO))

    def test_corrupt_state_file_is_ignored(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "connection_budget.json"
            path.write_text("esto no es json{{{", encoding="utf-8")

            subject = budget(total=50, reserve=10, state_path=path)

            self.assertEqual(subject.used_total, 0)
            self.assertTrue(subject.can_open(KIND_MANUAL))
            # Al primer registro se reescribe un archivo valido.
            subject.record(KIND_MANUAL)
            self.assertEqual(json.loads(path.read_text(encoding="utf-8"))["manual"], 1)

    def test_rollover_at_utc_midnight(self) -> None:
        before_midnight = datetime(2026, 9, 24, 23, 59, tzinfo=timezone.utc)
        after_midnight = datetime(2026, 9, 25, 0, 1, tzinfo=timezone.utc)
        clock = {"now": before_midnight}

        def now() -> datetime:
            return clock["now"]

        subject = budget(total=50, reserve=10, now=now)
        for _ in range(12):
            subject.record(KIND_AUTO)
        self.assertEqual(subject.used_auto, 12)

        clock["now"] = after_midnight

        self.assertEqual(subject.used_total, 0)
        self.assertTrue(subject.can_open(KIND_AUTO))
        # Y la fecha registrada en el archivo acompana al nuevo dia.
        self.assertEqual(subject.date, "2026-09-25")

    def test_describe_reports_usage_for_the_monitor(self) -> None:
        subject = budget(total=50, reserve=10)
        subject.record(KIND_MANUAL)
        subject.record(KIND_AUTO)

        text = subject.describe()

        self.assertIn("2 de 50", text)
        self.assertIn("1 de 40", text)

    def test_snapshot_exposes_the_numbers_for_status_endpoint(self) -> None:
        subject = budget(total=50, reserve=10)
        subject.record(KIND_AUTO)

        snapshot = subject.snapshot()

        self.assertEqual(snapshot["total_per_day"], 50)
        self.assertEqual(snapshot["manual_reserve"], 10)
        self.assertEqual(snapshot["auto_capacity"], 40)
        self.assertEqual(snapshot["used_auto"], 1)
        self.assertEqual(snapshot["remaining_auto"], 39)
        self.assertTrue(snapshot["enabled"])


if __name__ == "__main__":
    unittest.main()
