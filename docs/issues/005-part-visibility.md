# Add part hide, solo and lock for crowded models

Work package **RV-005** · M3 Manual authoring · help wanted, area: editor, status: design

## Problem

Overlapping roof and facade parts make detailed edits hard to inspect and select.

## Bounded contribution

Implement editor-only part visibility and selection locking with an explicit transient/persisted policy. Keep exported/game geometry independent of inspection visibility unless the UI clearly authors a removal.

## Acceptance

- [ ] Hidden/solo parts are clear in the list and recoverable.
- [ ] Locked parts cannot be accidentally transformed or painted.
- [ ] Changing visibility does not silently remove saved model geometry.
- [ ] Switching definitions, undo, keyboard selection and minimum-size layout remain predictable.

## Where to start

`src/studio/gui_voxel.inl`, `gui.cpp`, `group.h`; read `docs/architecture.md` input/persistence contracts.

## Validation and evidence

A real SDL journey on an overlapping house; compare the saved document and production mesh before/after a visibility-only operation.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-005 -->
