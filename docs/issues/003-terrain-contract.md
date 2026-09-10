# Add the first versioned authored terrain surface and shared height query

Work package **RV-003** · M2 Terrain · help wanted, area: terrain · terrain/connected explorer and model reuse merged; camera-driven loading in review

## Problem

The original DIORAMA floor was flat. Gameplay elevation bits are layer constraints, so mapping each value directly to a physical height gives incorrect terrain.

## Bounded contribution

The [first contract and implementation](../terrain-authoring.md) merged in
[PR #26](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/26):
v7 surfaces, explicit layer query, source snapshot identity, native-scale sides,
editor controls and save/history. Copied-neighbour cells reuse their owning
map's terrain instead of retaining an independently flat border. A local
Oldale–Route 101 fixture now includes both 8 px ledges and continuous grades
around their ends, with matching heights across the map boundary.

**Merged — [PR #27](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/27):** [six-map connected-region example](../terrain-regions.md), including
Oldale's north/west neighbours, regional corner constraints and Petalburg's offset
join. Keep context-only coastal/city geography and Route 104/110 frontiers explicit.
At that baseline, full-neighbour atlases and foundations were pending; follow-ups
are recorded below. The local example is not a completed map conversion.

**Merged — [PR #28](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/28):** [level water/shore contact](../terrain-water.md) in Petalburg,
Route 102 and Route 103. Author-selected material bounds and source guards keep
495 water cells level at 16 px with 266 flat shore cells; original artwork is
retained. Complete coastal geography remains open.

**Merged — [PR #29](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/29):** [Level foundation](../terrain-foundations.md) is an explicit,
undoable pad edit using the highest authored ground under a rigid model's base.
It preserves model shape/art and outside terrain. The shown Oldale hill is a
controlled test, not proposed geography; entrance approaches remain manual.
**Merged — [PR #30](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/30), connected explorer:** [full immediate neighbours](../connected-scene.md)
with per-map textures, ownership masks, cached buffers and desktop flight.
That merged baseline loads four maps from Oldale.
**Merged — [PR #31](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/31), [model reuse and six-map exploration](../region-model-reuse.md):**
the identical four-map allocation falls 76.2%; two authored hops now include
Littleroot and Petalburg. **In review — [PR #20](https://github.com/ChronoHaxx/rubyvr-studio/pull/20), [camera-driven map loading](../camera-map-streaming.md):**
prepare a moving window in the background, reuse unchanged GPU buffers and
release distant maps without moving the world or altering the editor.
The reported outer cutoff/void
belongs to this package's borders/undersides; horizon blending and fog belong
to M8. First-person alone is not an edge fix.

## Acceptance

- [x] Use stable source map identity and guards, not a ROM pointer as a public map key.
- [x] Keep elevation 0/15 semantics explicit; guest collision remains authoritative.
- [x] One query resolves the selected surface for model placement; unknown layers remain explicit.
- [x] Persist/undo/reopen exactly and preserve v5/v6 meaning.
- [x] Document connected maps, bridge layering and live camera/feet work still pending.
- [ ] Review complete Route 101 geography and Oldale's other connected regions;
  keep incomplete neighbours, incompatible atlases and unresolved foundations visible.
- [x] **Merged — PR #27:** guarded Route 102/103 ledges, six-map region constraints and
  200 production boundary checks; retain Route 101 exactly and reject a 1 px
  broken join. [Short visual review](../acceptance.md).
- [x] **Merged — PR #28:** level water and adjoining shore constraints, source-change
  refusal and native mesh checks; preserve source art, Route 101 and 200 join checks.
- [x] **Merged — PR #29:** rigid base pad, highest-corner height rule, conservative refusals,
  exact persistence and undo; 23 native checks and 14 SDL checkpoints pass.
  [Actual-render review](../terrain-foundations.md). Complex foundations and
  access-route authoring remain open.
- [x] **Merged — [PR #30](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/30), connected explorer:** four-map Oldale view, source-owned floor
  and overhangs, map-local materials, cached/cullable buffers and exact return
  to editing. Seventeen native checks, 20 SDL checkpoints and an exact 32,000-pixel
  different-atlas source-floor comparison. [Visual evidence](../connected-scene.md).
  The PowerShell argument-splitting launch bug is fixed in the same PR, with
  six native-argument cases added to CI and real GUI entry checked in both shells.

- [x] **Merged in PR #31 — model reuse/six-map explorer:** preserve ordered geometry and
  source indices while sharing rigid models, keep ground-following deformation,
  and verify wider flight and unchanged editor state. [Evidence](../region-model-reuse.md).
  Streaming, outer borders and full geography remain open.
- [ ] **In review — camera-driven loading:** bounded background preparation,
  fixed origins across unloading/return, unchanged-buffer reuse and cancellation
  without late publication. Both authored round trips and source-floor travel
  beyond the initial window are checked. [GIF and measured limits](../camera-map-streaming.md).
  Whole-world geography, outer borders and headset upload budgets remain open.

## Where to start

`src/vr/ruby_world.h`, `world_io.*`, `overrides.*`, `diorama.*`, `src/studio/pattern_io.*`, `docs/architecture.md`.

## Validation and evidence

[PR #26 actual-render acceptance](../acceptance.md#merged-first-terrain-foundation--pr-26): 197 original synthetic
checks, 49 SDL checkpoints, all 394 source maps/36,834 copied cells checked,
720 canonical terrain copies, 40 equal-height seam edges across two views and
746 continuous non-cliff edges inside Route 101.
The existing editor regressions pass with all eight frozen hashes unchanged.
The checked scope merged in PR #26, with passing source/build CI. PR #27 added
200 regional join checks and also merged with passing CI. The water follow-up
has 13 source-free tests, 495 queried water cells and 7,920 level surface
triangles; [exact evidence](../terrain-water-evidence-2026-09-10.json).
Full M2 exit criteria remain open.
No live/headset claim from editor tests.

Dependencies: RV-008 source identity is included in this foundation; the live
adapter remains future work.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-003 -->
