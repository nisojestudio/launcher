---
name: planning-and-task-breakdown
description: Descompone el trabajo en tareas ordenadas. Úsalo cuando tengas un spec y necesites dividirlo en tareas implementables.
---

# Planificación y Descomposición de Tareas

## Visión General

Descompón el trabajo en tareas pequeñas y verificables con criterios de aceptación explícitos. Cada tarea debe ser lo suficientemente pequeña para implementar, testear y verificar en una sesión enfocada.

## Cuándo Usarlo

- Tienes un spec y necesitas dividirlo en unidades implementables
- Una tarea se siente demasiado grande o vaga para empezar
- El trabajo necesita paralelizarse entre múltiples agentes o sesiones
- Necesitas comunicar scope a un humano
- El orden de implementación no es obvio

**Cuándo NO:** Cambios de un solo archivo con scope obvio, o specs que ya contienen tareas bien definidas.

## El Proceso de Planificación

### Paso 1: Modo Plan

Antes de escribir código, opera en modo read-only:
- Lee el spec y secciones relevantes del codebase
- Identifica patrones y convenciones existentes
- Mapa dependencias entre componentes
- Nota riesgos y unknowns

**NO escribas código durante la planificación.** El output es un plan document (`tasks/plan.md`) y un task list (`tasks/todo.md`).

### Paso 2: Identifica el Grafo de Dependencias

Mapa qué depende de qué. El orden de implementación sigue el grafo bottom-up: construye fundamentos primero.

### Paso 3: Slice Vertical

En vez de construir toda la DB, luego toda la API, luego toda la UI — construye un camino completo de feature a la vez.

**Mal (horizontal slicing):**
```
Tarea 1: Schema DB completo
Tarea 2: Todos los endpoints API
Tarea 3: Todos los componentes UI
```

**Bien (vertical slicing):**
```
Tarea 1: Usuario crea cuenta (schema + API + UI registro)
Tarea 2: Usuario inicia sesión (schema auth + API + UI login)
Tarea 3: Usuario crea tarea (schema task + API + UI creación)
```

### Paso 4: Escribe Tareas

Cada tarea sigue esta estructura:

```markdown
## Tarea [N]: [Título descriptivo corto]

**Descripción:** Un párrafo explicando qué logra esta tarea.

**Criterios de aceptación:**
- [ ] Condición específica y testeable
- [ ] Condición específica y testeable

**Verificación:**
- [ ] Tests pasan
- [ ] Build exitoso
- [ ] Check manual

**Dependencias:** [Números de tarea, o "Ninguna"]

**Archivos probablemente tocados:**
- `src/path/to/file`
- `tests/path/to/test`

**Scope estimado:** [Small: 1-2 | Medium: 3-5 | Large: 5+]
```

### Paso 5: Ordena y Checkpoints

1. Dependencias satisfechas
2. Cada tarea deja el sistema en estado funcional
3. Checkpoints de verificación después de cada 2-3 tareas
4. Tareas de alto riesgo temprano (fail fast)

## Guía de Tamaño de Tareas

| Tamaño | Archivos | Ejemplo |
|---|---|---|
| **XS** | 1 | Añadir regla de validación |
| **S** | 1-2 | Un componente o endpoint |
| **M** | 3-5 | Un slice de feature |
| **L** | 5-8 | Feature multi-componente |
| **XL** | 8+ | **Demasiado grande — dividir más** |

Si una tarea es L o más grande, divídela. Un agente rinde mejor en tareas S y M.

**Cuándo dividir más:**
- Tomaría más de una sesión enfocada (~2+ horas de agente)
- No puedes describir los criterios de aceptación en 3 o menos bullet points
- Toca dos o más subsistemas independientes
- Encuentras "y" en el título de la tarea

## Oportunidades de Paralelización

- **Seguro paralelizar:** Slices de feature independientes, tests para features ya implementadas, documentación
- **Debe ser secuencial:** Migraciones de DB, cambios de estado compartido, cadenas de dependencia
- **Necesita coordinación:** Features que comparten un contrato API (define el contrato primero, luego paraleliza)

## Archivos de Output

- `tasks/plan.md` — Documento del plan de implementación
- `tasks/todo.md` — Lista de tareas en formato checklist

## Racionalizaciones Comunes

| Racionalización | Realidad |
|---|---|
| "Lo resolveré sobre la marcha" | Así es como terminas con un desastre enmarañado y retrabajo. 10 minutos de planificación ahorran horas. |
| "Las tareas son obvias" | Escríbelas igual. Las tareas explícitas revelan dependencias ocultas y edge cases olvidados. |
| "Planificar es overhead" | Planificar es la tarea. Implementar sin plan es solo escribir. |
| "Lo puedo mantener todo en mi cabeza" | Las ventanas de contexto son finitas. Los planes escritos sobreviven los límites de sesión. |

## Red Flags

- Empezar implementación sin lista de tareas escrita
- Tareas que dicen "implementar la feature" sin criterios de aceptación
- Sin pasos de verificación en el plan
- Todas las tareas son tamaño XL
- Sin checkpoints entre tareas
- El orden de dependencias no se considera

## Verificación

- [ ] Cada tarea tiene criterios de aceptación
- [ ] Cada tarea tiene un paso de verificación
- [ ] Las dependencias están identificadas y ordenadas correctamente
- [ ] Ninguna tarea toca más de ~5 archivos
- [ ] Existen checkpoints entre fases principales
- [ ] El humano revisó y aprobó el plan

## Ver También

`references/definition-of-done.md` para el Definition of Done global del proyecto.
