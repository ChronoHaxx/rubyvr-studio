# Add one voxel primitive justified by an unmodeled scenery family

Work package **RV-017** · M3 Manual authoring · help wanted, area: geometry, status: design

## Problem

Sphere/cylinder/prism/dome and taper/bevel controls are visible in the reference but absent from RubyVR. Adding them all without a target would delay useful art work.

## Bounded contribution

Pick one outstanding real family, demonstrate why existing box/roof/relief parts are awkward, then implement one editable native-scale primitive or modifier with a documented material/pivot contract.

## Acceptance

- [ ] Record the specific family and intended silhouette before implementation.
- [ ] Keep voxel steps and intentional source materials on caps/sides.
- [ ] Produce valid exposed geometry across minimum/maximum and rotated cases.
- [ ] Version persistence as needed, with old files unchanged, undo and GUI/batch parity.

## Where to start

`src/vr/overrides.*`, `part_geometry.*`, `voxel_parts.inl`, `src/studio/gui_voxel.inl`, `docs/reference-parity.md`.

## Validation and evidence

Original diagnostic material plus actual family renders from all sides, targeted geometry/persistence checks and measured triangle/rebuild cost.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-017 -->
