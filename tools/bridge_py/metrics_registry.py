from __future__ import annotations

import statistics
import time
from collections import Counter, deque
from dataclasses import dataclass, field
from typing import Any


@dataclass(slots=True)
class MetricsSnapshot:
    counters: dict[str, int] = field(default_factory=dict)
    gauges: dict[str, float] = field(default_factory=dict)
    event_counts: dict[str, int] = field(default_factory=dict)
    throughput_events_per_sec: float = 0.0
    median_ingest_latency_ms: float = 0.0
    uptime_ms: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "counters": dict(self.counters),
            "gauges": dict(self.gauges),
            "event_counts": dict(self.event_counts),
            "throughput_events_per_sec": self.throughput_events_per_sec,
            "median_ingest_latency_ms": self.median_ingest_latency_ms,
            "uptime_ms": self.uptime_ms,
        }


class MetricsRegistry:
    def __init__(self) -> None:
        self._started_at = time.monotonic()
        self._counters: Counter[str] = Counter()
        self._gauges: dict[str, float] = {}
        self._event_counts: Counter[str] = Counter()
        self._recent_event_times = deque(maxlen=5000)
        self._recent_latencies_ms = deque(maxlen=5000)

    def increment(self, name: str, value: int = 1) -> None:
        self._counters[name] += int(value)

    def set_gauge(self, name: str, value: float) -> None:
        self._gauges[name] = float(value)

    def record_event(self, event_type: str, latency_ms: int = 0) -> None:
        self._counters["events_total"] += 1
        self._event_counts[event_type] += 1
        self._recent_event_times.append(time.monotonic())
        if latency_ms > 0:
            self._recent_latencies_ms.append(int(latency_ms))

    def snapshot(self) -> MetricsSnapshot:
        now = time.monotonic()
        cutoff = now - 1.0
        throughput = 0
        for timestamp in reversed(self._recent_event_times):
            if timestamp < cutoff:
                break
            throughput += 1

        median_latency = 0.0
        if self._recent_latencies_ms:
            median_latency = float(statistics.median(self._recent_latencies_ms))

        return MetricsSnapshot(
            counters=dict(self._counters),
            gauges=dict(self._gauges),
            event_counts=dict(self._event_counts),
            throughput_events_per_sec=float(throughput),
            median_ingest_latency_ms=median_latency,
            uptime_ms=int((now - self._started_at) * 1000),
        )

    def render_prometheus(self) -> str:
        snapshot = self.snapshot()
        lines: list[str] = []

        for name, value in sorted(snapshot.counters.items()):
            metric_name = f"livepanel_bridge_{name}"
            lines.append(f"# TYPE {metric_name} counter")
            lines.append(f"{metric_name} {value}")

        for name, value in sorted(snapshot.gauges.items()):
            metric_name = f"livepanel_bridge_{name}"
            lines.append(f"# TYPE {metric_name} gauge")
            lines.append(f"{metric_name} {value}")

        for event_type, value in sorted(snapshot.event_counts.items()):
            lines.append(
                f'livepanel_bridge_events_by_type{{event_type="{event_type}"}} {value}'
            )

        lines.append(
            f"livepanel_bridge_throughput_events_per_sec {snapshot.throughput_events_per_sec}"
        )
        lines.append(
            f"livepanel_bridge_median_ingest_latency_ms {snapshot.median_ingest_latency_ms}"
        )
        lines.append(f"livepanel_bridge_uptime_ms {snapshot.uptime_ms}")
        return "\n".join(lines) + "\n"
