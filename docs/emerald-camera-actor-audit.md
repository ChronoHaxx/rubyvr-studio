# Emerald camera and actor implementation audit

Checked **2026-09-12**, following the maintainer's PR #30 report: animation/facing
looks wrong, distant sprites pop in, and free camera yaw feels awkward with
four-direction walking. The previous source inventory identified free movement,
but the first RubyVR input change did not apply that camera/movement pairing.
These are open M5/M6/M9 gameplay defects, not accepted polish.

This pass traced implementation, including Emerald's shared engine path:

- Companion mod: `UNDERdecoded/Gen2Recomped-DramaticShapes`,
  `4a114b3e344db629ac7c7ac5108bd3d910fc4554` (still current at inspection).
- Engine: `UNDERdecoded/Gen2Recomped`,
  `b2a28281b1042eb25ce0b83941be0ef756fcade9` (newer than the prior audit).
- RubyVR comparison: PR #30's reported `6911329` implementation, and the
  pinned pokeruby source `63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1`.
- Local [Dramatic Shape VR 2.4.2 APK](https://github.com/prismaticShape/DramaticShapeVR/releases/tag/v2.4.2):
  SHA-256 `ff1bcf51f45dd01b7b61f79fdd0581691f13d2599ca9d17bd920abe08a834831`.
  Inspected `mods/DRAMATIC_SHAPE/lib/FreeMove.lua`, `FirstPerson.lua` and
  `VoxelScene.lua` from the existing extraction. They differ from the companion
  mod; that APK is a separate reference, not evidence of Emerald support.

No hands-on Emerald session or performance measurement was performed in this
audit. Source behavior below is verified in code; smoothness in every Emerald
situation is not established by reading it. No reference implementation was
copied into RubyVR.

## Camera modes and movement work together

The four familiar 15/35/50/75 options are **pitch presets**, not four arbitrary
yaw directions. The ordinary diorama keeps its south-side reading of the world.
First/third person use a separate rig and a continuous movement controller.

[`FreeMove.install` and `tick`](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/blob/4a114b3e344db629ac7c7ac5108bd3d910fc4554/lib/FreeMove.lua#L224-L385)
wrap the shared `OverworldController.handleInput`. They maintain fractional world
position, move a circular footprint with separate-axis wall sliding, update the
logical cell on a crossing and call `onStepComplete` for that arrival. Blocked
pushes delegate ledges, boulders and map exits. Leaving free mode returns the
position to its grid cell. This changes movement; it is more than input remapping.

[`FirstPerson.driving`, movement and facing](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/blob/4a114b3e344db629ac7c7ac5108bd3d910fc4554/lib/FirstPerson.lua#L150-L455)
gate controls on the overworld owning the UI stack, normalize the movement
vector and rotate it by continuous camera yaw. The shared controller's
[script/input boundary](https://github.com/UNDERdecoded/Gen2Recomped/blob/b2a28281b1042eb25ce0b83941be0ef756fcade9/src/world/OverworldController.lua#L2636-L2740)
includes Gen3 handling and applies script/transition ownership before the wrapped
input call. This establishes the Emerald path, rather than assuming the generic
mod README alone establishes it. It does not prove every special action is compatible.

**RubyVR correction merged through PR #30 into PR #29:** live grid mode now restricts yaw to cardinal
angles. J/L request one 90-degree turn per press; held arrows defer that turn
until released. Holding J/L does not spin. Tilt/zoom remain available. Full free
walking is a separate M9 movement adapter requiring verified Ruby collision,
cell-entry, ledge, warp, encounter and script behavior. Do not restore arbitrary
gameplay yaw on top of nearest-cardinal movement and call it complete.

The APK's `FreeMove.lua` adds a shared movement path for controller input and
physical room-scale displacement, an explicit cutscene ownership check,
bounded displacement per update and accumulated blocked pushes for doors.
`FirstPerson.lua` still separates camera-world movement from apparent facing.
These are useful additional M9/VR integration constraints: moving the tracked
head must not accidentally advance the game during a scripted scene. The APK
files are studied locally; they are not redistributed with this audit.

## Animation is a world pose, with a camera-dependent drawing

The engine's [`Player.update`, `walkPhase` and `pose`](https://github.com/UNDERdecoded/Gen2Recomped/blob/b2a28281b1042eb25ce0b83941be0ef756fcade9/src/world/Player.lua#L489-L694)
separate position, facing and walk phase. The walk clock advances with game
updates; landing and walking-in-place have explicit animation treatment.

The mod's [`frameFor`, `viewFacing` and billboard transform](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/blob/4a114b3e344db629ac7c7ac5108bd3d910fc4554/lib/VoxelScene.lua#L520-L643)
select directional sheet frames while retaining the animation phase. In free
views the drawing changes with the eye's location; normal diorama cards lean
about their feet to stay readable at the chosen tilt. The
[pose collection](https://github.com/UNDERdecoded/Gen2Recomped-DramaticShapes/blob/4a114b3e344db629ac7c7ac5108bd3d910fc4554/lib/VoxelScene.lua#L1402-L1468)
is shared by rendering passes so asking for shadows/reflections does not advance
the same actor pose repeatedly.

At the reported revision, RubyVR decoded the single selected OBJ/VRAM frame and rotated its
card. It cannot select a different side/back drawing from that one frame.
The facing/readability gap is confirmed. Additional animation timing/copy-order
defects reported by the maintainer still need a paired frame trace; this audit
does not invent one root cause for all animation symptoms.

**Follow-up merged through PR #30 into PR #29:** [normal-player directional art](live-facing.md) now uses
the pinned Ruby animation/image tables to capture four views at the displayed
phase. It matches resident pixels/flips when animation metadata leads image
copying, and selects apparent facing from the actual draw transform. The native
Brendan trace matches 741/741 poses, with eight transition recoveries. This applies
the reference's pose/view separation to our native adapter; broader actors,
effects and distant visibility still require their own work.

## Pop-in has two concrete native causes

RubyVR captures the 16 live object-event slots in `integration/runtime/ruby_world.cpp`.
`src/vr/actor_frame.cpp` treats the sprite's invisible flag as hidden, and
`actor_render.cpp` drops inactive objects. Ruby sets that sprite flag for both
intentional hiding and the **original 2D viewport's off-screen test**. It also
removes distant object events from the live slots. See pinned Ruby's
[`TrySpawnObjectEvents` / removal](https://github.com/pret/pokeruby/blob/63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1/src/event_object_movement.c#L1495-L1570)
and [off-screen visibility](https://github.com/pret/pokeruby/blob/63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1/src/event_object_movement.c#L7160-L7203).
Both boundaries are visible from a wider 3D camera. Ignoring one hidden bit alone
does not restore actors already removed from the live table.

The reference builds actors from the current map's
[object definitions and visibility state](https://github.com/UNDERdecoded/Gen2Recomped/blob/b2a28281b1042eb25ce0b83941be0ef756fcade9/src/world/OverworldController.lua#L917-L950).
It also maintains [neighbour-map visual actors](https://github.com/UNDERdecoded/Gen2Recomped/blob/b2a28281b1042eb25ce0b83941be0ef756fcade9/src/world/OverworldController.lua#L1340-L1372)
with map offsets and pooled identities. Those neighbour actors do not gain
current-map interaction/trainer/collision authority. The voxel pass consumes
those records directly, not hardware sprite visibility. Script-hidden objects,
including Emerald-specific hidden actors, still obey explicit visibility rules.

**M6 follow-up:** separate gameplay visibility from 2D draw culling, retain
authoritative active poses, and provide bounded presentation records outside
the original viewport using verified map/object state. Do not force-spawn every
actor in Ruby or expose script-hidden objects. Unknown dynamic state must remain
explicit rather than invented. M2/M5's missing decorative forest is separate.

The next actor acceptance sequence must show walking/turning at four views,
an NPC crossing both the 2D cull and live-slot boundaries while staying in 3D,
intentional script hiding, a neighbour transition, and checkpoint/warp invalidation.
Compare source frame identity and phase at matching guest frames; an orbit GIF
or a count of visible actors alone cannot accept animation or pop-in repair.

**Post-PR #32 NPC follow-up, in review:** [NPC views](live-npc-views.md) applies
the pose/view separation to verified ordinary NPC graphics and bypasses only
the 2D viewport cull for a bound active event. Ruby's `sprite.c` additionally
exposed mirror updates before a queued image copy; an exact pending-request
witness handles that boundary without guessing animation or carrying old
actors. Native four-view/phase/viewport evidence is recorded; live-slot and
neighbour presentation remain open. The full acceptance sequence above is not
claimed complete by this bounded viewport repair. First/third-person continuous
movement remains required under M9, as reconfirmed by the maintainer.
