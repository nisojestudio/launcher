"""Presupuesto diario de aperturas de sesion contra el proveedor.

El proveedor de TikTok expone un tope de conexiones por dia (50 en el plan
actual). Sin este guardián, cada reconexion, cada espera del vivo y cada
reinicio del panel gastan una conexion sin cuenta y el tope se agota a mitad
del live.

El presupuesto reparte esas aperturas en dos bolsas:

- ``manual_reserve``: conexiones que el operador puede disparar desde el panel
  (reiniciar la sesion, cambiar de usuario) sin que las reconexiones
  automaticas se las hayan comido.
- el resto (``total - manual_reserve``): exclusivo para reconexiones
  automaticas (caidas, silencio, espera del vivo).

El contador se persiste en JSON (best-effort) para que un reinicio del panel
no regenere el tope diario. Si no hay ``state_path``, el presupuesto vive solo
en memoria de la sesion actual.

La fecha de referencia es UTC: el rollover ocurre aunque el panel no se
reinicie. Un archivo corrupto o de otro dia se ignora y se arranca en cero,
nunca bloquea la conexion.
"""

from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

KIND_MANUAL = "manual"
KIND_AUTO = "auto"
_KINDS = (KIND_MANUAL, KIND_AUTO)

# Motivos por los que `can_open` puede negar una apertura. Viajan al log para
# distinguir "se acabo el total" de "se acabo la bolsa de reconexiones".
REASON_TOTAL_EXHAUSTED = "daily_total_exhausted"
REASON_AUTO_EXHAUSTED = "auto_reserve_exhausted"


def utc_date(moment: datetime | None = None) -> str:
    """Fecha YYYY-MM-DD en UTC (ventana de conteo del proveedor)."""
    current = moment or datetime.now(timezone.utc)
    if current.tzinfo is None:
        current = current.replace(tzinfo=timezone.utc)
    return current.astimezone(timezone.utc).strftime("%Y-%m-%d")


