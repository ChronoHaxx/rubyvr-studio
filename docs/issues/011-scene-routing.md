# Route field, dialogue, menu and battle transitions without stale scenery

Work package **RV-011** · M7 Game presentation · help wanted, area: runtime, status: desktop slice in review; full-game/VR coverage pending

## Problem

A captured world and original framebuffer do not yet constitute complete game presentation. Incorrect mode transitions can leave a stale world or duplicate layers.

## Bounded contribution

Define a small scene-mode state machine from observed guest signals and implement field → dialogue/menu → battle → field for a supported runner. Keep temporary full-frame fallback explicit.

The [native desktop play session](../live-play-session.md) is in review:
recognized field-menu retention, original field UI ownership and same-viewer
battle/interior fallback. Native starter battle, lab/exit, Party, save and
checkpoint evidence are included. This is the prepared private runner, not a
supported public installation or a completed headset route.

## Acceptance

- [ ] Each layer has one intentional destination.
- [ ] The first valid return frame replaces stale scene state.
- [ ] Text/options remain readable and controllable from both eyes.
- [ ] Log unrecognized states as unfinished coverage, without altering gameplay.

## Where to start

`integration/runtime/vr_layer.*`, `renderer.*`, `ruby_world.cpp`, roadmap M5/M7.

## Validation and evidence

Synthetic transition sequences and a local retained live walk/menu/battle/return recording. A headset check is separate from desktop replay.

Dependencies: RV-007, RV-008.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-011 -->
