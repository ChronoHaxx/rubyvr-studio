# Route 104 bridge over water

**In review — RV-003 / M2.** The original bent boardwalk now has a solid deck
above the pond. Both entrances stay level with their banks. This is a local
authored example, separate from the unchanged six-map preset.

![Three paired actual Studio views](media/terrain-bridge.gif)

12 seconds, three paused pairs of actual SDL captures. Before uses the existing
source floor; after uses the bridge recipe. Camera and source art are identical.
This is sampled evidence, not real-time footage or human/headset acceptance.

## What was authored

- 38 original boardwalk cells retain their top artwork and gameplay layer 3.
- All 299 pond positions have level water at 8 source pixels, including those
  beneath the deck. Water under the deck uses explicit gameplay layer 1.
- Banks and deck tops are 16 pixels high. A 4-pixel-thick deck leaves 4 pixels
  between its underside and water. These are authored heights; gameplay layer
  bits did not determine them.
- Route 104 uses **General + Rustboro** source materials. The existing six-map
  compiler still uses its own General + Petalburg pair. The original six terrain
  maps and all starter models remain unchanged.

The [recipe](../recipes/terrain-bridge.json) explicitly lists the inspected pond
and boardwalk coordinates. Its hash binds selected source cells, their complete
metatile definitions and the added materials. Changed source requires another
inspection. The compiler refuses missing/overlapping selections, ambiguous deck
layers, insufficient clearance and existing graded/stacked/nonmatching terrain.
It makes independent output without mutating its inputs.

Route 104's other cells use a flat 16-pixel context to meet Petalburg. This is
**not complete Route 104 geography**: the southern coast, cliffs, small puddles,
Rustboro City and Route 105 remain unfinished. Source shoreline/shadow pixels
are retained; support posts, revised plank-edge artwork and foliage are later
art work. Runtime walking/surfing, collision, reflections and headset acceptance
remain M5/M6/M8/M9 work.

## Run it

PR #25 must be checked out before using these commands. Main at `3252d05`
does not contain the bridge generator. Updating source also requires rebuilding
the ignored executable; an old GUI can still have the pre-#24 mouse bug.

In Ubuntu/WSL, start in your RubyVR Studio repository with a clean working tree
and [local dependencies/assets prepared](building.md). Check out the PR without
moving your main branch, then build it:

```bash
git fetch origin pull/25/head
git switch --detach FETCH_HEAD
git rev-parse HEAD
bash tools/build.sh --jobs 4
python3 tools/build-terrain-bridge-example.py
bash tools/run-studio.sh --map MAP_ROUTE104 \
  --overrides build/terrain-bridge/regions.json \
  --out build/terrain-bridge/my-bridge.json --connected
```

If you already have the supplied bridge review worktree and matching prebuilt
GUI, use that worktree for both start and resume; it needs no second checkout.
The launcher prints its checkout and GUI fingerprint so these can be compared.

The current `--terrain-regions --connected` command continues to open the
six-map preset. The bridge command opens Route 104, Petalburg and Route 102;
further maps load as the camera travels within the existing bounded explorer.

The bridge is in Route 104's **northern pond**, above the route's central path.
Hold right mouse to look, WASD to move, Q/E down/up, Shift faster. Release right
mouse to stop. Use **Return to editing** before saving. A saved personal bridge
session resumes **from the same checkout**, after Ctrl+S has created the file:

```bash
bash tools/run-studio.sh --map MAP_ROUTE104 \
  --out build/terrain-bridge/my-bridge.json --connected
```

## Checks and limits

The source-free compiler tests and existing 13 regional tests pass. Native
acceptance verifies 38 decks, 299 water planes, four bank contacts, 30 canonical
owner boundary edges and 6,000 terrain triangles with original material IDs/UVs.
Five deliberately broken fixtures fail: deck height, water step, bank height,
Petalburg seam and deck material. The existing six-map 200-edge check still passes.

Five SDL checkpoints cover editor → connected area → return → save → reopen.
The three-map connected view has zero rejected terrain/unresolved placements;
source bytes, document/history and all six previous maps stay unchanged.
Single-map copied padding from the other tileset remains explicitly unresolved;
the connected view checks each actual owner's geometry/materials instead.

```bash
python3 tools/test-terrain-bridges.py
python3 tools/test-terrain-regions.py
python3 tools/test-studio-bridge.py
python3 tools/render-terrain-bridge.py
```

Full local logs/captures are in `build/terrain-bridge/`. The [compact evidence](terrain-bridge-evidence.json)
identifies the native binaries. No renderer, format, camera or runtime behavior
changed; the new native entry point is an acceptance test.

## Human functional check — pending

Use the exact PR head or supplied prebuilt GUI and generated pack listed in the
PR handoff. Record the tested revision; these boxes require the maintainer's
results, separately from the agent's checks above.

1. [ ] Run the generation and launch commands above. Expect Route 104 and a
   connected view initially listing Route 104, Petalburg and Route 102.
2. [ ] Fly to the northern pond and look along the side of its bent boardwalk.
   Expect visible space between water and the deck underside, matching the GIF.
3. [ ] Inspect both entrances from above and near bank height. Expect level
   bank-to-deck contact without a step; release right mouse and confirm flight stops.
4. [ ] Click **Return to editing**, select an ordinary tree/model, then press
   Ctrl+S. Close Studio and use the resume command above. Expect the same bridge,
   terrain and models, with ordinary selection and no unsaved state on reopen.

**Human verdict: pending.** Do not merge until these results are reported.

Launch follow-up, 11 September: the maintainer reported normal flying through
the supplied bridge-worktree shortcut, but excessive mouse motion and a missing
generator through commands run from main `3252d05`. Main still contained the
old GUI (`a80b000e…`); the shortcut used the repaired GUI (`593e3b2b…`). The
handoff omitted checkout/rebuild steps. Those instructions and resume paths are
corrected, and the normal local GUI was refreshed with a backup retained.
All 26 launcher checks, 12 mouse checks on that refreshed GUI, and three real
hidden SDL launch/resume paths pass. Personal saves were preserved. These agent
checks do not establish the remaining physical start/resume or bridge verdict.
