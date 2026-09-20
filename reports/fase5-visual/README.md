# Fase 5 — verificación visual del motor

Capturas generadas renderizando el overlay real con Playwright (Chromium), mismo
estado del timer (`01:01:01`, contador de 120 px, paleta `cyber-blue`) salvo donde
se indica. El fondo es transparente: lo blanco es el lienzo del visor.

| Captura | Qué demuestra |
|---|---|
| `1-antes-fase3-1080p.png` | Referencia anterior (copia del overlay de Fase 3). El contador mide **612×151** con fuente de 120 px |
| `2-nuevo-defaults-1080p.png` | Overlay nuevo con la configuración de fábrica: **contador 612×151**, idéntico. Cero regresión |
| `3-nuevo-defaults-720p.png` | El mismo estado a 1280×720: todo mide **exactamente 2/3** (contador 408×101). La escala relativa (M16) funciona |
| `4-hud-futurista-1080p.png` | El diseño Futurista encendido a 1080p: marco neón, esquinas de bracket, rejilla, scanlines y contorno de 2 px |
| `5-hud-futurista-720p.png` | El mismo HUD a 1280×720: el marco mide 2/3 (841×375 → 561×250). El diseño no se desarma |

Medidas registradas en la verificación (no estimadas):

- Escala a 720p: `scale(0.666667)` — exactamente 1280/1920.
- Marco HUD a 1080p: `f-neon frame-brackets frame-grid frame-scanlines`,
  fondo `rgba(0,255,255,0.082)`, borde `1px rgb(0,255,255)`,
  `box-shadow rgba(0,255,255,0.47) 0 0 18px`, contorno `2px`.
- Cero errores de página en las cinco renderizaciones.

Comando de reproducción: se sirvió el HTML del overlay por HTTP con el estado
inyectado; ninguna captura proviene de una simulación del diseño, todas son el
overlay real.

## Incremento 2 — formato del tiempo, estados, medidor, efectos y dígitos

Mismo método. Capturas `6-` a `10-`:

| Captura | Qué demuestra |
|---|---|
| `6-incremento2-defaults-1080p.png` | Otra vez los defaults: contador **612×151**, sin medidor, sin marco. Cero regresión tras añadir ocho claves |
| `7-incremento2-hud-barra-1080p.png` | HUD con separador `.`, **horas ocultas** (`12.32` en vez de `00.12.32`), barra de progreso al **20.9 %** (`75.25deg` = 754/3600) y subtítulo **completo** |
| `8-incremento2-hud-barra-720p.png` | El mismo HUD a 720p: contador 118 px (**178 × 2/3**) y el mismo ángulo. El diseño es independiente de la resolución |
| `9-incremento2-anillo-50-1080p.png` | El medidor en forma de **anillo** al 50 % (`179.85deg`), envolviendo el contador sin pisar los dígitos |
| `10-incremento2-peligro-glitch-1080p.png` | Estado de peligro con **glitch** configurable (`counter-glitch` + `danger`) y umbrales propios (`danger_seconds = 30`) |

Medidas registradas en el incremento 2:

- Escala a 720p: `scale(0.666667)` en todos los casos; el HUD mide 2/3 exacto.
- Horas ocultas: `spansHidden [true, true, false, false, false]` y clase `hide-hours`,
  **sólo** cuando las horas valen `00`.
- Progreso: ángulos `75.25deg`, `0.50deg` y `179.85deg` para 754/3600, 5/3600 y 1800/3600.
- Color del medidor siguiendo al contador cuando `progress_color` va vacío.
- Cero errores de página en las once renderizaciones.

Dos defectos reales encontrados y corregidos gracias a estas capturas:

1. **El subtítulo salía recortado** dentro del marco: su `max-width: 90%` dependía del
   padre, que a su vez dependía del hijo (dependencia circular). Ahora el límite se
   calcula contra el ancho del lienzo (`--stage-w`), un valor definido.
