#!/usr/bin/env bash
set +e

echo '== Preflight report =='
for tool in git code node npm python3 pip3 cmake ninja clang++ g++ vcpkg emcc; do
  if command -v "$tool" >/dev/null 2>&1; then
    echo "$tool: OK"
  else
    echo "$tool: MISSING"
  fi
done
