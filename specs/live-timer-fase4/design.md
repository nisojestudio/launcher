# Live Timer — Fase 4: rediseño del sistema de configuración

Estado: **propuesta, pendiente de aprobación**. No implementar antes de que el
operador revise las decisiones de la sección «Decisiones abiertas».

Fase 3 (publicación del overlay) está hecha y desplegada: ver
`specs/live-timer-fase3/`.

---

## 1. Suposiciones

Expuestas antes de nada, porque cambian el plan si son falsas:

1. **El operador configura solo, desde el panel de escritorio.** No hay multi-usuario
   ni perfiles por cuenta. (Si algún día hay varios operadores, ver «Fuera de alcance».)
2. **El contrato con el backend no se rompe.** Las 44 claves de
   `POST /api/timer/configure` siguen siendo la verdad. Sacar un campo nuevo del
   backend (p. ej. `theme_id`) es *aditivo* y opcional.
3. **El overlay no se reescribe en esta fase.** Sólo se le añaden, si acaso, dos
   correcciones pequeñas (preview-mode y fits) que hoy son la causa de que la
   previsualización se vea mal.
4. **La estética objetivo es la del propio panel**: sus tokens (`--bg #09111d`,
   `--panel`, `--accent #22c55e`, radios 10/16/18, `Inter` + `JetBrains Mono`)
   ya son un design system; el problema no es el gusto, es que el formulario del
   timer no lo usa.
5. **Español como idioma único de la UI.** Los identificadores internos siguen en
   inglés (`counter_font_size`), las etiquetas en español.
6. **No se añaden dependencias** (regla 7 del `AGENTS.md`): nada de frameworks,
   ni de librerías de color/slider. Todo con HTML/CSS/JS nativo del panel.

## 2. Problema

El sistema de configuración del timer no es «feo»: es **una lista plana de 44
claves técnicas volcada en 5 acordeones anidados**. Inventario real medido hoy:

| Sección actual | Controles | Contenido |
|---|---|---|
| ⏱ Tiempos | 7 | tiempo inicial, tope, y segundos por like/share/follow/coin/chat |
| 🎨 Visual | 23 | por cada elemento (título, contador, subtítulo): texto, fuente, tamaño, color, negrita, efecto, glow — más presets de color y efecto de dígitos |
| ✨ Efectos | 3 | color de glow, intensidad, velocidad de pulso |
| 🔔 Sonidos y completado | 10 | 3 rutas de audio con volumen, texto/color/tamaño de «completado», repetir |
| 💬 Avisos | 2 | color de suma y de resta |

Total: **46 controles de configuración** (`input`/`select`/`textarea` con id `timer-*`)
que escriben **44 claves** del contrato, más 12 botones de acción, dentro de un
`<details>` dentro de otro `<details>`. Consecuencias medidas o evidentes:

1. **No se ve el resultado.** La previsualización está rota: `#timer-preview-wrapper`
   nace con `hidden` (`index.html:252`) y sólo la alcanza la tecla `V`
   (`app.js:3961`); y aunque se abra, el iframe mide 240 px (180 px si la pantalla
   tiene ≤800 px) para un diseño pensado a 1920×1080, así que se recorta por los
   cuatro lados. Medido: el contador de 120 px ocupa 174 px de esos 240 y su
   `overflow:hidden` corta los dígitos.
2. **El «modo preview» no funciona.** Existe `body.preview-mode #counter { font-size: 52px }`
   (`overlay/live-timer.html:25-27`), pero `applyStyles()` escribe `font-size` **inline**
   (`app.js`), y el inline gana al CSS. Es decir: la única reducción de tamaño
   pensada para la preview nunca se aplica.
3. **Todo se llama igual.** El operador lee `time_per_like_s` en su cabeza cuando la
   etiqueta dice «Segundos por like» y el control no muestra unidad.
4. **Los efectos se eligen a ciegas.** `none | glow | pulse` es un `<select>` de texto:
   no se ve qué hace «pulse» hasta aplicarlo y mirar el overlay.
