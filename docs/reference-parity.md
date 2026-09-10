# Dramatic Studio demo: observed workflow and RubyVR parity

Reference: [DramaticShape's Quick demo](https://x.com/DramaticShape/status/2088398259224035347),
posted 2026-08-14. Rechecked 2026-09-08 using the locally supplied video:
1280×720, 24 fps, 418 frames, about 17.42 seconds. The X page could not be
fetched by the research tool during this recheck; the local video and supplied
post screenshot were the visual evidence.

Review method: decode all frames; inspect the full sequence at six-frame
intervals (0.25 s), then inspect full-resolution frames around mask/model
changes, wedge manipulation, billboard duplication and the diorama. This is
a detailed video review, not hands-on testing of Dramatic Studio. Reference
frames/video remain local; this repo links to the creator's original demo.

The earlier project review dated 2026-09-04 already contained a frame-timed
inventory. The current audit checks that inventory against the video and the
current RubyVR source; earlier claims that our backend was superior to the
reference are not supported by this short demo and are not carried forward.

## Actions actually demonstrated

Times are approximate action windows; the short MASK segment is especially easy
to miss during normal playback.

| Time | Visible action/result | Current RubyVR equivalent | Assessment |
|---|---|---|---|
| 0–2.79 s | Click/drag tile membership; footprint, cell count and pattern change | SEGMENT membership, source identity and exact structural matches; `group.*`, `gui.cpp` | Core workflow exists; not a timed usability comparison |
| 2.79–3.08 s | MASK canvas; erase stroke removes some source pixels | Mask erase/restore; v6 object/ground/shadow brushes and connected-color flood | Core workflow exists; extra batch mask commands below remain gaps |
| ~3.08 s | MODEL opens under the source map; a thin masked billboard is seeded | v5 mask seeding and explicit v6 Start Voxel Model | Same authoring purpose; v6 uses an explicit action |
| ~3.3–4.5 s | Add a box and orbit around it | Box/relief/roof creation and orbit | Implemented |
| ~4.5–5.8 s | Add a wedge; handles change position/size; axis/direction controls visible | Wedges, transforms, handles and pixel controls | Implemented core operations; full v6 handle ergonomics need user trials |
| ~6 s | Duplicate and rotate the wedge to form the other roof face | Duplicate, rotation and independent source-face materials | Implemented; reusable symmetry is still separate work |
| ~6–11 s | Add/duplicate a billboard; nudge front detail into place | Reliefs, independent depth/front position, splitting source regions, duplicate | Implemented; reference's separate `+ RELIEF` button is not exercised |
| 11.13–17.42 s | DIORAMA places the authored house on the room; orbit and zoom to eye level | Accepted models on recovered flat ground, orbit/fly/focus and neutral inspection | Implemented room workflow; terrain heights and runtime gameplay are separate |

The demo shows a useful short path from a sprite to a manually adjusted house.
It does not show a benchmark, a full save/reopen cycle, or every unseen side at
close range. The visible triangle counter is not a like-for-like performance
comparison with our renderer.

## Controls visible but not demonstrated

| Reference control | RubyVR status | Contribution needed |
|---|---|---|
| Find Every Instance | Matching and current-room highlighting exist; batch audit covers maps | Clickable all-map search/results and persistent per-instance review |
| Undo/Redo, New, Dissolve, Re-auto, room navigation | Implemented with document regression coverage | Human usability review; reference behavior cannot be inferred fully |
| ISO/Front/Side/Top/Free, room framing | Implemented camera presets/focus | Verify discoverability and small-window ergonomics |
| Billboard SYM / grow both ways from midplane | v6 front/depth controls exist; no equivalent reusable symmetry control | Explicit front-anchored vs centered depth and geometry mirroring; avoid mirrored doors |
| Sphere, cylinder, prism, dome | Not implemented as editable native voxel primitives | Add only after a specific outstanding family demonstrates the need |
| Taper, bevel | Not implemented as authored primitive modifiers | Pixel-scale modifiers with valid geometry, undo and persistence |
| Erase Color, Flood BG, Clear, Invert | Brush/rectangle/connected flood and restore exist; this full shortcut set does not | Role-aware bulk actions that preserve legitimate foliage and source opacity |
| Ground row/band, per-tile Floor flags | Source-role masks and local floor recovery exist | Explicit underlay/terrain authoring; these are not equivalent to automatic height inference |
| Import / project dropdown | Local source adapter, CLI input pack and launcher exist | Friendly input/project flow; do not claim general OBJ/VOX interchange |
| 1×/2×/3× and parts filter | Zoom and part selection exist | Hide/solo/lock and predictable zoom presets; reference filter behavior untested |

Templates, multi-selection and cross-definition copy/paste are useful RubyVR
goals but are **not established by this demo**. Likewise save/export/mod
packaging is not shown. We cannot infer that the reference lacks those features.

## What establishes useful parity

Use a held-out house and a tree: select source, remove ground/shadow, create
body/roof/relief, correct side/back art, inspect all sides, then undo, save and
reopen. Record human time, corrections, awkward steps and any hidden controls.
The result must retain native source detail and have deliberate geometry from
all viewpoints. Do not substitute a feature count or matching screenshot layout
for this end-to-end task.

See [authoring](authoring.md), [verification](verification.md) and the
[manual-workflow work package](issues/004-manual-workflow.md).

## Project history relevant to this comparison

The private development Git history records the first Ruby VR integration on
2026-08-31, with Claude credited as co-author. The batch Studio appears on
2026-09-03 and GUI work on 2026-09-04. Project notes record Codex visual checks
from 2026-09-05 onward. These are earliest recorded commits/reviews, not proof
of the exact moment a directory was created or which model performed every
operation. The public standalone extraction starts on 2026-09-08.

The creator has not granted this project access to Dramatic Studio's source or
endorsed a parity claim. The public demo is a workflow reference; separately
licensed DRAMALESS source adaptations are documented in [references](references.md).
