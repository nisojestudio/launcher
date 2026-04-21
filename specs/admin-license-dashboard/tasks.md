# Implementation Plan

- [x] 1. Crear la especificación del dashboard admin, sincronización de usuarios y gestión de licencias
  - Dejar requisitos, diseño y validación esperada
  - _Requirement: 1, 2, 3, 4, 5, 6_

- [x] 2. Endurecer el Worker para rutas admin autenticadas
  - Agregar validación admin por Bearer Firebase
  - Proteger todas las rutas `/api/admin/*`
  - _Requirement: 6_

- [x] 3. Mejorar el alta/sincronización de perfiles web
  - Hacer upsert en `/api/register-profile`
  - Sincronizar desde `registro.html` y `panel.html`
  - _Requirement: 1_

- [x] 4. Crear endpoints admin de dashboard y detalle
  - Listado agregado de usuarios
  - Detalle por usuario con licencias y dispositivos
  - _Requirement: 2, 4_

- [x] 5. Crear endpoints para licencias
  - Emitir licencia con duración configurable
  - Activar y desactivar licencia
  - _Requirement: 3, 4_

- [x] 6. Integrar envío de correo de activación
  - Conectar Worker a proveedor externo
  - Exponer mensaje claro si faltan credenciales
  - _Requirement: 5_

- [x] 7. Rediseñar `admin.html` como dashboard operativo
  - Tabla de usuarios
  - Panel de detalle
  - Formulario de licencia y acciones rápidas
  - _Requirement: 2, 3, 4, 5_

- [x] 8. Ajustar estilos compartidos
  - Mejorar tablas, paneles, métricas y estados
  - _Requirement: 2, 4_

- [x] 9. Validar el flujo completo
  - Registro web -> usuario en admin -> licencia -> cambio de estado -> correo
  - Validado con smoke sintáctico y smoke del Worker con DB simulada; envío real de correo quedó pendiente solo por ausencia de secretos `RESEND_*`
  - _Requirement: 1, 2, 3, 4, 5, 6_
