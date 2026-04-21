from __future__ import annotations

import argparse
import importlib
import importlib.metadata as importlib_metadata
import json
import ssl
import sys
from pathlib import Path
from typing import Any

from bridge_config import load_bridge_config
from structured_logging import describe_log_destination


REQUIRED_MODULES = (
    "yaml",
    "websockets",
    "TikTokLive",
)

OPTIONAL_MODULES = (
    "certifi",
)

REQUIRED_FILES = (
    "run_tiktok_bridge.py",
    "bridge_server.py",
    "tiktok_connection.py",
)

REQUIRED_RUNTIME_FILES = (
    "python.exe",
    "libssl-3.dll",
    "libcrypto-3.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
)


def _normalize_bridge_root(value: Path | str | None) -> Path:
    if value is None:
        return Path(__file__).resolve().parent
    return Path(value).resolve()


def _normalize_config_path(bridge_root: Path, value: Path | str | None) -> Path:
    if value is None:
        return (bridge_root.parent.parent / "panel_config.json").resolve()
    return Path(value).resolve()


def _module_version(module_name: str, module: Any | None = None) -> str:
    try:
        return importlib_metadata.version(module_name)
    except Exception:
        pass

    if module is not None:
        version_value = getattr(module, "__version__", "")
        if isinstance(version_value, str) and version_value.strip():
            return version_value.strip()

    return "unknown"


def _build_report(
    bridge_root: Path,
    config_path: Path,
    expect_auth_required: bool,
) -> dict[str, Any]:
    runtime_python = bridge_root / "python_runtime" / "python.exe"
    dev_python = bridge_root / ".venv" / "Scripts" / "python.exe"
    site_packages = bridge_root / "python_runtime" / "Lib" / "site-packages"
    current_python = Path(sys.executable).resolve()
    bridge_config = load_bridge_config(bridge_root / "bridge_config.yaml")
    log_destination = describe_log_destination(bridge_config.logging.log_path)

    runtime_mode = "system"
    if current_python == runtime_python.resolve() if runtime_python.exists() else False:
        runtime_mode = "packaged"
    elif current_python == dev_python.resolve() if dev_python.exists() else False:
        runtime_mode = "venv"

    alerts: list[str] = []
    checks: list[dict[str, Any]] = []

    packaged_runtime_available = runtime_python.exists()
    dev_runtime_available = dev_python.exists()

    if not packaged_runtime_available and not dev_runtime_available:
        alerts.append(
            "No se encontro el runtime Python del bridge TikTok. Reinstala Panel Live para restaurar "
            "tools/bridge_py/python_runtime."
        )

    if packaged_runtime_available and not site_packages.exists():
        alerts.append(
            "La instalacion TikTok quedo incompleta: falta python_runtime/Lib/site-packages."
        )

    if packaged_runtime_available:
        for runtime_file in REQUIRED_RUNTIME_FILES:
            runtime_file_path = bridge_root / "python_runtime" / runtime_file
            runtime_ok = runtime_file_path.exists()
            checks.append({
                "type": "runtime_file",
                "id": runtime_file,
                "ok": runtime_ok,
                "path": str(runtime_file_path),
            })
            if not runtime_ok:
                alerts.append(
                    f"Falta el archivo critico del runtime Python empaquetado: {runtime_file}."
                )

    for relative_path in REQUIRED_FILES:
        file_path = bridge_root / relative_path
        file_ok = file_path.exists()
        checks.append({
            "type": "file",
            "id": relative_path,
            "ok": file_ok,
            "path": str(file_path),
        })
        if not file_ok:
            alerts.append(f"Falta el archivo requerido del bridge: {relative_path}.")

    module_versions: dict[str, str] = {}
    for module_name in REQUIRED_MODULES:
        try:
            module = importlib.import_module(module_name)
            module_versions[module_name] = _module_version(module_name, module)
            checks.append({
                "type": "module",
                "id": module_name,
                "ok": True,
                "version": module_versions[module_name],
            })
        except Exception as exc:  # pragma: no cover - depends on installed runtime
            checks.append({
                "type": "module",
                "id": module_name,
                "ok": False,
                "error": str(exc),
            })
            if module_name == "TikTokLive":
                alerts.append(
                    "TikTokLive no pudo cargarse en el runtime instalado. Reinstala el panel para reparar "
                    "las dependencias de TikTok."
                )
            else:
                alerts.append(
                    f"La dependencia Python {module_name} no esta disponible en el runtime TikTok."
                )

    optional_module_versions: dict[str, str] = {}
    certifi_bundle = ""
    for module_name in OPTIONAL_MODULES:
        try:
            module = importlib.import_module(module_name)
            optional_module_versions[module_name] = _module_version(module_name, module)
            checks.append({
                "type": "optional_module",
                "id": module_name,
                "ok": True,
                "version": optional_module_versions[module_name],
            })
            if module_name == "certifi" and hasattr(module, "where"):
                certifi_bundle = str(module.where())
        except Exception as exc:
            checks.append({
                "type": "optional_module",
                "id": module_name,
                "ok": False,
                "error": str(exc),
            })

    checks.append({
        "type": "logging",
        "id": "bridge_log_path",
        "ok": bool(log_destination.get("directoryWritable")),
        "requestedPath": log_destination.get("requestedPath"),
        "resolvedPath": log_destination.get("resolvedPath"),
        "usesLocalFallback": bool(log_destination.get("usesLocalFallback")),
    })
    if not log_destination.get("directoryWritable"):
        alerts.append(
            "La ruta de log efectiva del bridge TikTok no es escribible por este usuario."
        )

    config_target_user = ""
    config_auth_required = False
    config_exists = config_path.exists()
    if not config_exists:
        alerts.append(
            f"No se encontro panel_config.json para el instalador en {config_path}."
        )
        checks.append({
            "type": "config",
            "id": "panel_config.json",
            "ok": False,
            "path": str(config_path),
            "error": "missing",
        })
    else:
        try:
            parsed_config = json.loads(config_path.read_text(encoding="utf-8"))
            config_target_user = str(parsed_config.get("external_target_user") or "").strip()
            config_bridge_mode = str(parsed_config.get("bridge_mode") or "").strip()
            config_auth_required = bool((parsed_config.get("auth") or {}).get("required", False))
            config_ok = config_bridge_mode in ("external", "stub")
            checks.append({
                "type": "config",
                "id": "panel_config.json",
                "ok": config_ok,
                "path": str(config_path),
                "bridgeMode": config_bridge_mode,
                "targetUser": config_target_user,
                "authRequired": config_auth_required,
            })
            if not config_ok:
                alerts.append(
                    "panel_config.json no define un bridge_mode valido para Panel Live."
                )
            elif expect_auth_required and not config_auth_required:
                alerts.append(
                    "panel_config.json no exige autenticacion remota. Este instalador no bloqueara el acceso en el primer arranque."
                )
        except Exception as exc:  # pragma: no cover - depends on installed file state
            checks.append({
                "type": "config",
                "id": "panel_config.json",
                "ok": False,
                "path": str(config_path),
                "error": str(exc),
            })
            alerts.append(
                "panel_config.json no pudo validarse. Reinstala Panel Live para restaurar la configuracion inicial."
            )

    ok = not alerts
    if ok:
        if runtime_mode == "packaged":
            summary = "TikTok listo: runtime embebido verificado correctamente."
        elif runtime_mode == "venv":
            summary = "TikTok listo: entorno local de desarrollo verificado."
        else:
            summary = "TikTok listo: Python activo verificado."
    else:
        summary = alerts[0]

    return {
        "ok": ok,
        "summary": summary,
        "alerts": alerts,
        "bridgeRoot": str(bridge_root),
        "pythonExecutable": str(current_python),
        "pythonVersion": sys.version.split()[0],
        "runtimeMode": runtime_mode,
        "packagedRuntimeAvailable": packaged_runtime_available,
        "devRuntimeAvailable": dev_runtime_available,
        "configPath": str(config_path),
        "configTargetUser": config_target_user,
        "configAuthRequired": config_auth_required,
        "configuredBridgeLogPath": bridge_config.logging.log_path,
        "effectiveBridgeLogPath": log_destination.get("resolvedPath"),
        "bridgeLogDirectoryWritable": bool(log_destination.get("directoryWritable")),
        "bridgeLogUsesLocalFallback": bool(log_destination.get("usesLocalFallback")),
        "opensslVersion": ssl.OPENSSL_VERSION,
        "certifiBundle": certifi_bundle,
        "checks": checks,
        "moduleVersions": module_versions,
        "optionalModuleVersions": optional_module_versions,
    }


