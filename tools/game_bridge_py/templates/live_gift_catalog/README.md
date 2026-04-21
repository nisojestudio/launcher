# Live Gift Catalog Template

Plantilla reutilizable para juegos nuevos que reciben eventos `gift` desde Panel Live.

## Archivos

- `gift_catalog.generated.json`
  - catalogo completo trasladado desde el snapshot historico de `Nisoje.LivePanel.v2\app\assets\gifts\available_gifts.json`
  - cada entrada queda como:
    - `giftName`
    - `valor`
    - `accion`
- `gift_catalog.generated.js`
  - mismo catalogo, pero listo para cargarlo directo en un juego web sin import de JSON
- `gift_event_processor.js`
  - funcion generica para procesar eventos `gift` y disparar handlers por poder
- `gift_runtime_integration.js`
  - adapter reutilizable para conectar eventos `gift` del panel con el runtime de cualquier juego nuevo
- `gift_runtime_example.js`
  - ejemplo listo para copiar con handlers de gameplay y punto de entrada `onPanelBridgeEvent`

## Payload esperado

```json
{
  "kind": "gift",
  "user": {
    "id": "user-123",
    "name": "Nisoje"
  },
  "data": {
    "giftName": "Rose",
    "count": 3,
    "coins": 1,
    "diamond": 1
  },
  "ts": 1775675592005
}
```

## Integracion minima

```html
<script src="./gift_catalog.generated.js"></script>
<script src="./gift_event_processor.js"></script>
<script src="./gift_runtime_integration.js"></script>
<script>
  const handlers = window.NLP3GiftRuntimeIntegration.createDefaultGiftHandlers(game);
  const giftIntegration = window.NLP3GiftRuntimeIntegration.createPanelGiftIntegration({
    gameRuntime: game,
    handlers
  });

  function onPanelBridgeEvent(event) {
    return giftIntegration.handlePanelEvent(event);
  }
 </script>
```

## Integracion explicita con handlers custom

```html
<script src="./gift_catalog.generated.js"></script>
<script src="./gift_event_processor.js"></script>
<script src="./gift_runtime_integration.js"></script>
<script>
  const handlers = {
    disparos: (outcome) => game.fireShots(outcome),
    ola: (outcome) => game.spawnWave(outcome),
    laser: (outcome) => game.enableLaser(outcome),
    cazador: (outcome) => game.enableHunter(outcome),
    nuclear: (outcome) => game.triggerNuclear(outcome),
    nuclear_overdrive: (outcome) => game.triggerNuclearOverdrive(outcome),
    default: (outcome) => game.fireShots(outcome)
  };

  const giftIntegration = window.NLP3GiftRuntimeIntegration.createPanelGiftIntegration({
    catalog: window.NLP3GiftCatalog,
    handlers,
    powerWindows: [
      { accion: 'disparos', min: 1, max: 19 },
      { accion: 'ola', min: 20, max: 29 },
      { accion: 'laser', min: 30, max: 99 },
      { accion: 'cazador', min: 100, max: 199 },
      { accion: 'nuclear', min: 200, max: 499 },
      { accion: 'nuclear overdrive', min: 500, max: Infinity }
    ]
  });

  function onPanelBridgeEvent(event) {
    if (event.kind !== 'gift') return;
    return giftIntegration.handlePanelEvent(event);
  }
 </script>
```

## Ejemplo directo de llamada del panel

```js
const event = {
  kind: 'gift',
  user: { id: 'user-123', name: 'Nisoje' },
  data: {
    giftName: 'Rose',
    count: 3,
    coins: 1,
    diamond: 1
  },
  ts: 1775675592005
};

const result = window.NLP3GiftEventProcessor.processGiftEvent(event, {
  catalog: window.NLP3GiftCatalog,
  handlers: {
    disparos: (outcome) => game.fireShots(outcome),
    ola: (outcome) => game.spawnWave(outcome),
    laser: (outcome) => game.enableLaser(outcome),
    cazador: (outcome) => game.enableHunter(outcome),
    nuclear: (outcome) => game.triggerNuclear(outcome),
    nuclear_overdrive: (outcome) => game.triggerNuclearOverdrive(outcome),
    default: (outcome) => game.fireShots(outcome)
  }
});
```

## Como funciona

- El panel solo envia el evento `gift` generico.
- El juego resuelve internamente `giftName -> valor -> accion`.
- Los handlers traducen esa `accion` a funciones concretas del gameplay.
- Para agregar regalos nuevos, basta ampliar el catalogo.
- Para agregar tiers nuevos, puedes pasar `powerWindows` y handlers adicionales sin tocar `processGiftEvent`.

## Conexion con el bridge del panel

El runtime del juego debe llamar a la integracion cada vez que llegue un evento del panel:

```js
function onPanelBridgeEvent(event) {
  return giftIntegration.handlePanelEvent(event);
}
```

O bien conectar cualquier fuente que use callbacks:

```js
giftIntegration.attachToEventSource((callback) => {
  panelBridge.subscribe(callback);
  return () => panelBridge.unsubscribe(callback);
});
```

## Extension

- Nuevos regalos:
  - agrega entradas al catalogo y la funcion principal no cambia
- Nuevos poderes:
  - agrega `powerWindows` y un handler nuevo
- Fallback:
  - `default` o `fallbackAction` mantiene el juego operando aunque llegue un regalo no catalogado
## Reglas incluidas

- lookup por `giftName`
- uso de `coins` o `diamond` como valor unitario del regalo
- multiplicacion por `count`
- fallback a `disparos` si el regalo no existe en catalogo
- trigger especial: si `giftName` contiene `nuclear`, el valor efectivo sube al tier nuclear minimo
- tiers heredados de Arena Live v2:
  - `1-19` -> `disparos`
  - `20-29` -> `ola`
  - `30-99` -> `laser`
  - `100-199` -> `cazador`
  - `200-499` -> `nuclear`
  - `>=500` -> `nuclear overdrive`
