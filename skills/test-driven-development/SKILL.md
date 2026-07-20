---
name: test-driven-development
description: Impulsa el desarrollo con tests. Úsalo al implementar lógica, arreglar bugs o cambiar comportamiento.
---

# Test-Driven Development

## Visión General

Escribe un test que falle antes de escribir el código que lo hace pasar. Para bugs, reproduce el bug con un test antes de intentar arreglarlo. Los tests son la prueba — "parece funcionar" no es suficiente.

## Cuándo Usarlo

- Implementando lógica o comportamiento nuevo
- Arreglando bugs (Prove-It Pattern)
- Modificando funcionalidad existente
- Añadiendo manejo de edge cases
- Cualquier cambio que pueda romper comportamiento existente

**Cuándo NO:** Configuración pura, documentación o cambios estáticos sin impacto conductual.

## El Ciclo TDD

```
    RED                GREEN              REFACTOR
Escribe un test   Escribe código      Limpia la
que falle  ──→   mínimo para pasar ──→ implementación ──→ (repite)
     │                  │                    │
     ▼                  ▼                    ▼
  Test FALLA        Test PASA           Tests siguen PASANDO
```

### Paso 1: RED — Escribe un Test que Falle

Escribe el test primero. Debe fallar. Un test que pasa inmediatamente no prueba nada.

### Paso 2: GREEN — Hazlo Pasar

Escribe el código mínimo para que el test pase. No sobre-ingenierices.

### Paso 3: REFACTOR — Limpia

Con los tests en verde, mejora el código sin cambiar comportamiento:
- Extrae lógica compartida
- Mejora nombres
- Elimina duplicación
- Optimiza si es necesario

Ejecuta tests después de cada paso de refactor.

## El Patrón Prove-It (Bug Fixes)

Cuando se reporta un bug, **no empieces intentando arreglarlo.** Empieza escribiendo un test que lo reproduzca.

```
Llega reporte de bug
        │
        ▼
  Escribe test que demuestra el bug
        │
        ▼
  Test FALLA (confirma que el bug existe)
        │
        ▼
  Implementa el fix
        │
        ▼
  Test PASA (prueba que el fix funciona)
        │
        ▼
  Ejecuta suite completo (sin regresiones)
```

## La Pirámide de Tests

```
          ╱╲
         ╱  ╲         E2E Tests (~5%)
        ╱    ╲        Flujos completos de usuario
       ╱──────╲
      ╱        ╲      Integration Tests (~15%)
     ╱          ╲     Interacciones entre componentes
    ╱────────────╲
   ╱              ╲   Unit Tests (~80%)
  ╱                ╲  Lógica pura, aislada, milisegundos
 ╱──────────────────╲
```

**La Regla Beyonce:** If you liked it, you should have put a test on it. Cambios de infraestructura, refactors y migraciones no son responsables de atrapar tus bugs — tus tests sí.

### Guía de Decisión

```
¿Es lógica pura sin side effects?
  → Unit test

¿Cruza un límite (API, DB, filesystem)?
  → Integration test

¿Es un flujo crítico de usuario que debe funcionar end-to-end?
  → E2E test (solo caminos críticos)
```

## Escribir Buenos Tests

### Prueba Estado, No Interacciones

Afirma sobre el *resultado* de una operación, no sobre qué métodos se llamaron internamente.

### DAMP Sobre DRY en Tests

En código de producción, DRY es correcto. En tests, **DAMP (Descriptive And Meaningful Phrases)** es mejor. Un test debe leerse como una especificación.

### Prefiere Implementaciones Reales Sobre Mocks

```
Orden de preferencia:
1. Implementación real  → Máxima confianza, atrapa bugs reales
2. Fake                 → Versión en memoria de una dependencia
3. Stub                 → Datos prefijados, sin comportamiento
4. Mock (interacción)   → Verifica llamadas a métodos — usar con moderación
```

Usa mocks solo cuando: la implementación real es demasiado lenta, no determinista, o tiene side effects que no puedes controlar (APIs externas, envío de emails).

### Patrón Arrange-Act-Assert

```cpp
// Arrange: Prepara el escenario
auto task = createTask("Test", Deadline{2025, 1, 1});

// Act: Ejecuta la acción
auto result = checkOverdue(task, Date{2025, 1, 2});

// Assert: Verifica el resultado
REQUIRE(result.isOverdue == true);
```

### Una Afirmación Por Concepto

Cada test verifica un comportamiento. Tests con múltiples afirmaciones mezclan concerns y dificultan identificar qué falló.

### Nombres Descriptivos

```
Bien: "completa tarea y registra timestamp"
Bien: "lanza NotFoundError para tarea inexistente"
Bien: "es idempotente — completar una tarea ya completada es no-op"
Mal:  "funciona"
Mal:  "test 3"
```

## Anti-Patterns a Evitar

| Anti-Pattern | Problema | Solución |
|---|---|---|
| Testear implementation details | Tests se rompen al refactorizar aunque el comportamiento no cambie | Testea inputs y outputs, no estructura interna |
| Flaky tests (timing, orden) | Erosionan la confianza en el suite | Usa aserciones deterministas, aísla estado |
| Testear código del framework | Pierde tiempo probando código de terceros | Testea SOLO tu código |
| Sin aislamiento entre tests | Pasan individualmente pero fallan juntos | Cada test setup/teardown su propio estado |
| Mockear todo | Tests pasan pero producción se rompe | Prefiere real > fake > stub > mock |

## Racionalizaciones Comunes

| Racionalización | Realidad |
|---|---|
| "Escribiré tests después de que el código funcione" | No lo harás. Y los tests post-facto prueban implementación, no comportamiento. |
| "Esto es muy simple para testearlo" | El código simple se vuelve complejo. El test documenta el comportamiento esperado. |
| "Los tests me hacen más lento" | Te hacen más lento ahora. Te aceleran cada vez que cambias el código después. |
| "Lo probé manualmente" | La prueba manual no persiste. Un cambio mañana puede romperlo sin que lo sepas. |
| "Es solo un prototipo" | Los prototipos se vuelven código de producción. Tests desde el día 1 previenen la crisis de "deuda de tests". |

## Red Flags

- Escribir código sin tests correspondientes
- Tests que pasan en el primer intento (puede que no estén probando lo que crees)
- "Todos los tests pasan" pero no se ejecutó ningún test
- Bug fixes sin tests de reproducción
- Nombres de tests que no describen el comportamiento esperado
- Saltarse tests para que el suite pase

## Verificación

- [ ] Cada comportamiento nuevo tiene un test correspondiente
- [ ] Todos los tests pasan
- [ ] Bug fixes incluyen un test de reproducción que fallaba antes del fix
- [ ] Los nombres de tests describen el comportamiento verificado
- [ ] Ningún test fue saltado o deshabilitado
- [ ] Coverage no ha disminuido (si se trackea)

## Ver También

`references/testing-patterns.md` para patrones de test detallados y ejemplos.
