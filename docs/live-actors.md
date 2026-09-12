# Original field actors in the native voxel view

**In review — RV-010 / bounded M5–M6. Human acceptance pending.**
The player now walks and turns in the authored scenery while a tilted camera
follows smoothly. The same pass displays the other active field actors using
the original game's current sprite pixels. This is a native desktop prototype;
it does not complete the playable game or public runtime integration.

![Actual native Ruby player walking and turning in authored scenery](media/live-actors.gif)

The 12-second recording contains 720 native presentation frames, sampled every
four frames and played at nominal 60 Hz. It uses recorded original game input,
not editor flight. Camera motion follows the player. Recording writes BMPs and
is not a performance benchmark. Existing foliage and incomplete scenery remain
M4 work; the native view currently shows one live map and its copied padding.

## Delivered behavior

- Copy active object-event Sprite records, the selected ROM subsprite profile,
  OBJ tile memory and OBJ palette at the existing capture boundary. Presentation
  owns these copies and never follows guest pointers. A sprite slot must still
  identify its object event before it can be used.
- Decode ordinary 4bpp OBJ frames, 1D/2D tile layouts, palette banks,
  transparency and horizontal/vertical flips. Recompose bounded field subsprite
  pieces, including their flipped origins. Animated pixels replace the texture
  when they change; actor motion does not rebuild scenery.
- Recover fractional ground contact from sprite coordinates, global camera
  offsets and the field map's 256-pixel ring. The event's destination tile picks
  the correct ring copy; it is not used as a snapping animation position.
- Query the existing authored terrain using that position and the object's
  gameplay layer. Keep a sprite's jump/bob separate from ground height and
  camera height. Unresolved/mismatched surfaces hide the actor and increment
  the diagnostic count, rather than guessing a height from layer numbers.
- Render upright textured billboards with depth testing and transparent pixels.
  Follow the resolved player foot position. The title shows map identity and
  visible/unsupported/unresolved counts. Camera keys only react while that
  viewer has keyboard focus.
- Clear actors when field capture or scenery is unavailable, refresh after
  return, and leave disk snapshots/source-built editor scenes without actor art.
  The old inferred preview keeps its existing camera behavior; this actor pass
  is enabled in the authored diorama mode.

The capture schema is pinned to the same Ruby USA revision 1 ROM gate as
[live identity](live-map-identity.md). The pure decoder and synthetic tests are
`src/vr/actor_frame.*` and `tools/actor-frame-test.cpp`; shared presentation is
`src/vr/actor_render.*`. The capture offsets are in
`integration/runtime/ruby_world.cpp`. OBJ art is transient; snapshot v1/v2 bytes
and authored document formats retain their existing meaning.

## Checks and limits

Agent checks on 2026-09-12:

[Machine-readable results and reviewed file hashes](live-actors-evidence-2026-09-12.json).

| Evidence | Result and boundary |
|---|---|
| Actor component | 29 original synthetic checks in each optimized and ASan/UBSan build; no SDL/GL/assets/display |
| Negative control | Omitting horizontal pixel flip in a disposable decoder copy fails the intended flip assertion |
| Shared GL test | Actual red actor pixels, fractional movement without scenery rebuild, explicit 16 px terrain height, separate jump, wrong layer refusal, rejected scenery/invalid scene clearing and return/new identity |
| Existing components | Live identity: 55 checks/build; terrain region query: 9,299 checks/build, optimized and sanitized |
| Native Linux regression | Batch/GUI builds, 198 terrain/persistence checks, foundation and 69 connected checks; full editor suite including eight frozen geometry hashes, layouts, camera, voxel UI and direct part selection |
| Actual native walk | Route 101, five field actors in all 180 captured samples, no unsupported/unresolved actors in this sequence; x 16.5–18.5, z 22.5–24.5; one scenery build |
| Local terrain | Walking traverses authored 0–1 px ground grades in this small area; the 16 px height/jump contract is separately established by the synthetic GL fixture. A live ledge jump/bridge crossing is not established here |
| Native reload modes | Separate prepared field/Bag/return states: 5/0/5 actors; three frames presented per case |
| Human/headset | Pending / not tested |

