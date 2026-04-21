# Requirements Document

## Introduction

Este bloque define la primera integracion remota para distribucion privada de juegos de Nisoje LivePanel 3.0. El objetivo es publicar paquetes limpios de juegos en Cloudflare R2, exponer un catalogo licenciado desde el backend y permitir que el panel descargue, verifique, instale y ejecute esos juegos en Windows sin exponer codigo fuente ni depender de carpetas manuales en el escritorio.

## Requirements

### Requirement 1 - Publicacion privada de paquetes

**User Story:** Como operador de contenido, quiero subir juegos compilados a almacenamiento privado para distribuirlos sin publicar carpetas fuente ni archivos de desarrollo.

#### Acceptance Criteria

1. While a game is prepared for cloud distribution, when the release package is generated, the Nisoje LivePanel game distribution workflow shall publish only the compiled package, integrity metadata and minimal catalog metadata required for install and execution.
2. While a game package is published, when the target storage is Cloudflare R2, the Nisoje LivePanel distribution workflow shall store objects in a private bucket layout grouped by game identifier and version.
3. While a game package is published, when the package contains runtime content, the Nisoje LivePanel distribution workflow shall exclude source folders, tests, development logs, temporary files, WebView cache folders and debug binaries that are not required for gameplay.
4. While a game package is published, when integrity metadata is produced, the Nisoje LivePanel distribution workflow shall associate each package with a SHA-256 hash that the panel can verify before install.

### Requirement 2 - Catalogo remoto licenciado

**User Story:** Como usuario autenticado, quiero ver solo los juegos que mi licencia permite descargar para que el panel no exponga contenido no autorizado.

#### Acceptance Criteria

1. While a user session is authenticated, when the panel requests the remote game catalog, the Nisoje LivePanel backend shall validate the user identity and license before returning downloadable items.
2. While the backend returns a catalog, when a game is available to the current license, the Nisoje LivePanel backend shall include game identifier, display name, version, install metadata, integrity hash and a time-limited download URL or download token.
3. While the backend returns a catalog, when a game is not licensed for the current user, the Nisoje LivePanel backend shall omit the downloadable package details for that game.
4. While the panel merges catalog sources, when a game is already installed locally, the Nisoje LivePanel host shall preserve its local install state while still exposing remote update metadata.

### Requirement 3 - Descarga y verificacion local

**User Story:** Como usuario final, quiero que el panel descargue e instale juegos de forma segura para no tener que copiar carpetas manualmente ni correr paquetes alterados.

#### Acceptance Criteria

1. While a licensed game is not installed, when the operator selects Download in the panel, the Nisoje LivePanel host shall start a local download workflow for that game.
2. While a package is downloading, when progress information is available, the Nisoje LivePanel UI shall show download and install status without blocking the rest of the host UI.
3. While the package download finishes, when the local file is complete, the Nisoje LivePanel host shall verify the downloaded file against the expected SHA-256 hash before extraction.
4. While package verification fails, when the hash does not match or the package is malformed, the Nisoje LivePanel host shall reject the install, preserve the previous game state and expose a clear error to the UI.
5. While package verification succeeds, when extraction starts, the Nisoje LivePanel host shall install the game into a managed local directory under the current Windows user profile instead of requiring a desktop folder.
6. While a matching version is already installed, when the operator requests download again, the Nisoje LivePanel host shall avoid redundant reinstall unless an explicit update or repair action is requested.

### Requirement 4 - Registro local y ejecucion

**User Story:** Como operador del panel, quiero que los juegos descargados aparezcan como juegos instalados normales para iniciarlos igual que los juegos locales ya conocidos.

#### Acceptance Criteria

1. While a remote package is installed successfully, when the local catalog refreshes, the Nisoje LivePanel host shall register the installed game through the same runtime catalog used for existing local games.
2. While a remote-installed game is present, when the operator starts it, the Nisoje LivePanel host shall resolve the game entry point, config files and runtime inbox paths without requiring manual edits outside the install directory.
3. While a remote-installed game is active, when the panel polls state, the Nisoje LivePanel UI shall present that game with the same installed and active states used for local external games.
4. While a remote package is superseded by a newer catalog version, when the host compares versions, the Nisoje LivePanel UI shall be able to distinguish installed, downloadable and update-available states.

### Requirement 5 - Operacion segura y trazable

**User Story:** Como mantenedor del sistema, quiero trazabilidad de catalogo, descargas e instalaciones para diagnosticar fallos y operar soporte tecnico.

#### Acceptance Criteria

1. While the remote catalog is consumed, when the host records diagnostics, the Nisoje LivePanel runtime shall expose enough metadata to identify the selected remote game version and its install source.
2. While a download or install attempt runs, when the workflow changes state, the Nisoje LivePanel host shall record operator-visible status messages and failure reasons that the UI can surface.
3. While a package is installed or updated, when files are staged locally, the Nisoje LivePanel workflow shall keep the managed installation layout deterministic so later support scripts can inspect or repair it.

## Proposed R2 Layout

The following object layout is the proposed baseline for the first private distribution bucket:

```text
games-catalog/
  catalog/
    latest.json
    games/
      arena_live/
        20260411-030455/
          arena_live-20260411-030455.zip
          arena_live-20260411-030455.sha256.txt
      conquista/
        20260411-030455/
          conquista-20260411-030455.zip
          conquista-20260411-030455.sha256.txt
```

`latest.json` should be returned by the licensing backend or mirrored from it, but package objects should remain private and be downloaded only through time-limited signed URLs.
