---
name: incremental-implementation
description: Implementa cambios en slices verticales delgados. Úsalo al implementar cualquier feature o cambio que toque más de un archivo.
---

# Implementación Incremental

## Visión General

Construye en slices verticales delgados — implementa una pieza, pruébala, verifícala, luego expande. Cada incremento debe dejar el sistema en estado funcional y testeable.

## Cuándo Usarlo

- Implementando cambios multi-archivo
- Construyendo una feature nueva desde un task breakdown
- Refactorizando código existente
- Cada vez que quieras escribir más de ~100 líneas sin testear

**Cuándo NO:** Cambios de un solo archivo con alcance mínimo.

## El Ciclo del Incremento

```
┌──────────────────────────────────────┐
│                                      │
│   Implement ──→ Test ──→ Verify ──┐  │
│       ▲                           │  │
│       └───── Commit ◄─────────────┘  │
│              │                       │
│              ▼                       │
│          Next slice                  │
│                                      │
└──────────────────────────────────────┘
```

Por cada slice:

1. **Implementa** la pieza funcional más pequeña y completa
2. **Test** — ejecuta el suite de tests (o escribe uno si no existe)
3. **Verify** — confirma que el slice funciona (tests pasan, build ok, check manual)
4. **Commit** — guarda el progreso con mensaje descriptivo
5. **Siguiente slice** — continúa, no reinicies

## Estrategias de Slicing

### Vertical Slices (Preferida)

Construye un camino completo a través del stack:

```
Slice 1: Crear tarea (DB + API + UI básica)
    → Tests pasan, usuario puede crear una tarea

Slice 2: Listar tareas (query + API + UI)
    → Tests pasan, usuario puede ver sus tareas

Slice 3: Editar tarea (update + API + UI)
    → Tests pasan, usuario puede modificar tareas

Slice 4: Eliminar tarea (delete + API + UI + confirmación)
    → Tests pasan, CRUD completo
```

### Contract-First Slicing

Cuando backend y frontend necesitan desarrollarse en paralelo:

```
Slice 0: Define el contrato API (types, interfaces, OpenAPI spec)
Slice 1a: Backend contra el contrato + API tests
Slice 1b: Frontend con mock data que coincide con el contrato
Slice 2: Integración y test end-to-end
```

### Risk-First Slicing

Aborda la pieza más riesgosa o incierta primero:

```
Slice 1: Probar que la conexión WebSocket funciona (alto riesgo)
Slice 2: Construir actualizaciones en tiempo real sobre la conexión probada
Slice 3: Añadir soporte offline y reconexión
```

## Reglas de Implementación

### Regla 0: Simplicidad Primero

Antes de escribir código, pregúntate: "¿Cuál es la cosa más simple que podría funcionar?"

Después de escribir, revisa:
- ¿Se puede hacer en menos líneas?
- ¿Estas abstracciones justifican su complejidad?
- ¿Estoy construyendo para requisitos futuros hipotéticos o para la tarea actual?

### Regla 0.5: Disciplina de Alcance

Toca solo lo que la tarea requiere.

NO hagas:
- "Limpiar" código adyacente a tu cambio
- Refactorizar imports en archivos que no modificas
- Eliminar comentarios que no entiendes completamente
- Añadir features que no están en el spec porque "parecen útiles"
- Modernizar sintaxis en archivos que solo estás leyendo

### Regla 1: Una Cosa a la Vez

Cada incremento cambia una cosa lógica. No mezcles concerns.

### Regla 2: Mantenlo Compilable

Después de cada incremento, el proyecto debe compilar y los tests existentes deben pasar.

### Regla 3: Feature Flags para Features Incompletas

Si una feature no está lista para usuarios pero necesitas mergear incrementos:

```typescript
const ENABLE_TASK_SHARING = process.env.FEATURE_TASK_SHARING === 'true';
if (ENABLE_TASK_SHARING) { /* nueva UI */ }
```

### Regla 4: Safe Defaults

Código nuevo debe tener comportamiento seguro y conservador por defecto.

### Regla 5: Rollback-Friendly

Cada incremento debe ser revertible independientemente. Cambios aditivos (nuevos archivos) son fáciles de revertir. Modificaciones a código existente deben ser mínimas y enfocadas.

## Checklist por Incremento

- [ ] El cambio hace una cosa y la hace completa
- [ ] Todos los tests existentes siguen pasando
- [ ] El build es exitoso
- [ ] Type checking pasa
- [ ] Linting pasa
- [ ] La nueva funcionalidad funciona como se espera
- [ ] El cambio está commiteado con mensaje descriptivo

## Racionalizaciones Comunes

| Racionalización | Realidad |
|---|---|
| "Lo testearé todo al final" | Los bugs se acumulan. Un bug en Slice 1 invalida Slices 2-5. |
| "Es más rápido hacerlo todo de una vez" | Se *siente* más rápido hasta que algo se rompe y no encuentras cuál de 500 líneas lo causó. |
| "Estos cambios son muy pequeños para commitearlos separados" | Commits pequeños son gratis. Commits grandes esconden bugs. |
| "Añadiré el feature flag después" | Si la feature no está completa, no debería ser visible. Añádelo ahora. |
| "Este refactor es pequeño, lo incluyo" | Refactors mezclados con features hacen ambas cosas más difíciles de revisar y debuggear. |

## Red Flags

- Más de 100 líneas escritas sin ejecutar tests
- Múltiples cambios no relacionados en un solo incremento
- "Déjame añadir esto rápido también" (scope creep)
- Saltarse el paso de test/verify para ir más rápido
- Build o tests rotos entre incrementos
- Cambios grandes sin commit acumulándose
- Abstracciones prematuras sin que el caso de uso lo exija
- Tocar archivos fuera del alcance de la tarea

## Verificación

- [ ] Cada incremento fue testeado y commiteado individualmente
- [ ] El suite completo de tests pasa
- [ ] El build está limpio
- [ ] La feature funciona end-to-end como se especificó
- [ ] No hay cambios sin commit

## Ver También

`references/definition-of-done.md` para el Definition of Done global del proyecto.
