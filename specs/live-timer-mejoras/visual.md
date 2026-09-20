# Live Timer — Catálogo de capacidades visuales

## Estado de implementación (actualizado tras la Fase 5)

| # | Capacidad | Estado |
|---|---|---|
| M16 | Escala relativa al lienzo | **Implementado y verificado** (720p = 2/3 exacto del diseño) |
| V1 | Marco configurable (6 estilos) | **Implementado y verificado** (neón, tarjeta, cristal, cinta, insignia, ninguno) |
| V2 | Ornamentos: esquinas de bracket | **Implementado y verificado** |
| V3 | Fondo: rejilla técnica y scanlines | **Implementado y verificado** |
| V5 | Medidor de progreso (barra y anillo) | **Implementado y verificado** (ángulos exactos) |
| V6 | Formato del tiempo | **Implementado y verificado** (separador y horas) |
| V8 | Contorno del texto | **Implementado y verificado** |
| V9 | Efectos ambientales (7) | **Implementado** (falta medir rendimiento en tu equipo) |
| V11 | Partículas con presupuesto y auto-apagado | **Implementado y verificado** (escalera de FPS, tope, aviso sólo en preview) |
| V12 | Efectos de dígitos (8) | **Implementado** |
| V13 | Aspecto por estado | **Parcial**: umbrales y efecto de peligro configurables; falta estilo propio por estado (color de marco por estado) |
| V4 | Variantes de composición | Pendiente |
| V7 | Sistema de fuentes curado | **Parcial**: se añadieron Chakra Petch y Rajdhani y se verificó que cargan; falta el selector que muestra cada fuente |
| V10 | Entrada y salida | Pendiente |
| V14 | Galería de diseños | **Parcial**: 4 diseños predefinidos en la configuración; falta la galería con miniaturas generadas del overlay real |
| — | Color de borde independiente del relleno | Pendiente (`frame_border_color`): hace falta para «papel claro con borde negro» |

Convención de cada propuesta: **qué cambia · por qué importa · qué toca · coste · riesgo**.
Los costes son XS (1 archivo, horas), S (1-2), M (3-5), L (varias sesiones).

Segundo catálogo, hermano de `sugerencias.md` (que cubre reglas y operación). Aquí
sólo hay **capacidad visual nueva**: diseños, efectos, tipografía y marcos.

**Corrección de un error mío:** en `sugerencias.md` §9 metí «cambiar la paleta,
reordenar controles y temas visuales» en *lo que NO propongo*, como si el aspecto
fuera cosmética. No lo es: lo que hoy falta no es ordenar los controles, es que el
overlay **no puede hacer** más que tres textos con fuente, tamaño, color y negrita,
dos efectos (glow, pulso) y cuatro paletas. Eso es un hueco de producto.

## 0. Qué puede hacer el overlay hoy (medido en `overlay/live-timer.html`)

| Tiene | Detalle |
|---|---|
| 4 elementos de texto | `#title`, `#days-label`, `#counter`, `#subtitle`, más `#completed-banner` y la lane lateral `#event-container` |
| Estilo por elemento | `font_size_px`, `font_color`, `font_family`, `bold` — y nada más |
| 2 efectos | `.effect-pulse` (escala) y glow por `filter: drop-shadow`, por elemento, sobre color/intensidad globales |
| 5 animaciones de dígito | `flip`, `roll`, `pop`, `fade` y ninguna |
| 4 paletas | `neon-green`, `cyber-blue`, `clean-white`, `rose-gold`: sólo cambian los colores del contador por estado |
| Fuentes | 3 mono cargadas de Google Fonts (JetBrains, Space, Share Tech) + las del sistema |
| Fondo | Transparente, con `text-shadow` para que se lea encima del vídeo |

| NO tiene | Consecuencia |
|---|---|
| **Ningún marco ni contenedor** | El overlay es texto flotando sobre el vídeo; no hay tarjeta, caja, borde, cinta ni insignia. Es la razón número uno de que se vea «sin diseñar» |
| Ninguna variable de disposición | Siempre centrado, siempre apilado (día → título → reloj → subtítulo) |
| Ninguna tipografía real | Sólo hay un `<select>`; no hay pesos, ni tracking, ni contorno, ni números tabulares garantizados |
| Ningún efecto reactivo al evento | El «bump» del contador existe y **no es configurable**; no hay flash, sacudida ni destello al entrar una coin |
| Ningún estado visual | `running`, `paused`, `danger`, `completed` sólo cambian el color |
| Ninguna forma | No hay anillo, barra de progreso, ni medidor |

