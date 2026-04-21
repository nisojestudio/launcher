# Arquitectura inicial propuesta

## Capas
### 1. Core
- reglas de juego
- simulación
- estado
- eventos de dominio

### 2. Render
- canvas o frontend de representación
- render nativo o render WASM futuro
- adaptación visual por juego

### 3. Live integration
- chat
- gifts
- likes
- follows
- shares
- normalización de eventos externos

### 4. Audio
- música
- FX
- políticas de reproducción
- diagnóstico de fallos

### 5. Platform
- host runtime
- integración con panel
- config live
- carga de módulos

### 6. Diagnostics
- logging
- métricas
- trazas
- smoke tests

## Dirección técnica
- C++ para el núcleo nuevo
- interfaces claras para exponer estado al panel
- compatibilidad web como primera vía de integración
- soporte futuro para WebAssembly

## Regla práctica
El proyecto nuevo no debe copiar carpetas viejas “tal cual”.
Debe extraer patrones útiles y rediseñarlos donde convenga.