5. **Las fuentes no se pueden juzgar.** Un `<select>` con `Segoe UI, monospace` no
   muestra la tipografía; el overlay ya carga JetBrains Mono, Space Mono y Share Tech
   Mono desde Google Fonts y no se aprovechan.
6. **No hay «looks».** Existe `color_preset` (4 paletas) pero no cambia fuentes ni
   efectos, así que un look completo exige tocar 20 controles a mano.
7. **Los defaults del formulario contradicen al modelo.** `readTimerConfigFromForm()`
   usa `|| 300` para `initial_time_s` y `2.0 / 5.0 / 10.0 / 0.5` para los tiempos por
   evento, mientras el modelo arranca en `0.0`. Un campo vacío **reintroduce los
   5 minutos de regalo** que la Fase 1 eliminó (`CHANGELOG.md`, Fase 1) y regala
   tiempo por evento sin que el operador lo pida.
8. **Aplicar es un acto de fe.** Un solo botón «Aplicar configuración» al final de
   46 campos, con la validación sólo al pulsar.

## 3. Decisión arquitectónica

**Separar tres capas y no tocar la de abajo.**

```
┌─ Capa 1 · Modelo de datos (NO SE TOCA) ──────────────────────────────┐
│ Las 44 claves de  POST /api/timer/configure y LiveTimerGameState.    │
│ Fuente de verdad compartida con el overlay y con la persistencia.    │
└─────────────────────────────────────────────────────────────────────┘
              ▲                         │
              │ mapea 1:1               │ alimenta
┌─ Capa 2 · Modelo de presentación (NUEVO) ────────────────────────────┐
│ ui/timer-config-model.js — módulo PURO, sin DOM:                     │
│   · describe cada clave (etiqueta, unidad, rango, paso, grupo,       │
│     nivel basico/avanzado)                                           │
│   · fromBackend(json) -> estado de UI                                │
│   · toBackend(estadoUI) -> json   (lo que se envía hoy)              │
│   · aplica un «Look» completo sobre el estado                        │
│ Testeable con `node --test`, sin navegador y sin dependencias.       │
└─────────────────────────────────────────────────────────────────────┘
              ▲                         │
              │ render                  │ eventos
┌─ Capa 3 · Componentes de UI (NUEVO) ─────────────────────────────────┐
│ ui/timer-config.js + ui/timer-config.css:                            │
│   SettingRow · SliderNumber · ColorField · FontPicker · EffectPicker │
│   SegmentedControl · Toggle · Section · LookCard · LivePreview       │
└─────────────────────────────────────────────────────────────────────┘
```

**Por qué así y no otra:**

| Opción | Veredicto |
|---|---|
| Retocar el formulario actual (CSS + reordenar) | Barato, pero deja el problema de fondo: 46 controles sin jerarquía ni preview. Se queda corto |
| Reescribir también el backend y el overlay (temas con nombre, CSS por look) | Cambia el contrato, obliga a migrar el estado guardado del usuario y multiplica el riesgo por 3 |
| **Rediseño de UI con capa de modelo pura en medio** | **Elegida**: el backend y el overlay quedan intactos, el mapeo se testea sin navegador, y se puede convivir con el formulario viejo tras un flag hasta validar |

**Regla de oro:** ningún control nuevo inventa una clave. Si algo no cabe en las 44
claves actuales, primero se documenta aquí y se decide si se añade al backend.

## 4. UX objetivo

### 4.1 Estructura de la pantalla

```
┌─────────────────────────────┬──────────────────────────────────────┐
│  PREVIEW (sticky)           │  [ Tiempo ][ Aspecto ][ Efectos ]    │
│  caja 16:9, escalada        │  [ Avisos y sonido ]     [Avanzado]  │
│  WYSIWYG real               │                                      │
│  ─────────────────────      │  · controles del grupo activo        │
│  [Iniciar][Pausar][Reset]   │  · cada uno con etiqueta, unidad y   │
│  estado y tiempo restante   │    rango visible                     │
└─────────────────────────────┴──────────────────────────────────────┘
```

