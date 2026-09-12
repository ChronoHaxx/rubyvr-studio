# Connected scenery in the native game

**M5 / bounded M2 consumer — merged in [PR #31](https://github.com/ChronoHaxx/rubyvr-studio/pull/31).** The live viewer loads complete
nearby maps from the verified Ruby cartridge, using each map's own tiles and
palettes. Route 101 can show Oldale before entry and keep Route 101 visible
after crossing and turning around. Original guest movement remains authoritative.

![Native walk into Oldale, look back and return](media/live-connected-world.gif)

Fourteen seconds of actual native game captures, retimed for viewing. The local harness supplies
directional input and enables the existing on-foot obstacle bypass; it still
crosses through Ruby's normal connection logic. This is agent functional/visual
evidence, not physical-input, whole-game or headset acceptance.

## Try and reopen the same build

For the maintainer's prepared Windows workspace, use the existing command:

```powershell
& E:\Coding\vr-modding-research\_worktrees\live-camera\tools\run-dev-game.ps1
```

The launcher prints the tested source revision and preserves named checkpoints.
Focus the 3D window; arrows walk, J/L turn 90 degrees, R restores north-up.
The title shows how many complete maps are ready. Initial preparation takes a
few seconds while the existing single-map view remains visible. In the original
window, Esc opens Developer and Checkpoints. No extra asset-generation command
is needed. This remains the private native Windows runner; Studio's WSL editor
is a separate application.

## Known limits beside the human checks

- **M2/M5:** up to three complete maps near the player, with bounded two-hop
  discovery. The map just crossed is prioritized during handover. This fixes
  the tested close-range scenery disappearance; it does not load all Hoenn or
  eliminate the outer void when zoomed far out. Door/interior/battle routes and
  wider map cohorts still need native validation. Unsupported or over-budget
  regions retain the available view and report the problem.
- **M5/M9:** Walk through obstacles still only bypasses obstacles inside the
  current map. It does not permit walking arbitrarily beyond its boundaries or
  bypass story events. The maintainer's 2026-09-12 report that it only goes a
  short distance remains tracked; unrestricted travel/debug warp is not added.
- **M6:** actors are the game's current live actors. Distant NPC pop-in,
  special poses and broader animation/facing coverage remain open. Static
  neighbours have no separately simulated NPCs or scripted map changes.
- **M4/M10:** foliage polish is unchanged. The current map's animated tiles and
  palette stay live; neighbours use static source material until visited.
  Standalone/headset frame-rate acceptance remains open.
- **M2/M5/M7:** the reported ledge/collision-depth mismatch remains open. Bag
  still clears the viewer and uses the original window. Retained-world menu
  composition follows this connected-world slice.

## Human functional check — passed

All four steps below are checked in the maintainer's merged PR #31, verified
on 2026-09-12. Tested source: `c5bc19f`; merge: `f8a9e8d`. These results cover
this sequence, not the full game or PR #29's unreported detailed control checks.

1. [x] Launch using the command above. Wait for three maps in the title on
   Route 101. Expect the forest and complete neighbouring scenery; turn with
   J/L and verify movement still follows the view.
2. [x] From Route 101, walk north along the open path into Oldale. If the
   checkpoint's obstacles intervene, use Esc > Developer > Walk through
   obstacles, then focus the 3D view again. Expect Oldale's buildings before
   entry, continuous ground at the connection and no camera recentering jump.
3. [x] Release arrows, turn twice with J/L, and look back at Route 101. Walk
   back across the connection; expect both maps to remain in place. R restores
   north-up. The foliage/NPC limitations above are still known issues.
4. [x] Load Bag open, then Back from bag from Esc > Checkpoints. Expect the
   current temporary Bag behavior, then a restored connected world. Save a new
   named checkpoint, close/reopen with the same command, and load it.

## Source-first design and boundaries

Inspected before implementation, without copying restricted Lua:

- `UNDERdecoded/Gen2Recomped` commit
  `b2a28281b1042eb25ce0b83941be0ef756fcade9`,
  `src/world/OverworldController.lua::computeNeighbors`: all real directional
  connections, per-game cell size and translated full map bodies.
- DramaticShapes companion commit
  `4a114b3e344db629ac7c7ac5108bd3d910fc4554`,
  `mod/lib/VoxelScene.lua::masksFor/prefetch`: body ownership over padding,
  bounded live scenery and material caches.
- Extracted Dramatic Shape APK 2.4.2,
  `mods/DRAMATIC_SHAPE/lib/VoxelScene.lua`: retain outgoing full scenery while
  replacements prepare; publish incrementally instead of dropping the old map.
- Pinned `pret/pokeruby` commit `63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1`,
  `include/global.fieldmap.h`, `include/fieldmap.h`, `src/fieldmap.c`:
  native connection coordinates, 16-pixel metatiles, source border rules,
  512 tile slots and six palettes per primary/secondary half. The final LZ77
  token can exceed its declared size in this cartridge; finish that token only
  inside the explicit tile-half capacity, matching the BIOS behavior.

`live_scene::source_snapshot` reads only the independently hash-gated cartridge.
No extra decomp export is required and no guest bytes are modified. Immutable
neighbour snapshots feed the existing `prepare_region` shared production mesher
on one worker thread. GL publication remains on the native frame thread. Stale
requests are discarded; disconnected warps cannot publish an old coordinate
space. At most three maps / 24,000 source cells are selected; the existing
8-million stored / 16-million expanded vertex budgets remain unchanged.

The current map drives live actors and material updates without constructing a
second single-map mesh. Movement/palette animation does not trigger a mesh
rebuild. Four-cell position sectors update neighbour selection as the player
moves; selection is independent of camera yaw. World origins survive forward
and reverse crossings. Complete map bodies suppress copied padding through the
shared renderer's existing ownership rules.

## Verification

- `bash tools/test-live-scene.sh`: 99 synthetic memory/source-reader checks and
  17 neighbourhood checks, optimized and ASan/UBSan, without graphics or assets.
- Local verified-cartridge audit: all **394** maps decode and their full source
  grids match the independently assembled body/copied-border presentation;
  **36,834** copied neighbour cells, guest RAM unchanged. Private inputs stay local.
- Local WSLg GL regression: separate neighbour palettes are visible, current
  palette updates change actual pixels without mesh uploads, actors remain,
  asynchronous publication/return and invalid-scene clearing pass.
- Native Route 101 → Oldale → Route 101: five scripted checks pass, including
  preloading, entry, look-back, reverse entry and preserved outgoing-map origin.
  The final native camera/menu/checkpoint regression passes ten checks,
  including four walking directions, raw menu input, load reset and idle turns.
  These checks do not accept physical input or all maps.
- Initial five-map experiment exceeded the existing expanded geometry budget.
  The three-map Route 101 region prepared in roughly three seconds in the
  recorded local runs; its published buffers were about 39 MiB initially.
  Those are local diagnostics, not a frame-rate or target-device guarantee.
- Claude Opus max read-only review was attempted through the installed
  subscription CLI; it timed out after 140 seconds without a review. No paid
  fallback was used. Codex performed the implementation and acceptance checks.
