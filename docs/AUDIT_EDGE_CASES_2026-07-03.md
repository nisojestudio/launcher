# Audit Traceability — Edge Cases 2026-07-03

## Proposito

Documento de trazabilidad para 4 hallazgos edge-case identificados durante una
auditoria profunda del proyecto. En el contexto de uso actual del producto
(instalacion single-user, un solo operador, una sola maquina, modo `--ui`,
handles TikTok ASCII, flujo sin logout) **ninguno se manifiesta ni requiere
correccion urgente**.

Este runbook existe para dejar trazabilidad por si el contexto de uso cambia
mas adelante. Cada entrada describe el hallazgo, el contexto en el que es
inofensivo, el contexto en el que pasaria a ser relevante y la solucion
fundamentada para cuando toque abordarlo.

## Contexto de uso actual (inocuo)

- Instalacion single-user, un operador, una maquina.
- Arranque en modo `--ui` (no `--console`).
- Handles TikTok operados en ASCII (ej. `cocadevidrio80`).
- El operador no usa el flujo de logout explicitamente.
- No hay segundo usuario con acceso fisico al disco del host.

Bajo este contexto los 4 hallazgos son hipoteticos.

## Validacion ejecutada el 2026-07-03

- `ctest --test-dir build/release -j4` -> 31/31 PASSED en 20.90s.
- `python -m unittest discover -s tools/bridge_py/tests` -> 38 OK.
- `python -m unittest discover -s tools/game_bridge_py/tests` -> 3 OK.
- `dumpbin /dependents build/release/src/platform/NisojeStudio.exe` -> CRT Release (`MSVCP140.dll`, `VCRUNTIME140.dll`), sin `*D.dll`. Gate 2.6 pasado.
- Hardening de Origin en WS confirmado operativo (un test del bridge rechaza `https://example.invalid` con `InvalidOrigin`).

Total: 72 tests ejecutados, todos pasan.

## Hallazgos

### EC-1 — Hilo detached con use-after-free potencial en `--console`

- **Archivo:** `src/platform/main.cpp:601-614`
- **Severidad en uso actual:** Baja (no se manifiesta).
- **Severidad si cambia el contexto:** Alta.

**Descripcion:**
El hilo de lectura de consola se crea solo cuando `launch_options.console_mode`
es verdadero (linea 603). Captura por referencia `input_mutex`, `pending_lines`
e `input_closed` (locales de `run_application`) y se llama
`input_thread->detach()` (linea 614) sin join. Al salir del bucle principal,
`run_application` retorna y destruye esas variables mientras el hilo detached
puede seguir bloqueado en `std::getline(std::cin, ...)`.

**Por que es inofensivo ahora:**
El opereador arranca con `--ui` (README linea 106). En modo UI `console_mode`
es falso y la rama del hilo no se ejecuta. El defecto solo se activa en
`--console`, que es modo de diagnostico avanzado.

**Contexto que lo activaria:**
- Uso rutinario de `--console` para diagnostico.
- Cierre del panel desde modo consola sin cerrar stdin primero.

**Solucion fundamentada (cuando toque):**
Reemplazar `detach()` por un mecanismo de cierre cooperativo:
1. Senalizar cierre (cerrar/redirigir `std::cin` o usar una atomic flag).
2. Hacer `input_thread->join()` antes de retornar de `run_application`.
3. Alternativamente, mover las variables compartidas a un objeto cuyo lifetime
   se extienda hasta que el hilo termine (y unirse en el destructor).

Validacion: arrancar `NisojeStudio.exe --console`, ingresar comandos y cerrar
la ventana; el proceso no debe quedar colgado ni generar un crash de
use-after-free.

---

### EC-2 — `widen()` destruye UTF-8 en `external_bridge_runner`

- **Archivo:** `src/platform/external_bridge_runner.cpp:29-31`
- **Severidad en uso actual:** Baja-Media (no se manifiesta con handles ASCII).
- **Severidad si cambia el contexto:** Alta.

**Descripcion:**
La funcion helper `widen(std::string_view text)` hace
`return std::wstring(text.begin(), text.end());`, lo que copia cada byte UTF-8
como un `wchar_t` independiente. Se usa en `build_command_line` para
`--user <target_user>` (linea 169). Nombres de usuario TikTok con acentos,
emojis o cualquier no-ASCII llegan corruptos al proceso Python.

El mismo helper existe en `src/platform/external_game_bridge_runner.cpp:24`
implementado correctamente con `MultiByteToWideChar(CP_UTF8, ...)`. La
divergencia es la evidencia de que es un bug, no una decision de diseno.

**Por que es inofensivo ahora:**
El operador usa handles TikTok ASCII (`cocadevidrio80` y similares). Con ASCII
byte-a-wchar produce el resultado correcto por accidente.

**Contexto que lo activaria:**
- Conexion a un live cuyo handle contiene acentos, emojis o caracteres no
  latinos (ej. handles en cirilico, arabe, asiaticos o con emoji decorativo).

**Solucion fundamentada (cuando toque):**
Reemplazar la implementacion de `widen` en `external_bridge_runner.cpp:29`
por `MultiByteToWideChar(CP_UTF8, ...)` identica a la de
`external_game_bridge_runner.cpp:24`. Mejor aun: extraer ambos a un helper
comun `src/platform/string_util.{hpp,cpp}` y reusar, para evitar la
divergencia que origino el bug.

Validacion: arrancar un bridge externo con `--user` que contenga un caracter
multi-byte (ej. `naïve` o un emoji) y verificar que el runner Python recibe el
handle intacto en `sys.argv`.

---

### EC-3 — `timerWasRunning` en scope equivocado; el `confirm` nunca aparece

