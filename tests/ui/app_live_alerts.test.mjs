// Tests de la logica de alertas del panel (src/platform/ui/app.js).
//
// app.js es un IIFE y en el navegador solo se expone a traves de los hooks
// __NLP3_UI_TEST__ (ver el final del archivo). Aqui se carga el archivo real en
// un contexto de Node con el minimo DOM necesario, sin tocar la produccion.
//
//   node --test tests/ui/

import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const appPath = path.resolve(here, "..", "..", "src", "platform", "ui", "app.js");

// Solo los elementos que renderLiveAlerts() usa: el resto del DOM se deja en
// null para que las guardas (if (!els.x) return) se comporten como en una
// pagina que todavia no termino de montarse.
const ALERT_IDS = new Set(["#live-alerts", "#live-alerts-list", "#live-alerts-title"]);

function fakeElement() {
  return {
    hidden: false,
    innerHTML: "",
    textContent: "",
    value: "",
    checked: false,
    open: false,
    disabled: false,
    classList: {
      add() {},
      remove() {},
      toggle() {},
      contains() {
        return false;
      },
    },
    addEventListener() {},
    removeEventListener() {},
    focus() {},
    scrollIntoView() {},
    closest() {
      return null;
    },
    getAttribute() {
      return null;
    },
    setAttribute() {},
    removeAttribute() {},
    appendChild() {},
    querySelector() {
      return null;
    },
    querySelectorAll() {
      return [];
    },
  };
}

function loadApp() {
  const hooks = {
    api: null,
    expose(api) {
      hooks.api = api;
    },
  };

  const sandbox = {
    console,
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    AbortController,
    fetch: async () => {
      throw new Error("los tests no deberian llamar a fetch");
    },
    document: {
      visibilityState: "visible",
      hidden: false,
      title: "",
      body: fakeElement(),
      documentElement: fakeElement(),
      querySelector(selector) {
        return ALERT_IDS.has(String(selector)) ? fakeElement() : null;
      },
      querySelectorAll() {
        return [];
      },
      createElement() {
        return fakeElement();
      },
      addEventListener() {},
      removeEventListener() {},
    },
    window: {
      location: { href: "http://127.0.0.1/", search: "" },
      visualViewport: null,
      addEventListener() {},
      removeEventListener() {},
      setInterval,
      clearInterval,
    },
    location: { href: "http://127.0.0.1/", search: "" },
    localStorage: {
      getItem() {
        return null;
      },
      setItem() {},
      removeItem() {},
      clear() {},
    },
    navigator: { userAgent: "node-test", clipboard: null },
    performance: { now: () => Date.now() },
    __NLP3_UI_TEST__: hooks,
  };

  const context = vm.createContext(sandbox);
  vm.runInContext(fs.readFileSync(appPath, "utf8"), context, { filename: appPath });
  assert.ok(hooks.api, "app.js no expuso los hooks __NLP3_UI_TEST__");
  return hooks.api;
}

function bridgePayload({ code = "", severity = "", message = "", action = "", connectionState = "reconnecting" } = {}) {
  const external = {
    connectionState,
    lastStatusTimestampMs: Date.now(),
    lastAlertCode: code,
    lastAlertSeverity: severity,
    lastAlertAction: action,
    lastStatusMessage: message,
  };
  return { snapshot: { externalBridge: external } };
}

test("pushLiveAlert deduplica por clave, acumula el conteo y corta en 8", () => {
  const app = loadApp();

  assert.equal(app.pushLiveAlert("error", "", "sin titulo"), null);

  const first = app.pushLiveAlert("error", "No conecta", "Código: X");
  const second = app.pushLiveAlert("error", "No conecta", "Código: X");
  assert.equal(first, second, "la misma alerta debe reusarse, no duplicarse");
  assert.equal(first.count, 2);
  assert.equal(app.state.liveAlerts.length, 1);

  for (let index = 0; index < 12; index += 1) {
    app.pushLiveAlert("warn", `Aviso ${index}`, "");
  }
  assert.equal(app.state.liveAlerts.length, 8, "la lista se limita a 8 avisos");
});

test("pushLiveAlert guarda la accion y la clave de descarte del bridge", () => {
  const app = loadApp();

  const alert = app.pushLiveAlert("warn", "Rotando", "Código: KEY", {
    action: "rotate_key",
    bridgeKey: "warn|KEY",
  });

  assert.equal(alert.action, "rotate_key");
  assert.equal(alert.bridgeKey, "warn|KEY");
  assert.equal(app.liveAlertActionView(alert.action).button, "Ver API keys");
  assert.equal(app.liveAlertActionView("none"), null, "'none' no muestra nada");
  assert.equal(app.liveAlertActionView("accion-desconocida"), null);
  assert.equal(app.liveAlertActionView(""), null);
});

