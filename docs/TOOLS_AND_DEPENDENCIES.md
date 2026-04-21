# Herramientas y dependencias

## Requeridas
### Base
- Git
- VS Code
- CMake 3.27+
- Ninja
- Python 3.11+
- Node.js 20+

### C++
- Compilador compatible con C++20
  - Windows: Visual Studio 2022 Build Tools o MSVC
  - Linux: clang++ 16+ o g++ 13+
- vcpkg

## Recomendadas
- Emscripten SDK
- LLVM/Clang tools
- ccache o sccache
- Python venv
- pnpm o npm

## Dependencias previstas del proyecto
### C++ iniciales
- fmt
- spdlog
- nlohmann-json
- Catch2

### Tooling JS
- eslint
- prettier
- playwright (mas adelante, si se requieren pruebas UI/e2e)

### Tooling Python
- websockets
- requests
- pydantic (si el bridge nuevo lo necesita)

## Decisiones iniciales
- El proyecto debe poder construir el nucleo sin necesitar todavia Emscripten.
- WASM sera un target posterior, no un bloqueo del dia 1.
- El bridge legado puede convivir con el nuevo nucleo durante la transicion.

## Estado actual de runtime y packaging
- El producto es Windows-first por ahora; el build y la CI minima se validan primero en Windows.
- El camino de packaging/release usa un host C++ `Release` dedicado en `build/release`; no debe depender de CRTs Debug como `MSVCP140D.dll`, `VCRUNTIME140D.dll`, `VCRUNTIME140_1D.dll` o `ucrtbased.dll`.
- La politica actual de distribucion del host C++ acepta `Microsoft Visual C++ Redistributable x64` como prerrequisito oficial del portable Windows; no es una dependencia oculta y debe quedar visible para quien distribuya el ZIP.
- El packaging genera `PORTABLE_REQUIREMENTS.txt` en la raiz del package para dejar explicitos los prerrequisitos del host y de la UI embebida.
- `panel_config_storage.cpp` usa `nlohmann-json`, ya declarada en `vcpkg.json`, para lectura/escritura robusta de `panel_config.json`.
- `tools/bridge_py/.venv` se considera un runtime local soportado del bridge heredado, no basura accidental.
- El package portable de Windows ya no distribuye `.venv`; genera `tools/bridge_py/python_runtime` con el Python embebido oficial de Windows y los `site-packages` del bridge.
- El package portable tambien copia `tools/game_bridge_py` para lanzar juegos externos locales como `Arena Live`.
- `tools/bridge_py/.venv` sigue siendo la fuente de build local para preparar ese runtime portable.
- El packaging limpia `__pycache__` y `*.pyc` del runtime Python redistribuible antes de cerrar el package.
- Overrides soportados del bridge:
  - `LIVEPANEL_TIKTOK_PYTHON_EXE`
  - `LIVEPANEL_TIKTOK_RUNNER_SCRIPT`
  - `LIVEPANEL_LEGACY_BRIDGE_ROOT`
- Resolucion del runtime Python del bridge:
  - `LIVEPANEL_TIKTOK_PYTHON_EXE`
  - `tools/bridge_py/python_runtime/python.exe`
  - `tools/bridge_py/.venv/Scripts/python.exe`
  - `python` en `PATH`
- Resolucion del launcher del bridge de juegos externos:
  - `NLP3_LOCAL_GAME_BRIDGE_PYTHON_EXE`
  - `tools/bridge_py/python_runtime/python.exe`
  - `tools/bridge_py/.venv/Scripts/python.exe`
  - `python` en `PATH`
- Resolucion del catalogo local de juegos externos:
  - `NLP3_LOCAL_GAMES_ROOT`
  - `%USERPROFILE%\Desktop\Juegos`
- Resolucion por defecto de `panel_config.json`:
  - junto al ejecutable cuando existe
  - fallback al `cwd` actual
- Prerrequisitos oficiales del portable final:
  - `Microsoft Visual C++ Redistributable x64`
  - `Microsoft Edge WebView2 Runtime`
  - puertos locales libres para el host HTTP y el bridge
