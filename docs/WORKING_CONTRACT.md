# Contrato de trabajo

## Meta
Preparar y luego construir Panel Live 3.0 como plataforma nueva, tomando el proyecto actual como referencia funcional y técnica.

## Cómo debe trabajar el agente
1. **Inspección primero**
   - localizar módulos relevantes
   - identificar protocolos y dependencias
   - resumir hallazgos antes de rediseñar

2. **Diseño antes de implementación grande**
   - definir interfaces
   - proponer estructura de carpetas
   - separar componentes por responsabilidad

3. **Implementación incremental**
   - un bloque funcional por vez
   - cambios pequeños
   - sin refactors masivos si no son necesarios

4. **Validación explícita**
   - compila
   - corre tests
   - ejecuta smoke checks
   - documenta lo que no pudo validar

## Qué puede hacer
- crear carpetas, archivos, scripts y documentación
- proponer arquitectura y contratos
- preparar build C++ y tooling
- crear scaffolds para módulos y juegos
- integrar C++ con web/WASM por etapas
- migrar o adaptar protocolos existentes

## Qué debe evitar
- reescribir todo de una sola vez
- acoplar el nuevo sistema a la estructura vieja sin necesidad
- presentar opiniones como hechos verificados
- meter dependencias pesadas sin justificar costo/beneficio
- inspeccionar prerequisitos manualmente cuando hay un script que los resuelve
- ejecutar ninja o cmake sin cargar el entorno MSVC (vcvars64.bat) primero
- marcar una puerta (gate) como "skipped" sin haber ejecutado el script correspondiente

## Reglas obligatorias al ejecutar releases

Antes de tocar cualquier script de release, leer la sección **Hard Rules for Agent Operators**
en `docs/releases/RELEASE_PROTOCOL.md`. Esas reglas tienen prioridad sobre cualquier
intuición operativa del agente.

## Modo de entrega esperado
Cada respuesta técnica relevante debe incluir:
- objetivo
- archivos tocados
- decisión tomada
- validación hecha o pendiente
- riesgos conocidos
