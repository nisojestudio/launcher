from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from structured_logging import describe_log_destination, resolve_log_path


class StructuredLoggingTests(unittest.TestCase):
    def test_relative_log_path_stays_in_writable_workspace(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            with patch("structured_logging.Path.cwd", return_value=Path(temp_dir)):
                resolved = resolve_log_path("tools/bridge_py/logs/bridge.jsonl")

        self.assertEqual(
            resolved,
            (Path(temp_dir) / "tools/bridge_py/logs/bridge.jsonl").resolve(),
        )

    def test_relative_log_path_falls_back_to_local_appdata_when_parent_is_not_writable(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            with patch("structured_logging.Path.cwd", return_value=Path(r"C:\Program Files\Panel Live")), patch(
                "structured_logging._is_existing_parent_writable",
                return_value=False,
            ), patch.dict("os.environ", {"LOCALAPPDATA": temp_dir}, clear=False):
                info = describe_log_destination("tools/bridge_py/logs/bridge.jsonl")

        self.assertTrue(info["usesLocalFallback"])
        self.assertTrue(str(info["resolvedPath"]).startswith(str(Path(temp_dir).resolve())))

    def test_relative_log_path_falls_back_to_tempdir_when_local_appdata_missing(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fallback_temp = Path(temp_dir) / "tmp"
            fallback_temp.mkdir()
            with patch("structured_logging.Path.cwd", return_value=Path(r"C:\Program Files\Panel Live")), patch(
                "structured_logging._is_existing_parent_writable",
                return_value=False,
            ), patch("tempfile.gettempdir", return_value=str(fallback_temp)), patch.dict(
                "os.environ",
                {"LOCALAPPDATA": ""},
                clear=False,
            ):
                resolved = resolve_log_path("tools/bridge_py/logs/bridge.jsonl")

        self.assertEqual(
            resolved,
            (fallback_temp / "NisojeStudio" / "logs" / "bridge.jsonl").resolve(),
        )


if __name__ == "__main__":
    unittest.main()
