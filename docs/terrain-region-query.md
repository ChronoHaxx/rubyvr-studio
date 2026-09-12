# Connected-region terrain height query

This is a bounded **RV-003 / M2 component awaiting consumer integration**.
It adds a shared, read-only query over already loaded maps. It does not wire
scenery, character feet, effects or camera follow to terrain, and does not
complete gameplay or M2. Guest collision and movement remain authoritative.
No renderer, editor, runtime, recipe or terrain-format behavior is changed.

## API

Declared in `src/vr/terrain.h`, inside `vr::terrain`:

```cpp
struct RegionMapView {
    const world::Snapshot* source = nullptr;
    const Resolved* resolved = nullptr;
    int x = 0, z = 0;
};
struct RegionHeight {
    Height height{Status::Unresolved};
    int map_group = -1, map_number = -1;
    int cell_x = -1, cell_y = -1;
    float u = 0, v = 0;
};
RegionHeight query_region(const std::vector<RegionMapView>& maps,
                          double world_x, double world_z,
                          int gameplay_layer);
```

`height` is the existing `Height`: status, height in source pixels, and a
borrowed pointer to the selected `TerrainSurface` for authored results.
`map_group/map_number` identify the primary owner. `cell_x/cell_y` are its
integer **backup-map** coordinates; local y corresponds to world z. `u/v`
are the fractional positions within that cell, not a second map offset.

For an invalid argument/region or a point outside all primary bodies, the
whole result is default: Unresolved, zero pixels, null surface, owner and cell
coordinates all `-1`, and `u=v=0`. For an owned cell, metadata remains available
even when the result is Unresolved or SourceMismatch. Check `height.resolved()`
before using pixels as a usable placement height; an unresolved zero is not a
flat-ground answer.

## Coordinates and ownership

Coordinates are fractional map cells, **not pixels**. Each view's integer
`x/z` translates its snapshot's backup origin:

```text
world_x = backup_x + view.x
world_z = backup_y + view.z
```

This is the offset convention used by `src/studio/connected_scene.cpp`.
Do not add or subtract an extra seven cells at the call site. A snapshot's
primary body is the half-open backup rectangle
`[7, width-8) x [7, height-7)`, translated by that view's offsets. Its body
width is `width-15` and body height is `height-14`. Negative offsets work the
same way as positive offsets.

Only primary rectangles can own a query. Copied connection padding never
competes, even when it contains defined cells or a higher authored surface.
A point exactly on a shared edge belongs to the body whose west/north edge is
inclusive, not the body ending there. The same half-open rule handles corners.
Shared edges/corners and overlapping padding are allowed; any positive-area
body overlap invalidates the entire region, independent of the queried point.
Input order never breaks a tie or supplies a fallback.

For example, two 19-by-18 snapshots have four-by-four primary bodies. With
view offsets `(-11,-10)` and `(-7,-10)`, their world bodies are
`[-4,0) x [-3,1)` and `[0,4) x [-3,1)`. At `(0,-1.25)`, only the second owns
the query: backup cell `(7,8)` and `(u,v)=(0,.75)`.

Containment is checked in double precision before flooring or converting to
integers. The implementation floors world coordinates first, then subtracts
the integer offset. This avoids rounding a just-inside coordinate into an
adjacent backup cell during offset subtraction. Fractions are narrowed to the
existing query's `float` type; a fraction extremely close to one can round to
`1.f`. This does not change the selected cell or owner, and is accepted by
`Resolved::query`. There is no edge epsilon or map-boundary snapping.

## Validation and trust boundary

Every view is checked before any owner is returned, including views nowhere
near the point. The bounded structural checks require:

- Finite world coordinates, an explicit gameplay layer in `-1..15`, and one
  through nine non-null Snapshot/Resolved pairs.
- A valid snapshot with group/number in `0..255` and known SourceTable or
  LiveCapture identity provenance; `Snapshot::valid_connections()` must pass.
  Dimensions are checked before that method's rectangle arithmetic.
- Backup width `16..1024`, height `15..1024`, and at most 10,240 cells per map.
  `grid.size()` must equal the product exactly. Resolved dimensions must match,
  and both `cells` and `mismatched` must have exactly that product's length.
- Offsets within `[-8192,8192]`, unique group/number pairs, and no positive-area
  overlap of primary bodies. These bounds also keep endpoint arithmetic safe.

This query is not a terrain document loader or a second resolver. Callers must
supply **coherent Snapshot/Resolved pairs constructed once from validated
terrain data**, using the existing `terrain::valid`/validated loader and
`terrain::resolve`. A same-sized but stale or unrelated Resolved cannot be
identified from dimensions alone. Do not fabricate its cell pointers or modify
its storage after construction. Non-null pointers must actually refer to live
objects; this function cannot validate dangling pointers.

The query does not scan cell or asset arrays, copy a document, rebuild terrain,
allocate a terrain cache, infer a connection graph or solve world origins. It
checks up to nine map headers and their bounded connection slices, at most 36
map pairs, then reads one owned source cell and delegates to that map's existing
Resolved query. Surface selection only examines the selected cell's validated
surface list. Construction, source/material guard checking, consistent origins
and publication of a coherent region are the caller's responsibilities.
Rendering asset-array sizes are not revalidated by this height-only API.

