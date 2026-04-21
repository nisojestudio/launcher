# Requirements Document

## Introduction

Este bloque convierte el packaging portable actual de Nisoje Studio en una entrega instalable para Windows 10/11, con validacion obligatoria de usuario, contrasena y licencia antes de habilitar el panel. El objetivo es producir un instalador distribuible que prepare una instalacion limpia, incluya solo lo necesario para operar el panel y cubra prerrequisitos del host Windows.

## Requirements

### Requirement 1 - Aptitud real para empaquetado

**User Story:** Como operador del proyecto, quiero saber si la base actual esta lista para empaquetado antes de construir el instalador para no distribuir una build inestable.

#### Acceptance Criteria

1. While the repository is prepared for Windows packaging, when the release validation starts, the Nisoje LivePanel packaging workflow shall execute a real Release build.
2. While the release validation starts, when test coverage is available, the Nisoje LivePanel packaging workflow shall execute the existing automated test suites and record the result.
3. While the release validation starts, when the current portable packaging script is available, the Nisoje LivePanel packaging workflow shall generate the portable artifact and record whether it succeeds.
4. While validation evidence exists, when the aptitude report is produced, the Nisoje LivePanel packaging workflow shall distinguish verified readiness from missing installer-specific capabilities.

### Requirement 2 - Acceso obligatorio al panel

**User Story:** Como usuario final, quiero que el panel solicite mis credenciales y licencia al iniciar para que solo usuarios validados puedan usar la aplicacion.

#### Acceptance Criteria

1. While the desktop panel starts, when the initial UI becomes visible, the Nisoje LivePanel host shall block panel interactions until the access flow is resolved.
2. While access has not been validated, when the user opens the panel, the Nisoje LivePanel UI shall show a startup popup asking for email, password and license key.
3. While the user submits credentials, when the remote validation starts, the Nisoje LivePanel access flow shall verify the email and password against the configured Firebase project.
4. While the email and password are valid, when license validation runs, the Nisoje LivePanel access flow shall verify that the entered license key belongs to the authenticated account and is active in the Nisoje Studio API.
5. While the access flow is rejected, when the backend returns an error or the credentials do not match, the Nisoje LivePanel UI shall keep the panel locked and show a clear error message.
6. While access is validated, when the session is opened, the Nisoje LivePanel runtime shall mark the local process as authenticated and expose the validated license state through the panel snapshot.
7. While the local runtime is not authenticated, when protected panel actions are requested, the Nisoje LivePanel HTTP API shall reject game, control and runtime mutation commands.

### Requirement 3 - Configuracion de integracion de acceso

**User Story:** Como mantenedor del producto, quiero que la configuracion de acceso y licensing quede trazable y ajustable sin recompilar para poder adaptarla al backend real.

#### Acceptance Criteria

1. While the product configuration is saved, when auth settings are written, the Nisoje LivePanel config storage shall persist the remote auth and licensing endpoints needed by the startup popup.
2. While the product starts, when auth settings exist in config, the Nisoje LivePanel runtime shall load them and use them as the source of truth for access validation.
3. While the UI reads runtime state, when auth is required, the Nisoje LivePanel state payload shall expose enough auth metadata for the frontend to render the locked or unlocked state without guessing.

### Requirement 4 - Instalador Windows distribuible

**User Story:** Como distribuidor, quiero un instalador `.exe` para Windows 10/11 que despliegue el panel y sus dependencias para poder instalarlo en equipos limpios.

#### Acceptance Criteria

1. While the installer build starts, when the release payload is staged, the Nisoje LivePanel installer workflow shall package only the runtime files required to execute the panel on a clean machine.
2. While the installer runs, when the target machine is missing a required dependency, the Nisoje LivePanel installer shall install the bundled prerequisite silently before finishing the setup.
3. While the installer runs, when a required dependency is already present, the Nisoje LivePanel installer shall skip reinstalling that dependency.
4. While the installer runs, when the user reaches shortcut options, the Nisoje LivePanel installer shall offer creation of a desktop icon as an explicit user choice.
5. While the installer runs, when files and prerequisites are being deployed, the Nisoje LivePanel installer shall show a visual installation progress window.
6. While the installer finishes successfully, when the payload is installed, the Nisoje LivePanel installer shall leave the application ready to launch on Windows 10 and Windows 11.

### Requirement 5 - Identidad visual de la instalacion

**User Story:** Como usuario final, quiero ver la identidad de Nisoje Studio en el instalador y la app para reconocer facilmente el producto instalado.

#### Acceptance Criteria

1. While the Windows executable is built, when the application metadata is embedded, the Nisoje LivePanel app shall include the provided project icon as its Windows icon resource.
2. While the installer is generated, when wizard visuals are prepared, the Nisoje LivePanel installer shall use the provided icon and available artwork from `C:\Users\Nisoje\Desktop\nisoje-studio`.

### Requirement 6 - Entrega y trazabilidad

**User Story:** Como responsable del despliegue, quiero un informe final detallado con archivos, validacion y riesgos para revisar exactamente lo entregado.

#### Acceptance Criteria

1. While the work finishes, when the delivery report is written, the Nisoje LivePanel workflow shall list the changed or produced files.
2. While the work finishes, when validation evidence exists, the Nisoje LivePanel workflow shall state exactly which builds, tests and packaging commands were executed.
3. While residual risks remain, when the delivery report is produced, the Nisoje LivePanel workflow shall call out known limitations and follow-up recommendations explicitly.
