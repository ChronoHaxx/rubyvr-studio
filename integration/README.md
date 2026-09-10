# Native game integration: prototype source

The standalone CMake targets do **not** build these files. They preserve our
existing capture, renderer, viewer and OpenXR work so contributors can understand
the intended connection without importing the upstream game runner's history.
There is no supported end-user installation command for this folder yet.

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

## Licence compatibility before distribution

Our original integration source and shared renderer use the project's
Noncommercial and No-Sales terms. The standalone editor does not link the
runner. The pinned framework has its own Noncommercial terms and clarification;
the pinned RubySapphireRecomp base has no established redistribution permission.
Establish compatible rights for the actual combination before distributing it.
A plugin boundary or a similar licence name does not establish permission.

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
