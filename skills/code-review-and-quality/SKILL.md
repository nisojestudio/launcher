---
name: code-review-and-quality
description: Revisión multi-eje de código con quality gates. Úsalo antes de mergear cualquier cambio.
---

# Code Review y Calidad

## Visión General

Revisión multi-dimensional con quality gates. Todo cambio se revisa antes del merge — sin excepciones. Cubre cinco ejes: correctness, readability, architecture, security y performance.

**Estándar de aprobación:** Aprueba un cambio cuando definitivamente mejora la salud del código, incluso si no es perfecto.

## Cuándo Usarlo

- Antes de mergear cualquier PR o cambio
- Después de completar una implementación
- Cuando otro agente produjo código que necesitas evaluar
- Después de cualquier bug fix

## Los Cinco Ejes

### 1. Correctness
- ¿Coincide con el spec o requisitos?
- ¿Se manejan edge cases (null, vacío, valores límite)?
- ¿Se manejan caminos de error (no solo el happy path)?
- ¿Pasa todos los tests? ¿Los tests prueban lo correcto?
- ¿Hay off-by-one, race conditions o inconsistencias de estado?

### 2. Readability & Simplicity
- ¿Los nombres son descriptivos y consistentes? (No `temp`, `data`, `result`)
- ¿El flujo de control es directo? (Evitar ternarios anidados, deep callbacks)
- ¿El código está organizado lógicamente?
- ¿Hay trucos "ingeniosos" que deberían simplificarse?
- ¿Se puede hacer en menos líneas?
- ¿Las abstracciones justifican su complejidad? (No generalizar hasta el tercer caso de uso)

### 3. Architecture
- ¿Sigue los patrones existentes o introduce uno nuevo?
- ¿Mantiene límites de módulo limpios?
- ¿Hay duplicación que debería compartirse?
- ¿Las dependencias fluyen en la dirección correcta? (Sin circulares)
- ¿El refactor reduce complejidad o solo la reubica?

### 4. Security
Para guía detallada, ver `security-and-hardening`.
- ¿Input validado y sanitizado?
- ¿Secrets fuera de código, logs y version control?
- ¿Auth/authorization chequeada donde sea necesario?
- ¿Queries parametrizadas? (Sin concatenación de strings)
- ¿Outputs codificados para prevenir XSS?

### 5. Performance
Para profiling detallado, ver `performance-optimization`.
- ¿Patrones N+1?
- ¿Loops sin límite o fetching sin restricciones?
- ¿Operaciones sincrónicas que deberían ser async?
- ¿Falta de paginación en endpoints de lista?

## Tamaño del Cambio

```
~100 líneas cambiadas   → Bueno. Revisable en una sentada.
~300 líneas cambiadas   → Aceptable si es un solo cambio lógico.
~1000 líneas cambiadas  → Demasiado grande. Divídelo.
```

**Estrategias para dividir cambios grandes:**

| Estrategia | Cómo | Cuándo |
|---|---|---|
| **Stack** | Cambio pequeño, el siguiente basado en él | Dependencias secuenciales |
| **Por grupo de archivos** | Separar cambios que necesitan revisores distintos | Cross-cutting concerns |
| **Horizontal** | Código compartido/stubs primero, luego consumidores | Arquitectura en capas |
| **Vertical** | Slices full-stack pequeños de la feature | Trabajo de feature |

Separa refactors de trabajo de feature. Un cambio que refactoriza Y añade comportamiento son dos cambios.

## Proceso de Review

### Paso 1: Entiende el Contexto
Antes de mirar código: ¿Qué intenta lograr este cambio? ¿Qué spec implementa?

### Paso 2: Revisa los Tests Primero
Los tests revelan intención y cobertura. ¿Existen? ¿Prueban comportamiento, no implementación? ¿Cubren edge cases?

### Paso 3: Revisa la Implementación
Por cada archivo: Correctness → Readability → Architecture → Security → Performance.

### Paso 4: Categoriza Hallazgos