- **Archivo:** `src/platform/ui/app.js:3127` (declaracion) y `app.js:2082` (uso)
- **Severidad en uso actual:** Baja (no crashea, omite una confirmacion).
- **Severidad si cambia el contexto:** Baja-Media.

**Descripcion:**
En `bindEvents()` (linea 3127) se declara `let timerWasRunning = false;` local
a esa funcion. Pero `renderTimer()` (definida fuera de `bindEvents`, linea
2077) ejecuta `timerWasRunning = !!(timer.running && !timer.paused && !timer.completed);`
(linea 2082) sin `let`/`var`. Como la IIFE no usa `"use strict"` y
`timerWasRunning` no fue declarada en el scope de la IIFE, esa asignacion crea
una propiedad implicita en `window` (global). La variable local de
`bindEvents` queda siempre `false`.

Resultado: el guard `if (timerWasRunning && !confirm("¿Reiniciar el timer? Se
perdera el progreso actual.")) return;` en linea 3129 siempre evalua `false`,
asi que el boton `Start` reinicia el timer **sin advertir** incluso cuando
estaba corriendo, descartando el progreso del live. Ademas se filtra una
variable global a `window`.

**Por que es inofensivo ahora:**
No genera error visible. El operador normalmente hace click en `Start` solo
cuando el timer esta parado (caso normal) o usa `Reset`/`Stop` explicitamente.
En el edge case "click en Start con timer corriendo", el timer se reinicia
pero el operador probablemente no esperaba un confirm que nunca ha visto.

**Contexto que lo activaria:**
- Uso intensivo del timer en live: el operador hace click en `Start` estando el
  timer corriendo, pierde tiempo acumulado del live sin aviso.

**Solucion fundamentada (cuando toque):**
Mover la declaracion de `timerWasRunning` al scope de la IIFE (junto a
`let _timerOverlayUrl = "";` en linea 16) para que `renderTimer` y
`bindEvents` compartan la misma variable. Tras el cambio, al hacer click en
`Start` con el timer corriendo debe aparecer el `confirm()`.

Validacion: arrancar el panel, iniciar el timer, hacer click en `Start` de
nuevo y verificar que aparece el dialogo de confirmacion.

---

### EC-4 — Contraseña Firebase en texto plano persistida en localStorage

- **Archivo:** `src/platform/ui/app.js:426,429` (persistencia) y
  `app.js:467-473,2518` (limpieza incompleta en logout)
- **Severidad en uso actual:** Informativa (sin amenaza explotable).
- **Severidad si cambia el contexto:** Alta si el host pasa a multi-user o si
  se distribuye el producto publicamente.

**Descripcion:**
`saveAuthDraft()` (linea 423) serializa
`{ email, password, licenseKey }` a `localStorage["nlp3-auth-form-v1"]` con la
password en claro (linea 426, 429). `clearPersistentCredentials()` (linea 467)
solo borra `nlp3-auth-persistent-v1` (la cual guarda solo email+licenseKey, NO
password). `logoutAccess()` (linea 2512) llama a
`clearPersistentCredentials()` (linea 2518) pero no limpia el draft, asi que la
password sigue en localStorage tras logout. `attemptAutoLogin` la reutiliza en
cada arranque.

**Por que es inofensivo ahora:**
- No hay segundo usuario que pueda leer el `localStorage` de WebView2 (carpeta
  `%LOCALAPPDATA%\NisojeStudio\`).
- El operador no usa logout; el auto-login resultante es, de hecho, el
  comportamiento que quiere ("entro sin teclear en cada arranque").
- La API key de Firebase en `panel_config.json` es publica por diseno (Firebase
  Auth la expone en el cliente); la password del operador que se persiste es
  la del UI gate, no un secreto de servicio.

La amenaza solo se materializa con acceso fisico al disco del host (otro
usuario, malware, entorno compartido, copia del storage).

**Contexto que lo activaria:**
- El host pasa a ser compartido por mas de un operador.
- El producto se distribuye publicamente a terceros.
- Politica de compliance que exija no persistir credenciales en claro.

**Solucion fundamentada (cuando toque):**
1. No persistir `password` en el draft (guardar solo `email` + `licenseKey`).
2. En `logoutAccess`, anadir
   `window.localStorage.removeItem(AUTH_FORM_STORAGE_KEY);`.
3. Opcional: anadir un checkbox "Recordar en este equipo" explicito antes de
   persistir cualquiera de los campos.

Validacion: tras logout, inspeccionar `localStorage` en DevTools de WebView2 y
verificar que `nlp3-auth-form-v1.password` ya no existe.

## Matriz de decision

| Hallazgo | Toca corregir si... | No hace falta si... |
|---|---|---|
| EC-1 | Se empieza a usar `--console` repetidamente para diagnostico | Se sigue usando solo `--ui` |
| EC-2 | Se conecta a lives con handles no-ASCII (acentos, emojis) | Los handles siguen siendo ASCII |
| EC-3 | El operador hace click en `Start` con el timer corriendo y se quiere proteger el progreso | El flujo de timer no incluye ese edge case |
| EC-4 | El host pasa a multi-user, o se distribuye publicamente, o hay politica de compliance | Sigue siendo single-user en la maquina del operador |

## Notas metodologicas

- Este runbook se basa en lectura estatica del codigo y validacion con 72 tests
  ejecutados (31 C++/CTest + 38 bridge_py + 3 game_bridge_py) sin modificar
  codigo.
- Los hallazgos EC-1 y EC-2 implican concurrencia/encoding; para validarlos
  formalmente se recomienda reproducir el flujo especifico que los activa
  (shutdown de `--console`, conexion con handle no-ASCII).
- La severidad "en uso actual" refleja el contexto declarado por el operador;
  cambiar el contexto reabre la evaluacion.