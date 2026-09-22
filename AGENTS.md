# AGENTS.md — Contrato maestro del agente

Este repositorio está pensado para trabajar con agentes de programación en VS Code (GPT-5.4, Codex 5.3 u otros equivalentes).

## 1. Misión
Construir **Nisoje LivePanel 3.0** como una plataforma nueva para juegos live, con núcleo preparado para C++/WASM y con integración práctica al panel y herramientas heredadas.

## 2. Resultado esperado
El agente debe ayudar a:
- analizar el proyecto actual
- extraer la arquitectura útil
- proponer la arquitectura destino
- crear el nuevo proyecto por capas
- validar técnicamente cada etapa

## 3. Principios de trabajo
- Priorizar claridad arquitectónica.
- Hacer cambios pequeños y auditables.
- Escribir primero contratos de interfaz cuando una capa nueva aparezca.
- Mantener trazabilidad de decisiones.
- No confundir hipótesis con validación real.
- No re-configurar lo que ya está configurado — verificar builds existentes antes de ejecutar cmake o vcpkg.

## 4. Libertad operativa
En esta fase inicial **no existen restricciones especiales sobre cambios visuales** salvo que:
- rompan el build
- rompan compatibilidad externa necesaria
- contradigan una instrucción posterior explícita

## 5. Reglas obligatorias
### 5.1 Antes de cambiar código
El agente debe:
- leer `README.md`
- leer `docs/WORKING_CONTRACT.md`
- leer `docs/ARCHITECTURE_START.md`
- revisar los archivos directamente relacionados

### 5.2 Al modificar código
El agente debe:
- indicar qué archivos toca
- explicar por qué
- describir riesgos
- proponer forma de validar

### 5.3 Validación
El agente solo puede decir “validado” si realmente ejecutó la validación.
Si no pudo ejecutar, debe decir exactamente qué faltó.

### 5.4 Integración
El agente debe asumir que el sistema final puede incluir:
- núcleo C++
- capa web
- bridge Python o Node
- integración WebSocket / eventos live
- salida WebAssembly futura

### 5.5 Build sin reinstalación
Antes de ejecutar `cmake --preset`, `cmake -B`, `vcpkg install`, o cualquier comando
que re-configure el toolchain, el agente debe:

a) Verificar si existe un build previsto en `build/release-0.1.1/` (u otro
   directorio de build válido) con `build.ninja` y `CMakeCache.txt`.
b) Si existe, usar `ninja -C <build_dir>` directamente — jamás re-ejecutar
   cmake configure ni vcpkg bootstrap.
c) Si no existe build o hay error de compilación, consultar primero `README.md`
   y `docs/WORKING_CONTRACT.md` para las instrucciones exactas de build antes
   de probar comandos alternativos.
d) Esta regla tiene prioridad sobre cualquier impulso de "fresh config" o
   "preset por defecto".

### 5.6 Releases — Hard Rules
Cuando la tarea involucre preparar, empaquetar, versionar o publicar un release,
el agente debe leer y cumplir estrictamente la sección **Hard Rules for Agent Operators**
en `docs/releases/RELEASE_PROTOCOL.md`. Esas reglas están diseñadas para corregir
errores operativos documentados de sesiones anteriores y tienen prioridad sobre
cualquier otra política operativa.

Resumen de las reglas:
- **Ejecutar scripts, no inspeccionar prerequisitos** — si un gate dice
  `build_windows_installer.ps1`, se ejecuta, no se busca `ISCC.exe` manualmente.
- **Build siempre dentro del MSVC environment** — `vcvars64.bat` primero.
- **No marcar gates "skipped" sin ejecutar el script.**
- **Leer el script antes de asumir qué hace.**

## 6. Organización técnica deseada
- `src/core` → reglas y simulación
- `src/render` → renderizado
- `src/live` → eventos live y adaptadores
- `src/audio` → subsistema de audio
- `src/platform` → host, runtime y compatibilidad
- `tests` → validaciones
- `tools` → utilidades de apoyo

## 7. Política de dependencias
- Preferir dependencias justificadas y mantenibles.
- Evitar agregar una dependencia si C++ estándar o tooling existente basta.
- Toda dependencia nueva debe documentarse.

## 8. Política de agentes / skills
El agente puede usar:
- búsqueda en repositorio
- terminal
- CMake
- scripts PowerShell/Bash/Python/Node
- documentación local del proyecto
- el tool `skill` para cargar skills automáticamente

### 8.1 Activación automática de skills
El agente evalúa cada solicitud y activa los skills aplicables SIN que el usuario los pida explícitamente:

