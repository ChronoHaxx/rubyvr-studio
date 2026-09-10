# References, inspiration and tools evaluated

Checked 2026-09-08. This record separates dependencies, adapted techniques,
observed workflows and untested candidates. It is not a benchmark leaderboard.

## What RubyVR builds on

| Project | Relationship to RubyVR | Source reference |
|---|---|---|
| mstan/RubySapphireRecomp | Native game integration base; not bundled in this standalone repo | [Base commit](https://github.com/mstan/RubySapphireRecomp/tree/4d49909cbc6dccd3fbb0087cb68347ffbe55ce5d) |
| mstan/gbarecomp | Recompilation/hardware framework for that game base; not a Studio link dependency | [Base commit](https://github.com/mstan/gbarecomp/tree/a1de406b179addf10a534369b64f449e5fe28c4a), [current licence](https://github.com/mstan/gbarecomp/blob/main/LICENSE) |
| pret/pokeruby | Local map layouts, tile definitions, palette/indexed art and behavior reference | [Pinned source](https://github.com/pret/pokeruby/tree/63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1) |
| DRAMALESS_SHAPE | Source-inspected structural rules and rendering techniques adapted into our C++ work; also an art/workflow reference | [Inspected revision](https://github.com/artyrambles/DRAMALESS_SHAPE/tree/5ccc0e25417bd8b16cefd50cf08f61c44223e3d4) |
| Dear ImGui | Vendored GUI library, version 1.91.9b, via the development base's recomp-ui checkout | [Upstream](https://github.com/ocornut/imgui), [vendored licence](../third_party/imgui/LICENSE.txt) |
| SDL2, OpenGL, zlib, OpenXR headers | Editor window/input, graphics, PNG decompression and shared graphics types | [Notices and dependency boundaries](../THIRD_PARTY_NOTICES.md) |

`SOURCE_ORIGIN.json` records the initial export's source paths, hashes and local
development revisions. It is import provenance, not a promise that every file
remains unchanged after contributions. The public repository starts without
the private development repository's history or generated game code.

### DRAMALESS and Dramatic Studio

We inspected DRAMALESS_SHAPE's Lua source, including structured building bands,
stepped roofs, recessed details, run merging and template logic. Our inferred
renderer includes adaptations of source rules/constants, with attribution in
code. It would be inaccurate to describe the relationship as inspiration with
no adapted code. The [MIT notice is retained](../LICENSES/DRAMALESS_SHAPE-MIT.txt).
No Lua runtime or reference project's game assets are bundled here.

The [environment/OpenXR reuse review](reuse-review.md) records the later
`Sky.lua` / `DayNight.lua` adaptation and the public Quest host code inspected
as a future integration reference. It distinguishes implemented reuse from
source inspection and preview-only claims.

The [terrain and neighbour review](terrain-authoring.md#reference-and-geography-review)
records inspected authored height rules, neighbour offsets and suppression of
overlapping border geometry at the pinned DRAMALESS revision. It informs the
original C++ terrain/connection work and does not establish automatic regional
height inference or terrain parity with Dramatic Studio.

Dramatic Studio's [supplied 17.42-second demo video](https://x.com/DramaticShape/status/2088398259224035347) was observed as a workflow
reference. We did not have hands-on access to its editor. Selection, mask
strokes, box/wedge creation, transforms, duplication, billboard extrusion, orbit and zoom
were visible in use. Other visible buttons included undo/redo, find instances,
import, mask operations and additional primitives; their complete behavior was
not demonstrated. Save/export was not shown and part of the UI was clipped.
RubyVR does not claim full feature parity from that video.

The [frame review and current parity table](reference-parity.md) records the
demonstrated actions, visible but untested controls, and current implementation gaps.

The current [DRAMALESS_SHAPE README](https://github.com/artyrambles/DRAMALESS_SHAPE)
describes a Lua voxel mod for Gen1Recomp. Its 2.0 branch separated VR/Stadium
features. It is a reference for our work, not an installable plugin for our
native C++ game base. Preserve the distinction between similarly named projects
and versions when reporting comparisons.

## Sprite conversion and modeling tools

| Tool | What was actually done | Finding and remaining question |
|---|---|---|
| [2d-to-3d-voxelizer](https://github.com/GazPrash/2d-to-3d-voxelizer) | Repository/source inspected; not installed or benchmarked on a Ruby house | Single/multiple-view geometry, manual depth and OBJ export are relevant. A single sprite still needs decisions about unseen sides, ground and roof geometry. Measure a real import before claiming a time saving. |
| [SpriteMesh](https://github.com/98teg/SpriteMesh) | Godot plugin source inspected; not run | Sprite extrusion can help with flat props. The inspected approach uses uniform depth; it does not establish automatic multi-surface building reconstruction. |
| [Goxel](https://github.com/guillaumechereau/goxel) | Considered as an external voxel editor; not run | Potential manual modeling/import candidate. Its GPLv3 licensing and any integration boundary need to be respected. No workflow-speed or fidelity result recorded. |
| RubyVR Studio | Built and exercised with real source maps, hidden SDL input, save/reopen and renderer audits | Current evidence in [verification](verification.md). Manual efficiency on a held-out asset still needs a timed human trial. |

The follow-up inspection on 2026-09-08 also covered
[Blockbench](https://www.blockbench.net/), whose documented cuboid modeling,
pixel texture painting and custom-format plugins make it the preferred candidate
for a shape-authoring experiment, and
[Blender's glTF exporter](https://docs.blender.org/manual/en/4.0/addons/import_export/scene_gltf2.html),
which can retain custom properties as extras. Neither has been run on a RubyVR
round trip. See the [external-authoring proposal](external-authoring.md) for the
metadata that ordinary mesh export would lose and the bounded acceptance trial.

**No external sprite converter was installed or benchmarked in this evaluation.**
These inspections do not prove that RubyVR is the fastest tool, that no better
converter exists, or that any project is state of the art. A useful benchmark
would use the same source house/tree, count manual correction time, retain
native source pixels, review all sides and test an editable round trip.

## RubyVR and Gen2Recomped

The comparison below follows the projects' own documentation, not their names.

| Aspect | RubyVR Studio / intended native integration | Gen2Recomped |
|---|---|---|
| Game execution | Integration based on RubySapphireRecomp/gbarecomp translating GBA ROM instructions into native code, with documented fallback paths | Its README describes a LÖVE2D recreation: hand-written Lua engine, script VM and map behavior, with ROM-imported data; not assembly transpilation |
| Generation/data | Ruby is the current verified authoring source; Sapphire needs separate runtime validation | Gold/Silver recreation in the Gen1Recomp lineage |
| Mod support | Editor recipes and local JSON overrides work; supported public runner integration and installable package are pending | Documented registries, events/hooks, per-mod saves/options and in-game mod manager |
| Licence boundary | Original editor work is source-available under Noncommercial and No-Sales terms (earlier grants remain); external gbarecomp is PolyForm Noncommercial and the pinned game base has no licence file | Current licence file calls covered code Source-Available 1.1 and explicitly distinguishes it from open source; README also describes inherited MIT portions |
| Intended contribution | Source-faithful voxel authoring, shared native renderer and PC VR integration | Contributions must follow its own engine, tooling and licence terms |

Sources: [Gen2Recomped README](https://github.com/UNDERdecoded/Gen2Recomped),
[Gen2Recomped licence](https://github.com/UNDERdecoded/Gen2Recomped/blob/main/LICENSE),
[RubySapphireRecomp](https://github.com/mstan/RubySapphireRecomp),
[gbarecomp](https://github.com/mstan/gbarecomp).
Do not copy restricted Gen2 code into this repository or assume its entire tree
is MIT because inherited files have different terms.

Upstream gbarecomp documents versioned `.gbamod` packages and trusted native
plugins compiled into the runner. This is existing upstream capability. RubyVR
has not yet connected its pack format to that API, and does not currently
promise arbitrary DLL mods, hot loading or a complete mod ecosystem. Distinguish
the framework's capabilities from what our integration has actually verified.