class DailyConnectionBudget:
    """Contador diario de aperturas, con reserva manual y persistencia."""

    def __init__(
        self,
        *,
        total_per_day: int,
        manual_reserve: int,
        state_path: str | Path = "",
        now: Callable[[], datetime] | None = None,
    ) -> None:
        self._total = max(0, int(total_per_day or 0))
        self._manual_reserve = max(0, int(manual_reserve or 0))
        # Sin reserva configurada la bolsa de reconexiones es el total entero.
        if self._manual_reserve > self._total > 0:
            self._manual_reserve = self._total
        self._path = Path(state_path) if str(state_path or "").strip() else None
        self._now = now or (lambda: datetime.now(timezone.utc))
        self._date = utc_date(self._now())
        self._used: dict[str, int] = {KIND_MANUAL: 0, KIND_AUTO: 0}
        self._loaded = False
        self._persist_failures = 0
        self._load()

    # -- consultas ---------------------------------------------------------

    @property
    def enabled(self) -> bool:
        """False = sin tope diario configurado (0 = ilimitado)."""
        return self._total > 0

    @property
    def auto_capacity(self) -> int:
        """Aperturas automaticas que caben en el presupuesto de hoy."""
        if not self.enabled:
            return 0
        return max(0, self._total - self._manual_reserve)

    @property
    def date(self) -> str:
        return self._date

    @property
    def used_manual(self) -> int:
        self._rollover()
        return self._used[KIND_MANUAL]

    @property
    def used_auto(self) -> int:
        self._rollover()
        return self._used[KIND_AUTO]

    @property
    def used_total(self) -> int:
        self._rollover()
        return self._used[KIND_MANUAL] + self._used[KIND_AUTO]

    @property
    def remaining_total(self) -> int:
        if not self.enabled:
            return 0
        return max(0, self._total - self.used_total)

    @property
    def remaining_auto(self) -> int:
        if not self.enabled:
            return 0
        return max(0, self.auto_capacity - self.used_auto)

    def remaining_auto_ratio(self) -> float:
        """Fraccion de la bolsa automatica que queda (1.0 = recien usada).

        1.0 tambien cuando el presupuesto esta deshabilitado: sin tope no hay
        motivo para estirar las esperas.
        """
        if not self.enabled:
            return 1.0
        capacity = self.auto_capacity
        if capacity <= 0:
            return 0.0
        return max(0.0, min(1.0, self.remaining_auto / float(capacity)))

    def can_open(self, kind: str) -> bool:
        """True si queda sitio en la bolsa que corresponde a ``kind``."""
        if not self.enabled:
            return True
        self._rollover()
        if self._used[KIND_MANUAL] + self._used[KIND_AUTO] >= self._total:
            return False
        if kind == KIND_AUTO and self._used[KIND_AUTO] >= self.auto_capacity:
            return False
        return True

    def denial_reason(self, kind: str) -> str:
        """Motivo legible (para el log) de la ultima negacion."""
        if not self.enabled:
            return ""
        if self._used[KIND_MANUAL] + self._used[KIND_AUTO] >= self._total:
            return REASON_TOTAL_EXHAUSTED
        if kind == KIND_AUTO and self._used[KIND_AUTO] >= self.auto_capacity:
            return REASON_AUTO_EXHAUSTED
        return ""

    def describe(self) -> str:
        """Resumen corto para mensajes visibles en el monitor."""
        if not self.enabled:
            return "sin limite diario de conexiones"
        return (
            f"usadas {self.used_total} de {self._total} conexiones de hoy "
            f"({self.used_auto} de {self.auto_capacity} para reconexiones)"
        )

    def record(self, kind: str) -> None:
        """Gasta una apertura de la bolsa ``kind`` y persiste el contador."""
        if not self.enabled:
            return
        self._rollover()
        key = kind if kind in _KINDS else KIND_AUTO
        self._used[key] += 1
        self._persist()

    def snapshot(self) -> dict[str, Any]:
        """Estado del presupuesto para /status y logs."""
        return {
            "enabled": self.enabled,
            "date": self._date,
            "total_per_day": self._total,
            "manual_reserve": self._manual_reserve,
            "auto_capacity": self.auto_capacity,
            "used_total": self.used_total,
            "used_manual": self.used_manual,
            "used_auto": self.used_auto,
            "remaining_total": self.remaining_total,
            "remaining_auto": self.remaining_auto,
            "state_path": str(self._path) if self._path else "",
        }

    # -- persistencia ------------------------------------------------------

    def _rollover(self) -> None:
        today = utc_date(self._now())
        if today == self._date:
            return
        self._date = today
        self._used = {KIND_MANUAL: 0, KIND_AUTO: 0}
        self._persist()

    def _load(self) -> None:
        if self._path is None or self._loaded:
            return
        self._loaded = True
        try:
            raw = self._path.read_text(encoding="utf-8")
            data = json.loads(raw)
        except (OSError, ValueError):
            # Archivo inexistente, ilegible o corrupto: se arranca en cero.
            return
        if not isinstance(data, dict):
            return
        if str(data.get("date") or "") != self._date:
            # Contador de otro dia: no sirve, pero se sobrescribe al persistir.
            return
        for kind in _KINDS:
            try:
                value = int(data.get(kind) or 0)
            except (TypeError, ValueError):
                value = 0
            self._used[kind] = max(0, value)

    def _persist(self) -> None:
        if self._path is None:
            return
        payload = {
            "date": self._date,
            KIND_MANUAL: self._used[KIND_MANUAL],
            KIND_AUTO: self._used[KIND_AUTO],
        }
        try:
            self._path.parent.mkdir(parents=True, exist_ok=True)
            self._path.write_text(json.dumps(payload), encoding="utf-8")
            self._persist_failures = 0
        except OSError:
            # Best-effort: sin disco el presupuesto sigue en memoria y no se
            # bloquea la conexion por no poder escribir el contador.
            self._persist_failures += 1
