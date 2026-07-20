---
name: debugging-and-error-recovery
description: Debugging sistemático con triage estructurado. Úsalo cuando tests fallan, builds se rompen o el comportamiento no coincide con lo esperado.
---

# Debugging y Recuperación de Errores

## Visión General

Debugging sistemático con triage estructurado. Cuando algo se rompe, deja de añadir features, preserva evidencia y sigue un proceso estructurado para encontrar y arreglar la causa raíz.

## Cuándo Usarlo

- Tests fallan después de un cambio
- El build se rompe
- Comportamiento runtime no coincide con lo esperado
- Llega un reporte de bug
- Aparece un error en logs o consola
- Algo funcionaba antes y dejó de funcionar

## La Regla Stop-the-Line

```
1. STOP — deja de añadir features o hacer cambios
2. PRESERVE — evidencia (error output, logs, pasos para reproducir)
3. DIAGNOSE — usando el triage checklist
4. FIX — la causa raíz
5. GUARD — contra recurrencia
6. RESUME — solo después de que la verificación pase
```

No empujes código sobre un test fallido o build roto para trabajar en la siguiente feature. Los errores se acumulan.

## El Checklist de Triage

### Paso 1: Reproduce

Haz que el fallo ocurra de manera confiable. Si no puedes reproducirlo, no puedes arreglarlo con confianza.

```
¿Puedes reproducir el fallo?
├── SÍ → Procede al Paso 2
└── NO
    ├── Reúne más contexto (logs, detalles del entorno)
    ├── Intenta reproducir en un entorno mínimo
    └── Si es verdaderamente no reproducible, documenta condiciones y monitorea
```

Para test failures:
```bash
# Ejecuta el test específico
ctest -R "test_name" -V

# Ejecuta en aislamiento (descarta test pollution)
ctest -R "specific-test" --repeat-until-fail 3
```

### Paso 2: Localiza

Reduce DÓNDE ocurre el fallo:

```
¿Qué capa falla?
├── UI/Frontend     → Console, DOM, network tab
├── API/Backend     → Server logs, request/response
├── Database        → Queries, schema, integridad de datos
├── Build tooling   → Config, dependencias, entorno
├── Servicio externo → Conectividad, cambios de API, rate limits
└── El test mismo   → Verifica si el test es correcto (falso negativo)
```

**Bisect para bugs de regresión:**
```bash
git bisect start
git bisect bad
git bisect good <sha-conocido-funcional>
# Git checkout commits intermedios; ejecuta tu test en cada uno
```

### Paso 3: Reduce

Crea el caso mínimo que falla:
- Elimina código/config no relacionado hasta que solo quede el bug
- Simplifica el input al ejemplo más pequeño que dispara el fallo
- Reduce el test al mínimo que reproduce el issue

### Paso 4: Arregla la Causa Raíz

Arregla el problema subyacente, no el síntoma:

```
Síntoma: "La lista de usuarios muestra entradas duplicadas"

Fix del síntoma (mal):
  → Deducir en la UI: [...new Set(users)]

Fix de causa raíz (bien):
  → El endpoint API tiene un JOIN que produce duplicados
  → Arregla la query, añade DISTINCT, o arregla el modelo de datos
```

### Paso 5: Guarda Contra Recurrencia

Escribe un test que atrape este fallo específico. Debe fallar sin el fix y pasar con él.

### Paso 6: Verifica End-to-End

```bash
# Ejecuta el test específico
ctest -R "specific-test"
# Ejecuta suite completo (check regresiones)
ctest
# Build del proyecto
ninja -C build/release-0.1.1
```

## Patrones por Tipo de Error

### Test Failure
```
Test falla después de cambio:
├── ¿Cambiaste código que el test cubre?
│   └── SÍ → Verifica si el test o el código está mal
│       ├── Test desactualizado → Actualiza el test
│       └── Código tiene bug → Arregla el código
├── ¿Cambiaste código no relacionado?
│   └── SÍ → Posible side effect → Revisa shared state, imports, globales
└── ¿El test ya era flaky?
    └── Timing, orden de dependencias, dependencias externas
```

### Build Failure
```
Build falla:
├── Type error → Lee el error, verifica tipos en la ubicación citada
├── Import error → El módulo existe? Los exports coinciden?
├── Config error → Revisa archivos de configuración
├── Dependency error → Verifica vcpkg.json, package.json
└── Environment error → Versión de compilador, OS, toolchain
```

### Runtime Error
```
Error runtime:
├── Null/undefined → Verifica el flujo de datos: ¿de dónde viene este valor?
├── Network error / CORS → URLs, headers, server CORS config
├── Error de render / pantalla en blanco → Console, component tree
└── Comportamiento inesperado (sin error) → Añade logging en puntos clave
```

## Safe Fallback Patterns

```cpp
// Safe default + warning (en vez de crashear)
std::string getConfig(const std::string& key) {
    auto it = config.find(key);
    if (it == config.end()) {
        spdlog::warn("Missing config: {}, using default", key);
        return DEFAULTS[key];
    }
    return it->second;
}
```

## Reglas de Instrumentación

Añade logging solo cuando ayude. Elimínalo cuando termines.

**Cuándo añadir:** No puedes localizar el fallo a una línea específica; el issue es intermitente y necesita monitoreo; el fix involucra múltiples componentes que interactúan.

**Cuándo eliminar:** El bug está arreglado y los tests guardan contra recurrencia; el log solo es útil durante desarrollo; contiene datos sensibles.

**Instrumentación permanente:** Error boundaries con reporting; API error logging con contexto de request; métricas de performance en flujos clave de usuario.

## Racionalizaciones Comunes

| Racionalización | Realidad |
|---|---|
| "Sé cuál es el bug, solo lo arreglaré" | Puede que tengas razón el 70% de las veces. El otro 30% cuesta horas. Reproduce primero. |
| "El test que falla probablemente está mal" | Verifica esa suposición. Si el test está mal, arregla el test. No lo saltes. |
| "En mi máquina funciona" | Los entornos difieren. Revisa CI, config, dependencias. |
| "Lo arreglaré en el próximo commit" | Arrégialo ahora. El próximo commit introducirá nuevos bugs encima de este. |
| "Es un test flaky, ignóralo" | Tests flaky esconden bugs reales. Arregla la flakiness o entiende por qué es intermitente. |

## Red Flags

- Saltarse un test que falla para trabajar en nuevas features
- Adivinar fixes sin reproducir el bug
- Arreglar síntomas en vez de causas raíz
- "Ahora funciona" sin entender qué cambió
- No añadir test de regresión después de un bug fix
- Múltiples cambios no relacionados hechos durante debugging

## Verificación

- [ ] Causa raíz identificada y documentada
- [ ] Fix direcciona la causa raíz, no solo síntomas
- [ ] Existe un test de regresión que falla sin el fix
- [ ] Todos los tests existentes pasan
- [ ] Build exitoso
- [ ] El escenario original del bug verificado end-to-end
