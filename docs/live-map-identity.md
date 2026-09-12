# Verified live map identity and scene invalidation

**Merged — [PR #27](https://github.com/ChronoHaxx/rubyvr-studio/pull/27), RV-008 / M5, 2026-09-12. Five human functional checks passed.**
The native Ruby adapter now identifies the current map and copied neighbour
borders. Leaving the normal field callback clears the old scene; returning to
the same map rebuilds it. This is the first part of the
[native monitor gameplay proof](issues/007-native-integration.md#native-desktop-proof).
Actors, complete UI routing and playable terrain placement remain unfinished.

## Implemented boundary

- `integration/runtime/live_scene.*` reads immutable ROM/EWRAM/IWRAM spans.
  The runtime verifies the complete immutable 16 MiB Ruby ROM against SHA-1
  `610b96a9c9a7d03d2bafb655e7560ccff1a6d894` once per guest lifetime. Other
  revisions fail closed. `reset_capture()` must precede every new ROM lifetime;
  replacing ROM bytes in place during capture is unsupported.
- Normal field requires callback2 `0x080543c5` and a clear in-battle bit.
  `gSaveBlock1.location` supplies group/number, bounded by the pinned 34-group,
  394-map table. The copied live header must match that ROM entry's structural
  fields and layout ID. Dimensions, grid spans and connection pointers are
  validated before copying. No guest state is written.
- Map identity has `LiveCapture` provenance. Connection rectangles retain
  source order, clipped negative offsets, the eight-column east border and
  incompatible-art markers. Dive/emerge connections are not planar copies.
  Existing snapshot v2 stores these fields; no new disk format is introduced.
- Refusals clear identity, grid, connections and actor validity. Every capture
  reaches the viewer/handoff, including invalid ones. A sticky invalidation
  flag prevents a quick invalid/valid pair disappearing from the VR mailbox.
  The renderer clears geometry and rebuilds after return or a different map ID,
  including when two maps share the same layout pointer.
- The desktop prototype shows the map ID in its title. An unavailable scene
  clears its background and directs the user to the separate original game
  window. The original frame/game controls continue there.

Schema evidence: pinned symbol imports and source structure definitions,
cross-checked against the actual hash-verified ROM instructions/literals.
The ARM connection entry is **12 bytes**: direction at 0, offset at 4, group
and number at 8/9. Packed offset comments are insufficient evidence. The ROM
header lookup references map-group table `0x083085a0`; the normal field input
function compares callback2 with `0x080543c5`.

This is a conservative field detector, **not a complete UI mode router**. The
Start menu and dialogue retain the field callback; the Bag screen does not.
Intentional in-view UI composition, dynamic same-map edits, actor animation,
other ROM variants, comprehensive warps/battles and complete gameplay remain open.

### Menu presentation follow-up

Maintainer clarification, 2026-09-12: clearing the viewer during Bag is the
bounded safeguard tested in PR #27, **not the intended menu experience**.
Capture validity and presentation lifetime must be separate. A recognized
field menu should retain a host-owned copy of the last valid world, including
its textures and visible actors, while the original menu is presented over it.
Stop refreshing that cached world from menu graphics; do not pause the guest
game or freeze head tracking and camera rendering. On return, validate the
current field identity and refresh or rebuild before resuming world updates.

Implement this with observed menu-state signals under M5 and composition under
M7. `non-field` alone also covers other states and must not mean "keep the old
world behind any screen". Startup without a scene, warps, battles and unsupported
states need explicit presentation choices. Original 2D menus are the first
functional surface; in VR, place them in the world while preserving the scenery,
with diegetic interactions developed separately. This behavior is not implemented
by PR #27. Its passed checklist is not acceptance of the final menu design.

The retained-scene approach above applies to the original blocking menus. The
maintainer also wants to explore browsing a diegetic bag while walking (M7/M9).
That would use a separate inventory interface while the original field remains
active; original item use needs verified game-side integration. Keeping a frozen
scene behind the original Bag does not deliver concurrent field movement.

## Agent verification

| Check | Result |
|---|---|
| Asset-free decoder | 55 optimized checks and 55 ASan/UBSan checks pass |
| Shared terrain query | 16 cases / 9,299 checks pass in each optimized/sanitized run |
| Local WSLg renderer | Valid → unavailable → same map → different ID sharing a layout passes; geometry, title, cleared rendered pixels and rebuild checked |
| Regression strength | A disposable copy restoring the old invalid-scene early return fails at “invalid scene clears geometry” |
| Native Windows build | Compiles current public capture and shared renderer sources through the CMake include |
| Real native memory | Three Route 101 states identify `0.16`, 35×34 backup cells, with Oldale `0.10` and Littleroot `0.9` copied borders |
| Real Bag/return | Normal input opens Bag: `non-field`, no key/provenance. Returning restores `0.16` and both connections |
| Actual frame-sink path | Three presented frames each from isolated field, Bag and return states; field snapshots written, Bag snapshot refused |
| Existing connected-scene suite | 69 checks pass |

The GL pixel check reads the rendered back buffer before swapping. Hidden WSLg
front buffers and delayed resize allocation are unsuitable screenshot oracles.
It uses original synthetic pixels, no game assets. Physical input is a separate
human check. Native captures and save states stay local and ignored.

Native execution is **NOT_STATIC**, with background recompilation disabled for
these measurements. Field / Bag / return respectively recorded 21 / 11 / 19
distinct misses and 8,142 / 27,015 / 5,349 interpreted instructions; all had zero
failed or inflight healing. This is not fully static execution, oracle
equivalence, a performance claim or headset acceptance. One recorded three-case
run took 2.625 / 1.406 / 2.203 seconds including startup.

Same-layout identity changes and four-direction border clipping are synthetic
checks. A live connected-edge crossing and interior warp have **not** passed;
the supplied early-story states restricted movement. RV-008 remains open for
those captures and broader transition coverage. No scenery-polish claim is made.

## Reproduce public checks

From this repository in Bash/WSL, with C++20 GCC and sanitizers:

```bash
bash tools/test-live-scene.sh
```

That command needs no SDL/OpenGL, display or game assets and runs in CI.
For the optional GL check, follow [build dependencies](building.md) and use an
available desktop/WSLg display:

```bash
cmake -S . -B build-linux -DRUBYVR_BUILD_RUNTIME_TESTS=ON
cmake --build build-linux --target rubyvr_live_viewer_test --parallel 4
./build-linux/rubyvr_live_viewer_test
```

`integration/rubyvr-runtime.cmake` supplies our source list to the documented
private Windows runner. Its caller must supply runtime headers/libraries, safe
frame sink and startup/shutdown integration. It is not a drop-in public upstream
patch. [Integration/distribution requirements](../integration/README.md) remain
open; no runner binary or game data is published.

## Maintainer functional check

The merged PR specifies the exact revision, prepared binary hash and **one**
primary local launch command. Its ignored directory contains isolated test
states and a copy of the local scenery pack. The launcher verifies those inputs
and never opens the personal save for writing. Isolated default keys: arrows
move, Enter is Start, X is A, Z is B. Focus the original window for game input.

- [x] **1. Start:** run the PR's launcher. Expect original Ruby and a scenery
  viewer titled `live map 0.16`, with two connections.
- [x] **2. Leave field:** focus original Ruby, press Enter then X to open Bag.
  Expect Bag there and a cleared viewer titled `scene unavailable`.
- [x] **3. Return:** press Z to leave Bag, and Z again if the Start menu remains.
  Expect Route 101 scenery/title to return without a stale Bag palette or
  missing geometry.
- [x] **4. Ordinary inspection:** focus the viewer, briefly hold J or L, then
  release. Orbit must stop. Focus original Ruby and confirm Enter/Z still opens
  and closes its menu.
- [x] **5. Restart:** close original Ruby and run the same launcher again.
  Expect the same prepared scene. This deliberately reloads a fixed review
  state; it is not a game-save persistence test.

The maintainer checked all five items in PR #27 and reported testing and merging
on 2026-09-12. Tested source: `825b1ae94221ba9179bf1194a45beff3538d72c2`;
prepared executable SHA-256:
`8c296a473517363179d6174e911310af0a95dd924972d85452627e2ca09bdedb`.
The exact test time was not recorded. GitHub records merge commit
`75bffda2b79b83d9d12dc9bd397616ad86f5a480` at 12:35:47 UTC on that date;
this is the merge time, not the test time. Keep these original results when
implementing the menu follow-up; changed behavior needs its own acceptance.

## Work and repair record

Astra implemented and reviewed the automated/local evidence. No paid API call
was used. Optional Claude review was blocked before dispatch by automatic
approval review; no payload was sent. Whole-turn time and subscription token
cost were not measured.

Acceptance repairs: added identity-based remeshing independently of terrain
overrides, retained invalidation across overwritten mailbox updates, and fixed
the GL test's hidden-window sizing/read-buffer assumptions. Build setup needed
explicit UTF-8 when preserving private CMake and network access for its existing
pinned dependency fetch. These are part of this delivery, not a speed saving.
