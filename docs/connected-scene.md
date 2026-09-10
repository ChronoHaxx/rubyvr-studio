# Explore a connected area

**M2.** The initial explorer and launcher repair merged in
[PR #30](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/30).
**[Model reuse and six-map exploration](region-model-reuse.md) merged in PR #31.**
The current branch preloads Oldale, Routes 101/102/103, Littleroot and Petalburg
into one 3D view. The ordinary editing view remains available.
**In review: [camera-driven loading](camera-map-streaming.md)** moves this
bounded window as you fly, retaining the same world coordinates.

![Actual Studio six-map flight and overview](media/region-model-reuse.gif)

## Try it

After the [local setup and build](building.md), run from this repository:

```powershell
./tools/run-studio.ps1 -TerrainRegions -Connected
```

Or open the terrain example normally and click **Explore area** in DIORAMA.
Hold right mouse in the 3D view and use **WASD** to move, **Q/E** for height and
**Shift** for faster movement. Release right mouse to stop. **Fit area** shows
the loaded area; **Return to editing** restores the original map camera.
Escape stops an active flight; a further Escape returns to editing.
**Light → Noon** enables the existing sky preview.

The launcher creates a fresh personal output file. Exploring does not edit
the document. Pending model edits must be saved/applied before entering.
The batch GUI switch is `--connected`, which selects DIORAMA automatically.

The PowerShell launcher passes native arguments as an explicit array. This
fixes the reported usage/exit-code-2 failure with `-Connected` in PowerShell
5.1 and 7. A **Review export failed** warning concerns the optional review
browser; an out-of-date coverage ledger does not prevent exploration.

## What changed

Source connection offsets place each full map. Its primary body replaces
overlapping copied padding, and any remaining padding has one owner. Models
belong to the map owning their source anchor; their geometry can overhang an
edge without being cut off or emitted twice. The shared terrain query supplies
neighbour heights when removing buried faces.

Each map keeps its own indexed tile and palette textures. Region chunks use
the existing production mesher and shader, with independent buffers so leaving
the explorer restores editing immediately. Rigid repeats share one model mesh
within each map's source atlas; ordered placement offsets preserve draw order.
Ground-following models retain per-vertex deformation. Turning within the same
loaded area does not rebuild geometry; entering another map prepares a nearby
window in the background. Maps outside the camera's view are skipped.

This changes presentation, not the authored height recipe. It preserves the
66 starter models, six cleanup masks and the existing regional terrain/water.
PR #29's deliberately raised foundation test hill is not part of this example.

## Bounds and remaining work

- This is a desktop exploration preview. It has no game movement, collision,
  live characters, animated source state or headset acceptance.
- The loader takes two connection hops within authored terrain, otherwise one,
  at most nine maps. An unauthored neighbour remains a named frontier. It does
  load or unload maps as the camera moves in the [streaming follow-up](camera-map-streaming.md),
  currently in review.
- The six-map fixture has **10,007,814 expanded vertices** with **69.4 MiB of
  stored mesh, placement offsets and indexed textures**. The same four-map
  fixture as PR #30 falls from 198.4 to 47.1 MiB. CPU working data, driver
  allocation, UI and the retained editing mesh are excluded. The mesher rejects
  more than eight million stored vertices, sixteen million expanded vertices
  or 65,536 source cells. This is not a peak-RAM guarantee.
- Copied padding remains at outer frontiers. Undersides and deliberate outer
  borders are still M2 work; distant horizon/fog is M8. Source floor colours
  across loaded different-atlas bodies are verified, not every unloaded border.
- Tree, shrub and grass appearance remain unapproved and deferred to M4.

## Evidence

The 13-second GIF contains 27 actual production-render captures: a flight
from Littleroot through Route 101 and Oldale, a pause inside Route 103,
Petalburg's offset join and the six-map overview. No interpolated or generated
world views are used. [Current evidence](region-model-reuse-evidence-2026-09-10.json).

Twenty-eight native checks cover ownership, model overhangs, equal-height seams,
map-local material indices, atomic refusal and model reuse/deformation equivalence.
Twenty SDL checkpoints at two
window sizes cover entry, flight, stopping, disabled edits, return, repeated
entry and exact save/source preservation. A separate source-floor test compares
32,000 rendered pixels from Route 104 with its view from Petalburg's connected
scene; they match exactly. That test deliberately omits scenery patterns to
isolate material binding. Twenty-eight further checkpoints exercise the wider
north/west flights and overview. The 20 original camera views match PR #30
pixel for pixel, while the same four-map geometry hash is unchanged.

The launcher regression adds six source-free cases: plain launch, connected
terrain and a stale review index, in both Windows PowerShell 5.1 and PowerShell
7. It runs the actual script against a native argument recorder, including
paths with spaces, and is included in CI. Separate local checks run the real
launcher and renderer in both shells, with a test-only alias appending the
existing hidden SDL driver arguments. The four-map scene opens in each.
Those launcher checks belong to merged PR #30. Its [original evidence](connected-scene-evidence-2026-09-10.json)
retains the four-map measurements, historical GIF hashes and verified merge/CI.
The current tests use that same level-water pack with the wider loader.

```powershell
./build/rubyvr_studio.exe --test-connected
python tools/test-studio-launcher.py
./build/rubyvr_studio.exe --test-connected-source third_party/pokeruby build/terrain-regions/regions.json build/connected-scene/source-verification.json
python tools/test-region-model-reuse.py
python tools/render-region-model-reuse.py
```

Generate the regional pack first with the launcher or the command in the
[terrain example guide](terrain-regions.md). Source assets and intermediate
captures stay local. [Exact results and hashes](region-model-reuse-evidence-2026-09-10.json)
distinguish desktop measurements from whole-game or VR performance.

Next M2 work: load/unload further neighbours as the camera travels, then
author outer borders/undersides. Complete geography and
live traversal remain open in the [single roadmap](roadmap.md).