2. **El anillo circular cruzaba los dígitos** y **el odómetro los dejaba invisibles**
   mientras cambiaban (en un reloj que cambia cada segundo, eso es un parpadeo). El
   anillo ahora envuelve sólo el contador con 28 px de aire, y el odómetro desliza
   manteniendo opacidad en vez de desvanecerse a cero.

## Incremento 3 — partículas con presupuesto y auto-apagado

Capturas `11-` a `13-` (mismo método: overlay real, estado inyectado, medida justo
después del primer poll porque las partículas viven menos de 2 s):

| Captura | Qué demuestra |
|---|---|
| `11-incremento3-particulas-chispas-1080p.png` | **Chispas** tras un regalo grande: 18 partículas, bucle activo |
| `12-incremento3-particulas-confeti-1080p.png` | **Confeti** con presupuesto 40 y densidad 2.0: 36 vivas, **nunca supera el tope** |
| `13-incremento3-particulas-apagadas-1080p.png` | Con la config de fábrica: **cero partículas** y ningún bucle corriendo |

Medidas registradas:

| Caso | Resultado |
|---|---|
| Apagadas (default) | lista 0, `enabled false`, sin bucle |
| Chispas + evento de 120 s | **18** partículas (= 18 × densidad 1), bucle activo |
| Confeti, presupuesto 40, densidad 2.0 | 60 × 2 = 120 pedidas → **36 vivas, `respetado: true`** |
| Evento de 10 s | **0** partículas (el umbral de «regalo grande» es 50 s) |
| Escalera de auto-apagado | 2 ventanas bajas → **densidad 0.5**; 4 ventanas → **apagado**, bucle parado y aviso «Particulas off por rendimiento (1 fps)» |
| Con `particles_force` | 8 ventanas bajas y **sigue encendido**, densidad intacta |
| Aviso de rendimiento | `display: none` fuera del modo preview → **nunca aparece en el directo** |

El bucle se cancela cuando no quedan partículas vivas: en reposo el coste es cero,
no un canvas vacío repintándose 60 veces por segundo.

## Incremento 4 — los tres diseños con sus valores reales

Capturas `14-` a `17-`: el overlay renderizado con **los mismos valores que escribe
cada botón de diseño** de la UI, no con una maqueta.

| Captura | Diseño | Medidas que lo confirman |
|---|---|---|
| `14-diseno-hud-orbita-1080p.png` | **HUD Órbita** (futurista) | `f-neon frame-brackets frame-grid frame-scanlines`, relleno cian 0.09, borde 1 px cian, brillo `rgba(0,255,255,0.46)`, radio 4. Contador a 150 px en **Chakra Petch**, título en **Rajdhani**, subtítulo en **JetBrains Mono** — las tres `document.fonts.check() == true` |
| `15-diseno-cristal-liquido-1080p.png` | **Cristal Líquido** (moderna) | `f-glass`, relleno blanco **0.10**, radio 28, anillo de progreso, contador a 170 px |
| `16-diseno-pegatina-brutal-1080p.png` | **Pegatina Brutal** (divertida) | `f-card`, relleno papel `rgba(244,240,230,0.94)`, radio 20, borde 4 px, barra verde ácido, confeti activo |
| `17-diseno-clasico-1080p.png` | **Clásico** | `f-none`, sin medidor, contador **612×151** — idéntico al overlay de antes de la fase |

Dos cosas que estas capturas dejaron claras:

1. **`frame_opacity` ahora es literal.** Antes estaba escalada (×0.16), así que el
   control no significaba nada: con el mismo número no se podía pedir un neón
   translúcido ni una tarjeta de papel opaca. Con el cambio, un valor expresa el
   relleno real (18 % → neón, 10 % → cristal, 94 % → papel).
2. **Limitación conocida:** el marco usa **un solo color** para el relleno y el
   borde, así que «papel claro con borde negro grueso» (la identidad de Pegatina
   Brutal) no es expresable todavía. Necesita una clave `frame_border_color`
   (relleno y borde separados); hoy Pegatina es una primera pasada.