| Prefijo | Significado | Acción del Autor |
|---|---|---|
| *(sin prefijo)* | Cambio requerido | Debe abordarse antes del merge |
| **Critical:** | Bloquea merge | Security vuln, data loss, funcionalidad rota |
| **Nit:** | Menor, opcional | Puede ignorarse — estilo, preferencias |
| **Optional:** / **Consider:** | Sugerencia | Vale la pena considerar |
| **FYI** | Solo informativo | Sin acción requerida |

### Paso 5: Verifica la Verificación
- ¿Qué tests se ejecutaron?
- ¿El build pasó?
- ¿Se probó manualmente?

## Higiene de Código Muerto

Después de cualquier refactor, identifica código no alcanzado o no usado, y pregunta antes de eliminar:
```
CÓDIGO MUERTO IDENTIFICADO:
- formatLegacyDate() en src/utils/date.h — reemplazado por formatDate()
- LEGACY_API_URL en src/config.h — sin referencias restantes
→ ¿Seguro eliminar esto?
```

## Velocidad de Review

- Responde dentro de un día hábil
- Prioriza respuestas individuales rápidas sobre aprobación final rápida
- Cambios grandes: pide al autor dividirlos

## Manejo de Desacuerdos

1. **Datos y hechos técnicos** sobre opiniones y preferencias
2. **Guías de estilo** son autoridad absoluta en temas de estilo
3. **Diseño de software** evaluado en principios de ingeniería
4. **Consistencia del codebase** es aceptable si no degrada la salud

## Honestidad en Review

- No "LGTM" sin evidencia de revisión
- No suavices issues reales — cuantifica problemas cuando sea posible
- Rechaza enfoques con problemas claros
- Acepta override con gracia si el autor tiene contexto completo

## Disciplina de Dependencias

**Antes de añadir cualquier dependencia:**
1. ¿El stack existente lo resuelve?
2. ¿Qué tan grande es? (Impacto en bundle)
3. ¿Está mantenida activamente?
4. ¿Tiene vulnerabilidades conocidas?
5. ¿Licencia compatible?

**Regla:** Prefiere standard library y utilidades existentes sobre nuevas dependencias.

## Checklist de Review

```markdown
### Correctness
- [ ] Cambio coincide con spec/requisitos
- [ ] Edge cases manejados
- [ ] Caminos de error manejados

### Readability
- [ ] Nombres claros y consistentes
- [ ] Lógica directa, sin complejidad innecesaria

### Architecture
- [ ] Sigue patrones existentes
- [ ] Sin acoplamiento innecesario

### Security
- [ ] Sin secrets en código
- [ ] Input validado en boundaries
- [ ] Auth checks en su lugar

### Performance
- [ ] Sin patrones N+1
- [ ] Sin operaciones sin límite

### Verificación
- [ ] Tests pasan
- [ ] Build exitoso

### Veredicto
- [ ] **Approve** — Listo para mergear
- [ ] **Request changes** — Issues deben resolverse
```

## Racionalizaciones Comunes

| Racionalización | Realidad |
|---|---|
| "Funciona, es suficiente" | Código que funciona pero es ilegible, inseguro o arquitectónicamente incorrecto crea deuda que se compone. |
| "Lo escribí yo, sé que está correcto" | Los autores son ciegos a sus propias suposiciones. |
| "Lo limpiaremos después" | "Después" nunca llega. La review es el quality gate. |
| "Código generado por IA probablemente está bien" | El código de IA necesita más escrutinio, no menos. |
| "Los tests pasan, es suficiente" | Tests no atrapan problemas de arquitectura, seguridad o readability. |

## Red Flags

- PRs mergeados sin review
- "LGTM" sin evidencia de revisión real
- Cambios de seguridad sin review enfocado
- PRs grandes "demasiado grandes para revisar" (divídelos)
- Bug fixes sin tests de regresión
- Aceptar "lo arreglaré después"
- Bumps masivos de dependencias en un solo PR

## Verificación

- [ ] Todos los Critical resueltos
- [ ] Tests pasan
- [ ] Build exitoso
- [ ] La historia de verificación está documentada

## Ver También

`references/security-checklist.md` y `references/performance-checklist.md` para chequeos detallados.
