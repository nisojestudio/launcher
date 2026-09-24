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

    def test_auth_required_config_fails_when_release_auth_fields_are_missing(self) -> None:
        bridge_root = Path(__file__).resolve().parents[1]

        with tempfile.TemporaryDirectory() as temp_dir:
            config_path = Path(temp_dir) / "panel_config.json"
            config_path.write_text(
                json.dumps(
                    {
                        "bridge_mode": "external",
                        "auth": {
                            "required": True,
                            "firebase_api_key": "",
                            "nisoje_api_base": "",
                            "me_licenses_path": "/api/me/licenses",
                            "me_games_catalog_path": "",
                        },
                    },
                    ensure_ascii=False,
                    indent=2,
                ),
                encoding="utf-8",
            )

            report = perform_bridge_env_check(
                bridge_root,
                config_path=config_path,
                expect_auth_required=True,
            )

        self.assertFalse(report["ok"])
        self.assertFalse(report["configRemoteAuthReady"])
        self.assertIn("firebase_api_key", report["configMissingRemoteAuthFields"])
        self.assertIn("nisoje_api_base", report["configMissingRemoteAuthFields"])
        self.assertIn("me_games_catalog_path", report["configMissingRemoteAuthFields"])

    def test_auth_required_config_passes_when_release_auth_fields_are_present(self) -> None:
        bridge_root = Path(__file__).resolve().parents[1]

        with tempfile.TemporaryDirectory() as temp_dir:
            config_path = Path(temp_dir) / "panel_config.json"
            config_path.write_text(
                json.dumps(
                    {
                        "bridge_mode": "external",
                        "auth": {
                            "required": True,
                            "firebase_api_key": "test-api-key",
                            "firebase_project_id": "test-project",
                            "firebase_auth_domain": "test-project.firebaseapp.com",
                            "nisoje_api_base": "https://example.invalid",
                            "me_licenses_path": "/api/me/licenses",
                            "me_games_catalog_path": "/api/me/games/catalog",
                        },
                    },
                    ensure_ascii=False,
                    indent=2,
                ),
                encoding="utf-8",
            )

            report = perform_bridge_env_check(
                bridge_root,
                config_path=config_path,
                expect_auth_required=True,
            )

        self.assertTrue(report["ok"], report["summary"])
        self.assertTrue(report["configRemoteAuthReady"])
        self.assertEqual(report["configMissingRemoteAuthFields"], [])

    def test_missing_tiktools_api_key_is_a_warning_not_a_blocker(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            bridge_root = Path(temp_dir) / "bridge_py"
            bridge_root.mkdir()
            (bridge_root / "bridge_config.yaml").write_text(
                "connection_mode: tiktools\nconnection:\n  api_key: \"\"\n",
                encoding="utf-8",
            )
            config_path = Path(temp_dir) / "panel_config.json"
            config_path.write_text(
                json.dumps({"bridge_mode": "external", "auth": {"required": False}}, indent=2),
                encoding="utf-8",
            )

            report = perform_bridge_env_check(bridge_root, config_path=config_path)

        # La credencial llega por peticion desde la UI: su ausencia no debe
        # impedir arrancar el bridge, solo avisar.
        tiktools_check = next(
            check for check in report["checks"]
            if check.get("type") == "provider_connectivity" and check.get("id") == "tiktools"
        )
        self.assertFalse(tiktools_check["ok"])
        self.assertFalse(tiktools_check["blocking"])
        self.assertIn("Falta la API key de tik.tools", " ".join(report["warnings"]))
        self.assertNotIn("Falta la API key de tik.tools", " ".join(report["alerts"]))

    def test_missing_euler_api_key_is_a_warning_not_a_blocker(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            bridge_root = Path(temp_dir) / "bridge_py"
            bridge_root.mkdir()
            (bridge_root / "bridge_config.yaml").write_text(
                "connection_mode: euler\nconnection:\n  api_key: \"\"\n",
                encoding="utf-8",
            )
            config_path = Path(temp_dir) / "panel_config.json"
            config_path.write_text(
                json.dumps({"bridge_mode": "external", "auth": {"required": False}}, indent=2),
                encoding="utf-8",
            )

            report = perform_bridge_env_check(bridge_root, config_path=config_path)

        euler_check = next(
            check for check in report["checks"]
            if check.get("type") == "provider_connectivity" and check.get("id") == "euler_stream"
        )
        self.assertFalse(euler_check["ok"])
        self.assertFalse(euler_check["blocking"])
        self.assertTrue(any("Euler" in warning for warning in report["warnings"]))

    def test_stored_keys_in_the_vault_are_not_reported_as_missing(self) -> None:
        """La bóveda del panel guarda las keys: el chequeo no debe avisar faltante."""
        with tempfile.TemporaryDirectory() as temp_dir:
            bridge_root = Path(temp_dir) / "bridge_py"
            bridge_root.mkdir()
            (bridge_root / "bridge_config.yaml").write_text(
                "connection_mode: tiktools\nconnection:\n  api_key: \"\"\n",
                encoding="utf-8",
            )
            config_path = Path(temp_dir) / "panel_config.json"
            config_path.write_text(
                json.dumps({"bridge_mode": "external", "auth": {"required": False}}, indent=2),
                encoding="utf-8",
            )

            report = perform_bridge_env_check(
                bridge_root,
                config_path=config_path,
                has_stored_keys=True,
            )

        tiktools_check = next(
            check for check in report["checks"]
            if check.get("type") == "provider_connectivity" and check.get("id") == "tiktools"
        )
        self.assertTrue(tiktools_check["ok"])
        self.assertTrue(tiktools_check["api_key_configured"])
        self.assertEqual(tiktools_check["api_key_source"], "vault")
        self.assertNotIn("Falta la API key", " ".join(report["warnings"]))
        self.assertNotIn("Falta la API key", report["summary"])
        self.assertTrue(report["bridgeApiKeysStored"])

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
