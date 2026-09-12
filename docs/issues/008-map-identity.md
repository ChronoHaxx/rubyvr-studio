# Version map identity in source-built and live snapshots

Work package **RV-008** · M5 Live integration · help wanted, area: runtime, area: terrain · source and bounded live identity/invalidation merged; broader live captures pending

## Problem

A disk-built layout identifier and a live ROM layout pointer are not a stable common key for terrain or review state.

## Bounded contribution

**Source half merged in [PR #26](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/26), with [RV-003](003-terrain-contract.md):** validated
group ordering for all 394 source maps, snapshot v2 identity and copied-border
provenance, legacy-unknown reads and same-layout cache invalidation.
The [bounded live implementation](../live-map-identity.md) now validates pinned
Ruby map keys/connections and publishes invalidation. Synthetic and local native
checks pass; all five human checks passed in merged PR #27. Broader live
transition captures remain open.

Add explicit map group/number and validity/provenance to source and live snapshot adapters, with a versioned disk encoding. Verify the pinned source schema before reading guest offsets. Keep legacy snapshots explicitly unknown rather than guessing identity.

**Merged in [PR #27](https://github.com/ChronoHaxx/rubyvr-studio/pull/27), 2026-09-12:** populate verified live identity and copied
connection provenance at the existing safe capture boundary for one pinned Ruby
revision. Publish invalidation when leaving a valid field scene; plausible
retained map data during battle/menu is not a field-state signal. Separate the
pure validation/decision logic from guest memory access so synthetic checks
need no SDL, OpenGL, game assets or physical input. Retain real capture evidence
separately for same-layout map changes, a connected edge and an interior warp.
This enables the [native desktop proof](007-native-integration.md#native-desktop-proof);
it does not itself render actors, complete M5 or prove the full game playable.
The subsequent M5/M7 menu work must separate capture refusal from presentation:
recognized field menus retain the last valid world behind the original UI.
PR #27's cleared Bag background is a temporary safeguard, not the target UX.

## Acceptance

- [x] Source group ordering and map names map deterministically to identity.
- [x] Live reads happen at the capture boundary; missing/invalid state is represented for the pinned Ruby revision.
- [x] Read legacy files without fabricating a valid map ID; reject malformed/new unsupported formats.
- [x] Keep same-layout source maps distinguishable and invalidate their caches.
- [ ] Verify live map identities and transitions in actual captures.
  Route 101 and Bag/return passed; connected-edge crossings and interior warps
  remain unverified. Same-layout changes currently have synthetic evidence only.

## Where to start

`src/studio/decomp_source.*`, `snapshot_build.*`, `src/vr/ruby_world.h`, `world_io.*`, `integration/runtime/ruby_world.cpp`.

## Validation and evidence

Synthetic round-trip/invalid/legacy fixtures, local source identity checks and
separately reported captures from the private native prototype. A public runner
release is not required to collect local evidence; distribution rights and a
reproducible public integration remain separate RV-007 requirements.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-008 -->
