# AGENTS.md — Contrato maestro del agente

Este repositorio está pensado para trabajar con agentes de programación en VS Code (GPT-5.4, Codex 5.3 u otros equivalentes).

## 1. Misión
Construir **Nisoje LivePanel 3.0** como una plataforma nueva para juegos live, con núcleo preparado para C++/WASM y con integración práctica al panel y herramientas heredadas.

## 2. Resultado esperado
El agente debe ayudar a:
- analizar el proyecto actual
- extraer la arquitectura útil
- proponer la arquitectura destino
- crear el nuevo proyecto por capas
- validar técnicamente cada etapa

## 3. Principios de trabajo
- Priorizar claridad arquitectónica.
- Hacer cambios pequeños y auditables.
- Escribir primero contratos de interfaz cuando una capa nueva aparezca.
- Mantener trazabilidad de decisiones.
- No confundir hipótesis con validación real.

## 4. Libertad operativa
En esta fase inicial **no existen restricciones especiales sobre cambios visuales** salvo que:
- rompan el build
- rompan compatibilidad externa necesaria
- contradigan una instrucción posterior explícita

## 5. Reglas obligatorias
### 5.1 Antes de cambiar código
El agente debe:
- leer `README.md`
- leer `docs/WORKING_CONTRACT.md`
- leer `docs/ARCHITECTURE_START.md`
- revisar los archivos directamente relacionados

### 5.2 Al modificar código
El agente debe:
- indicar qué archivos toca
- explicar por qué
- describir riesgos
- proponer forma de validar

### 5.3 Validación
El agente solo puede decir “validado” si realmente ejecutó la validación.
Si no pudo ejecutar, debe decir exactamente qué faltó.

### 5.4 Integración
El agente debe asumir que el sistema final puede incluir:
- núcleo C++
- capa web
- bridge Python o Node
- integración WebSocket / eventos live
- salida WebAssembly futura

## 6. Organización técnica deseada
- `src/core` → reglas y simulación
- `src/render` → renderizado
- `src/live` → eventos live y adaptadores
- `src/audio` → subsistema de audio
- `src/platform` → host, runtime y compatibilidad
- `tests` → validaciones
- `tools` → utilidades de apoyo

## 7. Política de dependencias
- Preferir dependencias justificadas y mantenibles.
- Evitar agregar una dependencia si C++ estándar o tooling existente basta.
- Toda dependencia nueva debe documentarse.

## 8. Política de agentes / skills
El agente puede usar:
- búsqueda en repositorio
- terminal
- CMake
- scripts PowerShell/Bash/Python/Node
- documentación local del proyecto

El agente debe crear skills o subagentes solo si aportan valor real, por ejemplo:
- migración de protocolo
- auditoría de integración live
- generador de scaffolds de juego
- validador de contratos C++ ↔ web

La capa operativa del proyecto queda documentada en:
- `agents/definitions/AGENT_MAP.md`
- `agents/routing/ROUTING_POLICY.md`
- `skills/SKILL_CATALOG.md`
- `docs/releases/RELEASE_POLICY.md`
- `docs/runbooks/BACKUP_POLICY.md`

## 9. Entregables por etapa
Cada etapa debe terminar con:
- resumen de lo hecho
- archivos cambiados
- riesgos
- pasos de validación
- siguiente bloque de trabajo sugerido

## 10. Prohibiciones estrictas
- No inventar resultados de test.
- No borrar código heredado útil sin justificarlo.
- No mezclar core y render en la misma responsabilidad si puede evitarse.
- No introducir cambios grandes sin dejar una ruta de reversión o comparación.
