# Free walking, third-person and first-person

**Batch 2, M5/M6/M9 — implementation ready for review; human playtest pending.**
Third person and First person now move between Ruby's tile centres, including
diagonally. The view can turn smoothly with the mouse while walking. Grid remains
available. Player/NPC cards tilt toward the camera around their feet, so steep
views retain a readable sprite instead of showing its thin top edge.

![Actual native free walking, orbit, steep view and first person](media/free-camera-movement.gif)

This recording uses the actual native game and shared renderer, with scripted
input. It is sampled, captioned and retimed for review; it does not establish
physical mouse feel, performance or headset acceptance.

## Try the prepared build

Close the old game, then use the same Windows launcher:

```powershell
& E:\Coding\vr-modding-research\_worktrees\live-camera\tools\run-dev-game.ps1
```

The existing prepared runner, local game inputs and named checkpoints are the
prerequisites. This is the maintainer's prepared Windows game, not the separate
WSL Studio editor or a public installation recipe. The launcher verifies the
executable and pack; existing saves/checkpoints and the previous binary are kept.

Open **Demo controls → Camera and movement → Third person**. WASD/arrows walk;
right-click toggles mouse look. Right-click again releases it. Escape releases
the mouse and opens the panel; changing focus also releases it. **Mouse speed**
has Slow/Normal/Fast choices. J/L turn smoothly, I/K tilt, U/O change distance,
and R resets the view. **First person** uses the same walking controls and hides
the player's own sprite. **Grid** restores quarter-turn J/L and native stepping.
Enter opens Start, X confirms/talks, and Z goes back in every mode.

## Human playtest — pending before merge

Use the implementation/build revision recorded in the PR and printed by the
launcher. Agent results below do not tick these boxes.

- [ ] **Start and walk:** load **NPC views**, select **Third person**, close the
  panel and use W, then W+D. Expect smooth movement in the view's direction and
  a diagonal that is no faster than straight walking. Approach a tree/building:
  ordinary collision should stop you.
- [ ] **Mouse and release:** right-click, look and walk; select Slow if needed.
  Right-click again, press Esc, and Alt-Tab away/back. Expect a released cursor,
  usable panel and no stuck walking/spinning; release held keys before resuming.
- [ ] **Camera and actors:** orbit around the nearby NPC and tilt steeply with I.
  Expect readable, foot-anchored player/NPC art. Try **First person**, then
  **Grid**: your own card disappears only in first person; Grid restores 90° turns.
- [ ] **Ledge and connection:** load **Demo ledge**, choose Third person, walk
  down the straight ledge and try walking back up. Expect the native jump and
  blocked reverse climb. Walk between Route 101 and Littleroot and look back:
  expect the connected scenery to remain. Corner shape defects remain below.
- [ ] **Battle and ordinary UI:** choose a heading, walk in encounter grass,
  choose Run and return. Repeat with another heading. Expect the chosen view/mode
  to survive. Open Start → Bag and return, then talk to an NPC: original menus
  and dialogue should still respond without walking through them.
- [ ] **Save and reopen:** stop between tile centres, save a uniquely named
  checkpoint, move and reload it. Expect the same foot position within one source
  pixel. Close/reopen using the command above and load it again. The new process
  starts in Grid; camera/mouse preferences are currently session-only.

## Known limits beside the playtest

- **M2/M5:** broken ledge corners remain deferred. Special actions such as ledge
  jumps, pushing and map-border transfers hand back to Ruby and can centre the
  player within the current cell. Stopping exactly at the visible ledge lip and
  moving its physical trigger are not finished by this batch.
- **M5/M9:** free walking is an experimental on-foot adapter. Vehicles, forced
  movement and scripts retain original behavior. Obstacle bypass remains bounded
  by source-map and story rules. There is no all-map traversal acceptance.
- **M9:** camera collision avoidance is not implemented; orbit/first-person can
  clip scenery. Eye height uses terrain plus a fixed 1.35-cell offset. Modes and
  mouse speed are session-only; VR comfort/head tracking are untested.
- **M5/M7/M9:** the reported north-to-west battle reset was not independently
  reproduced on its original revision. A real successful Run and arbitrary-angle
  scene-return checks now pass, but broader battle/warp checks remain open. Ruby
  can centre an actor during an original scene transition.
