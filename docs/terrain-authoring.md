# Authored terrain and connected map surfaces

M2's first terrain foundation is **merged in [PR #26](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/26)**.
[Watch the current result](acceptance.md).
It provides explicit editable surfaces; complete geographic reconstruction and
live integration remain on the [roadmap](roadmap.md#m2-terrain-and-placement).

## Use the editor

Choose **Terrain** in the toolbar, drag a rectangle in the source map, enter a
height in source pixels, then choose **Set plateau** or **Steps north/south**.
The **Sloping ground** section adds **Slope north/south**: Height is the high
end and Stair rise is the drop per row, with a continuous grade across rows.
Each application is one undoable edit. Selection alone changes no document data.
**Erase terrain** restores the original floor. **Focus region** frames the edit.
**Save terrain + models** uses the same atomic document save as object authoring.

Top, Sides and Underlay accept metatile IDs or **Pick** from the source map.
`-1` retains the existing/recovered floor. Explicit underlay replaces the old
ground pixels beneath removed scenery; explicit top art controls the top face.
Source pixels repeat in native 8 px bands down a cliff instead of stretching.

The orange outline marks the current map body. Green cells show terrain copied
from a neighbour; edit those in their owning map. A boundary is a coordinate
change, not a reason to add a cliff. Corners must bound a raised area, including
the appropriate land and scenery behind them. Rectangular controls are initial
tools; region fills, traced boundaries and general constraint solving remain M2/M3.

Layer `-1` copies each selected cell's gameplay layer; it does not turn that
layer number into a physical height. A deck needs a separate explicit layer
and a positive thickness. Unknown/transition layers cannot arbitrarily choose
between stacked surfaces. The GUI shows rejected cells/placements for review.

## Data contract

Override v7 adds `terrain: {version: 1, maps: [...]}` alongside existing patterns.
V5/v6 files preserve their previous meaning and require no terrain migration.
The writer validates the whole candidate and atomically replaces a valid file.
Unknown versions, malformed types, duplicate positions/layers and missing
material guards fail without changing the old saved file.

| Record | Fields and meaning |
|---|---|
| Map | `group`, `number` from source map-group ordering; backup `width`, `height`; `tiles` and `cells` |
| Cell | `x`, `y` in its owning map's backup coordinates; full packed `expected` source cell; `underlay`; `surfaces` |
| Surface | Gameplay `layer`, authored pixel `height`, solid `thickness`, `kind` (`ground`, `water`, `deck`), `top` and `side` materials |
| Material guard | Metatile `id`, eight exact tile entries and attribute word; palette/index identity is retained |

Only primary-map cells can be authored: source `(0,0)` is backup `(7,7)`.
The low collision bits and layer bits remain part of the exact source guard.
Source/material changes reject affected terrain instead of silently reauthoring
it. Animation/palette changes alone do not invalidate geometry.

Heights currently span 0..256 px. Ground is solid from zero to its top; water
has zero thickness; decks have positive thickness no greater than their height.
Up to four non-overlapping surfaces use distinct layers. Multiple surfaces
require layers 1..14. A single surface can retain source layer 0 or 15.
Limits also bound document/map cells and a conservative 2,000,000-vertex visible
terrain allocation, including combined neighbours. Negative terrain, stacked
graded surfaces and detailed water/shore rendering remain future work.

Ground surfaces optionally include integer `rise_x`, `rise_z`, `corner_delta`
and `side_offset`. All default to zero, preserving earlier flat v7 files. Their
NW/NE/SW/SE corners are `height`, `height+rise_x`, `height+rise_z`, and
`height+rise_x+rise_z+corner_delta`. All corners must stay in 0..256 px.
Interpolation follows the NW–SE triangle diagonal, matching rendered triangles.
Grades are ground-only and cannot be stacked. `side_offset` selects a native
0..15 px vertical material phase; adjacent buried face portions are clipped.

`terrain::resolve` checks identity, source and material guards once for a scene
rebuild. `Resolved::query(x,y,layer,u,v)` returns authored height, legacy flat,
unresolved or source mismatch. Layers 1..14 match exactly; -1/0/15 resolve only
a sole surface. Models use this query at their existing centre/front anchor.
The optional local `u,v` coordinates default to the cell centre and must be in
0..1. Voxel patterns with `follow_ground: true` sample the same surface per
vertex; their source UVs remain unchanged. Rigid models keep one anchor.
The explicit [Level foundation action](terrain-foundations.md), **merged in PR #29**,
can flatten a whole-cell rectangle beneath a rigid zero-height base to the
highest authored corner. It does not run automatically or grade entrances.
Guest collision and movement remain authoritative; live player feet/camera
are not wired to it yet.

## Snapshot identity and copied neighbours

Snapshot v2 retains the v1 fields, then appends little-endian identity provenance
(`u8`), group and number (`i16` each), and a `u8` connection-slice count (0..64).
Each slice stores ten `i16` fields in this order: group, number, source backup
width/height, destination x/y, source x/y, width/height; a final `u8` records
whether both source tileset symbols match the current map's pair.

Source identity comes from `data/maps/map_groups.json`. Each connection slice
records exactly which cells Ruby's connection-copy arithmetic placed in the
border, including offsets, clipping and eight-column east padding. Overlapping
copies retain the last writer's ownership. Terrain stays stored once in the
owning map and is projected into copied coordinates for rendering and queries.
Adjacent solid faces are removed using the same resolved heights across the seam.

V1 files load with unknown identity and no connection provenance. V2 rejects
unknown provenance, invalid rectangles, trailing data and unsupported versions.
The existing live prototype clears these source-only fields until its own
verified adapter is implemented. Same-layout identity, grid, material and
connection changes invalidate the terrain mesh cache; an unchanged view does
not repeatedly upload geometry.

Different tileset pairs currently produce a terrain source mismatch. Full
neighbour bodies/atlases, global origin constraints, door/warp transitions,
continuous live traversal and frame-time budgets remain open. A matching
copied border does not establish a seamless complete world.

## Reference and geography review

Inspected DRAMALESS_SHAPE at
[`5ccc0e25417bd8b16cefd50cf08f61c44223e3d4`](https://github.com/artyrambles/DRAMALESS_SHAPE/tree/5ccc0e25417bd8b16cefd50cf08f61c44223e3d4).
Its [TileShape](https://github.com/artyrambles/DRAMALESS_SHAPE/blob/5ccc0e25417bd8b16cefd50cf08f61c44223e3d4/lib/TileShape.lua)
resolves authored class rules before walkability/water fallbacks; its
[height profiles](https://github.com/artyrambles/DRAMALESS_SHAPE/blob/5ccc0e25417bd8b16cefd50cf08f61c44223e3d4/data/voxel_heights.lua)
include hand-tuned ledge and terrace treatments. This does not establish a
general solver that automatically raises every region behind a ledge.

[VoxelScene](https://github.com/artyrambles/DRAMALESS_SHAPE/blob/5ccc0e25417bd8b16cefd50cf08f61c44223e3d4/lib/VoxelScene.lua)
places connected neighbours at their offsets and builds masks from their bodies.
[ChunkMesher](https://github.com/artyrambles/DRAMALESS_SHAPE/blob/5ccc0e25417bd8b16cefd50cf08f61c44223e3d4/lib/ChunkMesher.lua)
suppresses the overlapping border geometry and bounds cached neighbourhoods.
The design lesson is to retain canonical neighbour ownership and consistent
placement. The new C++ terrain implementation is original; existing adapted
reference code and its retained MIT notice are documented in [references](references.md).
Dramatic Studio's supplied model-editing demo does not establish its terrain
or connected-map implementation, and that editor has not been run here.

The local example reads pinned pokeruby Oldale and Route 101 maps. Both are
20×20 with the General/Petalburg tileset pair and reciprocal zero-offset
north/south connections. Route 101's first ledge has an elbow between source
rows 6 and 7. `terrain-seam-fixture.py` guards those actual metatiles, raises
Oldale and the upper region together by an **authored 16 px**. Two **8 px**
ledges descend to the southern 0 px boundary. Native lower-half cliff art uses
`side_offset: 8`. Shared corner constraints keep all 746 non-cliff internal
Route 101 edges continuous, with tapered ledge ends and graded walk-arounds.
These are explicit authored choices; gameplay layer bits supply no measurement
of the rise. The [six-map regional follow-up](terrain-regions.md) adds Oldale's
other neighbours and source-offset constraints. Complete geography and fitting
rigid foundations remain unfinished.

Launch this local example with `./tools/run-studio.ps1 -TerrainExample`. It
opens Route 101 with a new personal output path. The fixture remains under
`build/terrain-review/`; it does not replace the default starter pack.

Unapproved grass is available separately with
`python tools/build-voxel-world.py --experimental-foliage --out build/grass-trial.json --evidence build/grass-trial-source`.
Pass that pack to `terrain-seam-fixture.py --pack ... --out ...` to inspect its
terrain contact. The default retains 66 models and 6 ground-cleanup masks.

## Reproduce the evidence

After the documented local source setup and build:

```powershell
$env:PATH = 'C:\msys64\mingw64\bin;' + $env:PATH
./build/rubyvr_studio.exe --test-terrain build/terrain-test
python tools/test-studio-terrain.py
python tools/test-editor.py
python tools/render-terrain-acceptance.py
```

The SDL journey generates the local seam recipe and invokes
`--test-terrain-source third_party/pokeruby build/terrain-review/seam-fixture.json`.
That check builds all 394 source maps and validates copied-cell ownership. Only
the two-map fixture is used by that SDL journey. The native check now also
validates every included map boundary in any supplied regional pack, at both
corners/midpoint and against actual internal-wall geometry. Use
`python tools/test-terrain-region-source.py` for the six-map example and negative
checks. Synthetic tests use original input; real source packs and snapshots remain local.
