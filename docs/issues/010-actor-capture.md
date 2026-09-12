# Capture one animated actor sequence with correct source identity

Work package **RV-010** · M6 Actors and effects · help wanted, area: runtime · first slice merged; camera-facing follow-up ready

## Problem

Static object-event references do not carry all rendered OBJ animation, flips, palette and affine/subsprite state.

## Bounded contribution

Extend the immutable capture contract for one player idle/walk/turn sequence, then render it through the shared presentation path. Inventory unsupported fields rather than hiding them behind a static sprite.

The [first native slice](../live-actors.md) **merged in PR #28**:
4bpp animated field frames, bounded subsprite composition, original flips and
palette, fractional placement, explicit terrain height and a following camera.
Synthetic/sanitized, local GL and native walking evidence pass. The merged PR
records five checked human steps for `ae4a3ef`. Preserve that bounded result;
the maintainer subsequently reported wrong apparent facing when orbiting.
The current card turns toward the camera but retains the original view's frame.
View-correct facing and high-angle readability remain open, along with affine/
effects and full actor/mode coverage. See [M6/M9](../roadmap.md#current-focus).

## Acceptance

- [x] Preserve captured source frame, palette/index, original facing, foot pivot and subpixel position in the bounded sequence.
- [x] Do not read mutable guest data from the presentation thread.
- [x] Clear incompatible actor state across map generations.
- [x] Keep terrain layer/height and jump offsets distinct.
- [ ] Choose available directional art for apparent facing around the actor,
  preserving the guest's real facing and animation phase. Verify four viewing
  quadrants and low/steep pitch with stable feet; do not infer missing views
  from a single captured frame. Pair this with M9's explicit camera/input modes.

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
