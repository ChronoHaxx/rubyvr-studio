# References, inspiration and tools evaluated

Initial tool/workflow review: 2026-09-08. Native-engine comparison and camera/
editor/debug source audit refreshed 2026-09-12. This record separates
dependencies, adapted techniques, observed workflows and untested candidates.
It is not a benchmark leaderboard; dated tool trials keep their original scope.

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

The source-only inspection on 2026-09-08 also covered
[Blockbench](https://www.blockbench.net/), whose documented cuboid modeling,
pixel texture painting and custom-format plugins make it the preferred candidate
for a shape-authoring experiment, and
[Blender's glTF exporter](https://docs.blender.org/manual/en/4.0/addons/import_export/scene_gltf2.html),
which can retain custom properties as extras. That initial inspection did not
run a RubyVR round trip. See [external authoring](external-authoring.md) for
subsequent trial status and the metadata that ordinary mesh export would lose,
and [art direction](art-direction.md) for the later unapproved foliage trials.

**No external sprite converter was installed or benchmarked in this evaluation.**
These inspections do not prove that RubyVR is the fastest tool, that no better
converter exists, or that any project is state of the art. A useful benchmark
would use the same source house/tree, count manual correction time, retain
native source pixels, review all sides and test an editable round trip.

## RubyVR and Gen2Recomped

The comparison follows pinned public documentation/source, not project names.
Maintainer decision on 2026-09-12: retain the native route and prioritize
[actual monitor gameplay](issues/007-native-integration.md#native-desktop-proof).
The languages alone do not establish correctness, performance or bug counts.

| Aspect | RubyVR Studio / intended native integration | Gen2Recomped |
|---|---|---|
| Game execution | Integration based on RubySapphireRecomp/gbarecomp translating GBA ROM instructions into native code, with documented fallback paths | Its README describes a LÖVE2D recreation: hand-written Lua engine, script VM and map behavior, with ROM-imported data; not assembly transpilation |
| Generation/data | Ruby is the current verified authoring source; Sapphire needs separate runtime validation | Gen1/2 lineage; companion voxel source also contains an Emerald-specific adapter. This inspection does not establish complete Emerald gameplay/tool compatibility |
| Mod support | Editor recipes and local JSON overrides work; supported public runner integration and installable package are pending | Documented registries, events/hooks, per-mod saves/options and in-game mod manager |
| Licence boundary | Original editor work uses GPLv3-or-later (earlier grants remain); external gbarecomp and current RubySapphireRecomp declare PolyForm Noncommercial. The older pinned runner predates its declaration. The complete runtime stack is not claimed as FOSS | Inspected engine licence is Source-Available 1.2, including Emerald-specific work; the companion DramaticShapes mod has its own MIT licence. Check the actual file and notices before reuse |
| Intended contribution | Source-faithful voxel authoring, shared native renderer and PC VR integration | Contributions must follow its own engine, tooling and licence terms |

Sources: [Gen2Recomped README at b017ee1](https://github.com/UNDERdecoded/Gen2Recomped/blob/b017ee194d23e97029b598174d8f2893d42c9cc6/README.md),
[engine licence](https://github.com/UNDERdecoded/Gen2Recomped/blob/b017ee194d23e97029b598174d8f2893d42c9cc6/LICENSE),
[Emerald voxel adapter](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/blob/4a114b3e344db629ac7c7ac5108bd3d910fc4554/lib/Gen3.lua),
[companion mod licence](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/blob/4a114b3e344db629ac7c7ac5108bd3d910fc4554/LICENSE),
[RubySapphireRecomp](https://github.com/mstan/RubySapphireRecomp),
[gbarecomp](https://github.com/mstan/gbarecomp).
Do not copy restricted Gen2 code into this repository or assume its entire tree
is MIT because inherited files have different terms.

### AI-assisted Melee references (2026-09-12)

Coordinator review of the separate Smash research: retain the current native
route and the M5 monitor gameplay priority. The strongest independently checked
example is [Melee PR #3374](https://github.com/doldecomp/melee/pull/3374): its
author credits Codex/Astra with matching a 3,140-byte function in an existing
decompilation, and the merged PR's comparison report confirms that function
match. This supports using precise comparison checks with AI-assisted work;
it does not demonstrate a newly written game or a desktop/VR port by itself.

The research identified [Kevin Tang's MR post](https://x.com/_KevinTang/status/2098154213696249912),
but could not establish its runtime technique from an inspectable implementation.
The coordinator's direct post fetch was also blocked. No original-mechanics,
standalone-VR performance or migration claim is adopted from that demo.

The local gbarecomp copy contains `gba_mod_register_function_entry_plugin` and
`gba_mod_set_function_hook_enabled` in `src/runtime/mod_function_hooks.*`.
Declining a callback restores CPU registers, not arbitrary guest-memory writes.
Use this existing boundary for future verified Ruby operations; do not invent a
second hook system or treat its presence as a working inventory action API.

Apply the research's comparison idea under M5/M10 using the existing input
replay: first establish repeatable original-game behavior, then compare capture
only and full 3D at matching guest events with BMP recording disabled. Scope
the first measurement to the accepted actor sequence; broad profiling follows
when evidence requires it. This is pending work, not a new multi-day prerequisite
for menu presentation or a reason to replace the engine. The research's effort
estimate is not a delivery commitment. [Canonical tracking](roadmap.md#m10-performance-and-reliability).

The research also caught the [current runner licence declaration](https://github.com/mstan/RubySapphireRecomp/blob/8720324ca07741efd8b6785a0a6c46162fbc7099/LICENSE),
dated 2026-09-09. Notices now distinguish it from the older pinned-base absence;
the separate RV-007 integration/distribution question remains open.

### Camera, editor and debug reference audit

Inventory follow-up checked 2026-09-12: [Dramatic Shape VR's controls](https://github.com/prismaticShape/DramaticShapeVR#vr-1st-person-and-1st-person-mr)
document party/bag racks, grabbing and throwing balls, and a physical Pokédex.
These are interaction changes beyond voxel scenery. The guide does not establish
that every inventory action permits concurrent walking, nor why the author chose
that engine. Gen2Recomped's hand-written gameplay and documented hooks make more
direct control of such behavior plausible; that is an architectural inference,
not a measured comparison or statement of author intent.

[GBARecomp's mod boundary](https://github.com/mstan/gbarecomp#mods) also permits
game-owned compiled behavior through trusted plugins. It does not already
provide RubyVR with an inventory action API. RubyVR's M7/M9 follow-up separates
custom bag browsing over the active field from verified item use. No reference
implementation was copied, and the native gameplay proof remains the priority.

Checked 2026-09-12: engine `b017ee194d23e97029b598174d8f2893d42c9cc6`,
companion mod `4a114b3e344db629ac7c7ac5108bd3d910fc4554`.
These are documented/source-implemented features; no hands-on Emerald camera,
editor or gameplay acceptance was performed here. This audit copied no engine
or mod implementation into RubyVR.

| Reference feature | Verified source behavior | RubyVR follow-up |
|---|---|---|
| Voxel views | OFF, four tilt labels 15/35/50/75, experimental 1ST and 3RD | M9; one tilted follow view first |
| 1ST / 3RD | Mouse look; third-person boom distance/collision; these modes also change movement | Keep camera and gameplay-control changes separately scoped |
| V-CURVE | Separate off/three-strength horizon bend | Optional M9 presentation; preserve unbent gameplay coordinates |
| Map editor | Shared 2D/3D selection, neighbour/warp browsing, voxel heights, tiles, collision, NPCs and events | M3 map inspection/test launch first; broader game editing later |
| Developer console | Warp, give, flags, party, mod reload and event/hook tracing | M5 inspection/capture first, verified test actions later |

Sources: [mod controls](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/blob/4a114b3e344db629ac7c7ac5108bd3d910fc4554/README.md),
[WorldCurve](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/blob/4a114b3e344db629ac7c7ac5108bd3d910fc4554/lib/WorldCurve.lua),
[map editor guide](https://github.com/UNDERdecoded/Gen2Recomped/blob/b017ee194d23e97029b598174d8f2893d42c9cc6/docs/MAP_EDITOR.md),
[developer console](https://github.com/UNDERdecoded/Gen2Recomped/blob/b017ee194d23e97029b598174d8f2893d42c9cc6/src/dev/Console.lua).

The curved horizon is a visual bend, not a spherical planet simulation.
No dedicated god/invincibility or noclip command was found in the inspected
console's built-in verbs; that does not establish absence elsewhere. Requested
RubyVR test controls are proposals, not claims of reference feature parity.
Our old native viewer has orbit/follow controls, but the public live adapter
still clears map identity/connections and the current Studio terrain needs live
integration. Editor footage cannot close that gap.

Upstream gbarecomp documents versioned `.gbamod` packages and trusted native
plugins compiled into the runner. This is existing upstream capability. RubyVR
has not yet connected its pack format to that API, and does not currently
promise arbitrary DLL mods, hot loading or a complete mod ecosystem. Distinguish
the framework's capabilities from what our integration has actually verified.