---

## 1. Marcos y contenedores (lo que más falta)

### V1 · Marco configurable
**Qué es.** Un contenedor real alrededor del bloque (`#overlay-card`) con 6 estilos
listos: **Tarjeta** (fondo sólido + esquinas redondeadas), **Cristal** (translúcido con
desenfoque), **Neón** (borde luminoso), **Cinta** (banda diagonal detrás del título),
**Insignia** (píldora compacta sólo con el reloj) y **Ninguno** (como hoy).
**Por qué importa.** Es la diferencia entre «texto encima del directo» y «un overlay».
Además resuelve legibilidad sobre cualquier fondo sin depender del `text-shadow`.
**Control:** estilo, opacidad del fondo, grosor del borde, radio, color del borde,
sombra, relleno interior y separación del bloque.
**Toca:** overlay (elemento + CSS) + modelo (~10 claves nuevas) + UI.
**Nota técnica:** el desenfoque es `backdrop-filter: blur()`, soportado por Chromium
(TikTok Studio y OBS lo son) pero con coste de GPU; se avisa en la UI y se puede bajar
el radio. El fondo transparente global se mantiene: el marco es un hijo, no el `body`.
**Coste:** M · **Riesgo:** medio, porque toca la estructura del overlay y hay que
comprobar que no rompe la lane de popups ni el banner de completado.

### V2 · Ornamentos y esquinas
**Qué es.** Sobre el marco: esquinas marcadas, líneas de bracket, muescas, o una
segunda línea fina interior. 4 variantes.
**Por qué importa.** Es lo que da carácter de «HUD» sin ser una imagen.
**Control:** variante + color + grosor.
**Coste:** S · **Riesgo:** bajo (puro CSS/SVG dentro del marco de V1).

### V3 · Fondo y textura
**Qué es.** Fondo del marco: sólido, degradado lineal o radial, rejilla técnica sutil,
o rayas tipo scanline. Cuatro opciones.
**Por qué importa.** Permite estéticas concretas (cyber, retro, limpio) sin tocar el vídeo.
**Nota técnica:** todo con `background-image` en CSS (degradados y `repeating-linear-gradient`),
sin imágenes, así que no hay que servir assets ni cargar nada.
**Coste:** S · **Riesgo:** bajo.

---

## 2. Disposición y composición

### V4 · Variantes de composición
**Qué es.** 5 formas de colocar lo mismo: **Apilado** (como hoy), **En línea**
(`TÍTULO · 12:34 · subtítulo` en una sola fila), **Reloj protagonista** (reloj enorme
y etiquetas pequeñas a los lados), **Etiquetas laterales** (título y subtítulo en
columna a la izquierda del reloj) y **Solo reloj** (para quien usa su propio gráfico).
**Por qué importa.** Es cambiar el diseño sin reconfigurar 20 campos, y es lo que hace
que el overlay encaje con el resto del stream.
**Control:** un selector de variante + alineación (izq/centro/der).
**Coste:** M · **Riesgo:** medio (hay que rehacer el CSS de los 4 elementos en cada
variante y verificar que el modo preview y las animaciones de dígito siguen bien).

### V5 · Anillo o barra de progreso
**Qué es.** Un medidor alrededor o debajo del reloj que muestra el tiempo restante
respecto al máximo (o respecto a un objetivo, si se acepta M10 «Meta de coins»).
Anillo con `conic-gradient`, o barra horizontal con marca de hitos.
**Por qué importa.** Da información de un vistazo que un número no da: se ve «queda
poco» sin leer. Es el elemento visual que más se echa en falta en un formato de meta.
**Control:** forma (anillo/barra), grosor, colores, y qué representa (máximo u objetivo).
**Coste:** M · **Riesgo:** medio (el anillo hay que animarlo sin recargar el layout;
`conic-gradient` + variable CSS es barato, pero hay que medirlo).

