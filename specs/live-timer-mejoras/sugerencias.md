# Live Timer — Catálogo de mejoras de fondo

Esto **no es un rediseño**. Es una lista de cosas que el timer hoy **no puede hacer**
y que sí importan en un directo, ordenadas por valor/coste, para que elijas cuáles
convertir en spec. El rediseño de la pantalla de configuración
(`specs/live-timer-fase4/`) queda como **facilitador opcional**: sólo sirve para que
estas mejoras se puedan configurar sin 46 controles encima. No es el objetivo.

Cada propuesta lleva: **qué cambia · por qué importa · qué toca · coste · riesgo**.
Los costes son XS (1 archivo, horas), S (1-2), M (3-5), L (varias sesiones).

---

## 0. Los huecos que explican por qué se queda corto

Evidencia leída en el código, no impresiones:

| # | Hueco | Evidencia |
|---|---|---|
| H1 | **Todo evento vale lo mismo, valga lo que valga.** Los regalos son `diamantes × time_per_gift_coin`, un multiplicador plano. Una rosa y una galaxia se comportan igual salvo por el número de diamantes | `live_timer_game.cpp:616-626` |
| H2 | **Las ráfagas de likes cuentan como un like.** `delta = time_per_like` ignora `event.magnitude`, que es cuántos likes trae el lote | `live_timer_game.cpp:601-605` vs `host_event.hpp:53` |
| H3 | **No distingue quién lo hace.** `is_subscriber`, `is_moderator` y `is_follower` existen en el actor y el timer **no los usa nunca** | `host_event.hpp:24-31`, sin uso en el juego |
| H4 | **Los popups no dicen quién.** `add_event_popup(icon, label, delta)` — sólo icono, "gift" y segundos. `LiveTimerRecentEvent` no tiene usuario | `live_timer_game.cpp:666-669`, `live_timer_game.hpp:32-39` |
| H5 | **No hay topes ni suelo.** Nada limita cuánto aporta un usuario, ni cuánto por minuto, ni hasta dónde puede bajar el reloj. Sí hay un `max_time_s` y un clamp de cordura a 1 año | `live_timer_game.cpp:645-659` |
| H6 | **No hay hitos.** Sólo existe "completado". No hay 25%, 50%, "últimos 60 s" con comportamiento propio | `live_timer_game.hpp:41-104` |
| H7 | **El sonido es un disparo por evento sin freno.** Un usuario regalando 100 coins = 100 sonidos | `add_sound_path` único, `live_timer_game.cpp` |

Estos siete huecos son los que hacen que el timer se sienta «de juguete» aunque la
pantalla esté ordenada.

---

## 1. Reglas por evento — donde está el negocio

### M1 · Nombre en el popup y en el feed
**Qué cambia.** Los popups pasan de `🎁 gift +5s` a `🎁 Rosa ×5 · musitogamer +25s`.
El modelo de evento reciente gana `actor_name` (y `actor_id`), el overlay los pinta y
el feed del panel muestra quién.
**Por qué importa.** Es lo que convierte el overlay en algo que el directo agradece:
hoy nadie sabe quién aportó. Coste mínimo, impacto inmediato.
**Toca:** `live_timer_game.{hpp,cpp}`, `overlay/live-timer.html`, UI (feed).
**Coste:** S · **Riesgo:** bajo (campo aditivo; el overlay tolera que falte).

### M2 · Likes por magnitud
**Qué cambia.** `delta = magnitude × time_per_like`, con un valor por defecto sensato
(`magnitude` vacío = 1). Hoy se pierde todo lo que pase de un like.
**Por qué importa.** Arregla un conteo que está mal, y hace que la regla de likes sea
útil de verdad (con 500 likes por lote, poner 0.01 s por like empieza a tener sentido).
**Toca:** `live_timer_game.cpp` + un campo nuevo opcional `time_per_like_batch`.
**Coste:** XS · **Riesgo:** medio — **cambia el comportamiento actual**; hay que
decidir si es opt-in o por defecto, y avisar en el CHANGELOG.

### M3 · Reglas por tipo de espectador
**Qué cambia.** Multiplicadores que ya tienen los datos: suscriptor ×2, seguidor ×1.5,
moderador ×0 (o lo que se configure). Tabla de 4 filas.
**Por qué importa.** Es la palanca clásica de monetización: regalar se convierte en la
forma de subir de nivel. Y el moderador que escribe en el chat no debería inflar el reloj.
**Toca:** `live_timer_game.{hpp,cpp}` (4 claves nuevas), UI (4 filas).
**Coste:** S · **Riesgo:** bajo (claves aditivas con default 1.0 neutro).

