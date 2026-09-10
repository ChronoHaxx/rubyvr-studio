# Model reuse and the six-map explorer

This records PR #31's merged baseline. The [camera-driven loading follow-up](camera-map-streaming.md)
is in review; its moving window supersedes the fixed residency described here.

**M2, merged — [PR #31](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/31).** Connected views now store each rigid model once per map's
source atlas, with small placement offsets for repeated trees and buildings.
The same authored six-map pack can load Littleroot and Petalburg alongside
Oldale and Routes 101/102/103. Flying across these maps needs no map switch.

![Actual flight from Littleroot through Oldale, Petalburg's offset join and the six-map overview](media/region-model-reuse.gif)

The 13-second GIF shows actual Studio captures. Original art, shapes, terrain
and water heights are preserved. Foliage appearance remains deferred to M4.

## Try it

Build the current branch with `./tools/build.ps1`, then run:

```powershell
./tools/run-studio.ps1 -TerrainRegions -Connected
```

Hold right mouse and **WASD** to fly; **Q/E** changes height, **Shift** moves
faster. **Light → Noon** previews the sky. **Fit whole area** shows all loaded
maps; **Return to editing** restores the previous camera and document.

## What the memory measurement means

| Same generated pack | Expanded scene vertices | Stored vertices | Mesh, offsets and indexed art |
|---|---:|---:|---:|
| PR #30: four maps | 7,421,814 | 7,421,814 | 198.4 MiB |
| This change: same four maps | 7,421,814 | 1,755,624 | 47.1 MiB |
| This change: six maps | 10,007,814 | 2,584,128 | 69.4 MiB |

The like-for-like four-map allocation falls **76.2%**. Its ordered geometry
hash remains `3b0d708d12ea9e5b`. Twenty original camera views also match PR #30
pixel for pixel: **8,102,560 pixels** across two window sizes. These are
allocated mesh/texture bytes, not whole-process RAM: CPU working data, driver
overhead, the retained editing mesh and UI are excluded.

The six-map scene uses 68 model meshes for 442 rigid placements. Models are
shared within a source map, so numeric tile/palette slots always refer to the
correct atlas. Consecutive placements can share an instanced draw; original
claim order is retained to preserve overlapping pixel/depth ownership. Terrain
and ground-following models retain their original per-vertex geometry. Native
tests compare both paths against the unchanged single-map mesher on a slope.

This reduces storage; it does not reduce the expanded triangle count. The
six-map scene has 217 ordered draw batches before whole-map culling, versus
PR #30's four batches. Recorded draws remain under 1 ms at the tested close
cameras on this machine; those synchronized single-eye GL samples exclude UI,
CPU builds and streaming. They do not establish whole-app or headset FPS.

## Bounds and acceptance

The loader preloads at most two cardinal connection hops when terrain is
authored, otherwise one hop, with a nine-map maximum. Unauthored neighbours
remain named frontiers. The mesher limits stored vertices to eight million,
expanded vertices to sixteen million and source cells to 65,536; allocation
failure retains the previous view. These are preview bounds, not peak-RAM limits.

Checks pass: 28 native ownership/reuse/deformation/refusal checks; the existing
20 SDL editor/flight checkpoints; 28 further checkpoints across northward
travel, the overview and Petalburg's offset join; and 32,000 exact pixels for
the different-atlas source-floor fixture. Saves/source and camera-only upload
counts remain unchanged. Terrain, foundation, environment and editor regressions
are recorded in the [exact evidence](region-model-reuse-evidence-2026-09-10.json).

```powershell
python tools/test-region-model-reuse.py
python tools/test-camera-map-streaming.py
python tools/render-camera-map-streaming.py
```

An optional `--baseline <local-directory>` compares retained PR #30 probe
captures with identical current cameras. Source assets and intermediate
captures stay local; the evidence records the exact binary/input hashes.
The historical `render-region-model-reuse.py` and `render-connected-scene.py`
entry points require the fixed-window PR #31 captures; use the commands above
for the current moving-window film.

**Current M2 follow-up:** camera-driven loading is in review, then
deliberate outer borders and undersides. Complete coastal geography, bridges,
stairs, caves and live terrain placement remain open in the [roadmap](roadmap.md).
This preview has no gameplay collision or live/headset acceptance.
