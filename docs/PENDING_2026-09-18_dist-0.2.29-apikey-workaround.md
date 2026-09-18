# Pendiente: workaround API key en dist 0.2.29 (2026-09-18)

## Incidente
El panel 0.2.29 (binario) no conectaba el bridge TikTok con error
`runner_start_failed` / "tiktools provider selected but no API key configured",
aunque `panel_config.json` tenía `provider_api_key` configurada.

## Causa raíz
1. `dist/releases/0.2.29/NisojeStudio/tools/bridge_py/bridge_env_check.py` estaba
   desactualizado respecto al repo (sin soporte de `--api-key`).
2. El binario `NisojeStudio.exe` 0.2.29 no pasa `--api-key` al probe
   `bridge_env_check.py` (verificado con wrapper que registra argv reales).
   El fix ya existe en el repo (commit `8e60581` — "pass panel-provided API key
   to runtime probe") pero **no está incluido en la build empaquetada**.

## Workaround aplicado (temporal)
- Se copiaron `bridge_env_check.py` y `sample_events.py` actualizados al dist y
  se borró `__pycache__` para evitar bytecode viejo.
- Se escribió la API key en texto plano en
  `dist/releases/0.2.29/NisojeStudio/tools/bridge_py/bridge_config.yaml`
  (campo `connection.api_key`), que el probe sí lee como fallback.
- Validado end-to-end: `/api/bridge/connect` → `ok: true`,
  `connection_state: connected`, runner PID activo, WS 8765 aceptando mensajes.

## Pendiente (resolución definitiva)
- [ ] Recompilar el panel (`ninja -C build/release-0.1.1` u build dir vigente)
      con el HEAD actual, que ya incluye el paso de `--api-key` al probe.
- [ ] Generar release nuevo (según `docs/releases/RELEASE_PROTOCOL.md`) para
      reemplazar 0.2.29 en el equipo.
- [ ] Una vez validada la build nueva, **vaciar `connection.api_key`** del
      `bridge_config.yaml` del nuevo dist para no persistir credenciales en
      disco (ver commit `8e60581`: "stop persisting credentials").
- [ ] Agregar al proceso de release un paso que sincronice
      `tools/bridge_py/*.py` del repo hacia el dist (este desfasaje ya fue
      causa de incidentes anteriores — ver CHANGELOG "Dist desincronizado").