### M4 · Tramos de regalo por valor
**Qué cambia.** En vez de un multiplicador plano: una tabla de tramos
(`1-9 🪙 → +1 s`, `10-99 → +15 s`, `100+ → +5 min`) y, opcionalmente, excepciones por
nombre de regalo (`Rosa → +3 s`). El evento ya trae `gift_name` y `gift.value` sin usar.
**Por qué importa.** Es la diferencia entre "cada coin vale 0.5 s" y poder decir
"la galaxia vale 5 minutos". El directo se diseña alrededor del regalo grande.
**Toca:** `live_timer_game.{hpp,cpp}` (estructura de tramos + persistencia), UI (editor
de tabla), overlay no hace falta.
**Coste:** M · **Riesgo:** medio (nuevo formato de config; requiere versión de esquema
en el estado guardado y mantener el multiplicador antiguo como fallback).

### M5 · Topes, suelo y antispam
**Qué cambia.** Cuatro límites: por usuario (máx +60 s por minuto), por evento
(tope por regalo), global por minuto, y **suelo** del reloj (no baja de X). Con lista
de excepciones (moderador exento, suscriptor con tope doble).
**Por qué importa.** Hoy un usuario con un script puede llevar el reloj al clamp de un
año, y en el sentido contrario puede tumbarlo. Es la diferencia entre un juguete y una
herramienta que puedes dejar desatendida en un directo de 6 horas.
**Toca:** `live_timer_game.{hpp,cpp}` + UI. Sin cambios en el overlay.
**Coste:** M · **Riesgo:** bajo; es puramente limitante y por defecto puede venir
desactivado (`0 = sin tope`) para no cambiar nada.

### M6 · Contar suscripciones
**Qué cambia.** Verificar si el bridge emite el evento de suscripción de TikTok
(hoy `HostEventKind` no tiene `subscribe`) y, si lo emite, mapearlo a tiempo.
**Por qué importa.** Una suscripción es dinero real y hoy, si ocurre, el timer la
ignora por completo.
**Toca:** `host_event.hpp`, el codec del bridge, `live_timer_game.cpp`.
**Coste:** S si el bridge ya lo trae; M si hay que añadirlo al codec.
**Riesgo:** bajo. **Primero hay que verificarlo**, no darlo por hecho.

---

## 2. Información para el operador (deja de ser un formulario)

### M7 · Top contribuyentes de tiempo
**Qué cambia.** Ranking en vivo en el panel: quién ha aportado más segundos, con
cuántos eventos. Se puede exportar al final.
**Por qué importa.** Sirve para agradecer por nombre (lo que más engancha) y para
decidir a quién dar protagonismo. Hoy hay un feed de eventos sin agregación.
**Toca:** `live_timer_game.{hpp,cpp}` (agregado por actor), UI (lista).
**Coste:** M · **Riesgo:** bajo (memoria acotada a N usuarios; hay que decidir el N).

### M8 · Ritmo y proyección
**Qué cambia.** «+18 s en los últimos 60 s» y «a este ritmo el reloj aguanta 14 min»,
en el panel y opcionalmente en el overlay.
**Por qué importa.** Es la información que hace que el operador cambie de estrategia
en directo: si el reloj sube solo, sube la meta; si baja, mueve a la audiencia.
**Toca:** `live_timer_game.cpp` (ventana deslizante), UI, overlay si se quiere.
**Coste:** S · **Riesgo:** bajo.

### M9 · Aviso de «el reloj se muere»
**Qué cambia.** Con <60 s y sin eventos en 2 min, aviso en el panel.
**Por qué importa.** Es el momento exacto en el que el operador debe hablar. Hoy se
entera cuando el overlay ya muestra 00:00:00.
**Toca:** UI + `live_timer_game.cpp` (ya hay `activity_log` del panel).
**Coste:** XS · **Riesgo:** bajo.

---

## 3. Formatos con nombre, en vez de 46 campos

