# Native game integration: prototype source

**Current M5 handoff:** PR #29, including PR #30 camera/facing, is merged into main.
The combined prepared build also includes [source border restoration](../docs/live-borders.md).
Use that guide's single camera-session launcher; the old main-checkout prepared
developer executable is historical. Ledge collision/depth, Bag retention and
broader actor/terrain coverage remain open.
The active [connected native scenery](../docs/live-connected-world.md) slice
uses a verified-ROM source reader and the shared region renderer to keep up to
three nearby complete maps. It uses the same launcher, with a new pending
human checklist. Distant NPC simulation and unrestricted travel remain open.

The default standalone CMake targets do **not** build the game adapter. The
optional local GL test builds the desktop viewer with synthetic data. These files preserve our
existing capture, renderer, viewer and OpenXR work so contributors can understand
the intended connection without importing the upstream game runner's history.
There is no supported end-user installation command for this folder yet.

[Developer mode](../docs/developer-mode.md) documents the private runtime-menu
and dispatch-boundary adapter, named checkpoint isolation, pause/step and speed,
and the reviewed Ruby collision-entry hook. Its portable session controller is
tested without the external runner; the native build still needs the local
runner changes described there.

[Camera-relative gameplay](../docs/live-camera.md) adds the runtime-thread
`game_input::filter(keys, host_menu_open)` boundary after host bindings/settings
capture, before KEYINPUT and input recording. Replays bypass it because they
already contain guest directions. Checkpoint loads call `game_input::reset()`.
Physical input samples SDL focus; deterministic native drivers may provide an
explicit `Source` through `filter_from_source`, exercising the same mapper and
verified on-foot field gate. The local runtime call-site changes are still
required; publishing the adapter does not publish that separately licensed runner.

**Current priority, 2026-09-12:** retain this native game route and complete the
[monitor gameplay proof](../docs/issues/007-native-integration.md#native-desktop-proof).
Verified live identity/invalidation is the first bounded change, now
[merged with local native evidence and five human checks passed](../docs/live-map-identity.md). The existing
desktop viewer is a prototype, not evidence that the current Studio terrain
and complete gameplay already work together. See the [single roadmap](../docs/roadmap.md).

## Development base

| Component | Upstream base | Local development reference |
|---|---|---|
| RubySapphireRecomp | `4d49909cbc6dccd3fbb0087cb68347ffbe55ce5d` | `dad4c68251aa3adde77be47884b17870a151f429` plus working changes |
| gbarecomp | `a1de406b179addf10a534369b64f449e5fe28c4a` | `13cab0418106e86708cfd10b817379fe2318b201` with a frame-sink hook |

These local references document provenance; they are not promised public commits
that a contributor can fetch. Required runner/CMake/frame-sink changes are not
included as a ready-to-apply upstream patch. Do not claim that dropping this
directory into an upstream checkout builds RubyVR.

## Intended connection

1. At a safe emulation boundary, the game adapter reads supported guest memory
   into an immutable `vr::world::Snapshot` and captures the original frame.
2. The presentation side receives this snapshot; it does not follow mutable
   guest pointers from the VR render thread.
3. Shared `src/vr` code resolves accepted patterns and builds scenery. The
   renderer consumes the same geometry contract tested by the editor/batch tool.
4. OpenXR presents stereo scenery and/or the original frame on an intentional
   UI surface. Scene-mode routing, full animated-sprite capture and complete
   gameplay presentation remain unfinished.

`runtime/ruby_world.cpp` is the game-specific capture adapter.
`runtime/renderer.*`, `vr_layer.*`, `viewer.*` and `map_view.*` handle presentation.
Their include/build environment currently belongs to the development runner;
the public integration work package must define a reproducible boundary.

`rubyvr-runtime.cmake` provides our current source list for the private Windows
runner experiment; it still requires the caller's runtime headers, libraries
and frame-sink integration. See the [checks and limits](../docs/live-map-identity.md).

The [live actor slice](../docs/live-actors.md) **merged in PR #28**. It copies original
OBJ frames and selected subsprite profiles into the transient snapshot, renders
upright actors through the shared diorama path and follows their resolved feet.
The native desktop recording covers one small walk/turn scene. Menu caching,
other field effects, complete transitions and public game packaging remain open.

## Licence compatibility before distribution

Our original integration source and shared renderer use the project's
GPLv3-or-later terms. The standalone editor does not link the
runner. The pinned framework has its own Noncommercial terms and clarification;
the pinned RubySapphireRecomp base has no established redistribution permission.
Current upstream now declares PolyForm Noncommercial as well; see the dated
[licence update](../docs/licensing.md#separate-native-game-integration).
Establish compatible rights for the actual combination before distributing it.
Noncommercial restrictions cannot simply be added to a GPL-covered combined
work. That distribution needs compatible upstream permission or an appropriate
exception from the relevant rightsholders. No linking exception is granted by
this licence change. A plugin boundary alone does not establish permission.

This remains [RV-007 / M5](../docs/issues/007-native-integration.md). Standalone
editor work and private experiments can continue. See [licensing](../docs/licensing.md).

## Work required for a supported integration

- Clarify redistribution/integration terms for the pinned runner and honour
  the framework's existing licence. Do not copy restricted upstream code into
  the editor repo as a workaround.
- Select and document a public upstream revision/API. Evaluate its current
  `.gbamod` and compiled native-plugin interfaces against our snapshot and
  renderer needs, then implement the smallest supported adapter.
- Version scene/map identity, invalidation and capture lifetimes; keep old
  snapshots explicitly readable or explicitly rejected with a useful error.
- Provide commands using user-supplied supported ROM/BIOS inputs, plus local
  capture/replay checks. Synthetic fixtures should be used for public CI.
- Demonstrate field/warp/menu/battle/return state routing, then real headset
  tests. A successful Studio build proves none of these runtime outcomes.

See [source notices](../THIRD_PARTY_NOTICES.md),
[roadmap](../docs/roadmap.md) and [work packages](../docs/issues/README.md).