The native test uses private runner changes based on
`dad4c68251aa3adde77be47884b17870a151f429` and framework base
`13cab0418106e86708cfd10b817379fe2318b201`. The review manifest records the exact
Studio test revision and hashes of the executable, local state and pack.
The 720-frame run reported **47 distinct interpreter fallback misses and
2,716,551 interpreted instructions**, with no failed/inflight self-heal jobs.
Self-heal recompilation was disabled. This is not strict-static acceptance.

Unsupported affine, blended/object-window, mosaic and 8bpp actors are refused.
The whole-object path does not reconstruct each subsprite's original BG
priority: opaque 3D scenery supplies depth occlusion. Grass covering the feet,
reflections, shadows, independent field-effect sprites, cycling/surf/dive,
scripted special actors and all camera-relative facing modes remain M6 work.
The native OBJ buffer supports at most the existing 16 active object events.
The captured scene contains the player and four other events; it does not
establish coverage of every NPC or effect.

Bag still clears the scene as in PR #27. Keeping the complete world behind
recognized menus remains the next M5/M7 presentation step. No simultaneous
stock Bag/field execution or walking inventory is introduced here. Full map
streaming in gameplay, interiors, battles, save-game coverage and hardware
input/headset acceptance remain open.

## Try the prepared local build

For the maintainer's existing Windows workspace, the same command starts or
restarts this isolated review from the verified initial state:

```powershell
& E:\Coding\vr-modding-research\rubyvr-studio\build\live-actors\try-live-actors.ps1
```

The script verifies its manifest before starting, uses the supplied local
terrain pack, disables recording/replay for human input, and writes only an
isolated review save/logs. It opens the original game window and the voxel
viewer. Original game input still belongs to the original window. This is a
private local handoff, not a public executable download or a WSL game launcher.
Public native distribution is still tracked in RV-007.

Human check — required before merge, exact revision recorded in the PR:

- [ ] Start with the command above. Expect Route 101 with the original player
  and other field actors in the voxel viewer, without missing-DLL prompts.
- [ ] Focus the original game window. Hold Left briefly, release, then Right;
  repeat Up/Down in the nearby clear space. Expect original walk/turn frames,
  smooth camera following, and a stopped player after release. The tree to the
  right blocks movement at the starting position; it is not a missing input.
- [ ] Focus the voxel viewer. Use J/L and I/K to orbit, U/O to zoom and H to
  toggle follow/map centre. Expect readable upright sprites and no camera
  control response after switching focus to the original game window.
- [ ] In the original window press Enter, then X for Bag. Expect the existing
  temporary unavailable voxel view. Press Z to leave Bag and Z to close the
  menu if needed. Expect the player and scenery to return without stale art.
- [ ] Close the original game window and run the same command again. Expect
  the same initial review state and normal movement. This checks handoff
  reopening; it is not an in-game save/load acceptance test.

<details>
<summary>Developer component checks</summary>

```bash
bash tools/test-actor-frame.sh
bash tools/test-live-scene.sh
bash tools/test-terrain-region-query.sh
```

For the optional synthetic local GL test, after the native editor dependencies
in the build guide are installed:

```bash
cmake -S . -B build-linux -DRUBYVR_BUILD_RUNTIME_TESTS=ON
cmake --build build-linux --target rubyvr_live_viewer_test --parallel 4
./build-linux/rubyvr_live_viewer_test
```

`RUBYVR_VIEWER_CAPTURE` optionally names a local BMP prefix for the native
viewer. It captures every fourth valid presentation frame, capped at 720
frames. This diagnostic writes no source/asset files to the public repository.
Raw gameplay recordings, states and RAM remain local; only the reviewed
documentation GIF is published.

</details>

## Work and repair record

Claude Opus/xhigh was assigned only the decoder and synthetic tests, with
bounded file ownership. It returned a usage-limit error before implementation:
zero reported tokens/cost and 1.242 seconds of CLI execution. No paid fallback
was used. Astra implemented, integrated and accepted the code locally; this is
not a successful worker implementation or a claimed time saving.

The local integration needed real field subsprite composition and corrected
screen-ring/global-offset positioning after inspecting the pinned capture.
The first terrain test fixture omitted ground thickness and document version;
both were corrected, and rejected scenery now also hides actors. The isolated
native probe initially lacked its ROM/BIOS configuration and selected the XR
path; its explicit desktop/headless environment and local configuration were
corrected. Final native recordings use the verified desktop launcher setup.
These repairs and review time are coordinator work, not delegated work.