test("renderLiveAlerts pone severidad, codigo, accion y descarte, y escapa el HTML", () => {
  const app = loadApp();

  app.pushLiveAlert("error", 'Mal <img src=x onerror="alert(1)">', "Código: NET", {
    action: "rotate_key",
  });
  const markup = app.els.liveAlertsList.innerHTML;

  assert.match(markup, /live-alert-badge/, "falta la etiqueta de severidad");
  assert.match(markup, /Código: NET/, "falta el detalle del codigo");
  assert.match(markup, /Qu&eacute; hacer: /, "falta la fila de accion");
  assert.match(markup, /data-alert-intent="keys"/, "falta el boton de accion");
  assert.match(markup, /data-alert-dismiss=/, "falta el boton de descartar");
  assert.doesNotMatch(markup, /<img/, "el titulo debe escaparse");
  assert.match(markup, /&lt;img/, "el titulo debe salir escapado, no crudo");

  // Sin accion no hay fila de "que hacer".
  app.state.liveAlerts = [];
  app.state.liveAlertsMarkup = "";
  app.pushLiveAlert("info", "Solo aviso", "Código: OTHER");
  assert.doesNotMatch(app.els.liveAlertsList.innerHTML, /Qu&eacute; hacer:/);

  // Con la lista vacia el bloque entero se oculta.
  app.state.liveAlerts = [];
  app.renderLiveAlerts();
  assert.equal(app.els.liveAlerts.hidden, true);
});

test("la alerta del bridge se reescribe en el mismo lugar en vez de apilar", () => {
  const app = loadApp();

  app.detectIssueTransitions(
    bridgePayload({ code: "NOT_LIVE", severity: "warn", message: "Esperando vivo. Intento en 30s" })
  );
  assert.equal(app.state.liveAlerts.length, 1);
  assert.equal(app.state.liveBridgeAlert.action, "");

  // La cuenta atras cambia el mensaje: misma fila, texto nuevo.
  app.detectIssueTransitions(
    bridgePayload({ code: "NOT_LIVE", severity: "warn", message: "Esperando vivo. Intento en 5s" })
  );
  assert.equal(app.state.liveAlerts.length, 1, "se apilo una alerta por cada tic");
  assert.equal(app.state.liveBridgeAlert.title, "Esperando vivo. Intento en 5s");

  // Con accion nueva se reescribe tambien.
  app.detectIssueTransitions(
    bridgePayload({ code: "NOT_LIVE", severity: "warn", message: "Esperando vivo", action: "wait_for_live" })
  );
  assert.equal(app.state.liveAlerts.length, 1);
  assert.equal(app.state.liveBridgeAlert.action, "wait_for_live");

  // Cambia el codigo: es otra alerta.
  app.detectIssueTransitions(
    bridgePayload({ code: "QUOTA", severity: "error", message: "Sin cuota", action: "check_key" })
  );
  assert.equal(app.state.liveAlerts.length, 2);
});

test("descartar la alerta del bridge no la vuelve a traer mientras siga el codigo", () => {
  const app = loadApp();

  app.detectIssueTransitions(
    bridgePayload({ code: "NOT_LIVE", severity: "warn", message: "Esperando vivo" })
  );
  const alert = app.state.liveBridgeAlert;
  app.dismissLiveAlert(alert.key);

  assert.equal(app.state.liveAlerts.length, 0);
  app.detectIssueTransitions(
    bridgePayload({ code: "NOT_LIVE", severity: "warn", message: "Esperando vivo (2)" })
  );
  assert.equal(app.state.liveAlerts.length, 0, "volvio a aparecer un aviso descartado");

  // Un status sano borra la memoria de descartes...
  app.detectIssueTransitions(bridgePayload({ code: "", connectionState: "connected" }));
  // ...asi que el mismo codigo, si vuelve, avisa de nuevo.
  app.detectIssueTransitions(
    bridgePayload({ code: "NOT_LIVE", severity: "warn", message: "De nuevo fuera de vivo" })
  );
  assert.equal(app.state.liveAlerts.length, 1);
});

test("Limpiar descarta tambien la alerta actual del bridge", () => {
  const app = loadApp();

  app.detectIssueTransitions(
    bridgePayload({ code: "QUOTA", severity: "error", message: "Sin cuota" })
  );
  assert.equal(app.state.liveAlerts.length, 1);

  app.clearLiveAlerts();
  assert.equal(app.state.liveAlerts.length, 0);

  app.detectIssueTransitions(
    bridgePayload({ code: "QUOTA", severity: "error", message: "Sin cuota" })
  );
  assert.equal(app.state.liveAlerts.length, 0, "Limpiar no serviria de nada si volviera al instante");
});

test("una alerta sin codigo en el payload limpia el estado del bridge", () => {
  const app = loadApp();

  app.detectIssueTransitions(
    bridgePayload({ code: "STREAM_DISCONNECTED", severity: "error", message: "Se corto" })
  );
  assert.equal(app.state.lastBridgeAlertKey, "error|STREAM_DISCONNECTED");

  app.detectIssueTransitions(bridgePayload({ code: "", connectionState: "connected" }));
  assert.equal(app.state.lastBridgeAlertKey, "");
  assert.equal(app.state.liveBridgeAlert, null);
});
