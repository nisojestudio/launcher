# Live Timer — Fase 4: tareas

Diseño y decisiones: `design.md`. Convención: criterios de aceptación explícitos,
verificación por tarea, y nada que toque más de ~5 archivos.

Orden por dependencia, no por importancia. Cada tarea deja el sistema funcionando.

**Regla de la fase:** ninguna tarea cambia el contrato de `POST /api/timer/configure`
ni el comportamiento del reloj. El backend y el overlay quedan intactos salvo la
corrección puntual de la preview (T2).

---

## T1 — Modelo puro de configuración + tests

**Descripción.** Extraer a un módulo sin DOM la descripción de las ~44 claves y el
mapeo ida/vuelta con el backend. Es la pieza que hace testeable todo lo demás y
donde vive el riesgo de perder un campo. De paso corrige los defaults del formulario
actual, que contradicen al modelo (`|| 300` reintroduce los 5 minutos de regalo de
Fase 1).

**Criterios de aceptación:**
- [ ] `ui/timer-config-model.js` declara las 44 claves con `label`, `unit`, `group`,
      `min`, `max`, `step`, `level` (basico/avanzado) y `help`
- [ ] `toBackend(fromBackend(json)) === json` para el JSON real de `GET /api/timer/config`
- [ ] `fromBackend` no pierde ninguna clave desconocida (las conserva en `_extra`)
- [ ] Un campo vacío produce el **default del modelo** (`initial_time_s: 0`), no `300`
- [ ] `applyLook(estado, 'cyber-blue')` cambia colores, glow y efecto de dígitos de
      forma coherente, y los 4 looks + `custom` están cubiertos
- [ ] Sin dependencias nuevas: sólo JS estándar

**Verificación:**
- [ ] `powershell -File tools/ui/run_timer_config_tests.ps1` → verde con `node --test`
- [ ] `ctest --preset release` incluye `nlp3_timer_config_model_test` y **se salta**
      con mensaje claro si no hay `node` en la máquina
- [ ] Comparación campo a campo contra el JSON de producción: no falta ninguna clave

**Dependencias:** ninguna.
**Archivos:** `src/platform/ui/timer-config-model.js` (nuevo),
`tools/ui/timer-config-model.test.mjs` (nuevo),
`tools/ui/run_timer_config_tests.ps1` (nuevo), `tests/CMakeLists.txt`.
**Scope:** M (4 archivos).

---

## T2 — Preview escalado de verdad (y alcanzable con el ratón)

**Descripción.** El arreglo de mayor retorno. Hoy la preview nace `hidden`
(`index.html:252`) y sólo la abre la tecla `V`; y aunque se abra, el iframe a 240 px
(180 px en pantallas bajas) recorta un diseño de 1920×1080. Se hace visible, se
escala con `transform` y se le devuelve el sentido al modo preview del overlay.

**Criterios de aceptación:**
- [ ] `#timer-preview-wrapper` se abre y se cierra con el ratón, sin teclado
- [ ] El iframe renderiza a 1920×1080 y se escala al ancho de su caja, sin recortes
- [ ] Medido con el estado real del operador: `scrollWidth/Height` del contenido
      ≤ caja, en 4 tamaños de ventana (1280×720, 1600×900, 1920×1080, 2560×1440)
- [ ] En `body.preview-mode`, el tamaño del contador/título/subtítulo ya no es el
      inline de 120/48/32 px (el modo preview vuelve a surtir efecto)
- [ ] La ventana baja (≤800 px de alto) mantiene la preview usable, sin cortar
- [ ] Cero errores de consola

**Verificación:**
- [ ] Medición con Playwright contra el panel en marcha, antes/después, con capturas
- [ ] `ninja -C build/release` + `ctest --preset release` (32/32 + el nuevo de T1)
- [ ] Ojo: comparar antes/después del overlay (`overlay/live-timer.html`) para dejar
      constancia de que el cambio es sólo el escalado, no el diseño

**Dependencias:** ninguna (se puede hacer primero).
**Archivos:** `src/platform/ui/index.html`, `src/platform/ui/styles.css`,
`src/platform/ui/app.js`, `src/platform/overlay/live-timer.html`.
**Scope:** S (4 archivos, pocas líneas).

