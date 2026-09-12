# Camera-facing player art

**M6 / RV-010 supporting the active M5 desktop proof — in review in PR #30.**
The maintainer reported that the character appeared to walk sideways or backwards
after turning the camera, including after the 90-degree camera correction.
The controls rotated correctly, but the billboard kept Ruby's original 2D frame.

The prepared camera build now selects the original front, back or side art for
**Brendan and May's normal on-foot idle, walking and running profiles**. Position,
collision, actual facing, animation timing, palette, flips and foot pivot remain
owned by the game. Turning the camera while standing still changes the visible
side without turning or moving the game character.

![Native before/after walking and stationary camera turns](media/live-camera.gif)

Sixteen seconds of actual native captures, cropped and enlarged to make the
character readable. Walking compares separate runs from the same checkpoint;
the stationary sequence pairs the original and voxel windows from the corrected
run. These are retimed excerpts with scripted input, not a performance measurement
or physical-keyboard acceptance. Noclip is enabled only for the walking checks.

## Source and implementation

The [Emerald/APK audit](emerald-camera-actor-audit.md) identified the useful pattern:
separate the actor's world pose from the image selected for a particular camera.
The reference supplies directional frames from an animation phase. Our native
adapter reads that information from Ruby's own tables; no external Lua was copied.

The pinned pokeruby commit is `63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1`:
[`Sprite` ABI](https://github.com/pret/pokeruby/blob/63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1/include/sprite.h),
[`BeginAnim` / image copying](https://github.com/pret/pokeruby/blob/63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1/src/sprite.c),
and [object animation tables](https://github.com/pret/pokeruby/blob/63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1/src/data/object_events/object_event_anims.h).
The existing ROM identity gate precedes capture. Only the verified normal
Brendan/May image tables and their 24 animations are accepted. Capture owns four
bounded 256-byte images; the renderer does not read guest memory.

Animation metadata can advance before image copying completes. The adapter
first verifies that the selected phase's pixels and flips match resident OBJ.
If they differ, it finds an exact matching resident pose within the verified
profile. The alternate views use that displayed phase, without adding a timer or
carrying an old actor across snapshots. Unknown profiles or unmatched data keep
the original captured frame. The shared renderer derives apparent facing from
the actual view/model transform and selects art independently for each draw.

## Checks and remaining scope

- `bash tools/test-actor-frame.sh`: 2590 checks each in optimized and ASan/UBSan
  builds. The Windows build passes the same checks. Original synthetic pixels
  cover both profiles, animation phases, all 16 world/view directions, mirrors,
  transparency, pivots, paused/lagging frames, invalid tables and safe fallback.
- `build-linux/rubyvr_live_viewer_test`: actual directional pixels in all four
  GL views, stationary feet, no scenery rebuild and existing invalidation checks.
- A disposable original-view-only implementation fails the directional check.
- Native acceptance results and exact build identity are recorded in PR #30.
  `RUBYVR_ACTOR_TRACE=1` is an optional local capture diagnostic for comparing
  metadata with the verified displayed phase; the ordinary launcher clears it.

**Still open:** NPC apparent-facing selection, bikes/surfing/fishing and other
special player profiles, steep-view card readability, other animation/effect
defects, and distant actor pop-in. The native recording demonstrates the tested
Brendan sequence; May's table/profile handling has synthetic coverage but no
separate native May playthrough. None of this accepts complete M6 fidelity.

Use the single [camera-build launcher and human checklist](live-camera.md).
Check all four walking directions at each camera angle, turn the camera while
standing still, then load a checkpoint and repeat. Physical input and user visual
acceptance remain pending for the PR's current revision.
