# Live Timer — Tres direcciones de diseño

Tres diseños completos para el overlay, buscados y contrastados en fuentes: uno
**moderno**, uno **divertido** y uno **futurista**. Cada uno especifica marco,
composición, tipografía, paleta, efectos, estados y partículas, y dice qué capacidades
de `visual.md` necesita.

Incluye el **sistema de partículas con interruptor y auto-apagado**, tal como pediste.

## 0. De dónde sale cada dirección (investigación)

| Dirección | Fuentes |
|---|---|
| **Moderna** | Las tendencias 2026 ponen el **glassmorphism («liquid glass»)** y las **paletas contrastadas** en el primer plano, junto con diseño accesible y «profundamente humano» (movimiento suave, no agresivo) — [Idea Kraft, tendencias 2026](https://ideakraft.com/top-5-design-trends-for-2026/) |
| **Divertida** | La guía de campo 2026 del **retro/brutalismo** define tres estéticas: brutalista, revival Y2K y **OS clásico (System 7, Windows 98)**. Sus colores de referencia en el propio artículo: papel `#F4F0E6`, tinta `#111111`, ácido `#33FF66` y el teal clásico `#008080` — [Setproduct, retro & brutalist UI 2026](https://www.setproduct.com/blog/retro-brutalist-ui-design-2026) |
| **Futurista** | El sistema de diseño **Cyberpunk UI** da la paleta (`#0D0D0D` casi negro, `#00FF00` neón, `#FF00FF` magenta, `#00FFFF` cian) y las técnicas: **glow por `text-shadow`, glitch con skew/offset, scanlines con `::before`, tipografía monoespaciada** — [designmd.app Cyberpunk UI](https://designmd.app/library/cyberpunk-ui/) y [FontVibe, efectos de texto cyberpunk](https://fontvibe.ai/blog/cyberpunk-text-effects-guide) |

Y un dato tipográfico que condiciona los tres: un contador necesita **números
tabulares** (todos del mismo ancho) o los dígitos **bailan** al cambiar. Es la
diferencia entre «tabulares» y «proporcionales»: los tabulares ocupan exactamente el
mismo espacio horizontal — [Google Fonts, understanding numerals](https://fonts.google.com/knowledge/introducing_type/understanding_numerals).
El overlay **ya** aplica `font-variant-numeric: tabular-nums` en `.digit`, así que sólo
hay que garantizar que las fuentes elegidas lo soporten (no todas lo hacen).

Fuentes de números candidatas, confirmadas en las listas de referencia
([Just-in-mind, mejores fuentes de números](https://www.justinmind.com/blog/best-number-fonts/)):
**Inter** (muy citada, tiene `tnum`), **Oswald**, **Bebas Neue**, **Bungee**,
**Rubik Mono One**, **Monoton**, **Jersey**, **JetBrains Mono**, **Roboto Mono**, **Nunito**.

---

## 1. MODERNO — «Cristal Líquido»

**Concepto.** El overlay como una pieza de cristal pulido que flota sobre el vídeo:
nada de bordes duros, todo luz suave y profundidad. Es la traducción del *liquid glass*
a un contador, con movimiento calmado.

**Marco (V1 · Cristal).**
- `backdrop-filter: blur(24px) saturate(140%)` — el vídeo se ve detrás, difuminado.
- Fondo `rgba(255,255,255,0.10)`, borde `1px solid rgba(255,255,255,0.22)`, radio `28px`.
- Sombra suave y amplia (`0 24px 60px rgba(0,0,0,.35)`), más un **degradado de esquina**
  (`linear-gradient(135deg, rgba(255,255,255,.18), transparent 45%)`) que da el
  reflejo que hace que parezca cristal y no un rectángulo gris.
- Relleno interior generoso: `32px 48px` — el aire es parte del diseño.

**Composición (V4 · Reloj protagonista).** Título pequeño arriba en mayúsculas con
tracking amplio, **reloj enorme** en el centro, subtítulo debajo en gris suave.
Día (`DÍA 2`) como línea fina encima del título.

**Tipografía (V7).**
- Título: **Inter** 600, mayúsculas, `letter-spacing: .12em`, 20-26 px.
- Reloj: **Inter** 800 con `font-feature-settings: "tnum" 1`, tracking `-0.02em`.
  Tamaño 160-200 px. (Inter es la referencia de la investigación y tiene tabulares.)
- Subtítulo: **Inter** 400, 18-22 px, opacidad 0.72.

**Paleta.** Base neutra cálida con un solo acento, contraste alto (accesibilidad):
texto `#F7F9FC`, acento `#6EE7F9`, aviso `#FBBF24`, peligro `#FB7185`,
completado `#A7F3D0`. El cristal oscurece el vídeo lo justo para que el texto tenga
contraste real; el operador puede subir la opacidad del fondo si su escena es clara.

**Efectos.**
- V9 **Flotar** muy suave: `translateY` de 3 px en 6 s, `ease-in-out`. Casi imperceptible,
  que es justo el punto.
- V10 Entrada: fundido + subida de 8 px, 400 ms.
- V12 Dígitos: **odómetro** suave (deslizamiento de 180 ms).
- V11 Reacción al evento: un **halo** que se expande desde el marco al recibir una coin
  (600 ms). Sin partículas por defecto: la calma es la identidad de este diseño.

**Estados (V13).**
| Estado | Aspecto |
|---|---|
| Reposo | Cristal al 6 %, texto al 80 % |
| Corriendo | Cristal al 10 % |
| Pausa | Escala 0.98 y etiqueta `PAUSA` bajo el reloj |
| Aviso (≤60 s) | El acento pasa a `#FBBF24` |
| Peligro (≤10 s) | El borde late en `#FB7185` a 1 s |
| Completado | El cristal se vuelve blanco al 92 % y el texto pasa a `#0B0F14` (inversión total: máxima legibilidad y muy «Apple») |

**Anillo (V5).** Opcional, fino (4 px), `conic-gradient` en el acento, alrededor del
reloj. Encaja perfecto con esta dirección: parece un reflejo del cristal.

**Qué capacidades necesita:** V1 (cristal), V4, V7, V9, V10, V12, V13, V5.
**Coste:** M. **Riesgo:** el `backdrop-filter` es lo más caro de todo el catálogo
(ver §4); este diseño debe ser el primero en probarse en tu equipo.

---

## 2. DIVERTIDO — «Pegatina Brutal» (neubrutalismo + retro OS)

**Concepto.** Una pegatina de papel con borde grueso y sombra dura, como los overlays
de los canales que se ríen de sí mismos. Es el brutalismo de la guía 2026 —bordes
negros, sombra sin desenfoque, colores ácidos— mezclado con el teal del OS clásico.
Funciona sobre **cualquier** vídeo porque el papel da el contraste.

**Marco (V1 · Tarjeta).**
- Fondo **papel** `#F4F0E6` al 96 %.
- **Borde `4px solid #111111`**, radio `20px`.
- **Sombra dura desplazada: `8px 8px 0 #111111`** (cero desenfoque: eso es el
  brutalismo).
- La tarjeta entera va **rotada −2°**, como una pegatina pegada de cualquier manera.

**Composición (V4 · En línea).** `TÍTULO · 12:34 · subtítulo` en una sola fila, con el
reloj dentro de una **insignia** con su propio borde negro y su propia sombra dura,
rotada +1.5° en sentido contrario (el contraste de rotaciones es lo que lo hace
simpático). Los popups de eventos son **pegatinas sueltas** que entran rotadas
aleatoriamente entre −4° y +4°, con borde negro fino y sombra dura de 3 px.

**Tipografía (V7).**
- Título: **Bungee** (display, chunky, mayúsculas) 28-34 px.
- Reloj: **Bungee** 120-170 px, o **Lilita One** si se quiere algo más estrecho.
  Bungee tiene dígitos anchos y uniformes: no depende de `tnum` para no bailar.
- Subtítulo: **Nunito** 800, 20-24 px (redondeada, amable, contraste con el display).

**Paleta.** Papel `#F4F0E6`, tinta `#111111`, ácido `#33FF66`, teal OS `#008080`,
alerta `#FF5C39`, completado `#7B61FF` (púrpura Y2K).
Los cinco colores del confeti son estos mismos, y ahí está el truco: el confeti no
introduce color nuevo, así que la escena nunca se ve sucia.

**Efectos.**
- V9 **Sacudida** en el reloj al sumar tiempo: una sola vez, 250 ms, ±3°.
- V10 Entrada: **zoom con rebote** (overshoot a 1.06 y vuelta).
- V12 Dígitos: **pop** (el dígito escala 1.15 al cambiar).
- V11 Reacción: destello del marco en ácido (120 ms) + rebote del contador + confeti
  si el evento es grande (≥ 50 diamantes).

**Estados (V13).**
| Estado | Aspecto |
|---|---|
| Pausa | La tarjeta se inclina a −4° y aparece una **cinta** `EN PAUSA` cruzando |
| Aviso | El reloj cambia a `#FF5C39` |
| Peligro | El fondo del marco **parpadea** entre papel y `#FF5C39` cada 500 ms |
| Completado | **Confeti** + banner con borde negro y texto a 72 px sobre papel |

**Partículas (V11 + §4).** Es parte de la identidad: **confeti de papel** (rectángulos
pequeños, 6-10 px, con rotación propia) al completar y en regalos grandes. Activadas
por defecto.

**Qué capacidades necesita:** V1 (tarjeta + sombra dura), V4, V7, V9, V10, V12, V13,
partículas de confeti.
**Coste:** M. **Riesgo:** bajo de rendimiento (todo es CSS barato y el confeti es corto),
medio de diseño: es un estilo que **o te encanta o te horroriza**, así que hay que
validarlo contigo antes de construirlo entero.

---

## 3. FUTURISTA — «HUD Órbita»

**Concepto.** El contador como panel de instrumentos de una nave: etiquetas técnicas,
esquinas de bracket, líneas de escaneo y un reloj que parece medir algo crítico. Es la
traducción literal de las técnicas del sistema Cyberpunk UI.

**Marco (V1 · Neón + V2 · esquinas).**
- Fondo `rgba(13,13,13,0.55)`.
- Borde `1px solid rgba(0,255,255,0.55)`, radio `4px` (casi recto, es un instrumento).
- **Esquinas de bracket** dibujadas con SVG en las cuatro esquinas (líneas en L de
  18 px), en `#00FFFF`.
- **Línea interior** fina al 20 % separada 6 px del borde.
- **Scanlines** con `repeating-linear-gradient(0deg, rgba(0,255,255,.05) 0 1px, transparent 1px 4px)`.
- Una **rejilla técnica** muy tenue de fondo (`background-size: 24px 24px`) que da el
  aire de panel.

**Composición (V4 · Etiquetas laterales).**
```
┌ ─ SYS // LIVE TIMER ─────────────── SES 0x1F ─ LINK OK ─ ┐
│                                                          │
│   EXTENDER EL LIVE            01 : 42 : 07    ◜◝ anillo  │
│   cada coin suma 0.5s         RITMO +12s/30s             │
│                                                          │
│   +2.0/LIKE   +0.5/COIN   +10/FOLLOW   04 EVENTOS        │
└ ─ ESC // 60 FPS ──────────────────── NISOJE 3.0 ──────── ┘
```
Micro-etiquetas arriba y abajo (datos del sistema: sesión, enlace, ritmo, contadores),
reloj al centro, lecturas a la derecha. Ese marco de etiquetas es lo que hace que
parezca un HUD y no un contador con un borde.

**Tipografía (V7).**
- Reloj: **Chakra Petch** 700 (técnica, angulosa) con `tnum`. Alternativa: **Orbitron**
  700 si se quiere aún más sci-fi.
- Micro-etiquetas: **JetBrains Mono** 400-500, mayúsculas,
  `letter-spacing: .18em`, 11-12 px. (Ya se carga en el overlay.)
- Título: **Rajdhani** 600, mayúsculas.

**Paleta.** Fondo `#0D0D0D`, texto `#E8FFFF`, acento cian `#00FFFF`, secundario
`#00FF00`, alerta magenta `#FF00FF`, peligro `#FF2D55`, completado `#00FF00`.

**Efectos.**
- V9 **Parpadeo neón** en el reloj + glow (`text-shadow` con dos capas, la técnica del
  sistema Cyberpunk UI).
- V12 Dígitos: **desenfoque** al cambiar (blur 2 px → 0 en 200 ms).
- V13 Peligro: **glitch** cada 2 s — dos copias del reloj desplazadas ±3 px, recortadas
  con `clip-path`, en cian y magenta. Es el efecto que define la estética.
- V10 Entrada: revelado por **scanline** (la línea de escaneo baja y va revelando).
- V11 Reacción: destello del anillo + **chispas digitales** (ver §4).

**Estados (V13).** Peligro: glitch + el acento pasa a magenta. Completado:
`TERMINADO` con revelado por scanline y una barra que se llena al 100 %.

**Partículas (V11 + §4).** **Chispas digitales**: segmentos cortos de 8-14 px que salen
del marco hacia fuera, en cian/blanco, con desvanecido rápido. Coherentes con la
estética y más baratas que el confeti (se dibujan como líneas, no como polígonos).
Activadas por defecto.

**Qué capacidades necesita:** V1 (neón), V2 (brackets), V3 (rejilla + scanlines), V4,
V5 (anillo, aquí es central), V7, V9, V10, V12, V13, partículas de chispas.
**Coste:** M-L (es el que más piezas usa: SVG de esquinas, glitch con clip-path,
scanlines y anillo). **Riesgo:** medio; el glitch con dos copias del texto hay que
medirlo, y hay que asegurarse de que el glow no se come la legibilidad a 1080p.

---

## 4. Partículas: sistema, presupuesto y auto-apagado

Pediste partículas **sí**, con opción de desactivarlas si pesan. La investigación da
un motivo fuerte para tomárselo en serio, no como un detalle.

### Por qué hay que ser conservador (evidencia)

- OBS pone los procesos del browser source en **«Efficiency Mode»** y, cuando OBS
  pierde el foco, **Windows los estrangula** (~80 % menos rendimiento). Hay usuarios
  reportando caídas a **1-2 FPS** con overlays transparentes y aceleración por hardware
  activada — [obs-studio issue #12982](https://github.com/obsproject/obs-studio/issues/12982).
- Los browser sources ya se comen la GPU por sí solos en equipos modestos —
  [foro de OBS: browser sources overloading GPU](https://obsproject.com/forum/threads/browser-sources-overloading-gpu.184305/).
- La guía de rendimiento de MDN: animar sólo `transform` y `opacity`; `width`, `left`,
  márgenes o tamaño de fuente disparan layout y pintado; `will-change` sólo en casos
  contados porque cada capa extra consume memoria —
  [MDN, CSS/JS animation performance](https://developer.mozilla.org/en-US/docs/Web/Performance/Guides/CSS_JavaScript_animation_performance).

**Conclusión de diseño:** el overlay debe ser **correcto y legible a 5 FPS**. Las
partículas son decoración; si se apagan, no se pierde información.

### Cómo se implementan

- **Un solo `<canvas>`**, cubriendo el viewport, `devicePixelRatio` forzado a **1**
  (en OBS no hace falta 2x) y `will-change: transform`.
- Dibujo por `requestAnimationFrame` (la recomendación de MDN para canvas), con
  partículas como `fillRect` / `stroke` de líneas: nada de imágenes ni sprites.
- **El bucle se cancela cuando no hay partículas vivas** (`cancelAnimationFrame`): en
  reposo el coste es **cero**, no un canvas vacío redibujándose 60 veces por segundo.
- Tres tipos, uno por diseño: `confeti` (rectángulos con rotación), `chispas`
  (segmentos), `estrellas` (puntos con glow). El tipo lo fija el diseño, no el operador.

### Presupuesto y controles

| Control | Valor por defecto | Qué hace |
|---|---|---|
| `particles_enabled` | on en Divertida/Futurista, off en Moderna | Interruptor maestro |
| `particles_budget` | **120** simultáneas (opciones 40 / 120 / 300) | Tope duro: si se supera, no se emiten más |
| `particles_trigger` | sólo eventos grandes y completado | Evita que 200 coins suelten 200 partículas |
| `particles_density` | media (0.5× / 1× / 2× dentro del presupuesto) | Ajuste fino sin tocar el tope |

### Auto-apagado (lo que pediste)

1. El overlay mide sus propios FPS con un contador propio (no depende del sistema).
2. Si la media de 3 s cae por debajo del **75 %** del objetivo (p. ej. < 45 fps con
   objetivo 60) **dos ventanas seguidas**, apaga partículas y **baja un escalón la
   densidad** antes de apagarlas del todo.
3. Se registra en el panel: «Partículas desactivadas por rendimiento (42 fps)», con un
   botón para reactivarlas (y un aviso: se volverán a apagar si el rendimiento no aguanta).
4. El operador puede forzarlas siempre con `particles_force = true`, y entonces el panel
   muestra el aviso de riesgo, no lo oculta.

### Modo ahorro

Un botón en el panel que deja: partículas off, efectos en «ninguno» salvo el estado,
glow a 4 px y scanlines off. Pensado para portátiles y para directos largos. Es la
red de seguridad para que el overlay nunca sea el motivo de perder frames.

---

## 5. Comparación y recomendación

| | Moderna (Cristal Líquido) | Divertida (Pegatina Brutal) | Futurista (HUD Órbita) |
|---|---|---|---|
| Se lee sobre vídeo claro | Muy bien (el cristal oscurece) | **Perfecto** (papel opaco) | Bien (fondo oscuro al 55 %) |
| Coste de GPU | **Alto** (`backdrop-filter`) | Bajo | Medio (glow + glitch) |
| Partículas | No (halo) | Confeti | Chispas |
| Riesgo de gustar o no | Bajo, es sobrio | **Alto**: o encanta o horroriza | Medio, nicho claro |
| Encaja con un directo de… | juego, charla, entrevista | sorteos, reacciones, gaming casual | gaming, tecnología, retro |
| Piezas nuevas que estrena | V1 cristal | V1 tarjeta + sombra dura | V1 neón + V2 + V3 + V5 + glitch |

**Recomendación de orden:** construir **primero el Futurista** y **luego el Divertido**,
dejando el Moderno para el final. Motivo: el futurista estrena casi todas las piezas
nuevas (marco, esquinas, rejilla, scanlines, anillo, glitch), así que al construirlo se
construye el motor visual entero; el divertido reutiliza ese motor y sólo añade la
sombra dura y el confeti; y el moderno, que es el más caro de GPU, se hace al final
cuando ya se sabe medir en tu equipo si el `backdrop-filter` aguanta.

**Lo que necesito de ti antes de construirlos:**
1. ¿Alguno de los tres no te pega nada? Con dos basta para empezar; el tercero puede
   esperar o sustituirse por otro concepto.
2. Referencias: si tienes 2-3 overlays que te gusten, los miro y ajusto antes de tocar
   código. Si no, arranco con estos.
3. El orden: ¿empiezo por el futurista como propongo, o prefieres el divertido?

## 6. Cómo se implementan (para que no quede en un dibujo)

- Cada diseño es un **archivo JSON de datos** con las claves del modelo (las que ya
  existen y las nuevas de `visual.md`). Añadir un diseño **no toca el motor**.
- La **miniatura del selector se genera renderizando el overlay real** con la config de
  ese diseño en un iframe pequeño, así que nunca miente.
- Las tres direcciones usan el mismo mecanismo de marco/composición/estados de
  `visual.md`: no son tres overlays distintos por dentro, son tres **ajustes** del mismo
  motor. Eso es lo que hace que la galería sea sostenible y no un pantano de CSS.
- Las partículas (§4) son una pieza única compartida por los tres, con el tipo y el
  presupuesto como parámetros.
