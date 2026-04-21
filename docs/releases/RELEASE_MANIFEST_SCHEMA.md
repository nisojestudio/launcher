# Release Manifest Schema

## Purpose

Every release should have a JSON manifest that identifies exactly what was built, from which commit, and which files were published.

## Required Fields

```json
{
  "product": "Panel Live",
  "version": "0.2.0",
  "generatedAt": "2026-04-21T14:00:00-04:00",
  "git": {
    "commit": "abcdef1",
    "branch": "release/0.2.0",
    "dirty": false
  },
  "artifacts": [
    {
      "path": "dist/releases/0.2.0/panel-live-0.2.0-win-x64.exe",
      "fileName": "panel-live-0.2.0-win-x64.exe",
      "platform": "windows",
      "architecture": "x64",
      "kind": "installer",
      "sizeBytes": 123456789,
      "sha256": "ABCDEF..."
    }
  ],
  "releaseNotes": "CHANGELOG.md",
  "validation": {
    "build": "pending",
    "tests": "pending",
    "installer": "pending",
    "backup": "pending"
  },
  "knownRisks": []
}
```

## Artifact Kinds

Supported initial values:
- `installer`
- `portable_zip`
- `checksum`
- `manifest`
- `release_notes`

## Validation Values

Use:
- `passed`
- `failed`
- `pending`
- `skipped`

Never mark a field as `passed` unless the command was actually executed.
