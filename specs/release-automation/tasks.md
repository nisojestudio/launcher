# Implementation Plan

- [x] 1. Make installer metadata version-aware
  - Add Inno Setup preprocessor defines for app version, output base name, and file version.
  - _Requirement: 1_

- [x] 2. Add portable ZIP release naming
  - Allow `package_windows.ps1` to receive a release ZIP file name.
  - _Requirement: 1, 4_

- [x] 3. Generate versioned release outputs
  - Update `build_windows_installer.ps1` to write under `dist/releases/<version>/`.
  - Generate installer, portable ZIP, checksums, and manifest in the same release folder.
  - _Requirement: 1, 2_

- [x] 4. Add one-command release preparation
  - Add `prepare_release.ps1` with Git hygiene, backup, validation, packaging, manifest status propagation, and dry-run support.
  - _Requirement: 2, 3_

- [x] 5. Update docs
  - Update README, release policy, roadmap, changelog, and ADR.
  - _Requirement: 4_