| Si la tarea es... | Activar skill(s) |
|---|---|
| Diseñar una feature, proyecto o cambio grande | `spec-driven-development` |
| Planificar la implementación | `planning-and-task-breakdown` |
| Implementar código (1+ archivos) | `incremental-implementation` + `test-driven-development` |
| Arreglar un bug o error | `debugging-and-error-recovery` + `test-driven-development` |
| Revisar código antes de merge | `code-review-and-quality` |
| Evaluar seguridad | `security-and-hardening` |
| Simplificar código existente | `code-simplification` |
| Preparar release/deploy | `shipping-and-launch` + `ci-cd-and-automation` |
| Documentar decisión arquitectónica | `documentation-and-adrs` |
| Cambiar flujo git o versionado | `git-workflow-and-versioning` |

Si un skill aplica, el agente DEBE cargarlo con el tool `skill` y seguir sus pasos. No saltarse workflows.

### 8.2 Personas especializadas
Para revisiones profundas el agente puede activar una persona desde `agents/<name>.md`:

- `code-reviewer` — revisión 5 ejes (Staff Engineer)
- `test-engineer` — estrategia de tests y coverage
- `security-auditor` — vulnerabilidades y threat modeling
- `web-performance-auditor` — Core Web Vitals

### 8.3 Creación de nuevos skills
El agente debe crear skills o subagentes solo si aportan valor real, por ejemplo:
- migración de protocolo
- auditoría de integración live
- generador de scaffolds de juego
- validador de contratos C++ ↔ web

### 8.4 Referencias rápidas
Checklists disponibles en `skills/references/` para consulta durante cualquier skill.

La capa operativa del proyecto queda documentada en:
- `agents/definitions/AGENT_MAP.md`
- `agents/routing/ROUTING_POLICY.md`
- `skills/SKILL_CATALOG.md`
- `agents/code-reviewer.md` — persona de revisión
- `agents/security-auditor.md` — persona de seguridad
- `agents/test-engineer.md` — persona de testing
- `agents/web-performance-auditor.md` — persona de performance
- `docs/releases/RELEASE_POLICY.md`
- `docs/runbooks/BACKUP_POLICY.md`
- `docs/agent-skills-orchestration.md` — reglas de orquestación multi-persona

## 9. Entregables por etapa
Cada etapa debe terminar con:
- resumen de lo hecho
- archivos cambiados
- riesgos
- pasos de validación
- siguiente bloque de trabajo sugerido

## 10. Prohibiciones estrictas
- No inventar resultados de test.
- No borrar código heredado útil sin justificarlo.
- No mezclar core y render en la misma responsabilidad si puede evitarse.
- No introducir cambios grandes sin dejar una ruta de reversión o comparación.

## 11. Estado del release (baseline actual)

- Versión en `master`: **0.3.2** (2026-09-20). Tag público más reciente: `v0.3.1`
  (0.3.2 se publicó oficialmente con Fase 3 completa + Fase 5 motor visual).
- Artefactos 0.3.2: `dist/releases/0.3.2/installer/panel-live-0.3.2-win-x64.exe`,
  `panel-live-0.3.2-win-x64-portable.zip`, `NisojeStudio\NisojeStudio.exe`,
  `SHA256SUMS.txt` y `release-manifest-0.3.2.json` (gates
  build/tests/installer/backup = `passed`).
- El panel de escritorio (`Panel Live 3.0.lnk` → `panel_desktop_launcher.py`)
  arranca el `NisojeStudio.exe` **más reciente** de `dist/releases/*/NisojeStudio/`
  y `build/release-*/src/platform/` (ojo: el glob es `release-*`, así que
  `build/release/` **no** entra). Tras 0.3.2 arranca el binario con motor visual.
- Rollback (Gate 6.3): dejar que el launcher vuelva a 0.3.1 borrando o
  renombrando `dist/releases/0.3.2/`; el código puede volver con
  `git checkout -- <archivo>` y el sitio con `git revert` en `sitio/`.
  Backup previo al release:
  `C:\Users\Nisoje\Desktop\PanelLiveBackups\Panel live 3.0-code-2026-09-20_09-51-13`.
- Para lanzar un release, ejecutar el script (Hard Rules de
  `docs/releases/RELEASE_PROTOCOL.md`), cargando ANTES el entorno MSVC:

  ```powershell
  cmd /c "call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" >nul && powershell -ExecutionPolicy Bypass -File .\scripts\release\prepare_release.ps1 -Version X.Y.Z -BackupMode code"
  ```

- `-BackupMode full` falla en este equipo (robocopy exit 8 por archivos
  bloqueados); usar `code` y registrar el desvío si el protocolo pide `full`.
- El release sincroniza `tools/bridge_py/*.py` hacia el paquete.
- **Nota sobre el launcher**: el glob `release-*` de `discover_versioned_panel_executables()`
  sí incluye `build/release/src/platform/NisojeStudio.exe` como fallback, por lo que
  el launcher del escritorio detecta automáticamente 0.3.2 desde `dist/releases/`.

