# Maps load around the camera

**M2 / RV-003 — [PR #20, in review](https://github.com/ChronoHaxx/rubyvr-studio/pull/20).** Explore area now moves its loaded neighbourhood
with the camera. Fly from Oldale toward Littleroot or Petalburg: distant maps
are released, nearby maps are prepared in the background, and returning
restores the same world positions. The document and editing camera stay intact.

![Actual Studio flight with changing resident map counts](media/camera-map-streaming.gif)

The 11-second GIF uses actual SDL flight and production-renderer frames,
sampled with pauses. It is visual evidence of residency changes, not a
real-time frame-rate recording. Terrain, water heights and scenery recipes
are unchanged. Outer borders remain M2 work; foliage appearance remains M4.

## Run it

After the [local setup](building.md), rebuild and launch:

```powershell
./tools/build.ps1
./tools/run-studio.ps1 -TerrainRegions -Connected
```

Hold right mouse in the view and use **WASD**; **Q/E** changes height and
**Shift** moves faster. **Light → Noon** shows the sky. The inspector shows
the current map, loaded maps and loading status. **Return to editing** cancels
pending work and restores the original camera. Escape releases flight look;
when look is already released, Escape returns to editing.

## What changes during travel

Entering a neighbour's primary map body, with a one-cell inset to avoid
repeated changes at a seam, starts one background job. It owns its source
cache, snapshots and recipes. The existing renderer continues drawing the
last complete area while the production mesher prepares a replacement.
The first entry into Explore area still loads synchronously.

Completed geometry is published on the GL thread. Unchanged map buffers are
reused after comparing ordered world geometry, placement counts and indexed
art. New allocations are staged before replacing the old view; failures retain
that view and expose a retry button. Leaving exploration cancels the job;
a late result cannot overwrite the editor or a newly entered exploration.

All maps retain the first area's coordinate system, including negative and
offset connections. Previously visited map identities retain their origins
after unloading, so a conflicting loop is refused. Map bodies, padding and
overhanging models use the existing ownership rules and per-map palettes.

## Bounds and measured cost

The window takes two cardinal connection hops within authored terrain,
otherwise one, up to nine maps. The mesh limits remain 65,536 source cells,
eight million stored vertices and sixteen million expanded vertices.
There is one worker, and at most 1,024 remembered map origins within the
existing ±8,192-cell coordinate limit. Unauthored terrain remains a named
frontier: the supplied regional example still stops at Routes 104 and 110.

| Settled location | Resident maps | GPU mesh, offsets and indexed art |
|---|---:|---:|
| Oldale | 6 | 69.4 MiB |
| Littleroot | 3 | 29.6 MiB |
| Petalburg | 3 | 42.0 MiB |
| Back at Oldale | 6 | 69.4 MiB |

The return test completes eight window changes, releases six map entries and
reuses 24 map buffers cumulatively. Oldale's ordered geometry hash is identical
after both return journeys. These figures exclude the editor mesh, CPU working
data, driver overhead and UI. Old/new data overlap while staging, so the limits
are not a whole-process peak-memory budget.

The recorded preparations at settled stops took **0.64–1.24 seconds** in the
background. The largest measured main-thread streaming update was **50.8 ms**,
including publication/cleanup; rendering and UI are excluded. Upload budgeting,
CPU chunk caching and whole-app/headset performance remain M10 work. This is
a desktop exploration step, not a claim of hitch-free VR or completed gameplay.

## Verification

```powershell
./build/rubyvr_studio.exe --test-connected
python tools/test-camera-map-streaming.py
python tools/test-region-model-reuse.py
python tools/render-camera-map-streaming.py
python tools/test-editor.py
```

The new travel suite checks 24 SDL checkpoints: both round trips, cancellation
during preparation, exact return/reentry, unchanged source/document/history,
resident memory reduction and fixed coordinates. A separate source-floor
fixture loads Rustboro and Route 105 beyond the starting window, then returns;
that verifies loading topology without claiming new authored geography.
The GIF adds 36 inspected production captures.

The existing connected-input tests cover two window sizes and a 32,000-pixel
comparison against a neighbour's own atlas. Native ownership, terrain,
foundation and frozen editor geometry checks remain required. Detailed results
and executable/pack/GIF hashes are in the [evidence record](camera-map-streaming-evidence-2026-09-10.json).
The [roadmap](roadmap.md) remains the sole completion tracker.