- El preview **siempre visible** mientras se configura, y **escalado de verdad**:
  iframe a 1920×1080 dentro de un contenedor con `transform: scale(factor)`. Nada de
  recortes ni de fuentes alteradas: lo que se ve es lo que se emite.
- 4 pestañas + un interruptor «Avanzado» que revela los campos de ajuste fino
  (intensidad de glow, velocidad de pulso, volúmenes, rutas de audio). Objetivo:
  **≤15 controles visibles** en el camino normal.
- En pantallas estrechas (< 1100 px) el preview pasa arriba y se colapsa.

### 4.2 Los cuatro grupos

**Tiempo (7 claves, 5 visibles)**
- Tiempo inicial y tope máximo: `SliderNumber` con unidad y atajos (1 min, 5, 10, 30, 1 h).
- «Cuánto suma cada evento»: cinco filas `+2 s por like`, `+5 s por compartir`,
  `+10 s por seguidor`, `+0.5 s por coin`, `+0 s por chat`, con `SliderNumber` de
  −60 a +300 y campo numérico. Permite **negativo** (restar), que hoy está enterrado.
- Nota de producto: las coins son la palanca del negocio del operador; va primero y
  con una frase de ayuda («cuánto sube el reloj por cada coin recibida»).

**Aspecto (23 claves, 8 visibles + avanzado)**
- **Look**: 4 tarjetas visuales (Neón, Cyber, Clean, Rose Gold) + «Personalizado».
  Elegir un look aplica de golpe `color_preset`, los 3 colores de fuente, glow y
  efecto de dígitos, coherentes entre sí. Es lo que hace que en 5 segundos el
  overlay cambie de carácter, en vez de en 20 controles.
- **Tipografía**: `FontPicker` cuyo desplegable **renderiza cada opción con su
  propia fuente** (incluye mono del overlay: JetBrains Mono, Space Mono, Share Tech
  Mono). Un solo control por elemento con su muestra.
- **Tamaño**: `SliderNumber` 8–400 px con la muestra escalando en vivo.
- **Color**: `ColorField` = muestra + hex + paleta curada de 12 + «usar color del look».
- **Negrita** y **efecto de dígitos** (ninguno / flip / roll / pop / fade) como
  `SegmentedControl` con icono.

**Efectos (3 claves + 6 banderas de glow)**
- `EffectPicker` por elemento (título / contador / subtítulo) con 3 tarjetas
  «Ninguno / Glow / Pulso» **que se animan al pasar el ratón**: la tarjeta demuestra
  el efecto, no lo describe.
- Glow por elemento como interruptor; color e intensidad de glow **globales**, en
  Avanzado (hoy están en una sección aparte que nadie relaciona con el efecto).

**Avisos y sonido (12 claves)**
- Colores de suma/resta junto a una **simulación de popup** que se dispara al
  cambiar el color.
- «Al completar»: texto, color, tamaño, y audio con botón **Probar**.
- Tick y suma: ruta + volumen con botón Probar, en Avanzado.

### 4.3 Detalles que hacen que se sienta moderno (y no decorativo)

| Detalle | Por qué |
|---|---|
| Preview escalado y sticky | Es el cambio de mayor retorno: convierte 46 campos ciegos en ajuste visual |
| Aplicado automático con debounce (~300 ms) + estado «Guardado» | Se acaba el «Aplicar configuración» como acto de fe. Se mantiene un «Aplicar ahora» para forzar |
| Rangos visibles y validación en vivo | Los límites del servidor (1 s–1 año, ±3600 s, −60..300, 0–2 de volumen, 8–400 px) se muestran como marcas del slider, no como error tras pulsar |
| Unidades siempre a la vista | `+0.5 s por coin`, no `0.5` |
| Estados vacíos explícitos | «Sin audio: silencio» en vez de una ruta vacía que no dice nada |
| Restablecer por grupo y «Restablecer todo» | Hoy sólo existe `POST /api/timer/reset-config` global |
| Exportar / Importar con validación previa | Ya existe; se conserva y se le da sitio propio, con vista del JSON que se enviará |
| Foco visible, navegación con teclado, `aria-describedby` por ayuda | El panel hoy sólo responde a atajos sueltos (V, +, −) |