### V6 · Formato del tiempo
**Qué es.** Control del `00:00:00`: separador (`:` `·` `.` o espacio), mostrar u ocultar
horas, ceros a la izquierda sí/no, etiquetas (`h m s`), y el formato del distintivo de
días (`DÍA 2` / `2 días` / oculto).
**Por qué importa.** Hoy está fijado en el código. Muchos formatos quieren `12:34` o
`12m 34s`, no `00:12:34`.
**Toca:** overlay (`formatTime`) + modelo + UI.
**Coste:** S · **Riesgo:** bajo, pero hay que mantener compatibilidad con el formato
actual como opción por defecto.

---

## 3. Tipografía de verdad

### V7 · Sistema de fuentes curado
**Qué es.** Sustituir el `<select>` por una lista **cargada y garantizada**: 8-10
familias (3 mono técnicas ya presentes, 3 de display, 3 de texto), cada opción
renderizada con su propia fuente, con su peso y su muestra numérica.
**Por qué importa.** Hoy se puede elegir una fuente que el overlay no carga: se ve
distinta en el panel y en el directo, y eso es una trampa.
**Control:** familia, peso (400/700/900), tamaño, interletraje (tracking), espaciado de
dígitos (tabular sí/no), mayúsculas sí/no.
**Nota técnica:** las familias van por `@font-face`/Google Fonts con
`font-display: swap` y se declaran en la propia página del overlay, que ya carga tres.
Se puede ofrecer «añadir mi fuente» por ruta local para el operador avanzado.
**Coste:** M · **Riesgo:** bajo (aditivo; el campo actual `font_family` se mantiene).

### V8 · Contorno, sombra y relleno del texto
**Qué es.** Además del glow actual: contorno duro (`-webkit-text-stroke`), sombra
desplazada, y texto con degradado (`background-clip: text`).
**Por qué importa.** Es cómo se hace legible un reloj sobre vídeo claro sin meterlo en
una caja. Hoy sólo hay glow y un `text-shadow` fijo.
**Control:** contorno (grosor + color), sombra (x, y, desenfoque, color), y relleno
(sólido o degradado de 2 colores con ángulo).
**Coste:** S · **Riesgo:** bajo.

---

## 4. Efectos

### V9 · Efectos ambientales del elemento
**Qué es.** Ampliar de 2 a 7: **ninguno, glow, pulso, latido** (doble golpe), **flotar**,
**parpadeo neón**, **sacudida**. Todos con intensidad y velocidad.
**Por qué importa.** Con dos efectos, dos overlays distintos se parecen demasiado.
**Nota técnica:** todas son animaciones CSS de `transform`/`opacity`/`filter`
(compuestas en GPU) y ninguna debe tocar `width`/`height` para no recargar el layout.
**Coste:** S · **Riesgo:** bajo.

### V10 · Efectos de entrada y salida
**Qué es.** Cómo aparece y desaparece el bloque completo: fundido, subida, zoom, golpe
y **máquina de escribir** para el título.
**Por qué importa.** Hoy el overlay aparece de golpe al conectar. En un directo, la
entrada es parte del espectáculo.
**Coste:** S · **Riesgo:** bajo.

### V11 · Reacción al evento (configurable)
**Qué es.** Cuando entra una coin: destello del marco, rebote del contador (existe, sin
configurar), destello de color, y **confeti/partículas** en eventos grandes.
**Por qué importa.** Es el efecto que hace que el espectador regale otra vez: ve que su
regalo ha movido algo.
**Control:** por tamaño de evento (pequeño/medio/grande), elegir reacción y umbral.
**Nota técnica:** partículas con `<canvas>` 2D y `requestAnimationFrame`, dibujadas con
`fillRect`/`stroke` (sin imágenes), `devicePixelRatio = 1`, un solo canvas y **bucle
cancelado cuando no hay partículas vivas** (coste cero en reposo). Presupuesto por
defecto 120 simultáneas, interruptor y auto-apagado por FPS: especificado en
`disenos.md` §4. Recomiendo empezar por destello + rebote (CSS puro) y añadir las
partículas como paso separado, porque son la única pieza que puede costar frames de
verdad.
**Coste:** S para destello/rebote · M para partículas · **Riesgo:** medio en partículas
(puede comerse la GPU del encoder en OBS).

