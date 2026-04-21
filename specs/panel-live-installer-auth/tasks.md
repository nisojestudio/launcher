# Implementation Plan

- [x] 1. Validate the existing Windows packaging baseline
  - Run the Release build
  - Run `ctest --preset release`
  - Run `scripts/package_windows.ps1 -SkipBuild`
  - _Requirement: 1_

- [x] 2. Add runtime access and license configuration
  - Extend `PanelConfig` and `PanelConfigStorage`
  - Persist auth and license metadata
  - _Requirement: 3_

- [x] 3. Implement the remote authentication and license service
  - Add a C++ service with login, logout, and snapshot support
  - Validate Firebase email/password via REST
  - Validate an active license through the Nisoje Studio API
  - _Requirement: 2, 3_

- [x] 4. Expose auth state and HTTP routes
  - Add `auth` to the snapshot and JSON state
  - Add `/api/auth/login` and `/api/auth/logout`
  - Block protected actions until access is granted
  - _Requirement: 2, 3_

- [x] 5. Design and implement the startup access popup
  - Create a blocking startup overlay
  - Wire the form and validation states
  - Adapt the dashboard to locked and unlocked states
  - _Requirement: 2_

- [x] 6. Add Windows branding for the executable
  - Embed the official icon in the binary
  - Reuse branding for the installer and shortcuts
  - _Requirement: 5_

- [x] 7. Create the minimal installer staging payload
  - Prepare a payload with only the required runtime files
  - Download and stage redistributable prerequisites
  - Prepare installer artwork
  - _Requirement: 4, 5_

- [x] 8. Author the Inno Setup installer
  - Locate or install the Inno Setup compiler
  - Create the `.iss` with progress, optional desktop shortcut, and conditional prerequisites
  - _Requirement: 4, 5_

- [x] 9. Orchestrate the end-to-end installer build
  - Add an installer build script
  - Generate the final distributable artifact
  - _Requirement: 4_

- [x] 10. Validate final artifacts and write the report
  - Execute the final validations
  - Document delivered files, risks, and executed steps
  - _Requirement: 6_
