# Emerald geometry reuse evaluation

**17 September 2026: reuse selected generators and rules while keeping Ruby's
native gameplay and shared mesher. Start with common furniture.** The source
and input comparison is complete; a rendered adapter comparison is still to do.
No upstream geometry has been integrated or visually accepted by this evaluation.

The separate map-crossing, performance and apparent fast-forward plateau report
is recorded in [issue #38](https://github.com/ChronoHaxx/rubyvr-studio/issues/38)
under M5/M10. Its cause is unconfirmed. The maintainer prioritised this M4
evaluation before investigating it.

## What we actually inspected

- RubyVR base: `ca2d3b7c442054205f33c30c24a3987c48d6bcc5` (merged PR #37).
- [Gen2Recomped-DramaticShapes](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/tree/726782f223cac76b4e78cdadb24fa6ac78edaef0):
  `726782f223cac76b4e78cdadb24fa6ac78edaef0`, 15 September, newer than our earlier
  `4a114b3e` audit. Inspected the recursive tree and 21 pinned source/data/test files.
- Ruby inputs: `pret/pokeruby` at `63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1`.
- Emerald comparison inputs: `pret/pokeemerald` at
  `c65e93f20a5275ab03b07d6f6411096a82a60ffd`, the source revision recorded in the
  companion's generated map metadata. Only five local tileset input families
  and two headers were retrieved; no ROM or game was run.

The companion's pinned [MIT licence](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/blob/726782f223cac76b4e78cdadb24fa6ac78edaef0/LICENSE)
permits code adaptation with its DramaticShape/UNDERdecodedHD notices retained.
This finding applies to that companion repository, not the separately licensed
engine, Dramatic Shape APK or game artwork. This evaluation copied no upstream
implementation into RubyVR; the input comparison tool is original code.

The public tree contains procedural generators and authored tables, rather than
ready `.vox`, `.obj`, `.gltf` or `.bbmodel` scenery assets. The Python building
voxelizer and numbered building documentation largely concern Gen 1 templates;
they cannot be counted as completed Emerald buildings. Actual Gen 3 work lives
primarily in `Gen3.lua`, `Structures.lua` and the `data/gen3_*` tables.

## Which parts are useful

All upstream links below are pinned to the inspected revision.

| Area | Actual implementation and Ruby comparison | Decision |
|---|---|---|
| Trees and bushes | [`roundTemplate` and `buildCylinders`](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/blob/726782f223cac76b4e78cdadb24fa6ac78edaef0/lib/Structures.lua#L13139) read carved silhouettes, revolve row widths, taper clipped crowns and reuse templates. Our `build-voxel-world.py::foliage` already revolves rows and repairs clipped crowns, but uses authored masks. | Reuse applicable masking, neighbour ownership and canopy detection. Changing to the same broad shape method alone is unlikely to fix the rejected art. Test against Ruby pixels and six views. |
| Buildings and roofs | [`Gen3.buildingsOf`](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/blob/726782f223cac76b4e78cdadb24fa6ac78edaef0/lib/Gen3.lua#L7142) exposes inferred door-linked footprints; `Structures.buildVolume` and roof props consume them. Our starter roofs use explicit recipes and box/wedge parts. | Adapt footprint/roof inference for missing families. Retain editable parameters and Ruby's source guards. Do not transplant the four-shade Gen 1 building templates as Hoenn models. |
| Common interiors | [`gen3_shapes.lua`](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/blob/726782f223cac76b4e78cdadb24fa6ac78edaef0/data/gen3_shapes.lua#L918) identifies appliances, cabinets, TVs and stairs. `standGen3Furniture` recovers object tops from the row behind; `buildGen3Joinery` adds further furniture shaping. Our 127 room recipes use four coarser profiles. | **Best first implementation:** transfer the matching May/Brendan kitchen roles and adapt their standing-depth rules into our shared part generator. Preserve source artwork, furniture footprints and native interactions. |
| Terrain and bridges | `gen3_terraces.lua`, `gen3_palings.lua` and `Structures` contain many map-specific rules and exceptions. | Keep as targeted references. Emerald elevation/map pins are not Ruby world heights; do not bulk-replace our authored terrain or collision. |
| Build cost and caching | [`VoxelDiskCache`](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/blob/726782f223cac76b4e78cdadb24fa6ac78edaef0/lib/VoxelDiskCache.lua), `VoxelPrebake`, `BuildBudget` and `ChunkMesher.pump` cache/prebuild meshes and budget work. Ruby already reuses bounded map/model buffers. | Useful references when profiling #38. Adopting the generator does not establish a performance improvement: upstream also documents large mesh allocations and transition stalls. |

Upstream source comments also describe unresolved furniture misclassification
(including a false stair in May's living room) and incomplete Center stairwell
geometry. These are inspected limitations, not failures reproduced here. There
is no basis to call its entire Hoenn output visually accepted or bug-free.

## Executed input compatibility check

[`audit-emerald-reuse.py`](../tools/audit-emerald-reuse.py) compared every metatile
in five selected owning tilesets, reading both games' actual indexed PNGs and
binary definitions. It checks all eight 8px subtiles separately, including flips,
palette slots, transparent indices and the complete attribute word. Reordered
tile storage can match after decoding; changed artwork cannot pass merely
because IDs and definitions agree.

| Owning tileset | Emerald rows | Same-ID indexed-layer + attribute candidates | Different-ID candidates | No exact candidate | Unresolved pixels |
|---|---:|---:|---:|---:|---:|
| General | 512 | 284 | 0 | 228 | 0 |
| Petalburg | 144 | 21 | 0 | 121 | 2 |
| Building | 8 | 8 | 0 | 0 | 0 |
| Brendan/May house | 196 | 104 | 11 | 81 | 0 |
| Pokémon Center | 232 | 187 | 9 | 36 | 0 |

These are **candidate tile correspondences, not model counts or an art completion
percentage**. Palette RGB, animated frames, whole-object membership, local floor
ownership and placement/collision still need checks. Some unmatched artwork can
still use the same algorithm after adaptation. Missing pixels fail closed.

Concrete findings:

- General has **511/512 identical definitions and attributes**, yet only 284
  also match the indexed layers. Border-tree IDs 468/469/476/477 are among the
  artwork mismatches. A definition-only import would miss this distinction.
- House kitchen IDs **568–572** match, as do TV IDs 576/577; TV-related 586/691
  do not. Transfer the proven part of a rule instead of treating the entire
  tileset or room as interchangeable.
- Center stair IDs **640/641/648/649/656/657** all match at this level. The room
  placement and stairwell treatment still require Ruby-specific review.
- Petalburg 586/587 reference pixels beyond the supplied static sheet and remain
  unresolved. No blank or fabricated pixel data was substituted.

Run from the repository with Python and Pillow, using local source inputs:

```sh
python tools/audit-emerald-reuse.py --selftest
python tools/audit-emerald-reuse.py --ruby third_party/pokeruby --emerald build/emerald-reuse/pokeemerald --out build/emerald-reuse/input-comparison.json
```

The second command assumes the above pinned Emerald input subset is already
present; the tool does not download it. JSON includes every candidate ID,
rejection and SHA-256 of the inspected input files. The local retrieval receipt
records Git blob identities. No source art is committed. The synthetic check
passes pixel-order/flip, palette/attribute, changed-art, remapping and malformed
input cases. The real comparison completed without SDL/OpenGL or desktop input.

## Integration choice and next bounded proof

**Prefer adapting rules into the existing shared part generator.** It keeps
Studio editing, exact save/load and native runtime rendering on one path.
Start with one kitchen run in May's house; a Center stair and outdoor tree/roof
comparison follow after the first transfer establishes the contract.

There is also a useful offline comparison entry point:
[`ChunkMesher.geometry(map, bodyOnly, masks, split)`](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/blob/726782f223cac76b4e78cdadb24fa6ac78edaef0/lib/ChunkMesher.lua#L2178)
returns CPU vertices/indices, optionally separating water. It still needs a
real map/tileset context, atlas pixels, ground carving and engine-facing asset
interfaces; grass/flowers/figures use additional paths. We inspected this entry
point but did **not** execute a Lua geometry export. Upstream's interior test
also expects engine modules and a hard-coded generated Emerald data directory.
Its headless missing-pixel fallback can produce plain volumes, so passing that
path would not validate the intended shape.

A raw mesh exporter is therefore a possible comparison harness, not yet a
drop-in import. Ruby's v6 `voxel` field records pixel ownership, while editable
geometry remains box/billboard/wedge parts; it is not an arbitrary vertex or
voxel-grid payload. Adding such a payload would need a deliberate shared format,
material mapping and editor/runtime implementation. Embedding the entire Lua
engine would broaden the dependency and maintenance scope beyond this need.

The first adapted room proof must retain upstream notices, guard actual Ruby
inputs, preserve walkable gaps/interaction/save behavior, and compare current
versus adapted output through the production mesher. Use hidden/offscreen
captures from elevated, orbit and first-person views, plus representative room
replays. Human art acceptance stays separate. **Those rendered and gameplay
checks remain pending; this report establishes feasibility and selects the
first transfer, not a visual win.**