- **M6/M9:** original actor art still supplies four directions, not new 3D bodies
  or eight-direction artwork. Distant NPCs can retain their last known pose.
- **M4/M5/M10:** foliage, missing scenery, void edges, save latency, public runner
  setup and performance budgets remain open. Battles/interiors still use the
  original graphics where voxel coverage is unavailable.
- **M5:** the existing button-only input recording/replay format cannot capture
  fractional movement and camera vectors. Free modes are refused when that
  recorder/replayer is active; use Grid for those developer sessions.

## Implementation and reference checks

The small body advances at most one source pixel per guest tick and resolves
each crossed cell through Ruby's original event handling. Native collision,
ledge, dialogue, encounter and connection routines remain authoritative. A
checkpoint load invalidates host movement state and reconstructs the foot from
the original sprite/camera data. Returning focus to the original window returns
movement ownership to its normal controls.

Native testing exposed a Ruby camera case that normal full-tile movement does
not reach: reversing partway through a Y scroll updates X instead. A bounded
function-entry adapter executes the original camera routine through its correct
Y path. Repeated partial reversals now preserve X. The public
[`free-walk-hooks.toml`](../integration/free-walk-hooks.toml) declares the three
required guards; [integration notes](../integration/README.md#continuous-on-foot-adapter)
explain the private host requirement.

The focused reference pass used cached Emerald `OverworldController.lua` at
engine `b2a28281b1042eb25ce0b83941be0ef756fcade9`, companion `FreeMove.lua` and
`VoxelScene.lua` at `4a114b3e344db629ac7c7ac5108bd3d910fc4554`, and Dramatic Shape
APK 2.4.2's `FreeMove`, `FirstPerson` and `VoxelScene` modules. Relevant behavior:
fractional feet, normalized direction, small-body collision, once-per-cell
events, original special-action handoff and camera-facing cards. These informed
the contract; no restricted implementation was copied or translated. Ruby's
pinned `pret/pokeruby` source is `63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1`;
the adapter verifies Ruby USA rev1 before reading/writing its supported layout.

## Agent verification — passed, separate from human results

| Evidence | Result and scope |
|---|---|
| Free-walk component | 14 checks, optimized and ASan/UBSan: view direction, normalized diagonal, body collision, sliding, corner blocking, bounded steps and one cell crossing |
| Billboard component | 10,079 assertions, optimized and ASan/UBSan: basis, near-vertical view, foot pivot, finite guards and vertex geometry |
| Existing camera/presentation/actor checks | Camera 1,107; presentation 53; player frame 3,500; NPC frame 16,363; actor range 25. Actor suites also pass sanitizers |
| Actual Windows OpenGL viewer | Steep card retains height; first person hides only player; arbitrary yaw retained; battle-return yaw/pitch/mode preserved; existing actor, menu, overlay and connected-scene checks pass |
| Native movement sequence | 16 checks: fractional/diagonal movement, grid return, native ledge/reverse collision, fractional checkpoint, repeated partial reversals and scripted Windows mouse capture/release |
| Native gameplay journey | Six checks: a real wild encounter, successful Run (`gBattleOutcome == 4`) with north/free mode preserved, Bag retention/return, native connection and return |
| Native Windows build | Passed with existing upstream warnings. No physical input or headset acceptance inferred |

The native connection outward leg used the existing obstacle bypass to reach
the border; return used ordinary collision. The Bag sequence followed battle
return; it is not exhaustive coverage of every fractional menu position. Native
drivers, private saves and raw captures remain local under `build/dev-session`.
The reproducible public component checks need no SDL, OpenGL or game assets:

```bash
bash tools/test-free-walk.sh
bash tools/test-billboard.sh
bash tools/test-actor-frame.sh
bash tools/test-actor-range.sh
python3 tools/test-camera-input.py
python3 tools/test-live-presentation.py
```

**Worker accounting:** the configured InferX DeepSeek Harness implemented only
the independent billboard helper/tests. Session
`rubyvr-inferx-billboard-d2a7edb4cbfe4c8a85158f6064a1b680` completed in **20m 24s**
within the 30-minute limit. Expected promotional API charge: **US$0**; provider
debit was not exposed. No paid fallback. Astra independently ran the helper
tests, integrated the renderer and accepted the bounded result; no functional
repair to the worker helper was required. Native movement, integration, the
partial-reversal repair, recording and acceptance work were Astra's work and
are not claimed as worker time savings.