> **Checkpoint 1 (tras T1–T2):** la preview se ve bien y el modelo está testeado.
> Si algo de lo siguiente falla, el sistema sigue siendo mejor que antes.

---

## T3 — Shell del nuevo panel: dos columnas, pestañas y Avanzado

**Descripción.** Montar el contenedor nuevo (preview sticky a la izquierda, grupos a
la derecha) con los tokens del design system, dejando el formulario viejo detrás de
un flag para poder comparar.

**Criterios de aceptación:**
- [ ] Tokens nuevos en `:root` (`--space-*`, `--control-h`, `--focus-ring`,
      `--field-label`, `--group-gap`) y ningún color literal nuevo
- [ ] Contenedor a dos columnas; < 1100 px pasa a una columna con preview colapsable
- [ ] 4 pestañas + interruptor «Avanzado»; el estado de la pestaña activa se conserva
      mientras se navega por el panel
- [ ] El formulario viejo sigue accesible tras `?timer-config=legacy` y funciona igual
- [ ] Nada del shell depende del modelo todavía (se puede montar vacío)

**Verificación:**
- [ ] Capturas en 1280×720, 1920×1080 y 2560×1440
- [ ] Build + tests verdes

**Dependencias:** T2.
**Archivos:** `src/platform/ui/timer-config.css` (nuevo),
`src/platform/ui/index.html`, `src/platform/ui/styles.css`.
**Scope:** M (3 archivos).

---

## T4 — Grupo «Tiempo» (segundos y coins)

**Descripción.** Los 7 controles de tiempo como filas con unidad y rango visible:
tiempo inicial, tope, y «cuánto suma cada evento» con signo (permite restar).

**Criterios de aceptación:**
- [ ] Las 7 claves de tiempo (`initial_time_s`, `max_time_s`, `time_per_like_s`,
      `time_per_share_s`, `time_per_follow_s`, `time_per_gift_coin_s`,
      `time_per_chat_s`) editables desde el grupo nuevo
- [ ] Cada fila muestra unidad (`s`, `min`) y el rango permitido de forma visible
- [ ] El valor por coin va primero y con frase de ayuda explícita
- [ ] Atajos de tiempo inicial: 1, 5, 10, 30 min y 1 h
- [ ] Valores negativos permitidos donde el servidor los acepta (per-event)
- [ ] Rangos coherentes con `validateTimerConfigClientSide()` (1 s–1 año, ±3600 s)
- [ ] «Restablecer» del grupo sólo toca las claves del grupo

**Verificación:**
- [ ] Test del modelo: cada fila escribe exactamente su clave y ninguna otra
- [ ] `GET /api/timer/config` antes/después de mover un slider: sólo cambia esa clave
- [ ] `nlp3_live_timer_api_smoke_test` verde (el reloj no se altera)

**Dependencias:** T1, T3.
**Archivos:** `src/platform/ui/timer-config.js` (nuevo),
`src/platform/ui/index.html`, `src/platform/ui/timer-config.css`.
**Scope:** M (3 archivos).

---

## T5 — Grupo «Aspecto»: tipografía y tamaños

**Descripción.** Selector de tipografía que se ve a sí mismo (incluye las mono que el
overlay ya carga) y tamaños con muestra en vivo, por elemento.

**Criterios de aceptación:**
- [ ] `FontPicker` renderiza cada opción con su propia fuente; incluye JetBrains Mono,
      Space Mono y Share Tech Mono (las que el overlay carga por Google Fonts)
- [ ] `SliderNumber` de 8–400 px por elemento (título, contador, subtítulo) con el
      valor numérico editable y la muestra escalando en vivo
- [ ] Aviso visible si la fuente elegida no es una de las que el overlay garantiza
- [ ] Se mantiene el comportamiento de `font_family` actual (misma clave, mismo formato)

**Verificación:**
- [ ] Comparar la lista de familias ofrecidas con las cargadas en `overlay/live-timer.html`
- [ ] Aplicar una fuente y comprobar en la preview escalada que se ve igual que en el overlay

**Dependencias:** T3.
**Archivos:** `src/platform/ui/timer-config.js`, `src/platform/ui/index.html`,
`src/platform/ui/timer-config.css`.
**Scope:** M (3 archivos).

---

## T6 — Grupo «Aspecto»: colores y tarjetas de Look

