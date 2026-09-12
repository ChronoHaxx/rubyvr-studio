# Camera-relative native gameplay

**M5 desktop proof / bounded M9 controls — merged into main with PR #29
on 2026-09-12 (`9d895c7`).** The combined launch/border/camera human step is checked
for `2e07576`; the other four combined developer checks remain unreported.
The viewer starts north-up
and lets the player walk using its camera direction. The command below now
opens the combined developer/camera build with [border restoration](live-borders.md).
Existing camera-session checkpoints are preserved.

![Actual native walking from four camera directions](media/live-camera.gif)

Sixteen seconds from the actual native game and shared renderer: a close-up
before/after walking comparison, followed by stationary camera turns. These are
separate checkpoint-based runs and retimed excerpts. Input is scripted, with
noclip enabled for the walking check. This is not physical-input or performance
acceptance. [Player-facing repair, source and evidence](live-facing.md).

## Try this camera build

For the maintainer's prepared Windows workspace, this single command launches
and reopens the current combined PR #29 build:

```powershell
& E:\Coding\vr-modding-research\_worktrees\live-camera\tools\run-dev-game.ps1
```

Click the **3D window** and use the arrows to walk. **J/L** turn by **90 degrees
per press**; holding them does not spin. Held arrows defer the turn until arrow
release. **I/K** change tilt, **U/O** zoom and **R** restores north-up and player
following. Tilt/zoom use elapsed time so fast-forward does not multiply sensitivity.
The normal on-foot player now shows the appropriate front, back or side image
for that view, including when the camera turns while the player stands still.

In the original Ruby window, **Esc > Camera** offers North/West/South/East-up
presets, a camera-relative movement toggle and Reset north-up. Developer and
Checkpoints retain pause/frame-step, speed, noclip and named situations. Initial
checkpoints are copied into this separate test session; subsequent captures in
either test session do not overwrite the other session's files.

The live grid view stays at cardinal yaw angles; screen Up maps to the
corresponding map direction. The game still owns movement, collisions and scripts.
Free yaw with continuous walking remains a separate mode to implement. Release
held arrows after switching windows, changing menu context or loading a
checkpoint before starting the next walk. Buttons such as A/B/Start are not rotated.

The original game window retains map-relative controls. Stock Start-menu,
dialogue and Bag navigation retain their screen directions. The verified input
gate currently covers ordinary on-foot Ruby USA revision 1 field play; bikes,
surfing, underwater movement, battle and unsupported callbacks retain original
behavior. Input recording records resulting guest directions; replay bypasses
the camera mapping. Physical input is blocked when neither game window has focus
or when the host settings menu owns input.

## Known visible limitations beside the human check

- **M9:** this fixes the initial diagonal view and adds camera-relative grid
  movement for the focused 3D window. It does not add free analogue/diagonal
  walking, a first-person camera, camera collision or a complete camera-mode menu.
- **M6/RV-010:** this repairs apparent facing and displayed-phase matching for
  normal on-foot Brendan/May profiles. [Ordinary NPC views](live-npc-views.md)
  and loaded viewport visibility are now a separate candidate with pending human
  checks. Special profiles, steep-view readability and broader animation remain
  open. Distant live-slot pop-in reported on `6911329` is not fixed. See the
  [player-facing scope](live-facing.md) and [Emerald implementation audit](emerald-camera-actor-audit.md).
- **M2/M5:** source border forest is restored in the combined build. The new
  post-merge ledge depth/collision report remains open; see [border scope](live-borders.md).
- **M4/M10:** tree/grass art and the additional border geometry cost need further work.
- **M5/M7:** Bag still clears the 3D view. Navigate it in the original window;
  preserving scenery behind menus remains open.
- **M10:** speed targets remain hardware/rendering limited. This is not a
  performance benchmark or standalone/headset acceptance.

## Human functional check — pending

Use the source revision in the PR and printed by the launcher. No earlier revision's checked
boxes count as acceptance of this input change.

