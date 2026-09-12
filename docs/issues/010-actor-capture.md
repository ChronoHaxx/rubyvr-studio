# Capture one animated actor sequence with correct source identity

Work package **RV-010** · M6 Actors and effects · help wanted, area: runtime, status: In review

## Problem

Static object-event references do not carry all rendered OBJ animation, flips, palette and affine/subsprite state.

## Bounded contribution

Extend the immutable capture contract for one player idle/walk/turn sequence, then render it through the shared presentation path. Inventory unsupported fields rather than hiding them behind a static sprite.

The [first native slice](../live-actors.md) is implemented and **In review**:
4bpp animated field frames, bounded subsprite composition, original flips and
palette, fractional placement, explicit terrain height and a following camera.
Synthetic/sanitized, local GL and native walking evidence pass. Human checks
remain pending, so the acceptance below stays unchecked. Affine/effects and
full actor/mode coverage remain M6 work.

## Acceptance

- [ ] Preserve source frame, palette/index, facing, foot pivot and subpixel position.
- [ ] Do not read mutable guest data from the presentation thread.
- [ ] Clear incompatible actor state across map generations.
- [ ] Keep terrain layer/height and jump offsets distinct.

## Where to start

`integration/runtime/ruby_world.cpp`, `renderer.*`, `src/vr/ruby_world.h`; roadmap M5/M6.

## Validation and evidence

Original synthetic animation fixture for CI, plus separately retained local live sequence showing idle, turn and walk; no save/snapshot uploads.

Dependencies: RV-007, RV-008.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-010 -->
