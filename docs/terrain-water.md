# Level water and shore contact

**M2, merged — [PR #28](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/28).** The regional terrain solver previously treated ponds and sea
tiles as solid ground. Near a terrace, some water followed the land grade:
Route 102's pond reached 18 px and Route 103's sea reached 20 px instead of
staying at the chosen 16 px level. [Watch the small correction](acceptance.md).

Selected water cells now use the existing v7 water surface, held flat with zero
solid thickness. A one-cell strip around them also stays flat, including
diagonal neighbours, because source shoreline tiles can contain both land and
water pixels. The adjoining dry terrain grades continuously from that strip.

This does **not** lower water below the shore. The strip and water share a
height; the original artwork still provides the visible bank edge. Raised
banks, underwater depth, waterfalls and complete coastal geography remain M2.

| Authored area | Water cells | Flat shore cells | Water/shore height |
|---|---:|---:|---:|
| Petalburg ponds | 141 | 84 | 16 px |
| Route 102 pond | 26 | 26 | 16 px |
| Route 103 sea | 328 | 156 | 16 px |

## Run and author

After the [source setup and build](building.md):

```bash
bash tools/run-studio.sh --terrain-regions --map MAP_ROUTE102
```

Use the map dropdown for Route 103 or Petalburg. [Fly controls and the
one-map-plus-padding limitation](terrain-regions.md#run-it) still apply.

Each `water` entry in [the recipe](../recipes/terrain-regions.json) provides a
name, source-body bounds `[x,y,width,height]`, material behavior IDs, a height
in original pixels, a shore width and a source guard. Behavior selects material
inside those bounds; it never determines physical height. This example selects
pond behavior 16 and sea behavior 21 in the pinned General/Petalburg tilesets.
Other water types are not inferred automatically.

The SHA-256 guard covers packed source cells and full material attributes in
the bounds plus any shore outside them. Changed water membership, shore tile identity or
attributes require inspecting the source before updating the recipe. Empty or
overlapping selections, conflicting heights and overlap with authored ledge
clearing are refused. Water constraints also coexist with retained fixed
profiles; contradictory pins fail instead of silently overriding each other.

No engine rewrite or new art is involved. The compiler preserves source top
art, underlays, expected cell identity, model patterns and Route 101's corner
field. Generated packs stay local; existing personal terrain is not overwritten.

## Checked scope

```bash
python tools/test-terrain-regions.py
python tools/test-terrain-region-source.py
python tools/render-terrain-water.py
```

The source-free suite has 13 tests. Real-source checks verify all 495 water
cells and 266 shore cells, 7,920 level production-mesh triangles with no water
bottom faces, and 200 boundary checks across both views of 100 joined segments.
Broken water levels, joins, guards and contradictory land/water anchors fail.
All 197 native synthetic checks and 49 SDL editing checkpoints still pass.

The 12-second GIF pairs eight actual application captures at identical
cameras. Codex inspected each frame. [Exact hashes and results](terrain-water-evidence-2026-09-10.json)
record desktop evidence; live surf/traversal, animation and headset acceptance
remain open. [Level foundation](terrain-foundations.md) merged in PR #29;
the [connected explorer](connected-scene.md) merged in PR #30, with [six-map model reuse](region-model-reuse.md) merged in PR #31. Foliage appearance remains M4.
Completion is tracked only in [the roadmap](roadmap.md).