1. [ ] Launch with the one command above. Expect the original game, a north-up
   tilted voxel view, and Camera/Developer/Checkpoints tabs under Esc.
2. [ ] Focus 3D and use all four arrows in nearby clear space. Expect movement
   and character facing in the corresponding screen direction: Up shows the
   back, Down the front, Left/Right the matching profile. Keep normal tree collision.
3. [ ] In Esc > Camera choose West up, return to 3D, then press Up after releasing
   the keys. Expect movement toward the screen top (west on the original map).
   Repeat all four arrows at the other presets; the character should face the
   direction of screen movement at each one. R restores north-up.
4. [ ] Press J/L: expect one 90-degree turn per press, with no spinning while
   held. Hold an arrow and tap J/L: expect the camera to wait until arrow release,
   then turn once. Standing still, turn through all four views: only the visible
   side should change, without moving or turning the original-game character.
   The next arrow press follows the new view. Switch windows;
   expect original compass controls and no stuck movement or queued turn.
5. [ ] At a rotated view open Start and navigate its options; load Bag open and
   navigate there. Expect original menu directions, no unintended walking, and
   the existing unavailable 3D view during Bag. Load Back from bag to return.
6. [ ] Save a new named checkpoint, move, load it and release/repress the arrows.
   Expect the saved position, normal speed and noclip off. Close/reopen using
   the same command and verify the named capture persists.

## Implementation and verification boundary

The platform-independent `src/vr/camera_input.*` maps active-low directional bits
and owns hold/context transitions. The private runtime calls our
`integration/runtime/game_input.*` adapter after host key bindings and settings
capture, before writing KEYINPUT and recording it. It runs on the runtime thread.
It changes input only, not guest map, actor, collision or script memory.

`live::field_controls_available` requires verified Ruby ROM identity, normal
CB1/CB2 overworld callbacks, no battle flag, unlocked field controls and on-foot
movement. The pinned source's `ArePlayerFieldControlsLocked` at `0x08065568`
loads `0x030006a4`; the literal was confirmed in the hash-gated local ROM.
Start-menu and dialogue scripts use this lock. This is a separate input gate
from scenery validity; a visible field alone is not permission to remap menus.

`python tools/test-camera-input.py` runs 1079 synthetic assertions on Windows
and WSL, including all four viewpoints, action bits, combinations, held orbit,
context/focus changes, reset and refusal cases. WSL ASan/UBSan also passes.
The added cases cover held-turn suppression, deferred turns, opposing camera
keys, focus cancellation and release/re-arm. The GL test refuses non-cardinal yaw.
The 55 existing live-scene checks pass in optimized and sanitized builds.
The actual shared GL viewer regression passes, including default/reset north-up,
orbit without a scenery rebuild and ordinary actor/scenery invalidation.

Ten final native scripted checks pass: screen Up moves north/west/south/east at
the four corresponding views; stock Start-menu Down remains Down without moving
the player; Bag Down remains Down; loading resets noclip; north-up reset restores
the default view; four idle camera views preserve player position. The facing
trace matched all 741 displayed poses, recovering eight metadata/image transitions.
Windows/WSL actor checks (2590 per build) and actual GL directional-pixel checks
also pass. The native May sequence and physical facing checks remain pending.
The first attempt could not acquire OS keyboard focus, so its
walking checks were blocked.
Controlled native checks use an explicit viewer input source through the same
mapping/gating adapter; they do not establish physical focus/keyboard acceptance.
The checkpoint selector in that local harness was also corrected to account for
Previous/Next clamping rather than wrapping. A second attempt skipped scheduled
actions when the game stalled; the final harness advances one logical stage at
a time. The movement expectations were retained. These failed attempts are
retained locally. Claude's attempted read-only review returned an allowance-limit error
without reviewing code; Codex owns the checks. No paid API fallback ran.

The public repository contains our adapter and tests. The separately licensed
runner changes, executable, ROM/BIOS and checkpoint/pack files remain local,
under the same integration boundary as [developer mode](developer-mode.md).