**Descripción.** Los 4 looks como tarjetas visuales que cambian el conjunto de
colores/efectos de golpe, más color por elemento con paleta curada y hex.

**Criterios de aceptación:**
- [ ] 4 tarjetas de Look (Neón, Cyber, Clean, Rose Gold) + «Personalizado»; la tarjeta
      muestra una miniatura real del look, no un nombre
- [ ] Elegir un look escribe `color_preset` y los colores dependientes en **una sola**
      operación; editar luego un color pasa el look a «Personalizado»
- [ ] `ColorField` con muestra + hex validado (`#RGB`/`#RRGGBB`, ya existe la regex) +
      paleta de 12 colores curados
- [ ] Los 7 colores configurables cubiertos: 3 de fuente, 2 de popup, 1 de glow,
      1 de texto de completado
- [ ] Coherencia con el overlay: los presets deben ser los mismos que
      `PRESET_COLORS` de `overlay/live-timer.html`

**Verificación:**
- [ ] Test del modelo: `applyLook()` produce el conjunto esperado por look
- [ ] Cambiar de look en la preview y comprobar los colores contra el overlay real

**Dependencias:** T3.
**Archivos:** `src/platform/ui/timer-config.js`, `src/platform/ui/index.html`,
`src/platform/ui/timer-config.css`.
**Scope:** M (3 archivos).

> **Checkpoint 2 (tras T4–T6):** Tiempo y Aspecto completos y verificados. La preview
> ya refleja cada cambio en vivo. Se puede enseñar al operador para feedback antes de
> seguir con Efectos.

---

## T7 — Grupo «Efectos» (con demostración)

**Descripción.** Elegir efecto viendo el efecto: tarjetas que se animan al pasar el
ratón, glow por elemento y sus parámetros globales en Avanzado.

**Criterios de aceptación:**
- [ ] `EffectPicker` por elemento con «Ninguno / Glow / Pulso», cada tarjeta animando
      su propio efecto al hover (y respetando `prefers-reduced-motion`)
- [ ] Interruptor de glow por elemento (3 banderas) junto a su efecto
- [ ] `glow_color`, `glow_intensity_px` (1–60) y `pulse_speed_s` (0.2–5) en Avanzado,
      con la advertencia de que afectan a los 3 elementos
- [ ] Efecto de dígitos (none/flip/roll/pop/fade) como control segmentado con icono
- [ ] Los valores enviados son exactamente los que valida el overlay (`none|glow|pulse`)

**Verificación:**
- [ ] Test del modelo: sólo se aceptan los valores de la lista blanca
- [ ] Comprobar en la preview que pulso y glow se ven; y en el overlay real por la URL pública

**Dependencias:** T3.
**Archivos:** `src/platform/ui/timer-config.js`, `src/platform/ui/index.html`,
`src/platform/ui/timer-config.css`.
**Scope:** M (3 archivos).

---

## T8 — Grupo «Avisos y sonido»

**Descripción.** Colores de popup con simulación, texto de completado y los tres
audios con botón Probar.

**Criterios de aceptación:**
- [ ] `popup_add_color` y `popup_subtract_color` con una simulación de popup que se
      dispara al cambiarlos
- [ ] Texto, color y tamaño (8–400) de «completado» con vista previa del banner
- [ ] Audio de completado: ruta, volumen (0–2), repetir, y botón **Probar**
- [ ] Tick y suma: ruta y volumen, en Avanzado, con Probar
- [ ] Estado vacío explícito: «Sin audio: silencio» en vez de una ruta en blanco
- [ ] Prueba de audio con el `Audio` del navegador, sin bloquear la UI si falla

**Verificación:**
- [ ] Aplicar cada cambio y comprobar `GET /api/timer/config`
- [ ] Probar un audio real y comprobar que suena con el volumen configurado

**Dependencias:** T3.
**Archivos:** `src/platform/ui/timer-config.js`, `src/platform/ui/index.html`,
`src/platform/ui/timer-config.css`.
**Scope:** M (3 archivos).

---

## T9 — Flujo de guardado: auto-aplicar, estados y export/import

**Descripción.** Quitarle peso al botón «Aplicar»: auto-aplicar con debounce, estado
legible, y conservar exportar/importar con vista del JSON.

