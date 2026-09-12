# Capture one animated actor sequence with correct source identity

Work package **RV-010** · M6 Actors and effects · help wanted, area: runtime · player and ordinary NPC slices merged; broader actor coverage pending

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

The maintainer's PR #30 report on `6911329` adds animation defects and distant
sprite pop-in. The [Emerald implementation audit](../emerald-camera-actor-audit.md)
confirms that our draw visibility inherits both Ruby's original 2D off-screen
flag and live object-slot lifetime. Animation timing needs its own paired trace;
do not assume all animation symptoms are explained by facing.

**Normal-player follow-up merged through PR #30 into PR #29 (`9d895c7`):** [camera-facing original art](../live-facing.md)
for Brendan/May's normal on-foot profiles. Reads the verified directional tables
and matches the actually displayed pose before selecting another view. The native
trace matches 741/741 poses and recovers eight metadata/image transitions; the
synthetic/GL and native Brendan checks pass. The combined launch/border/camera
human step is checked; native May, special profiles and distant pop-in remain
open. The broader acceptance
items below remain unchecked.

**Dated follow-up, 2026-09-12, after PR #32 (`a6c8aa0`):** the maintainer reports
NPCs showing the opposite view during camera turns. The previous player-only
scope did not cover NPC profiles; its original acceptance remains recorded.
The [NPC views and viewport repair](../live-npc-views.md) merged in PR #33 (`b4f6423`):
ordinary directional profiles, exact displayed phase/palette/feet, queued-copy
flip transitions and active-event visibility past the original screen. Eight
native replay checks pass. All five user retest steps are checked for `09d28c0`;
live-slot/neighbor presentation, unsupported profiles and full effects remain open. No paid worker was used;
exact Codex token/time attribution for the repair is not separately available.

## Acceptance

- [x] Preserve captured source frame, palette/index, original facing, foot pivot and subpixel position in the bounded sequence.
- [x] Do not read mutable guest data from the presentation thread.
- [x] Clear incompatible actor state across map generations.
- [x] Keep terrain layer/height and jump offsets distinct.
- [ ] Choose available directional art for apparent facing around the actor,
  preserving the guest's real facing and animation phase. Verify four viewing
  quadrants and low/steep pitch with stable feet; do not infer missing views
  from a single captured frame. Pair this with M9's explicit camera/input modes.
- [ ] Keep visible 3D actors stable across the original 2D culling and live-slot
  boundaries using verified presentation records; preserve script/event hiding,
  neighbour ownership and warp/checkpoint invalidation. Do not force gameplay
  actors to spawn just to fill a wider camera view.
- [ ] Compare idle/walk/turn phase at matching guest frames and four camera views;
  identify and repair any capture/animation timing mismatch instead of accepting
  an unrelated movement or orbit recording as proof.

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
