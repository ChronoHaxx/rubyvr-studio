# Connected terrain example

**M2, merged — [PR #27](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/27).** Oldale's north and west entrances now meet their neighbours
without an artificial map-edge wall. Route 102 has authored south/east ledges;
Route 103's western approach has two terraces. [Watch the 12-second comparison](acceptance.md#merged-connected-region-example--pr-27).

**Merged in PR #28:** the [level-water follow-up](terrain-water.md) keeps selected
pond/sea cells and their immediate shore at 16 px. The launcher below includes
that follow-up. Complete banks/coastal depth remain open.

## Run it

After the [local source setup and build](building.md):

```bash
bash tools/run-studio.sh --terrain-regions
bash tools/run-studio.sh --terrain-regions --map MAP_ROUTE102
```

The launcher compiles `build/terrain-regions/regions.json`, starts Oldale by
default and gives the session a fresh personal output path. The default starter
and existing personal files stay unchanged. `--terrain-example` still opens the
older two-map example. The two switches cannot be combined with each other or
with `-Overrides`.

In **DIORAMA**, tick **Fly** beside **Room** above the 3D view. Hold the right
mouse button over that view to look around, then use **WASD** to move,
**Q/E** to descend/ascend and **Shift** to move faster. Release right mouse or
press **Escape** to stop; **Room** resets the view to the current map.

Use the top map dropdown to inspect `MAP_OLDALE_TOWN`, `MAP_ROUTE101`,
`MAP_ROUTE102`, `MAP_ROUTE103`, `MAP_LITTLEROOT_TOWN` and `MAP_PETALBURG_CITY`
with the same loaded pack. **Light** above the 3D view provides the existing
editor lighting presets. These are preview phases, not a running day/night cycle.

The generated pack authors 4,700 map-body cells. The editing view displays one
map plus copied padding. Add **`--connected`** to the launcher or click **Explore
area**. The four-map explorer merged in PR #30; the [six-map follow-up](region-model-reuse.md)
merged in PR #31 and includes Littleroot and Petalburg through model reuse.
[Fly controls and bounds](connected-scene.md). This preloads the area; it does
not load the next map when the camera reaches an outer boundary.

## Authored geography

| Map | Height choices, in original sprite pixels | Reviewed scope |
|---|---|---|
| Oldale | 16 | Three same-height connections |
| Route 101 | 16 → 8 → 0 | Previous corner field retained exactly; two ledges and walk-around grades |
| Littleroot | 0 | Connection context only |
| Route 102 | Central terrace 24; approaches 16; southern pocket 8 | South/east ledges, continuous approaches; level pond/shore merged in PR #28 |
| Petalburg | 16 | Eastern connection with a 10-cell offset; level ponds merged in PR #28; city geography remains open |
| Route 103 | Western approach 16 → 24 → 32 | Two western terraces; level sea/shore merged in PR #28; complete coast/cliffs remain context |

These heights are explicit choices in [the recipe](../recipes/terrain-regions.json).
Neither colour nor gameplay elevation bits establish physical height. Ledge
metatile positions are guarded against the pinned source. Cleared ledge tops
use the chosen grass underlay; native cliff art keeps its 8 px vertical phase.
The upper elbow on Route 103 includes the original corner cell, avoiding a
leftover flat corner graphic.

The report names two frontiers: Petalburg–Route 104 and Route 103–Route 110.
Those connections are **not authored by this example**. Only the shared
General/Petalburg tileset pair is supported here; a different pair is refused.

## Corner constraints

The original [Python solver](../tools/terrain_regions.py) derives map origins
from declared source connections and offsets, rejecting inconsistent loops,
overlapping bodies and disconnected selections. It joins corners across
ordinary ground edges and included map boundaries. Explicit cliff edges keep
their two sides separate; endpoints that join around a walkable end taper to
zero instead of leaving a wall across the path.

Authored pins and cliff lips constrain a harmonic grade field. Seeds initialize
the relaxation; they are not additional fixed height measurements. Integer
rounding happens once per shared corner, so neighbouring cells cannot round
apart. Conflicting anchors, unanchored components and failure to converge are
errors. The retained Route 101 field is fully pinned and unchanged.

The compiler accepts a terrain-free input pack and preserves its model patterns.
It refuses an input that already contains authored terrain rather than silently
discarding that terrain. Generated packs and source assets stay local.

## Verification

```bash
python tools/test-terrain-regions.py
python tools/test-terrain-region-source.py
python tools/render-terrain-regions.py
```

PR #27's eight source-free tests cover offset boundaries, integer continuity, reversed
elbows, tapered ends and invalid inputs. They also run in CI. Local tests use
the production C++ query and mesh: **200 boundary checks across both views of
100 shared segments**, with no internal map walls. The solver checks **8,994
non-cliff internal edges**. The existing 197 native checks and 49 SDL checkpoints
still pass. The original two-map test still has 40 matching seam edges.

The source test deliberately introduces a 1 px Route 102–Oldale gap and changes
the Route 103 elbow guard; both are rejected. It also checks refusal to overwrite
existing terrain and verifies unchanged source recipes, input bytes and patterns.
All 394 source maps are built to check connection ownership; this does not
approve all-map terrain. [Exact evidence](terrain-regions-evidence-2026-09-10.json).

The water follow-up extends the suite to 13 source-free tests and adds native
water surface checks; [current water evidence](terrain-water-evidence-2026-09-10.json).

Codex inspected the actual comparison frames plus the Petalburg offset view.
The GIF contains four paused before/after views, cropped and captioned from
the production renderer. It is a desktop geometry check, not a gameplay demo.

Raised banks/underwater depth, complex foundations, world streaming and live
traversal remain M2 work. [Level foundation](terrain-foundations.md) merged in
PR #29; full immediate neighbours with their own textures merged in PR #30.
[Model reuse and six-map exploration](region-model-reuse.md) merged in PR #31.
Trees, shrubs
and grass appearance remain deferred to M4. Track completion only in the
[canonical roadmap](roadmap.md).
