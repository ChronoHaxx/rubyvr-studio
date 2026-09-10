# Add centered depth and geometry mirroring with intentional face art

Work package **RV-006** · M3 Manual authoring · help wanted, area: editor, area: geometry, status: design

## Problem

The reference shows symmetric billboard extrusion. RubyVR has front/depth controls, but no convenient reusable symmetry operation; geometry can drift while front-only art must stay deliberate.

## Bounded contribution

Design and implement one explicit depth anchor choice (front or center) and one-axis duplicate/mirror operation. Preserve existing v6 front anchoring for old files. Mirrored geometry must not automatically place doors on every face.

## Acceptance

- [ ] State the pivot and axis visually and in pixel units.
- [ ] Preserve a fixed front when front-anchored and a fixed midplane when centered.
- [ ] Mirror rotated asymmetric diagnostic geometry correctly with explicit face-art rules.
- [ ] Undo and save/reopen preserve exact coordinates and source sampling.

## Where to start

`src/studio/gui_voxel.inl`, `voxel_authoring.h`, `src/vr/part_geometry.*`, `voxel_parts.inl`, `overrides.*`.

## Validation and evidence

Asymmetric original diagnostic artwork, neutral front/back/side views and persistence/input checks. Record this as a new control rather than existing demo parity.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-006 -->