### V12 · Efectos de los dígitos
**Qué es.** Hoy hay 5 (`none/flip/roll/pop/fade`). Añadir **odómetro** (los dígitos
suben como un cuentakilómetros), **máquina de escribir**, **desenfoque** y **color
intermedio** al cambiar.
**Por qué importa.** El cambio de segundo es lo que más se ve en pantalla: pasa 3.600
veces por hora. Un buen efecto aquí vale más que uno en el marco.
**Coste:** S · **Riesgo:** bajo, pero hay que medir porque se dispara cada segundo.

---

## 5. Estados con vida propia

### V13 · Aspecto por estado
**Qué es.** Definir el look de **reposo, corriendo, pausa, peligro (≤10 s), aviso
(≤60 s) y completado** por separado: color del marco, color del texto, efecto y, si se
quiere, un texto propio («PAUSA», «ÚLTIMOS SEGUNDOS»).
Hoy sólo cambia el color del contador entre 4 paletas, y el peligro/aviso están fijados
en el código a 10 s y 60 s.
**Por qué importa.** Es lo que hace que el overlay comunique, no sólo muestre.
**Control:** umbrales de aviso y peligro configurables + estilo por estado.
**Coste:** M · **Riesgo:** medio (multiplicar estados × elementos; hay que acotar la
matriz para que no se vuelva inmanejable: estilo por estado sólo en marco, contador y
banner de completado, no en los 4 elementos).
**Aquí es donde encaja M11 (hitos intermedios)** del otro catálogo: los hitos son
estados nuevos con su propio aspecto.

---

## 6. La galería de diseños (la respuesta a «moderno y fácil»)

### V14 · Diseños listos para elegir
**Qué es.** En vez de contestar 30 preguntas de estilo, el operador abre una **galería
de diseños** con miniaturas reales (no nombres) y elige una. Cada diseño es un conjunto
coherente de marco + composición + tipografía + efectos + paleta + estados:
p. ej. **Neón Arcade**, **Cyber HUD**, **Cristal Limpio**, **Retro CRT**,
**Insignia Mínima**, **Cuenta Atrás de Sorteo**, **Meta de Coins**.
Después puede ajustar lo que quiera, y el diseño pasa a «Personalizado».
**Por qué importa.** Es literalmente lo que pediste: moderno y fácil de entender.
Nadie debería tener que saber qué es un «glow de 8 px» para tener un overlay bonito.
Y no es «lo mismo con otra cara»: cada diseño trae capacidades de las de arriba
(marco, anillo, variante de composición), no sólo colores.
**Cómo se hace bien:** cada diseño es un **archivo de datos** (JSON con las claves del
modelo), no código: así añadir un diseño no toca el motor y se pueden versionar. Las
miniaturas se generan renderizando el overlay real con la config del diseño en un
iframe pequeño, así que **nunca mienten**.
**Coste:** M el mecanismo + S por diseño (7 iniciales ⇒ M/L en total).
**Riesgo:** medio. El riesgo real es de diseño, no de código: si las 7 propuestas se
parecen entre sí, no sirve de nada. Mitigación: cada diseño debe diferir en marco,
composición y tipografía, no sólo en color; y hay que validarlos contigo antes de
implementar los siete.

---

## 7. Prioridad visual (impacto × coste)

| # | Capacidad | Impacto | Coste | Nota |
|---|---|---|---|---|
| V1 | Marco configurable | **muy alto** | M | Es el cambio que más «diseño» aporta |
| V14 | Galería de diseños | **muy alto** | M+L | Contesta «moderno y fácil»; depende de V1/V4/V7 |
| V7 | Sistema de fuentes curado | **muy alto** | M | Elimina la trampa de la fuente no cargada |
| V13 | Aspecto por estado | alto | M | Incluye umbrales de aviso/peligro configurables |
| V4 | Variantes de composición | alto | M | Cambia el diseño sin tocar 20 campos |
| V8 | Contorno, sombra, degradado | alto | S | Legibilidad real sobre vídeo claro |
| V9 | 7 efectos ambientales | alto | S | |
| V6 | Formato del tiempo | alto | S | Quite donde más se nota: `12:34` vs `00:12:34` |
| V12 | Efectos de dígitos nuevos | medio | S | Se ve 3.600 veces por hora |
| V10 | Entrada y salida | medio | S | |
| V3 | Fondo y textura | medio | S | |
| V2 | Ornamentos | medio | S | |
| V11 | Reacción al evento | **muy alto** | S+M | Empezar por destello + rebote, partículas al final |
| V5 | Anillo o barra | alto | M | Se multiplica si se acepta «Meta de coins» |

