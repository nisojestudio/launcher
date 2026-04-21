#!/usr/bin/env bash
set -e
mkdir -p build
if [ ! -d .venv ]; then
  python3 -m venv .venv || true
fi
echo 'Bootstrap base completo.'
echo 'Siguiente paso sugerido: cmake --preset default'
