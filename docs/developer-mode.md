# Native developer mode

**Current: [desktop demo batch 1](desktop-demo-batch.md), awaiting human acceptance.**
The test controls are now available over the voxel viewer. This is the prepared
private Windows game, separate from the Studio editor and a public installation.

```powershell
& E:\Coding\vr-modding-research\_worktrees\live-camera\tools\run-dev-game.ps1
```

The same launcher starts **NPC views** in Littleroot. Click **Demo controls** or
press **Esc in the voxel viewer** for pause, one-frame stepping, speed, obstacle
bypass and a direct list of named checkpoints. **Return to game** closes the
panel. The original Ruby window retains its existing Esc menu.

Select **Demo ledge**, **Demo battle**, **Demo lab ready** or another situation
and click **Load selected situation**. Loading resets speed to 1x and disables
obstacle bypass. Save under a new name; existing names are refused. Checkpoints
stay separate from the normal game save. All existing prepared states are kept.

For normal play, arrows walk, J/L turn in 90-degree steps, R resets the view,
I/K tilt and U/O zoom. Enter opens Start, X confirms/talks and Z goes back.
Original Bag/Party/Options retain scenery; battles and interiors use the original
frame in this viewer. The WSL Studio editor remains separate; Linux live-game
support is not established by this prepared Windows build.

Use the [current five-step human check and visible limitations](desktop-demo-batch.md#combined-human-check--pending).
In particular, the measured route checkpoint save took about 28 seconds to reach
a safe boundary; wait for **Saved** before closing. Unseen/neighbour NPCs, other
routes' contact defects, free camera/movement, foliage polish, public setup and
release performance remain unfinished. No new human acceptance is claimed yet.

## Earlier developer slice

The underlying controls merged in PR #29. Its launch/camera step was reported
passed, while the detailed original developer checklist remained unreported.
The new combined checklist supersedes those instructions for this revision;
it does not retroactively tick the historical checks. PR #32's bounded menu/
battle/interior session and PR #33's ordinary NPC views were accepted separately.

The recording and implementation notes below preserve the earlier PR #29 scope.
They do not describe the new panel or prove current physical-input acceptance.

![Earlier native developer menu and collision check](media/native-developer.gif)
## Verification and integration boundary

`python tools/test-dev-session.py` compiles the host checkpoint/transport code
with C++20 and runs 40 checks without SDL, OpenGL, game data or a display. It
passes on Windows/MinGW and WSL/GCC and runs in hosted Linux CI.

Native verification exercises the real runner: pause, exactly one VBlank step,
new checkpoint publication, reload, blocked tree movement with noclip off,
passing through those trees with it on, reset of speed/noclip on load, and
accelerated/uncapped execution. The initial pause and pacing faults failed this
probe before repair. A westward movement test was rejected as a collision test
after source inspection identified Birch's coordinate event; the final test
uses the trees immediately east of the start. Events remain enabled.

Eight native functional checks pass. The separate throughput threshold does
**not** pass in the final capture: 119 guest frames at normal, 194 at 4x requested,
and 210 at MAX, each over two seconds. The speed controls accelerate execution,
but this measured performance limitation remains M10 work. Earlier runs reached
the 2x threshold; that does not override the final measurement.

The pinned local integration uses RubySapphireRecomp `dad4c68251aa3adde77be47884b17870a151f429`
and gbarecomp `13cab0418106e86708cfd10b817379fe2318b201`, with the existing local
runner changes. Only Ruby USA revision 1 is supported. The collision entry is
`CheckForPlayerAvatarCollision`, Thumb `0x08058DD4`, from the imported function
symbols and pinned pokeruby `63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1`.

The public files are our host session controller and runtime-menu adapter.
The external runner is separately licensed and its modified source, executable,
ROM, BIOS and captures/checkpoints remain local. A fresh public checkout alone
cannot produce this private developer build. See [integration](../integration/README.md).

The local runner adapter must:

1. Call `vr::dev::configure(opts)` before starting the runtime. An absent
   `RUBYVR_DEV_DIR` leaves normal execution unchanged; a valid directory opts in.
2. Attach the supplied runtime UI callbacks/items. Refresh location from verified
   guest data on the runtime thread. No GUI/VR thread writes guest memory.
3. Service checkpoint requests only after unwinding to the outer dispatch loop,
   using the runtime's existing snapshot save/load functions. Never restore
   memory inside the present-in-place callback. Re-arm that callback afterward;
   restore presentation and invalidate captured world state after a successful load.
4. Apply pause and frame-step boundaries to both the outer and present-in-place
   paths, keeping input/presentation responsive while paused. Do not count a
   repeated presentation of the same VBlank as another simulation frame.
5. Apply developer pacing in both presentation paths and disable the ordinary
   frame pacer while it owns timing. Suppress accelerated audio. Disable the
   legacy ROM-adjacent state-slot writes in the isolated developer session.
6. Retain the runtime's trusted function-entry hook registry for noclip. A build
   with this entry compiled ahead of time must emit its reviewed hook guard;
   the currently tested entry uses the runtime's existing interpreter bridge.

The private handoff manifest records the executable and prepared-input hashes.
It must be refreshed after rebuilding; Git checkout/pull does not refresh ignored
executables. Full map editing and the public runner packaging contract remain M5 work.