### 4.4 Definición de «moderno y fácil de entender» (testeable)

- Cambiar de look completo: **≤ 2 clics**.
- Cambiar «cuánto suma una coin»: **≤ 3 clics** desde que se abre el panel.
- Un operador nuevo encuentra «el tiempo por coin» **sin abrir Avanzado**.
- Cero mensajes de error de validación en el flujo normal (1 min–1 h, 0.5–30 s por coin).
- La preview **no recorta** en ninguna combinación: medido con `scrollWidth/Height`
  del contenido ≤ caja, en 4 tamaños de ventana.
- Nada del overlay cambia si se envía la misma configuración importada.

## 5. Sistema visual

Se reutilizan los tokens existentes y se añaden sólo lo que falta, en `:root`:

```css
/* ya existen */                 /* se añaden */
--bg --panel --panel-soft        --space-1..6   (4,8,12,16,24,32)
--border --border-strong         --control-h    (32px inputs, 28px compactos)
--accent --accent-soft           --focus-ring   (0 0 0 3px var(--accent-soft))
--warning --danger               --group-gap
--text --text-soft --text-muted  --field-label  (11px, mayúsculas, tracking)
--radius-sm/md/lg --shadow
--font-ui --font-mono --transition-fast
```

Ejemplo de la convención (vale más que tres párrafos):

```html
<div class="cfg-row" data-key="time_per_gift_coin_s">
  <label class="cfg-label" for="coin-seconds">
    Coin
    <span class="cfg-help" id="coin-seconds-help">Cuánto sube el reloj por cada coin</span>
  </label>
  <div class="cfg-control">
    <input class="cfg-range" type="range" min="-60" max="300" step="0.5" value="0.5"
           aria-describedby="coin-seconds-help">
    <div class="cfg-number">
      <input id="coin-seconds" class="cfg-input" type="number" min="-60" max="300" step="0.5" value="0.5">
      <span class="cfg-unit">s</span>
    </div>
  </div>
</div>
```

## 6. Estructura de archivos

```
src/platform/ui/
  timer-config-model.js   NUEVO  modelo puro + descripción de claves + looks (sin DOM)
  timer-config.js         NUEVO  componentes y render (DOM)
  timer-config.css        NUEVO  estilos del nuevo panel
  index.html              EDIT   sustituye el bloque de 5 acordeones por el contenedor nuevo
  app.js                  EDIT   wiring: puente al modelo, preview, aplicar/auto-aplicar
  styles.css              EDIT   sólo tokens nuevos y ajustes del shell del panel
tools/ui/
  run_timer_config_tests.ps1  NUEVO  ejecuta `node --test` sobre las pruebas del modelo
tests/
  CMakeLists.txt          EDIT   registra el test de Node (se salta si no hay node)
src/platform/overlay/live-timer.html  EDIT (opcional, T3)  preview-mode real + ajuste de escalado
tools/bridge_py/panel_desktop_launcher.py  (sin cambios)
```

El módulo de modelo va separado a propósito: es la única parte **testeable sin
navegador**, y es donde vive el riesgo real (no perder ninguna de las 44 claves).

## 7. Riesgos

