# Author one scenery family

Generate the starter pack and open the editor using [building](building.md).
The launcher resumes `build/my-scenery.json`; `-Fresh` uses the current generated
pack and a new dated output. **File...** in the header shows the save path.
The launcher takes a temporary input baseline when resuming your personal file,
so the editor's source-template protection does not prevent continued editing.

Start in the scene: click a building or tree in the map, or choose a model on
the left. The list initially shows models in this room; **Room / All** switches
to the whole library and **Search** finds a model by name. The selected-object
panel shows the source drawing and an **Edit Shape** button. The highlighted
header button gives the next action at each step.

1. **SEGMENT:** select a source group or paint its member cells. Check that it
   includes the whole object and excludes neighboring scenery.
2. **MASK:** separate Object, Ground and Shadow pixels. Brushes, rectangles
   and flood fill edit roles. Shadow also clears source-only occlusion, such as
   clipped neighbouring canopy fragments. Ground stays on the map; removed object/shadow
   pixels reveal recovered local ground in DIORAMA.
3. **MODEL:** click a visible part directly in the 3D view or choose it in the
   part list. Its name and yellow outline show the selection. Choose **Move**,
   **Resize** or **Rotate**, then drag a coloured handle. Dragging elsewhere
   orbits the camera; clicking empty space keeps the selection. You can also
   select source pixels and add relief, a box or a roof. Dimensions
   and offsets use source pixels. Split a relief selection before recessing a
   door/window. Assign front/back/side/top artwork separately and inspect repeat,
   offset and flip controls. Preserve the original pixel aspect ratio.
4. **Inspect:** orbit through the sides and back. Use Neutral to inspect shape,
   Ortho for proportions, 1/3/7 for front/right/top and 0 for isometric. F frames
   the model; **Shift+F** frames just the selected part, preserving the viewing
   angle. **View → Focus selected part** does the same. Scroll zooms; middle-drag pans the source; the View menu includes
   back and left. Check the model in DIORAMA at ground level as well.
5. **Save Model / Ctrl+S:** save commits the current valid edit and writes your
   working copy. Reused placements update together. Ctrl+Z/Y undo/redo. Escape cancels an active
   stroke or handle edit. Reopen the file and confirm the geometry is unchanged.

## Preview lighting

Use **Light: Neutral** above the 3D view to choose Dawn, Noon, Dusk or Night.
Choose Neutral again to restore the original lighting. This affects the 3D
preview during the current session; source pixels, saved models and undo
history stay unchanged. The MODEL toolbar's existing **Neutral** option removes
textures for shape inspection; it is independent of the lighting reset.

## What to submit

Record the source family ID, map, tile region, model assumptions and recipe
change. Include real front/back/left/right/oblique and ground-contact views.
Attach the relevant compact audit results, including any failed placements.
Generated packs contain source-derived metadata/masks and are local output;
submit authored recipe/code changes under the [asset policy](asset-policy.md).

The existing houses use a centred roof and aligned wall widths. Unseen surfaces
reuse selected source patches; that is an explicit modeling decision. A front
door is not repeated around the building. Trees/bushes use the silhouette and
authored depth, not brightness as a height map.

Terrain height/layer brushes, part hide/solo/lock, multi-selection and reusable
cross-definition templates are still planned. The starter models are editable
assumptions, not automatic reconstructions of unseen geometry.
