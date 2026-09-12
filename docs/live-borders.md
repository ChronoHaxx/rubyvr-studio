# Live map border scenery

**M2/M5 — merged in PR #29 at `9d895c7`.** The native voxel view now draws Ruby's
repeating border forest. Previously, it omitted the undefined padding cells
even though the original game draws a border pattern there.

![Four native views before and after restoring the border](media/live-border.gif)

Twelve seconds: four stationary views from separate native runs of the same
Route 101 checkpoint, held for three seconds each. These are actual captures
of the shared renderer, not a frame-rate or physical-input demonstration.

## Try the current combined build

The existing camera-session command now launches the combined developer tools,
camera/facing fixes and border change. It preserves that session's checkpoints:

```powershell
& E:\Coding\vr-modding-research\_worktrees\live-camera\tools\run-dev-game.ps1
```

Focus the 3D window, walk with the arrows and turn with J/L. In the original
Ruby window, Esc opens Camera, Developer and Checkpoints. The old developer
build in the main checkout is historical; it is not another required test step.
The current prepared revision is printed by the launcher. This page records
the merged border change; [connected native scenery](live-connected-world.md)
describes the newer prepared build and its own pending human checklist.
PR #29 records its launch/border/camera step passed on `2e07576`; the remaining
four combined developer checks were not reported.

- [x] Launch Route 101. Expect the forest to continue outside the left and
  right map edges shown in the comparison. Turn through all four views with J/L.
- [ ] With Walk through obstacles off, approach the trees. Ordinary blocked
  movement remains blocked; the added scenery grants no extra walkable area.
- [ ] Load Bag open and then Back from bag using Esc > Checkpoints. The Bag
  still uses the original window; returning restores the world and border.
- [ ] Save a new named checkpoint, close the game and reopen with the same
  command. Load that checkpoint; expect the same position and scenery.

## Known limitations beside the checks

- **M2/M5 ledge alignment:** the maintainer reported apparent walkable space
  being blocked and ledge activation feeling too deep after merging PR #30 on
  2026-09-12. Exact location/running revision was not supplied. This change
  does not fix ledge depth, jump takeoff/landing or other geometry/collision
  disagreements. Ruby's original movement rules remain authoritative.
- **M4 foliage:** existing tree shapes, repetitive placement, leftover ground
  artwork and unapproved grass art remain. This restores missing placements.
- **M6 actors:** [ordinary NPC views](live-npc-views.md) and loaded viewport
  visibility are in review. Special-player facing, animation coverage and distant
  sprite pop-in remain open. The merged normal-player facing fix is included.
- **M5/M7 menus:** Bag still clears the 3D view. Retaining the world behind
  recognized menus follows the active connected-scenery slice.
- **M2/M10 extent/performance:** the border fills only the existing backup
  buffer (seven cells north/south/west, eight east). It does not load whole
  neighbouring maps, close the outer void or establish a frame-rate budget.
  This Route 101 scene grows from 1,429,710 to 3,949,998 mesh vertices
  (59 to 171 raised instances). Live reuse/culling and performance work remain.
  No standalone or headset performance acceptance is claimed.

## Source-first implementation

The pinned native source is `pret/pokeruby`
`63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1`:
[MapLayout](https://github.com/pret/pokeruby/blob/63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1/include/global.fieldmap.h)
contains an eight-byte, 2x2 border table;
[fieldmap.c](https://github.com/pret/pokeruby/blob/63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1/src/fieldmap.c)
defines its repeating phase and the metatile/collision/elevation accessors.
An undefined backup cell uses border art, reports blocked collision and reports
elevation zero. Border-table elevation bits are not physical terrain height.

The cached Emerald comparison was inspected before implementation:

- `UNDERdecoded/Gen2Recomped` at `b2a28281b1042eb25ce0b83941be0ef756fcade9`,
  `src/world/OverworldController.lua`: preserve real neighbour bodies over filler.
- The DramaticShapes companion at `4a114b3e344db629ac7c7ac5108bd3d910fc4554`,
  `mod/lib/Gen3.lua`, `metatileAt`/`blockedAt`: repeat all four border quarters;
  keep off-map collision separate from scenery and terrain assumptions.
- Locally extracted Dramatic Shape APK 2.4.2,
  `mods/DRAMATIC_SHAPE/lib/VoxelScene.lua`, `groundAt`: its border-height/seam
  warning reinforces preserving copied neighbour ground instead of treating a
  forest border as the walker's height. This is reference behavior, not proof
  that the APK supplies the native Ruby integration.

No external Lua was copied. `live::inspect` validates the border ROM span and
owns four copied words. `copy_presentation_grid` applies the native border
lookup only to undefined padding in a host-owned grid. It preserves body cells
(including undefined body holes), real neighbour words and connection provenance.
The existing shared mesher and structurally matched models draw the result.
Guest memory, game collision, authored terrain, disk snapshots and the editor's
source adapter are unchanged. Invalid scenes clear the output.

## Verification

- `bash tools/test-live-scene.sh`: **75 checks**, optimized and ASan/UBSan;
  no graphics or game assets. The same tests pass on Windows/MinGW.
- A disposable copy with border substitution disabled fails the border-phase
  assertion. Production code and existing checks were retained.
- Local asset-backed audit of **394** maps using the verified ROM and production
  inspector/copy function: **281,939** padding cells resolved, **289,680**
  body/defined cells preserved, including **36,834** copied neighbour cells;
  guest RAM unchanged. This simulated-grid audit is separate from live play.
- Actual native Route 101 run: **10** camera movement/menu/checkpoint/idle-turn
  checks pass. All four recorded border views were inspected. Checkpoint copies
  and recordings are isolated from the maintainer's saves.
- Source-publication and whitespace checks pass. PR #29 is merged; the
  launch/border/camera human step is checked and the other four combined
  developer steps remain unreported. CI is reported separately in PR #29.

Private audit helpers, captures and build provenance remain under the local
`build/dev-session` directories; no ROM, saved state or runtime dependency is
included in the public change.