### M10 · Plantillas de formato
**Qué cambia.** «Extiende el live» (lo de hoy), «Meta de coins» (barra de progreso
hacia un objetivo, no cuenta atrás), «Cuenta atrás para sorteo» (tiempo fijo que los
regalos alargan), «Sudden death» (últimos 60 s con reglas y aspecto propios). Elegir
una pre-configura todo y, si hace falta, cambia el modo de render del overlay.
**Por qué importa.** Hoy cambiar de formato es reconfigurar a mano y rezar. Es la
mejora que más amplía lo que puedes hacer con el producto, no lo que se ve.
**Toca:** estructura de plantillas + persistencia + UI + overlay (el modo «meta»
necesita otra vista).
**Coste:** L (una plantilla ≈ S-M; se puede entregar de una en una).
**Riesgo:** alto si se hace de golpe; bajo si se empieza por «Meta de coins» con la
cuenta atrás intacta como modo por defecto.

### M11 · Hitos intermedios
**Qué cambia.** A 25/50/75%: mensaje corto en el overlay. Al 100%: banner + sonido
(que ya existe). Y «últimos 60 s» con su propio color/tamaño/sonido.
**Por qué importa.** Convierte un contador en una narrativa de directo. Es lo que la
gente mira.
**Toca:** `live_timer_game.{hpp,cpp}`, overlay (banner + disparadores), UI.
**Coste:** M · **Riesgo:** medio (no debe dispararse dos veces ni al restaurar estado).

---

## 4. Sonido

### M12 · Freno al sonido y un sonido por tipo
**Qué cambia.** Máximo N sonidos por segundo (con agrupación: 20 coins = un sonido),
sonido distinto por tipo de evento y prioridad (el completado pisa al tick).
**Por qué importa.** Hoy 100 coins son 100 pitidos. Es el motivo más común de que el
audio se vuelva inservible en directo.
**Toca:** `live_timer_game.{hpp,cpp}` + overlay (`applySounds`) + UI.
**Coste:** S · **Riesgo:** bajo.

### M13 · Biblioteca de sonidos incluida
**Qué cambia.** Sonidos listos para usar, con botón de escuchar, en vez de escribir
una ruta a mano. Se mantiene la ruta personalizada para quien quiera la suya.
**Por qué importa.** Hoy hay que tener el fichero y saber su ruta exacta. Es una
barrera tonta para algo que debería ser un clic.
**Toca:** assets + UI + overlay (servir el audio).
**Coste:** M · **Riesgo:** bajo.

---

## 5. Poder probarlo sin estar en directo

### M14 · Simulador de eventos
**Qué cambia.** Botones «simular 1 like / 20 likes / 1 rosa / 1 galaxia / 20 coins en
2 s / suscriptor» que inyectan eventos reales en el motor. Se ve el overlay, los
popups, los topes y el sonido **antes** de emitir.
**Por qué importa.** Hoy la única forma de comprobar una regla es emitir. Y con M1-M5
(multiplicadores, tramos, topes) probar se vuelve imprescindible, no un lujo.
**Toca:** UI + un endpoint de simulación (`POST /api/timer/simulate`) + el motor.
**Coste:** M · **Riesgo:** bajo, si el endpoint está detrás del mismo control de
acceso que el resto del panel y no se puede llamar desde fuera.

---

## 6. Análisis post-directo

### M15 · Informe de sesión
**Qué cambia.** Al terminar: segundos aportados por tipo de evento, cuántos eventos,
qué reglas **no se dispararon nunca** (p. ej. chat a 0 s = interacción gratis
desperdiciada), curva del reloj y cuántas veces se completó.
**Por qué importa.** Convierte la configuración en decisiones. Es el único camino para
saber si «0.5 s por coin» está bien o se queda corto.
**Toca:** `live_timer_game.cpp` (acumuladores por tipo), persistencia, UI/informe.
**Coste:** L · **Riesgo:** bajo (sólo lectura y agregados).

---

## 7. Robustez del overlay

### M16 · Escala relativa en vez de píxeles absolutos
**Qué cambia.** Tamaños en `vw/vh` (o un factor de escala configurable) en vez de
`px` para un lienzo de 1920×1080.
**Por qué importa.** Si el browser source de TikTok LIVE Studio no es exactamente
1920×1080, hoy el reloj sale enorme o minúsculo y hay que reconfigurar los tres
tamaños a mano. Es un fallo real, no una preferencia.
**Toca:** overlay + UI (mostrar la equivalencia en px para el tamaño de lienzo elegido).
**Coste:** S · **Riesgo:** medio (cambia cómo se ven los tamaños ya guardados; hay que
mantener el modo absoluto como opción).