| Riesgo | Mitigación |
|---|---|
| **Perder una clave** al reescribir el formulario: el operador aplica y su look cambia sin querer | El módulo de modelo declara las 44 claves y un test compara `toBackend(estado)` con el JSON actual de producción campo a campo |
| Regresión en el tiempo del timer (el requisito central de Fase 1) | El nuevo flujo no toca `apply_config` ni el reloj: solo envía el mismo JSON. Test `nlp3_live_timer_api_smoke_test` como red |
| El formulario nuevo no cabe en la ventana del WebView2 | Diseño a 1100 px de ancho mínimo con preview colapsable; captura obligatoria en 3 tamaños antes de cerrar la fase |
| Doble sistema de config temporal (viejo + nuevo) se queda para siempre | Flag de UI con fecha de caducidad declarada: el viejo se borra en la tarea final de la fase |
| El preview escalado dispara `iframe` a 1920×1080 y consume CPU | Escala con `transform` (coste GPU, no de layout); el iframe se pausa (polling detenido) cuando el panel no está visible |
| Audio: rutas locales vs remotas | Ya resuelto en Fase 3 para el overlay público; en el panel se mantiene el comportamiento actual (el botón Probar usa la ruta tal cual) |
| Sobre-diseño: 3 semanas de CSS y ninguna mejora medible | Cada tarea tiene criterio de aceptación medible (§4.4); el orden pone primero lo que se ve |

## 8. Plan de verificación

1. **Modelo (sin navegador):** `node --test` sobre `tools/ui/timer-config-model.test.mjs`
   — mapeo 1:1 de las 44 claves, redondeos, clamps, looks completos, defaults del
   modelo (no los `|| 300` del formulario viejo). Se registra como test de CTest y
   **se salta** si no hay `node` en la máquina.
2. **Backend:** `ninja -C build/release` + `ctest --preset release` (32/32 hoy).
3. **UI (navegador real):** guion Playwright temporal contra el panel en marcha
   (`http://127.0.0.1:18913/`) que mida, en 4 tamaños: preview sin recorte
   (`scrollWidth/Height ≤ caja`), ≤15 controles visibles, look en 2 clics, y cero
   errores de consola. Capturas antes/después.
4. **Contrato:** `GET /api/timer/config` antes y después de aplicar un look; el JSON
   debe seguir teniendo exactamente las mismas claves.
5. **Regresión de la preview:** comparar la medición de hoy (contador 120 px inline,
   caja recortada) contra la nueva (escalado, sin recorte).

## 9. Fuera de alcance

- Reescribir la paleta global del panel o sus otras pantallas.
- Multi-usuario / perfiles por operador / sincronización en la nube de los looks.
- Temas con nombre persistidos en el backend (se decide en T7 si hace falta; hoy no
  hay clave para guardar «qué look» y no se añade sin necesidad).
- Editor visual de posiciones (drag & drop del título/contador en el canvas).
- Animaciones nuevas en el overlay más allá de las que ya existen
  (none/glow/pulse + flip/roll/pop/fade).
- Accesibilidad completa (WCAG AA) del resto del panel: sólo la sección del timer.

## 10. Decisiones abiertas (necesitan respuesta del operador)

1. **¿Auto-aplicar con debounce o mantener el botón Aplicar?** Propuesta:
   auto-aplicar a los 300 ms + botón «Aplicar ahora». Riesgo: escrituras frecuentes
   en el config y en disco; mitigable con un guardado diferido a 2 s.
2. **¿«Avanzado» como interruptor global o como acordeón dentro de cada grupo?**
   Propuesta: interruptor global arriba a la derecha (más predecible).
3. **¿Un look debe poder guardarse con nombre propio?** Hoy no hay clave para eso.
   Opción A: no (sólo los 4 fijos + personalizado). Opción B: sí, con una clave nueva
   `look_id` (aditivo, requiere tocar backend).
4. **¿Prioridad del bloque Tiempo frente a Aspecto?** Propuesta: Tiempo primero, porque
   es donde vive el negocio (coins) y donde el operador entra cada día.
5. **¿La preview debe ser interactiva** (poder disparar un evento de prueba y ver el
   popup) o sólo lectura? Propuesta: lectura en Fase 4, interactiva en una fase aparte.