**Primer bloque recomendado (V1 + V7 + V9 + V8 + V6 + V12):** marco, fuentes de verdad,
más efectos, contorno/degradado y control del formato del tiempo. Es lo que cambia el
aspecto del overlay de forma perceptible sin tocar todavía la disposición ni los
estados, y todo es aditivo (nada de lo guardado se rompe).

**Segundo bloque (V4 + V13 + V5 + V11):** composición, estados, medidor y reacciones.
Aquí es donde el overlay empieza a «comunicar».

**Tercer bloque (V14 + V2 + V3 + V10):** la galería de diseños encima de todo lo
anterior, más los detalles.

## 8. Notas técnicas que condicionan todo lo visual

- **Nada de imágenes ni assets externos.** Todo el catálogo se puede hacer con CSS y
  SVG en línea; así el overlay no depende de ficheros que haya que servir por el túnel
  (que en Fase 3 quedó restringido a `/api/overlay/*`).
- **Presupuesto de rendimiento.** El overlay se compone sobre vídeo en vivo. Regla:
  animar sólo `transform`, `opacity` y `filter`; nunca `width`, `height` ni
  propiedades de layout. Cualquier efecto nuevo se mide con la preview y con el
  overlay real antes de darse por bueno.
- **Transparencia.** El `body` sigue transparente; marcos y fondos son hijos, para no
  tapar el vídeo entero.
- **Compatibilidad.** Todo lo propuesto (degradados, `backdrop-filter`,
  `conic-gradient`, `-webkit-text-stroke`, `background-clip: text`) funciona en
  Chromium, que es lo que usan TikTok LIVE Studio y OBS.
- **Los tamaños y la escala** siguen pendientes de M16 del otro catálogo: sin escala
  relativa, cualquier diseño bonito a 1920×1080 se desarma a otra resolución. **M16
  debería ir antes o junto con V1.**

## 9. Cómo se relaciona con el otro catálogo y con el rediseño

- `sugerencias.md` → **qué hace** el timer (reglas, información, formatos, sonido).
- `visual.md` (este) → **cómo se ve** el overlay (marcos, composición, tipografía,
  efectos, estados, diseños).
- `specs/live-timer-fase4/` → **dónde se configura**. Sigue siendo el último de los
  tres: con 20 capacidades visuales nuevas, tener 46 controles planos sería peor que
  ahora. El rediseño pasa a ser *consecuencia* de aceptar mejoras, no el objetivo.

Orden por dependencias: **M16 (escala) → V1 (marco) → V7/V8/V9/V6/V12 (aspecto fino) →
V4/V13/V5/V11 (composición y estados) → V14 (galería) → Fase 4 (pantalla de
configuración) sobre todo lo anterior.**

## 10. Decisiones abiertas

1. **¿Los diseños de V14 los propongo yo y tú eliges, o tienes referencias?** Lo ideal
   es que me pases 2-3 overlays que te gusten (de cualquier juego o streamer) y yo
   traduzco a diseños concretos.
2. **¿«Meta de coins» entra ahora?** Cambia el overlay de cuenta atrás a barra/anillo
   y es la mejora visual con más impacto de negocio, pero es la única que cambia de
   concepto.
3. **¿Fuentes propias?** ¿Te vale una lista curada de 8-10, o quieres poder añadir
   fuentes tuyas por ruta?
4. **Partículas: decidido SÍ**, con interruptor maestro, presupuesto y **auto-apagado**
   por rendimiento. Especificación completa en `disenos.md` §4. El motivo para no
   tratarlo a la ligera: OBS pone sus browser sources en «Efficiency Mode» y Windows
   los estrangula cuando OBS pierde el foco (~80 % menos rendimiento, con reportes de
   1-2 fps en overlays transparentes). Por eso el overlay debe ser legible a 5 fps y
   las partículas son lo primero que se apaga.
