# Implementation Plan

- [x] 1. Create requirements for the operating workflow
  - Capture agent routing, skills, release, backup, and non-disruptive adoption requirements.
  - _Requirement: 1, 2, 3, 4, 5_

- [x] 2. Create technical design
  - Define file layout and adoption decisions.
  - _Requirement: 5_

- [x] 3. Add agent map and routing policy
  - Document primary and secondary agent roles.
  - Document task classification and ownership rules.
  - _Requirement: 1_

- [x] 4. Add shared skill catalog
  - Document shared, stack, QA, release, docs, and security skills.
  - _Requirement: 2_

- [x] 5. Add release governance docs
  - Define SemVer, branches, release gates, artifact naming, and manifest schema.
  - _Requirement: 3_

- [x] 6. Add backup and restore runbooks
  - Define backup cadence, modes, exclusions, and restore testing.
  - _Requirement: 4_

- [x] 7. Add automation scripts
  - Add project backup script.
  - Add release manifest script.
  - _Requirement: 3, 4_

- [x] 8. Add root project docs
  - Add changelog, roadmap, and contributing guide.
  - Link the workflow from AGENTS and README.
  - _Requirement: 1, 2, 3, 4, 5_