## Layers and height semantics

For a **defined** owned source cell, the implementation passes the caller's
explicit layer and the returned `u/v` directly to `Resolved::query`, preserving
its status, pixel height and original surface pointer. It does not infer a
layer from source elevation, choose a highest surface or retry another map.
Layer numbers are gameplay constraints, never physical heights.

| Owned cell | Result |
|---|---|
| Water at 8 px on layer 1 plus deck at 16 px on layer 3 | Layer 1 returns water; layer 3 returns deck |
| Same stack, another layer or ambiguous `-1/0/15` | Unresolved, with owner retained |
| Sole authored surface | `-1/0/15` resolve that surface; `1..14` still require an exact layer match |
| Defined unauthored cell | LegacyFlat at **0 px**, with no surface pointer |
| Source cell equals `world::kGridUndefined` | Unresolved with owner retained, even when Resolved would return LegacyFlat or SourceMismatch |
| Defined cell rejected by source/material guards | SourceMismatch with owner retained; no padding fallback |

Graded heights use the existing NW-SE triangular interpolation; the region
query does not implement interpolation itself. The connected preview's
**-16 px underside is not a gameplay surface** and is never substituted for
legacy ground. Existing read/write, resolve and single-map query semantics,
including old terrain formats, remain unchanged.

## Lifetime and use

Keep the snapshots, Resolved objects, **and their original terrain document**
alive and unchanged throughout query use. Resolved cells and authored result
surfaces borrow that original document; they are not owned copies. The same
lifetime rule applies while a caller retains or dereferences a result's surface
pointer. Do not use a temporary document to build Resolved, mutate/reallocate
its cells or surfaces, or relocate objects while views point to them.

A caller with already validated, stable inputs can construct a view once:

```cpp
// snapshot and document are the validated originals, with stable lifetimes.
const auto resolved = vr::terrain::resolve(snapshot, document);
const std::vector<vr::terrain::RegionMapView> views{
    {&snapshot, &resolved, offset_x, offset_z}
};
const auto sample = vr::terrain::query_region(views, world_x, world_z, gameplay_layer);
// Only use sample.height.pixels for placement when sample.height.resolved().
// Keep document alive while retaining sample.height.surface.
```

When a source/document generation changes or maps unload, the caller must stop
using old views/results and rebuild or replace the relevant resolved region.
The API provides no global state, ownership transfer or concurrency control.
This component leaves consumer integration and synchronization policy pending.

## Headless verification

From the repository root in Ubuntu/WSL, use a C++20-capable GCC installation
with its sanitizer runtime. The same command runs locally and in CI:

```bash
bash tools/test-terrain-region-query.sh
```

Expect `SUMMARY 16 passed, 0 failed; 9299 checks` twice, first optimized and
then under ASan/UBSan, with exit 0. This compiles fresh executables each time;
it does not use an installed Studio binary or modify authored documents.
The four existing indentation warnings in terrain read/write are recorded in
the [coordinator review](terrain-region-query-review.md).

<details>
<summary>Individual compiler commands for development</summary>

```bash
mkdir -p build
g++ -std=c++20 -O2 -Wall -Wextra -pedantic -Isrc/vr \
  tools/terrain-region-query-test.cpp src/vr/terrain.cpp src/vr/json_scan.cpp \
  -o build/terrain-region-query-test
env -u DISPLAY -u WAYLAND_DISPLAY ./build/terrain-region-query-test
```

AddressSanitizer and UndefinedBehaviorSanitizer, when available:

```bash
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Isrc/vr tools/terrain-region-query-test.cpp src/vr/terrain.cpp \
  src/vr/json_scan.cpp -o build/terrain-region-query-test-sanitized
env -u DISPLAY -u WAYLAND_DISPLAY ./build/terrain-region-query-test-sanitized
```

</details>

The original standalone test uses synthetic snapshots and terrain documents,
production validation/resolve/query, named cases, independent expected heights,
and a nonzero failure exit. Ordinary fixtures are not mocked. Malformed-storage
cases deliberately corrupt views after constructing valid fixtures. Coverage
includes layers, triangular samples, negative/offset joins, copied-padding
exclusion, exact and adjacent-double edges, all 24 orders of four maps, source
and material guard refusal, structural bounds, pointer identity and immutable
repeatability. The 8,000 repeated probes check determinism, not 8,000 distinct
scenarios or a performance benchmark.

No SDL, OpenGL, OpenXR, source assets, window system, physical input or external
test framework is required. These commands do not compile the renderer, the
connected editor, or `src/studio/terrain_test.cpp`. Existing tests and CMake
are unchanged; the coordinator added the command above to CI. The separate
[coordinator review](terrain-region-query-review.md) records full-project
build/regression results and the pending human CLI check. Consumer integration,
live gameplay and headset acceptance remain outside this component.
