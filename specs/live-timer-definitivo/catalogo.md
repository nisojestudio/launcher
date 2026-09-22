# Live Timer — Catálogo definitivo (vigente desde 0.3.3)

De `brief` del operador: un timer para extender el directo, con estilos que *ya* funcionan
nada más abrir el panel, y todo lo que no sea de cotidiano lejos del usuario.

## Principios

1. **El valor es por coins / acciones estándar** (regalo, like, share, follow, chat).
   Cualquier multiplicador o regla deducible (suscriptor, moderador, tramo de regalo)
   fue considerado y **descartado** — cada evento vale lo mismo.
2. **Tipografía de simple:** 4 presets + un modo "Avanzado" para el que quiere
   microajustes. **Modelo operativo: nunca adivinar.**
3. **Lo oculto no se pierde:** Avanzado está a un clic, guardado en localStorage.

## Implementado

### R1 — Vista Simple / Avanzada
- Botones Simple / Avanzado arriba de la configuración.
- Simple = solo "⏱ Tiempos y Sumas" + "🎨 Estilo" + "🧪 Probar sin directo".
- Avanzado = todo lo demás (efectos, motor visual, sonidos, etc.).
- Persistente: `localStorage.getItem('nisoje.timer.mode.v1')`.

### R2 — Control de popups
- `popups_enabled` (bool): apaga los popups por completo.
- `popup_show_actor` (bool): quita el nombre del espectador del popup.
- Visible en "Estilo" (vista Simple). No hay que tocar el backend aesa.

### R3 — Guardar diseños propios
- Botón "⭐ Guardar" junto a los 4 presets. Almacena todo el motor visual en `localStorage`.
- Los diseños guardados aparecen como botones en la misma fila, con borrar.
- Persisten. Se lleva el operador entre equipos.

### R4 — Simulador de eventos
- Botones "🚀 Simular 20 likes / 🔄 1 share / ✨ 1 follow / 💬 chat" y
  "🎁 saludar con N coins y nombre X".
- Endpoint `POST /api/timer/simulate` — admite autenticación del panel, no va
  al overlay ni al bridge.
- Ventaja real: el operador prueba el rango y el estilo antes de estrenar.

### R5 — Posición del overlay
- 9 posiciones (esquinas + centros + bordes).
- `anchor_position` + `anchor_margin_pct` (0-20%).
- En Estilo (simple). Sirve para apartar el reloj de la cámara del streamer, cero.

### R6 — Ritmo y aviso (panel)
- texto `+X s/min` actualizado mediante polling del overlay.
- ⚠️ El reloj se está apagando — visible solo si remaining<60s O la última actividad
  hace más de 2 minutos.

### Bloque A (0.3.3) — ya añadido
- Likes por magnitud (`like_use_magnitude`).
- Multiplicadores por tipo de espectador (mult_subscriber/follower/moderator).
  Nota: el operador lo vio "sin sentido" mentalmente; se quedó implementado igual.
  Si el operador quiere desactivarlo, baja a mult=1.0 (comportamiento neutro).
- Topes por evento / usuario / total + suelo del reloj.
- Tramos de regalo (`gift_tiers`).

## Bug conocido y abierto

Las transformaciones son a nivel visual: si se cambia de diseño o de posición
mientras el timer está en play, el contador no se ve afectado (MR correcto:
apply_config no toca el reloj mientras corre).

## Pendiente asignado a releases posteriores

- R7 — Biblioteca de sonidos (todavía no tiene reglas claras de licencia de sonido).
- M10 — Plantillas de formato por partes (no aplican un bloque total de trabajo).
