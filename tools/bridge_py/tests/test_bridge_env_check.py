from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from bridge_env_check import perform_bridge_env_check


class BridgeEnvCheckTests(unittest.TestCase):
    def test_repo_bridge_environment_is_ready_in_test_runtime(self) -> None:
        bridge_root = Path(__file__).resolve().parents[1]
        report = perform_bridge_env_check(bridge_root)

        self.assertTrue(report["ok"], report["summary"])
        self.assertIn(report["runtimeMode"], {"packaged", "venv", "system"})
        self.assertTrue(report["checks"])
        self.assertIn("TikTokLive", report["moduleVersions"])
        self.assertIn("effectiveBridgeLogPath", report)
        self.assertIn("bridgeLogDirectoryWritable", report)
        self.assertIn("opensslVersion", report)

    def test_cli_can_write_report_without_stdout(self) -> None:
        bridge_root = Path(__file__).resolve().parents[1]
        script_path = bridge_root / "bridge_env_check.py"

        with tempfile.TemporaryDirectory() as temp_dir:
            report_path = Path(temp_dir) / "bridge-env-report.json"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(script_path),
                    "--bridge-root",
                    str(bridge_root),
                    "--format",
                    "json",
                    "--report-path",
                    str(report_path),
                    "--no-stdout",
                ],
                capture_output=True,
                text=True,
                check=False,
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(completed.stdout, "")
            self.assertTrue(report_path.exists())

            report = json.loads(report_path.read_text(encoding="utf-8"))
            self.assertTrue(report["ok"], report["summary"])
            self.assertIn("TikTokLive", report["moduleVersions"])


if __name__ == "__main__":
    unittest.main()
