# Solid ground in the connected explorer

**M2 / RV-003 — desktop checks pass; merged in PR #21.** Ground no
longer disappears when the camera flies below the loaded area. Legacy floors
and authored Ground now close at **-16 source pixels**, one map cell below the
original floor. Existing terrain heights and scenery placement stay unchanged.

![Actual before/after Studio views](media/connected-ground-base.gif)

The 12-second comparison shows the underside, an exposed edge and the existing
scenery from above at identical cameras. The base is a deliberate desktop
preview boundary. Its depth does not describe the game's geography or collision.

Shared edges use the neighbouring world cell's owner, including missing copied
padding and cells beyond a map snapshot. Solid neighbours hide buried side
faces. Source tile/palette indices stay local to their map; exposed sides repeat
native pixels using the existing ground/side material. The single-map editor
keeps its previous geometry.

Explicit Water and deck-only cells receive no inferred seabed or bridge support.
In a mixed cell, only Ground gets the base. Rejected source guards remain
rejected and retain their legacy floor. Legacy source floors have no authored
water classification, so this change does not infer one from their colours.

Native checks pass: **69 connected, 197 terrain and 23 foundation**. Existing
editor, connected-flight and camera-streaming suites pass, including all eight
frozen geometry hashes and the neighbour-atlas pixel comparison. Six actual
captures were inspected. A 171,000-pixel interior comparison is identical;
newly visible edge faces are deliberately outside that comparison. The tested
six-map mesh/art allocation rises from **69.4 to 71.7 MiB**. These are desktop
results, not a headset frame-time or whole-process memory measurement.
[Exact verification](connected-ground-base-evidence-2026-09-10.json).

From a checkout containing this change, use the same launch options:

```bash
bash tools/build.sh
bash tools/run-studio.sh --terrain-regions --connected
```

Complete coastlines, water depth, caves/cutaways and authored outer geography
remain M2 work. Horizon blending belongs to M8; foliage appearance remains M4.
This base makes the current loaded area solid; it does not finish those tasks.
