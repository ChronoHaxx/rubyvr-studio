# Version map identity in source-built and live snapshots

Work package **RV-008** · M5 Live integration · help wanted, area: runtime, area: terrain · source half merged; live half remains design

## Problem

A disk-built layout identifier and a live ROM layout pointer are not a stable common key for terrain or review state.

## Bounded contribution

**Source half merged in [PR #26](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/26), with [RV-003](003-terrain-contract.md):** validated
group ordering for all 394 source maps, snapshot v2 identity and copied-border
provenance, legacy-unknown reads and same-layout cache invalidation.
The live prototype explicitly clears source identity/provenance; verified
live map keys and connections have not been implemented or captured.

Add explicit map group/number and validity/provenance to source and live snapshot adapters, with a versioned disk encoding. Verify the pinned source schema before reading guest offsets. Keep legacy snapshots explicitly unknown rather than guessing identity.

## Acceptance

- [x] Source group ordering and map names map deterministically to identity.
- [ ] Live reads happen at the capture boundary; missing/invalid state is represented.
- [x] Read legacy files without fabricating a valid map ID; reject malformed/new unsupported formats.
- [x] Keep same-layout source maps distinguishable and invalidate their caches.
- [ ] Verify live map identities and transitions in actual captures.

## Where to start

`src/studio/decomp_source.*`, `snapshot_build.*`, `src/vr/ruby_world.h`, `world_io.*`, `integration/runtime/ruby_world.cpp`.

## Validation and evidence

Synthetic round-trip/invalid/legacy fixtures, local source identity checks and a separately reported live capture once the public runtime exists.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-008 -->
