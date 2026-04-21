# Implementation Plan

- [x] 1. Sanear y preparar el Worker actual
  - Eliminar bloques duplicados de `/api/admin/license-by-key` en `worker.js`
  - Aislar helpers comunes de respuesta JSON, resolucion de usuario y consulta de licencias
  - Preparar bindings y nombres esperados para `DB` y `R2`
  - _Requirement: 2, 5_

- [x] 2. Endurecer autenticacion del Worker
  - Agregar validacion de `Authorization: Bearer <firebase_id_token>`
  - Resolver `firebase_uid` desde claims del token para rutas protegidas
  - Mantener compatibilidad temporal con `firebase_uid` por query string en `/api/me/licenses`
  - _Requirement: 2, 5_

- [x] 3. Exponer el catalogo remoto licenciado
  - Implementar `GET /api/me/games/catalog` en el Worker
  - Leer `catalog/latest.json` desde R2 o desde el origen configurado del catalogo
  - Filtrar juegos segun licencias activas y adjuntar URL firmada de descarga
  - _Requirement: 2_

- [x] 4. Completar metadatos y estructura del catalogo en R2
  - Subir `catalog/latest.json` usando el JSON ejemplo validado
  - Documentar la convencion de versionado por `game_id/version`
  - Verificar que cada objeto tenga hash SHA-256 consistente con el package publicado
  - _Requirement: 1, 2_

- [x] 5. Conservar el token y el contexto de licencia en el panel
  - Extender `ServerLicenseService` para retener `idToken` ademas de `firebase_uid`
  - Exponer al host el contexto autenticado necesario para consultar catalogo remoto
  - Mantener el flujo de login actual sin romper la pantalla de acceso
  - _Requirement: 2, 5_

- [x] 6. Crear el servicio remoto de distribucion en el host
  - Añadir `RemoteGameDistributionService` en `src/platform`
  - Consultar el endpoint `/api/me/games/catalog`
  - Persistir estado de descargas e instalaciones administradas
  - _Requirement: 2, 3, 5_

- [x] 7. Crear el registro local de instalaciones administradas
  - Persistir `remote_game_installs.json` bajo `%LOCALAPPDATA%\\NisojeStudio\\state`
  - Registrar `game_id`, version, hash, install root y fecha de instalacion
  - Permitir reconstruir el estado del catalogo despues de reiniciar el panel
  - _Requirement: 3, 4, 5_

- [x] 8. Implementar descarga, verificacion y extraccion local
  - Descargar ZIP a `%LOCALAPPDATA%\\NisojeStudio\\downloads`
  - Calcular y verificar SHA-256 antes de instalar
  - Extraer el paquete a `%LOCALAPPDATA%\\NisojeStudio\\games\\<gameId>\\<version>`
  - Validar `module_manifest.json` y entry executable antes de registrar la instalacion
  - _Requirement: 3, 4_

- [x] 9. Integrar el catalogo remoto al catalogo actual del panel
  - Crear una fuente fusionada para built-in, locales externos y remotos
  - Reflejar `installed`, `enabled`, `source` y version en la respuesta del panel
  - Preparar estado futuro de `update available` sin romper juegos ya instalados
  - _Requirement: 2, 4_

- [x] 10. Exponer endpoints locales del panel para descarga e instalacion
  - Implementar `POST /api/game/download`
  - Exponer estado de instalacion dentro de `/status` y/o un endpoint dedicado
  - Rechazar descargas cuando la sesion no este autenticada
  - _Requirement: 3, 5_

- [x] 11. Actualizar la UI del panel
  - Reemplazar el placeholder actual del boton `Descargar`
  - Mostrar estados `Descargando`, `Verificando`, `Instalando` y `Error`
  - Refrescar el catalogo despues de una instalacion exitosa
  - _Requirement: 3, 4, 5_

- [ ] 12. Validar end to end
  - Probar login real y carga del catalogo remoto
  - Probar descarga e instalacion de `Arena Live` desde R2
  - Verificar que el juego instalado aparezca como `Instalado` y pueda iniciarse
  - Documentar riesgos residuales y tiempos de expiracion de URLs firmadas
  - _Requirement: 1, 2, 3, 4, 5_
