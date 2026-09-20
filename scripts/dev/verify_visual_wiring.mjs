// Verifica el contrato entre los controles del panel y el backend del timer.
//
// Por que existe: el mapeo de configuracion es explicito en ambos sentidos, asi
// que una clave que falte en cualquiera de los tres lados (HTML, app.js, handler
// HTTP) se ignora EN SILENCIO. Ese fue el origen de dos bugs reales de producto.
//
// Comprueba:
//   1. Todo id de la seccion de diseno del HTML esta referenciado en app.js.
//   2. Toda referencia $("#...") de app.js existe en el HTML.
//   3. Toda clave que emite readVisualEngineFromForm la acepta el handler HTTP.
//   4. Las opciones de los selects de fuentes son fuentes que el overlay carga.
//
// Uso: node scripts/dev/verify_visual_wiring.mjs

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const raiz = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const rutaHtml = join(raiz, "src", "platform", "ui", "index.html");
const rutaApp = join(raiz, "src", "platform", "ui", "app.js");
const rutaHttp = join(raiz, "src", "platform", "panel_http_server.cpp");
const rutaOverlay = join(raiz, "src", "platform", "overlay", "live-timer.html");

const html = readFileSync(rutaHtml, "utf8");
const app = readFileSync(rutaApp, "utf8");
const http = readFileSync(rutaHttp, "utf8");
const overlay = readFileSync(rutaOverlay, "utf8");

const fallos = [];
const avisos = [];

// --- 1. ids de la seccion de diseno -------------------------------------------------
const inicioSeccion = html.indexOf("Diseno (motor visual)");
const inicioBloque = html.lastIndexOf("<details", inicioSeccion);
const finBloque = html.indexOf("</details>", inicioSeccion);
const seccion = html.slice(inicioBloque, finBloque);

const idsSeccion = [...seccion.matchAll(/id="([^"]+)"/g)].map((m) => m[1]);
const idsHtml = new Set([...html.matchAll(/id="([^"]+)"/g)].map((m) => m[1]));

const referenciados = new Set([...app.matchAll(/\$\("#([^"]+)"\)/g)].map((m) => m[1]));

// Los botones de diseno se cablean en bucle con un id construido, asi que no
// aparecen como $("#id"). Se consideran cableados si el bucle existe y el preset
// correspondiente esta declarado.
const bucleDisenos = /getElementById\('timer-design-' \+ name\)/.test(app);

for (const id of idsSeccion) {
  if (referenciados.has(id)) continue; // cableado directo con $("#id")
  const esBotonDiseno = id.startsWith("timer-design-") && id !== "timer-design-status";
  if (esBotonDiseno) {
    const nombre = id.slice("timer-design-".length);
    const declarado = new RegExp(`^\\s{6}${nombre}: \\{`, "m").test(app);
    if (!bucleDisenos || !declarado) fallos.push(`boton de diseno sin cablear: #${id}`);
    continue;
  }
  if (!referenciados.has(id)) fallos.push(`control sin cablear en app.js: #${id}`);
}

// --- 2. referencias de app.js que no existen en el HTML -----------------------------
const huerfanos = [];
for (const id of referenciados) {
  if (!idsHtml.has(id)) huerfanos.push(id);
}
if (huerfanos.length > 0) {
  avisos.push(`${huerfanos.length} ids referenciados en app.js que no estan en index.html ` +
    `(preexistentes, se crean por JS o son restos): ${huerfanos.join(", ")}`);
}

// --- 3. claves que emite el formulario vs claves que acepta el backend --------------
const cuerpoLector = app.slice(
  app.indexOf("function readVisualEngineFromForm"),
  app.indexOf("function readTimerConfigFromForm"),
);
const clavesUi = new Set(
  [...cuerpoLector.matchAll(/put(?:Str|Int|Bool)\('([a-z_]+)'/g)].map((m) => m[1]),
);
// Las dos que se asignan directamente (no via put*) y las booleanas sin fallback.
for (const clave of ["progress_color", "particles_style", "particles_density"]) clavesUi.add(clave);

for (const clave of clavesUi) {
  // El handler acepta la clave si aparece en la lista del GET y en el parseo del POST.
  const apariciones = http.split(`"${clave}"`).length - 1;
  if (apariciones < 2) {
    fallos.push(`la clave '${clave}' que envia la UI no la acepta el handler HTTP (${apariciones} apariciones)`);
  }
}
if (clavesUi.size !== 27) {
  fallos.push(`readVisualEngineFromForm deberia emitir 27 claves del motor visual, emite ${clavesUi.size}`);
}

// --- 4. fuentes ofrecidas vs fuentes que el overlay carga ---------------------------
// Regla V7 de specs/live-timer-mejoras/visual.md: el overlay no debe ofrecer (ni el
// panel listar) una fuente que no se carga de verdad.
const linkFuentes = overlay.match(/fonts\.googleapis\.com\/css2\?([^"]+)"/);
const cargadas = new Set();
if (linkFuentes) {
  for (const parte of linkFuentes[1].split("&")) {
    const m = parte.match(/^family=([^:&]+)/);
    if (m) cargadas.add(decodeURIComponent(m[1].replace(/\+/g, " ")));
  }
}

for (const id of ["timer-title-font-family", "timer-counter-font-family", "timer-subtitle-font-family"]) {
  const ini = html.indexOf(`id="${id}"`);
  const fin = html.indexOf("</select>", ini);
  const bloque = html.slice(ini, fin);
  for (const m of bloque.matchAll(/value="([^"]*)"/g)) {
    // El valor lleva comillas HTML-escapadas y una lista de respaldo:
    //   &quot;Chakra Petch&quot;, sans-serif  ->  Chakra Petch
    //   Consolas, monospace                  ->  Consolas
    const familia = m[1].split(",")[0].replace(/&quot;/g, "").trim();
    if (/^(Segoe UI|Arial|Helvetica|Verdana|Trebuchet MS|Courier New|Consolas|Georgia|Impact|Times New Roman)$/.test(familia)) {
      continue; // fuentes del sistema, no necesitan carga
    }
    if (!cargadas.has(familia)) {
      fallos.push(`#${id} ofrece '${familia}', que el overlay no carga (fuente fantasma)`);
    }
  }
}

// --- informe ------------------------------------------------------------------------
console.log(`claves del motor visual en la UI : ${clavesUi.size}`);
console.log(`controles con id en la seccion    : ${idsSeccion.length}`);
console.log(`fuentes cargadas por el overlay   : ${[...cargadas].sort().join(", ")}`);

for (const aviso of avisos) console.log(`AVISO  ${aviso}`);
for (const fallo of fallos) console.log(`FALLO  ${fallo}`);
console.log(fallos.length === 0 ? "CONTRATO OK" : `CONTRATO ROTO (${fallos.length} fallos)`);
process.exit(fallos.length === 0 ? 0 : 1);
