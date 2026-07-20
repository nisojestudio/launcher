---
name: spec-driven-development
description: Crea specs antes de codificar. Úsalo al empezar un proyecto, feature o cambio significativo sin especificación existente.
---

# Spec-Driven Development

## Visión General

Escribe una especificación estructurada antes de escribir código. El spec es la fuente de verdad compartida entre el agente y el humano — define qué construimos, por qué y cómo sabremos que está listo.

## Cuándo Usarlo

- Empezando un proyecto o feature nuevo
- Requisitos ambiguos o incompletos
- El cambio toca múltiples archivos o módulos
- Estás por tomar una decisión arquitectónica
- La tarea tomaría más de 30 minutos

**Cuándo NO:** Fixes de una línea, correcciones tipográficas o cambios con requisitos auto-contenidos y no ambiguos.

## El Workflow de Gates

```
SPECIFY ──→ PLAN ──→ TASKS ──→ IMPLEMENT
   │          │        │          │
   ▼          ▼        ▼          ▼
 Humano     Humano    Humano    Humano
 revisa     revisa    revisa    revisa
```

### Fase 1: Especificar

Empieza con una visión de alto nivel. Pregunta al humano hasta que los requisitos sean concretos.

**Expón suposiciones inmediatamente.** Antes de escribir contenido del spec, lista lo que estás asumiendo:

```
SUPOSICIONES:
1. Es una aplicación web (no mobile nativo)
2. La autenticación usa cookies de sesión (no JWT)
3. La base de datos es PostgreSQL
→ ¿Correcto? O corrijo y procedo.
```

Escribe un spec con estas seis áreas:

1. **Objective** — ¿Qué construimos y por qué? ¿Quién es el usuario? ¿Cómo se ve el éxito?
2. **Commands** — Comandos ejecutables completos con flags (build, test, lint, dev)
3. **Project Structure** — Dónde vive el código fuente, tests, docs
4. **Code Style** — Un snippet real de código que muestre el estilo (vale más que tres párrafos)
5. **Testing Strategy** — Framework, ubicación de tests, coverage esperado, niveles
6. **Boundaries** — Sistema de tres niveles:
   - **Siempre:** Ejecutar tests antes de commits, seguir naming conventions, validar inputs
   - **Preguntar primero:** Cambios de schema DB, añadir dependencias, cambiar CI
   - **Nunca:** Commiteaar secrets, editar vendor directories, eliminar tests fallidos sin aprobación

**Reframea instrucciones como criterios de éxito:**
```
REQUISITO: "Hacer el dashboard más rápido"

CRITERIOS REFORMULADOS:
- Dashboard LCP < 2.5s en 4G
- Carga inicial de datos en < 500ms
- Sin layout shifts (CLS < 0.1)
→ ¿Son estos los targets correctos?
```

### Fase 2: Planificar

Con el spec validado, genera un plan técnico de implementación:

1. Identifica componentes principales y sus dependencias
2. Determina el orden de implementación
3. Anota riesgos y estrategias de mitigación
4. Define checkpoints de verificación entre fases

> Sigue `planning-and-task-breakdown` para el mapeo de dependencias y slicing vertical.

### Fase 3: Tareas

Divide el plan en tareas discretas e implementables:

- Cada tarea completable en una sesión enfocada
- Cada tarea con criterios de aceptación explícitos
- Tareas ordenadas por dependencia, no por importancia percibida
- Ninguna tarea requiere cambiar más de ~5 archivos

### Fase 4: Implementar

Ejecuta tareas una a una siguiendo `incremental-implementation` y `test-driven-development`.

## Mantener el Spec Vivo

- **Actualiza cuando cambien decisiones** — Si el modelo de datos cambia, actualiza el spec primero
- **Actualiza cuando cambie el scope**
- **Committea el spec** — Pertenece al version control junto al código
- **Referencia el spec en PRs**

## Racionalizaciones Comunes

| Racionalización | Realidad |
|---|---|
| "Es simple, no necesito spec" | Tareas simples no necesitan specs *largos*, pero siguen necesitando criterios de aceptación. |
| "Escribiré el spec después de codificar" | Eso es documentación, no especificación. El valor del spec está en forzar claridad *antes*. |
| "El spec nos hará más lentos" | Un spec de 15 minutos previene horas de retrabajo. |
| "Los requisitos cambiarán de todas formas" | Por eso el spec es un documento vivo. Un spec desactualizado sigue siendo mejor que ningún spec. |
| "El usuario sabe lo que quiere" | Incluso los pedidos claros tienen suposiciones implícitas. El spec las revela. |

## Red Flags

- Empezar a escribir código sin requisitos escritos
- Preguntar "¿empiezo a construir?" sin aclarar qué significa "listo"
- Implementar features no mencionadas en ningún spec
- Decisiones arquitectónicas sin documentar
- Saltarse el spec porque "es obvio lo que hay que construir"

## Verificación

- [ ] El spec cubre las seis áreas principales
- [ ] El humano revisó y aprobó el spec
- [ ] Los criterios de éxito son específicos y testeables
- [ ] Los boundaries (Always/Ask First/Never) están definidos
- [ ] El spec está guardado en un archivo del repositorio
