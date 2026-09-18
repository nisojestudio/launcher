"""Sound alerts for Bridge connection state changes.

Plays system sounds when the Bridge connects or disconnects from TikTok.
Uses Windows winsound when available, falls back to terminal bell.
"""
from __future__ import annotations

import asyncio
import os
import sys
from typing import Any

from structured_logging import log_json


def _play_winsound(frequency: int, duration_ms: int) -> None:
    """Play a tone using Windows winsound (blocking)."""
    try:
        import winsound
        winsound.Beep(frequency, duration_ms)
    except (ImportError, RuntimeError):
        pass


def _play_winsound_async(frequency: int, duration_ms: int) -> None:
    """Play a tone in a thread to avoid blocking the event loop."""
    try:
        import winsound
        winsound.Beep(frequency, duration_ms)
    except (ImportError, RuntimeError):
        pass


def _terminal_bell() -> None:
    """Fallback: terminal bell character."""
    sys.stdout.write("\a")
    sys.stdout.flush()


class SoundAlerts:
    """Plays audio alerts on connection state changes.

    Sounds:
    - Connected: ascending two-tone (800Hz → 1200Hz)
    - Disconnected: descending three-tone (1200Hz → 800Hz → 400Hz)
    - Reconnecting: single tone (600Hz)
    - Error: low tone (300Hz)
    """

    # Frequencies in Hz
    CONNECTED_TONES = [(800, 150), (1200, 200)]
    DISCONNECTED_TONES = [(1200, 150), (800, 150), (400, 250)]
    RECONNECTING_TONE = (600, 200)
    ERROR_TONE = (300, 300)

    def __init__(self, *, enabled: bool = True, volume: int = 100) -> None:
        self._enabled = enabled
        self._volume = volume
        self._last_state = ""
        self._is_windows = sys.platform == "win32"
        # Check if winsound is available
        self._has_winsound = False
        if self._is_windows:
            try:
                import winsound
                self._has_winsound = True
            except (ImportError, RuntimeError):
                pass

    def _play_tone(self, frequency: int, duration_ms: int) -> None:
        """Play a single tone."""
        if not self._enabled:
            return
        if self._has_winsound:
            _play_winsound(frequency, duration_ms)
        else:
            _terminal_bell()

    def _play_sequence(self, tones: list[tuple[int, int]]) -> None:
        """Play a sequence of tones (blocking). Must be called from a thread."""
        if not self._enabled:
            return
        if self._has_winsound:
            for freq, dur in tones:
                _play_winsound(freq, dur)
        else:
            _terminal_bell()

    async def play_connected(self) -> None:
        """Play the connected sound asynchronously."""
        if not self._enabled:
            return
        await asyncio.to_thread(self._play_sequence, self.CONNECTED_TONES)

    async def play_disconnected(self) -> None:
        """Play the disconnected sound asynchronously."""
        if not self._enabled:
            return
        await asyncio.to_thread(self._play_sequence, self.DISCONNECTED_TONES)

    async def play_reconnecting(self) -> None:
        """Play the reconnecting sound asynchronously."""
        if not self._enabled:
            return
        freq, dur = self.RECONNECTING_TONE
        await asyncio.to_thread(self._play_tone, freq, dur)

    async def play_error(self) -> None:
        """Play the error sound asynchronously."""
        if not self._enabled:
            return
        freq, dur = self.ERROR_TONE
        await asyncio.to_thread(self._play_tone, freq, dur)

    def should_alert(self, new_state: str) -> bool:
        """Check if we should play an alert for this state transition."""
        if new_state == self._last_state:
            return False
        self._last_state = new_state
        return True
