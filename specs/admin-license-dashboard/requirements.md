# Requirements Document

## Introduction

Este bloque agrega un dashboard admin operativo para gestionar usuarios registrados desde la web, emitir y controlar licencias por duración, y enviar correos de activación con los datos necesarios para usar Panel Live.

## Requirements

### Requirement 1 - Registro sincronizado con backend

**User Story:** Como operador de Panel Live, quiero que los usuarios registrados desde la web aparezcan en la base operativa para poder asignarles licencias sin procesos manuales.

#### Acceptance Criteria

1. While a visitor creates an account with email/password or Google on the public access site, when authentication succeeds, the web admin flow shall registrar o actualizar el perfil del usuario en la base operativa mediante la API.
2. While an authenticated user already exists in the operational database, when the sync flow runs again, the system shall actualizar correo y nombre visibles sin duplicar registros.
3. When the sync flow cannot reach the API, the system shall mostrar un error claro y mantener trazabilidad del fallo para soporte.

### Requirement 2 - Dashboard admin con listado de usuarios

**User Story:** Como administrador, quiero ver una tabla clara de usuarios registrados para poder localizar cuentas, revisar su estado y operar licencias rápidamente.

#### Acceptance Criteria

1. While the admin is authenticated with the configured administrator account, when the admin page loads, the system shall mostrar una lista de usuarios registrados con nombre, correo, UID, estado de licencia y resumen operativo.
2. When the admin applies a search or filter, the system shall reducir la tabla por nombre, correo, UID o clave de licencia sin recargar toda la página.
3. When the admin selects a user, the system shall mostrar detalle de la cuenta, licencias asociadas, duración, expiración y dispositivos.

### Requirement 3 - Emisión y control de licencias

**User Story:** Como administrador, quiero crear y administrar licencias por duración para poder habilitar o deshabilitar acceso de forma simple.

#### Acceptance Criteria

1. When the admin creates a license for a user, the system shall permitir elegir duración predefinida de 1 día, 2 días, 7 días, 15 días, 30 días, 90 días o una cantidad personalizada de días.
2. When the admin creates a license, the system shall generar o aceptar una clave de licencia, calcular `expires_at`, dejarla activa y devolver el registro creado.
3. When the admin deactivates a license, the system shall marcarla como inactiva sin borrar historial.
4. When the admin reactivates a license, the system shall devolverla al estado activo y recalcular expiración si la duración fue cambiada.
5. While a user has varias licencias, when the admin views the account, the system shall mostrar todas las licencias con estado, duración, límite de dispositivos y expiración.

### Requirement 4 - Acciones rápidas por fila

**User Story:** Como administrador, quiero operar desde la misma fila del usuario para no perder tiempo entrando a pantallas secundarias.

#### Acceptance Criteria

1. When the admin reviews the users table, the system shall ofrecer acciones directas para crear licencia, activar, desactivar, copiar clave y enviar correo.
2. When a row action is executing, the system shall bloquear solo los controles de esa operación y mostrar feedback inmediato.
3. When an action completes, the system shall refrescar los datos del usuario y los indicadores globales sin perder el contexto de búsqueda actual.

### Requirement 5 - Correo de activación

**User Story:** Como administrador, quiero enviar al cliente sus datos de activación para reducir trabajo manual y errores de soporte.

#### Acceptance Criteria

1. When the admin sends an activation email, the system shall enviar correo al email del usuario con nombre, licencia, fecha de expiración, enlace al instalador y pasos básicos de activación.
2. While the outbound email provider is not configured, when the admin tries to send the activation email, the system shall responder con un mensaje claro indicando que faltan credenciales de correo.
3. When the activation email is sent successfully, the system shall mostrar confirmación visible y conservar una previsualización reutilizable del mensaje.

### Requirement 6 - Seguridad del backoffice

**User Story:** Como propietario de la plataforma, quiero que el panel admin esté protegido para que nadie sin permisos pueda consultar o modificar licencias.

#### Acceptance Criteria

1. When a request reaches an admin API route, the system shall exigir `Authorization: Bearer <Firebase idToken>`.
2. When the authenticated Firebase account does not match the configured administrator email, the system shall devolver acceso denegado.
3. When a public page calls an authenticated user route, the system shall usar el token de Firebase para obtener identidad de forma consistente con el backend del panel desktop.