### M17 · Anclaje y márgenes
**Qué cambia.** Elegir dónde vive el bloque (arriba/centro/abajo, izquierda/centro/
derecha) y sus márgenes, con 9 posiciones y un margen en %.
**Por qué importa.** Hoy el bloque está siempre centrado; si el streamer tiene su
cámara o sus alertas en el centro, el overlay le tapa contenido.
**Toca:** overlay + UI.
**Coste:** S · **Riesgo:** bajo.

---

## 8. Prioridad (impacto × coste)

| # | Mejora | Impacto | Coste | Cuándo |
|---|---|---|---|---|
| M2 | Likes por magnitud (arregla un conteo erróneo) | alto | XS | ya |
| M9 | Aviso «el reloj se muere» | medio | XS | ya |
| M1 | Nombre en el popup y el feed | **muy alto** | S | ya |
| M3 | Reglas por tipo de espectador | **muy alto** | S | ya |
| M12 | Freno al sonido + sonido por tipo | alto | S | ya |
| M16 | Escala relativa del overlay | alto | S | ya |
| M17 | Anclaje y márgenes | medio | S | pronto |
| M8 | Ritmo y proyección | alto | S | pronto |
| M5 | Topes, suelo y antispam | alto | M | pronto |
| M4 | Tramos de regalo por valor | **muy alto** | M | pronto |
| M7 | Top contribuyentes | alto | M | pronto |
| M11 | Hitos intermedios | alto | M | pronto |
| M14 | Simulador de eventos | alto | M | después |
| M13 | Biblioteca de sonidos | medio | M | después |
| M10 | Plantillas de formato | **muy alto** | L | por partes |
| M15 | Informe de sesión | medio | L | después |
| M6 | Suscripciones (verificar primero) | ? | S/M | investigar |

**Si sólo se pudieran hacer tres:** M1 (nombre), M3 (tipo de espectador) y M4 (tramos
de regalo). Con esas tres, el timer pasa de «cuenta atrás que suma segundos» a «reglas
de un formato de directo». M2 y M12 son casi gratis y quitan dos comportamientos que
hoy están mal o molestan.

---

## 9. Lo que NO propongo aquí (y por qué)

Este catálogo cubre **qué hace** el timer. Lo visual —diseños, marcos, tipografía y
efectos— está en **`visual.md`**, porque es capacidad de producto y no cosmética:
corrijo aquí un error de la primera versión de este documento, donde metí «temas
visuales» en la lista de lo que no proponía.

Lo que sigue sin proponerse en ninguno de los dos:

- Cambiar la paleta, los radios o las sombras **del panel**: ahí el design system ya
  funciona. Lo que falla es el overlay, no el panel.
- Reordenar los 46 controles como fin en sí mismo. Eso es `specs/live-timer-fase4/` y
  sólo tiene sentido **después** de aceptar mejoras, porque son ellas las que añaden
  controles que hay que colocar.
- Un editor de arrastrar y soltar del overlay: útil, pero `visual.md` V4/V17 cubren el
  90 % del caso con variantes de composición y anclaje, sin editor.
- Animaciones decorativas sin función (confeti permanente, ruido de fondo constante).

## 9.bis Relación entre los tres documentos

| Documento | Cubre |
|---|---|
| `sugerencias.md` (este) | **Qué hace** el timer: reglas, información, formatos, sonido, prueba, informes |
| `visual.md` | **Cómo se ve**: marcos, composición, tipografía, efectos, estados, galería de diseños |
| `../live-timer-fase4/` | **Dónde se configura**: la pantalla, al final y como consecuencia |

---

## 10. De propuesta a trabajo

Cada mejora elegida se convierte en su propio spec siguiendo la convención del repo
(`specs/<nombre>/design.md` + `tasks.md`), como se hizo con la Fase 3. Orden sugerido
para el primer bloque, porque comparten sitio en el motor y conviene hacerlos juntos:

1. **Bloque A — Reglas:** M2, M3, M5, M4, M1 (modelo de reglas + popups con nombre).
2. **Bloque B — Experiencia:** M9, M8, M7, M11 (información y narrativa).
3. **Bloque C — Robustez:** M12, M16, M17, M13 (sonido y overlay).
4. **Bloque D — Formato:** M10 por partes (empezando por «Meta de coins»), M14, M15.

Los bloques A y C son, de largo, los que más cambian el producto por lo que cuestan.
