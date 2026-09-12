# Define a supported public RubySapphireRecomp integration boundary

Work package **RV-007** · M5 Live integration · help wanted, area: runtime · native desktop proof prioritized; implementation pending

## Problem

The editor is independent, but our native runtime prototype still relies on local runner/frame-sink changes. It is not an installable public mod.

## Bounded contribution

Document a compatible public upstream revision and required API contract. Establish the runner redistribution/integration permission before publishing runner code. Evaluate the existing .gbamod/compiled-plugin API, then propose one small adapter PR rather than copying the engine.

## Native desktop proof

Maintainer decision, 2026-09-12: continue the native RubySapphireRecomp/gbarecomp
route. Use the existing Studio renderer and terrain contracts in actual game
execution on a monitor before adding further headset presentation. This is a
bounded M5/M6/M7 demonstration; complete world coverage remains in the roadmap.

The first implementation is [RV-008 live identity/invalidation](008-map-identity.md).
The current public capture adapter resets identity to unknown and clears
connections. The old local desktop viewer can orbit/follow a captured map, but
does not prove the current authored terrain, actors or game-mode routing.
Refreshing that local runner must preserve its unrelated working changes and
use the current shared renderer, not another mesh implementation.

Build the proof in small steps:

1. Capture verified map identity, connections and valid/invalid generations for
   a pinned Ruby revision. Test malformed/inactive snapshots headlessly, then
   retain real transition captures. No guessed map names from layout pointers.
2. Connect one authored outdoor area and one adjacent map to the native desktop
   viewer. Show the original animated player and one NPC with explicit terrain
   layers. Start with a fixed tilted follow camera and original movement rules.
3. Preserve the original frame for menus, battle and unsupported scenes; expose
   fallback status. Enter/leave an interior, talk, open/close a menu, enter/exit
   a battle and save/reload. Never present a retained outdoor scene as a live
   battle/interior or label a fallback interior as completed voxel art.

Use existing authored geography and locally available gameplay fixtures when
selecting the route; do not claim a reachable sequence from editor map names.
Keep a separate test save, record the runner/core/Studio revisions and active
pack, and give one exact launch command once that build has been verified.
The Studio launch command still opens an editor, not this proof.

The first developer tools are a read-only state/height/timing display,
inspection camera and reproducible capture. Pause/step must use the runner's
clock. Warp, encounter, party/flag and noclip controls need separately verified
game-side operations; changing a host camera must not move the guest player.
Map-inspection UI is M3; additional view modes are M9. The source audit is in
[references](../references.md#camera-editor-and-debug-reference-audit).

**Evidence to return:** headless identity/invalidation results; an actual
native-game recording of the sequence above; logs of source identity, layer,
fallback transitions, geometry update and frame timings; and revision-specific
human checks for startup, movement, transitions, menu/battle and save/reopen.
Keep automated, visual, human and headset results separate. No playable/native
or headset acceptance is added by this planning update. Public distribution
still needs the existing rights/API acceptance below.

## Acceptance

- [ ] Record exact upstream licence/API references and unresolved permission questions.
- [ ] Establish compatible distribution rights for RubyVR's GPLv3-or-later
  source, the framework's Noncommercial terms and the pinned game runner.
  Resolve any required permission or exception with the relevant rightsholders;
  this licence change grants no linking exception. A plugin boundary alone is
  not permission. See [licensing](../licensing.md#separate-native-game-integration).
- [ ] Describe snapshot lifetime, clock ownership, original-frame delivery and renderer lifecycle.
- [ ] Provide a reproducible source-only integration plan using user-supplied game inputs.
- [ ] Clearly distinguish implemented adapter work from missing live/full-game/headset validation.

## Where to start

`integration/README.md`, `integration/runtime/`, `src/vr/ruby_world.h`, `THIRD_PARTY_NOTICES.md`.

## Validation and evidence

Start with source/API evidence and an original synthetic producer/consumer fixture. Any later runner proof must record the exact public revision and inputs without distributing those inputs.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-007 -->
