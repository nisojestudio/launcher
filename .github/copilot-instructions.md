# Copilot / GPT / Codex instructions for Nisoje LivePanel 3.0

## Mission
Help build a new generation live-game panel platform with a C++-ready architecture, keeping the current product as reference but not as a hard implementation constraint.

## General behavior
- Work in small, reviewable steps.
- Prefer explicit architecture over hidden coupling.
- Explain tradeoffs when changing boundaries between subsystems.
- Do not assume legacy behavior is correct; verify it.
- Preserve backward compatibility only when it is worth the complexity.

## What you must do
- Read relevant docs before coding.
- Keep notes of assumptions.
- Add validation steps for anything non-trivial.
- Separate core logic, rendering, live integration, audio, diagnostics, and tooling.
- Prefer clear interfaces over direct cross-module access.

## What you must not do
- Do not rewrite unrelated areas just because you can.
- Do not introduce hidden dependencies between UI/render and core game rules.
- Do not silently change external protocols.
- Do not claim something is validated if it was not actually run.

## Preferred output style
- concise
- concrete
- file-by-file when editing code
- honest about uncertainty
