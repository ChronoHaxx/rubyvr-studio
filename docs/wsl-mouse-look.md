# WSL right-mouse movement

**In review — [PR #24](https://github.com/ChronoHaxx/rubyvr-studio/pull/24), RV-014 / M10:** automated checks pass; human check and merge pending.
After PR #22, moving the physical mouse with right button held caused excessive
turning in WSL. Holding it still did not. The original hidden SDL checks skipped
the real OS pointer grab, so they missed this difference.

## Behavior

WSL now uses window-coordinate drag for looking, with the same camera
sensitivity as before. Inconsistent raw relative deltas cannot amplify the
turn. Near a window edge, release right mouse, move back into the view and hold
again. Release, focus loss and out-of-window drag coordinates stop looking;
Escape also releases flight. WASD, Q/E and Shift retain their existing behavior.

Other native Linux desktops retain relative input by default. If enabling SDL
relative mode fails, Studio reports the error and falls back to drag. Startup
prints the selected video backend and mouse mode. Detection uses Linux's
`WSL_DISTRO_NAME` environment variable. Explicit diagnostic overrides are:

```bash
RUBYVR_MOUSE_LOOK=drag bash tools/run-studio.sh --terrain-regions --connected
RUBYVR_MOUSE_LOOK=relative bash tools/run-studio.sh --terrain-regions --connected
```

The relative override can reproduce the original WSL problem. This change
avoids that input path; no physical event trace has established its exact
WSLg/SDL cause. It does not certify every WSL backend or mouse configuration.

## Automated and agent evidence

![Old build on the left, corrected drag on the right, actual SDL checkpoint renders](media/wsl-mouse-look.gif)

12-second comparison, four paused checkpoints: initial view, small movement,
stationary pointer, and reverse movement. Both sides receive identical window
coordinates and deliberately inconsistent raw deltas. These are actual hidden
SDL renderer captures with synthetic input, not physical-pointer UAT or
real-time footage. No scene geometry or textures were edited for the comparison.

- The previous binary fails seven of the twelve new regression checks; the
  corrected binary passes all twelve. A 20-pixel horizontal move turns 0.12
  radians, and repeated raw deltas at a stationary position produce no turn.
- All 37 existing camera/typing/input checks pass using automatic WSL selection.
- All 39 native headless checks pass: 29 original file/capture cases and ten
  mouse-policy cases. They do not exercise an OS pointer grab.
- Existing connected-area and camera-streaming suites pass. Geometry, undo and
  document bytes remain unchanged; 32,000 sampled source-atlas pixels match.

The [compact record](wsl-mouse-look-evidence.json) identifies both binaries.
Local full logs and captures stay under `build/mouse-look*/`,
`build/studio-camera*`, `build/connected-scene/` and `build/camera-map-streaming/`.

Reproduce with local source assets after [building](building.md):

```bash
python3 tools/test-studio-mouse-look.py
python3 tools/test-studio-camera.py
python3 tools/test-native-portability.py
python3 tools/build-terrain-region-example.py
python3 tools/test-connected-studio.py
python3 tools/test-camera-map-streaming.py
```

## Human functional check — required before merge

**Human verdict: pending. Tested revision: not yet reported.** The maintainer
reports these results; automated checks above do not tick these boxes.

From this PR's checkout in an Ubuntu/WSL Bash terminal, with its local source
assets already prepared:

```bash
source .venv/bin/activate
git rev-parse HEAD
bash tools/build.sh --jobs 4
bash tools/run-studio.sh --terrain-regions --connected
```

Record the printed commit and confirm startup says `mouse_look=drag (WSL)`.
The task's prebuilt test command is an alternative for the same implementation;
record its binary SHA-256 from the compact evidence instead of another checkout's
commit. No save/restart test is required for this input-only repair.

1. [ ] **Small movement and stillness:** hold right mouse in the 3D view, move
   it gently a short distance, then hold still for three seconds. Expect a
   modest turn, no sudden revolutions and no continued turning while still.
2. [ ] **Release and re-grab:** release right mouse, reposition the cursor and
   hold again; also try near a window edge. Expect no jump when releasing or
   re-grabbing. Look is bounded at the edge and resumes with a new drag.
3. [ ] **Focus recovery:** while looking, switch to another app, release right
   mouse there, then return. Expect an ordinary usable cursor and a stationary
   camera until a fresh right drag. Escape during a drag should release it.
4. [ ] **Movement and UI:** while holding right mouse, briefly try WASD, Q/E
   and Shift; release it while a movement key is still held. Expect movement
   only during the held drag. Click **Return to editing**, select a map object,
   and use the wheel over the view. Expect ordinary selection/zoom, with no
   stuck capture or spinning.

Report the tested revision/binary and any failed step. Keep failures visible;
repairs require the relevant steps again on the new revision. The fix remains
unmerged while this check is pending. M2 bridge/bank work remains next after
this M10 usability blocker.
