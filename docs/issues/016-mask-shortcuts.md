# Add role-aware bulk mask actions with a preview

Work package **RV-016** · M3 Manual authoring · help wanted, area: editor, status: design

## Problem

Brush, rectangle and connected flood exist, but clear/invert/global-color shortcuts visible in the reference are missing. Naive color removal can erase legitimate leaves.

## Bounded contribution

Implement one previewed bulk action first, with an explicit target role or selected relief. Distinguish connected flood from every matching color. Expand to clear/invert only after the ownership semantics are covered.

## Acceptance

- [ ] Preserve source opacity and non-target ownership.
- [ ] Show affected pixel count before committing.
- [ ] One operation is one undo step; cancel leaves the exact document unchanged.
- [ ] Use a fixture where grass and decoration share a color to prevent semantic color erasure.

## Where to start

`src/studio/voxel_authoring.h`, `gui_voxel.inl`, `gui.cpp`, `src/vr/cutout.*`.

## Validation and evidence

Original asymmetric color/alpha fixture, exact mask/undo checks and an actual hidden SDL journey; preserve the existing authoring suite.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-016 -->
