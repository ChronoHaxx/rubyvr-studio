# Level a model's foundation

**M2, merged — [PR #29](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/29).** A rigid building on sloping ground can float at one end and
intersect the ground at the other. **Level foundation** makes a flat terrain
pad beneath one selected model. [Watch the 12-second comparison](acceptance.md).

In Scene/DIORAMA, select an applied model and choose **Level foundation** in
the inspector. Save any model edits first. Ctrl+Z restores the previous slope;
Ctrl+S saves terrain and models together. Repeating the action on a level pad
changes nothing and adds no history entry.

## How the height is chosen

The tool reads horizontal faces at the model's zero-height base from the actual
production mesh. It bounds those faces with a rectangle, rounds outward to
whole 16 px source cells, and includes the renderer's existing placement anchor.
Higher roof overhangs do not enlarge the pad. It finds the **highest of all
ground corners within that rectangle**, then raises the other corners to it.
The status message reports the chosen height in source pixels.

This is an explicit authoring action, not automatic geography inference.
Ruby's gameplay elevation bits identify layers; they do not measure height.
Models retain their shape, palette and texture coordinates, and move rigidly
with the shared terrain query. Only the pad's terrain heights change.

The preview uses a **deliberately authored Oldale test slope**, then levels
16 cells to **24 px**. That hill is not a proposed change to Oldale's geography.
Neither the default starter nor the regional terrain recipe is changed.

## Limits and remaining work

A raised rectangular pad can leave a step at the entrance or perimeter. Use
the existing Terrain controls to author the approach, stairs or a different
pad height. This action does not design access routes, terraces or retaining
walls, and is not a complete solution for concave bases, stilts or shared pads.

The whole pad must have authored, resolvable, single Ground surfaces in its
owning map. Partial terrain, water, decks, stacked surfaces, stale source art,
other rigid objects claiming pad cells and copied neighbour terrain are
preserved with an explanatory refusal. This object-overlap check is
conservative: it uses source membership rather than exact physical overlap.
Ground-following cover is skipped. A completely unauthored legacy floor is
already flat and remains unchanged. Pads are limited to 1,024 cells.

The exposed outer edge/void remains **M2** work: wider terrain, map undersides
and deliberate outer borders. The [connected explorer](connected-scene.md) is
merged in PR #30 for immediate neighbours; [model reuse/six maps](region-model-reuse.md) merged in PR #31. Neutral editor lighting has a
dark backdrop; Noon already previews a sky. Outdoor horizons and distance
fog remain **M8**. First-person views still need nearby geometry; horizon
blending is a distant presentation treatment. [Tracked scope](roadmap.md).

## Reproduce the check

After the [build and local source setup](building.md):

```powershell
./build/rubyvr_studio.exe --test-foundation build/foundation-test
python tools/test-studio-foundation.py
python tools/render-terrain-foundations.py
```

The native check uses original synthetic data and needs neither game art nor
OpenGL; CI runs it. The SDL journey uses the controlled local source fixture
at 1600×1100 and 1280×720, checking application, undo/redo, repeat, save/reopen,
unchanged models/source and untouched terrain outside the pad. The GIF contains
three inspected, paired actual renders with identical before/after cameras.

[Exact evidence](terrain-foundations-evidence-2026-09-10.json). Desktop checks
do not establish live traversal, human usability or headset acceptance.