def perform_bridge_env_check(
    bridge_root: Path | str | None = None,
    config_path: Path | str | None = None,
    expect_auth_required: bool = False,
) -> dict[str, Any]:
    normalized_bridge_root = _normalize_bridge_root(bridge_root)
    normalized_config_path = _normalize_config_path(normalized_bridge_root, config_path)
    return _build_report(normalized_bridge_root, normalized_config_path, expect_auth_required)


def render_text_report(report: dict[str, Any]) -> str:
    lines = [report.get("summary") or "Sin resumen."]
    for alert in report.get("alerts") or []:
        lines.append(f"- {alert}")
    if report.get("ok"):
        lines.append(f"Python: {report.get('pythonExecutable', '')}")
        lines.append(f"Modo: {report.get('runtimeMode', '')}")
        lines.append(f"Log: {report.get('effectiveBridgeLogPath', '')}")
    return "\n".join(lines).strip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Verifica que el runtime TikTok del panel este listo.")
    parser.add_argument("--bridge-root", default="", help="Ruta tools/bridge_py a validar.")
    parser.add_argument(
        "--format",
        choices=("json", "text"),
        default="json",
        help="Formato de salida del reporte.",
    )
    parser.add_argument(
        "--report-path",
        default="",
        help="Ruta opcional para guardar el reporte serializado.",
    )
    parser.add_argument(
        "--config-path",
        default="",
        help="Ruta opcional al panel_config.json que debe quedar listo junto al bridge.",
    )
    parser.add_argument(
        "--expect-auth-required",
        action="store_true",
        help="Falla si panel_config.json no deja auth.required=true.",
    )
    parser.add_argument(
        "--no-stdout",
        action="store_true",
        help="No imprime el reporte en stdout; util cuando otro proceso lo guarda por archivo.",
    )
    args = parser.parse_args()

    bridge_root = _normalize_bridge_root(args.bridge_root or None)
    report = perform_bridge_env_check(
        bridge_root,
        args.config_path or None,
        expect_auth_required=args.expect_auth_required,
    )
    serialized = json.dumps(report, ensure_ascii=False, indent=2) if args.format == "json" else render_text_report(report)

    if args.report_path:
        report_path = Path(args.report_path)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(serialized, encoding="utf-8")

    if not args.no_stdout:
        sys.stdout.write(serialized)
        if args.format == "json":
            sys.stdout.write("\n")
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