**Criterios de aceptación:**
- [ ] Auto-aplicar a los 300 ms del último cambio + botón «Aplicar ahora» para forzar
- [ ] Estado visible: «Guardando…» / «Guardado» / «Error (motivo)»
- [ ] Sin escrituras duplicadas: un cambio rápido de slider produce un solo POST
- [ ] Los avisos del servidor (warnings) se muestran sin perder la configuración
- [ ] Exportar/Importar conservan el comportamiento actual; importar valida antes de aplicar
- [ ] Si el POST falla, la UI no miente: queda en «Error» y reintenta al siguiente cambio

**Verificación:**
- [ ] Contar los POST con el log del panel al mover un slider de punta a punta
- [ ] Cortar el panel (matar proceso) durante un guardado y comprobar el estado de error
- [ ] `ctest --preset release` verde

**Dependencias:** T4–T8.
**Archivos:** `src/platform/ui/app.js`, `src/platform/ui/timer-config.js`,
`src/platform/ui/index.html`.
**Scope:** M (3 archivos).

---

## T10 — Retirada del formulario viejo, accesibilidad y cierre visual

**Descripción.** Borrar el sistema antiguo, dejar el nuevo como único camino y cerrar
accesibilidad y detalles visuales.

**Criterios de aceptación:**
- [ ] El bloque de 5 acordeones (`tv-section`) y su CSS asociado se eliminan;
      `?timer-config=legacy` también desaparece
- [ ] Ningún control huérfano: las 44 claves siguen siendo configurables
- [ ] Foco visible en todos los controles nuevos; recorrido completo con teclado;
      `aria-describedby` enlazando ayuda y control
- [ ] Etiquetas de campo accesibles (`<label for>`), sin `span` sueltos como etiqueta
- [ ] ≤ 15 controles visibles sin abrir Avanzado
- [ ] Capturas finales en 3 tamaños y comparación con las de T3

**Verificación:**
- [ ] Recorrido completo sólo con `Tab`, sin ratón, cambiando look y tiempo por coin
- [ ] `node --test` + `ctest --preset release` + medición de recorte de la preview
- [ ] Checklist de `skills/references/definition-of-done.md`

**Dependencias:** T9.
**Archivos:** `src/platform/ui/index.html`, `src/platform/ui/app.js`,
`src/platform/ui/styles.css`, `src/platform/ui/timer-config.css`.
**Scope:** M (4 archivos).

> **Checkpoint 3 (tras T9–T10):** rediseño terminado y verificado. Toca release.

---

## T11 — Release 0.3.2 con el rediseño

**Descripción.** Publicar el rediseño en el escritorio del operador siguiendo el
protocolo, con backup y tests.

**Criterios de aceptación:**
- [ ] `CHANGELOG.md` con entrada `## 0.3.2 - <fecha>` (Added/Changed/Removed)
- [ ] Bump en `CMakeLists.txt`, `src/platform/CMakeLists.txt`, `vcpkg.json`
- [ ] `prepare_release.ps1 -Version 0.3.2 -BackupMode code` termina con gates
      `build/tests/installer/backup = passed`
- [ ] El binario nuevo de `dist/releases/0.3.2/` es el que elige el launcher de escritorio
- [ ] `AGENTS.md` §11 actualizado con el nuevo baseline y el plan de rollback

**Verificación:**
- [ ] `SHA256SUMS.txt` coincide con los artefactos
- [ ] Abrir el panel desde el acceso directo y recorrer el nuevo panel de configuración
- [ ] La preview no recorta y el overlay público sigue igual

**Dependencias:** T10.
**Archivos:** ninguno en código; es release (`CHANGELOG.md`, `AGENTS.md`).
**Scope:** S.

---

## Checkpoints

| Checkpoint | Tras | Qué se valida |
|---|---|---|
| 1 | T1–T2 | Modelo testeado + preview sin recortes (el arreglo más visible, temprano) |
| 2 | T4–T6 | Tiempo y Aspecto completos; feedback del operador antes de seguir |
| 3 | T9–T10 | Rediseño cerrado, accesible y sin el sistema viejo |

## Fuera de alcance de la fase

Ver `design.md` §9. En resumen: no se toca la paleta del resto del panel, no hay
perfiles por operador, no hay editor de posiciones y no se añaden animaciones nuevas
al overlay.
