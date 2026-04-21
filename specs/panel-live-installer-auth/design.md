# Technical Design

## Overview

La base actual ya valida el punto de partida:

- `cmake --build --preset release` sin trabajo pendiente
- `ctest --preset release` con 26/26 tests aprobados
- `scripts/package_windows.ps1 -SkipBuild` genera `dist\NisojeStudio\` y `dist\NisojeStudio-portable.zip`

Eso confirma que el host y el package portable estan listos para servir como capa base del instalador. El trabajo nuevo se concentra en dos brechas: acceso/licencia obligatorio y authoring de instalador Windows.

## DESIGN SPECIFICATION

1. Purpose Statement: La pantalla de acceso debe bloquear el panel sin generar friccion innecesaria y dejar claro que la aplicacion valida la cuenta y la licencia antes de habilitar herramientas de live. Debe sentirse parte del producto actual, pero mas ceremonial y confiable que un formulario tecnico plano.
2. Aesthetic Direction: Industrial/utilitarian
3. Color Palette:
   - `#09111D`
   - `#101A2B`
   - `#1A2A44`
   - `#22C55E`
   - `#F59E0B`
4. Typography:
   - `"Bahnschrift"`
   - `"Segoe UI"`
   - `"JetBrains Mono"`
5. Layout Strategy: Overlay de bloqueo a pantalla completa con dos columnas; una columna estrecha de marca y estado, otra columna principal con el formulario y mensajes de validacion. El fondo rompe la simetria con una capa de panel diagonal y un bloque lateral de identidad para evitar un modal centrado generico.

## Architecture

### Access Flow

Se agrega una capa de autenticacion local en el host C++:

- `ServerLicenseService`
  - mantiene el `LicenseSnapshot`
  - guarda el estado de sesion autenticada del proceso
  - valida email/password contra Firebase Identity Toolkit via REST
  - consulta `https://nisoje-api.nisojestudio.workers.dev/api/me/licenses?firebase_uid=...`
  - confirma que la licencia ingresada exista y este activa

- `PanelApp`
  - reemplaza el servicio de licencia local por el servicio remoto/configurable
  - expone login/logout y estado de acceso
  - bloquea acciones protegidas cuando la sesion no esta autenticada

- `PanelHttpServer`
  - agrega rutas locales:
    - `POST /api/auth/login`
    - `POST /api/auth/logout`
  - deja `GET /api/state` como endpoint libre para que el frontend renderice el estado bloqueado
  - rechaza comandos mutables si el runtime no esta autenticado

- `PanelSnapshot` y `panel_http_json`
  - agregan `auth` al payload para que la UI sepa si debe mostrar bloqueo, sesion activa o error previo

### Frontend Access UI

La UI local existente se mantiene, pero suma una capa superior de acceso:

- overlay inicial bloqueante
- formulario `email + password + license key`
- estado `validando`, `error`, `autenticado`
- boton de cierre de sesion
- deshabilitacion visual del dashboard mientras `auth.authenticated == false`

### Installer Workflow

Se construye una capa de staging dedicada al instalador:

1. `scripts/package_windows.ps1`
   - sigue siendo la base para producir el runtime portable

2. `scripts/stage_windows_installer.ps1`
   - toma el output portable
   - crea un staging minimo para el instalador
   - incorpora prerequisitos redistribuibles descargados oficialmente
   - incorpora icono/arte de instalacion

3. `installer/PanelLiveInstaller.iss`
   - usa Inno Setup
   - instala payload en `Program Files`
   - detecta y ejecuta prerequisitos faltantes
   - expone tarea opcional de icono de escritorio
   - muestra progreso nativo del wizard

4. `scripts/build_windows_installer.ps1`
   - orquesta package portable, staging y compilacion del instalador

## Minimal Payload Policy

El instalador incluira solo:

- `NisojeStudio.exe`
- `WebView2Loader.dll`
- `panel_config.json`
- `tools\bridge_py\...`
- `tools\game_bridge_py\...`
- prerequisitos redistribuibles necesarios para Windows limpio
- iconos/arte estrictamente usados por el ejecutable o el wizard

No se incluiran documentos operativos ni archivos de desarrollo en la instalacion final.

## Windows Prerequisites

El instalador cubrira:

- Microsoft Visual C++ Redistributable x64
- Microsoft Edge WebView2 Runtime

El chequeo sera condicional:

- si el runtime ya existe, se omite
- si falta, se instala en modo silencioso

## Executable Branding

Se agregara un recurso `.rc` para que `NisojeStudio.exe` use el icono oficial del proyecto. El mismo icono se reutilizara para el acceso directo y para el instalador.

## Validation Strategy

Se validara con evidencia ejecutada:

- build `Release`
- `ctest --preset release`
- `scripts/package_windows.ps1`
- `scripts/build_windows_installer.ps1`
- smoke del instalador generado, al menos verificando produccion del `.exe` final y contenido staged

## Risks

- La API publica visible en los HTML no expone un endpoint claro de activacion por dispositivo; por diseno se validara licencia activa perteneciente al usuario autenticado. Si luego aparece un endpoint de activacion dedicado, el contrato ya quedara encapsulado en `ServerLicenseService`.
- El compilador de instalador no esta presente inicialmente en la maquina, por lo que el workflow debe instalar o localizar Inno Setup durante la build.
- El arte de fondo del instalador puede requerir conversion de formato para el wizard de Inno Setup.
