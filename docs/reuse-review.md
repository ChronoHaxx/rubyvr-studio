# Environment and OpenXR reuse review

Source review on 2026-09-08. This records inspected public code and the resulting
RubyVR change. It does not establish access to Dramatic Studio's editor source,
compatibility with another project's mods, or headset performance.

## Adapted now: DRAMALESS sky and phase palettes

The reference is DRAMALESS_SHAPE revision
`5ccc0e25417bd8b16cefd50cf08f61c44223e3d4`, covered by the retained
[MIT notice](../LICENSES/DRAMALESS_SHAPE-MIT.txt).

- [Sky.lua](https://github.com/artyrambles/DRAMALESS_SHAPE/blob/5ccc0e25417bd8b16cefd50cf08f61c44223e3d4/lib/Sky.lua)
  draws palette bands with checker transitions, follows the projected horizon,
  uses a short backdrop when the horizon is outside a downward view, and keeps
  sky drawing out of the depth buffer. A nearest-filtered palette texture avoids
  an array of fragment uniforms.
- [DayNight.lua](https://github.com/artyrambles/DRAMALESS_SHAPE/blob/5ccc0e25417bd8b16cefd50cf08f61c44223e3d4/lib/DayNight.lua)
  supplies phase palettes and tints separately from geometry. Its clock,
  celestial bodies, outdoor policy and shadow rig belong to its game integration.

`src/studio/environment_preview.cpp` adapts the four six-band palettes, phase
tints and checker-band method into an editor-only OpenGL pass. It retains 35%
of the original tint for legibility. The native source palette is never rewritten;
the shared renderer receives an optional multiplier on each draw. Classification
and untextured geometry inspection ignore that multiplier.

**Light: Neutral / Dawn / Noon / Dusk / Night** appears above the 3D viewport.
Neutral is the default and resets the preview. The source map and masking view
keep their original colors. No environment field enters a model, recipe, undo
transaction or save. This is a banded backdrop and fixed lighting preview, not
a running day cycle or a completed skybox/weather system.

## Inspected for later: Quest display and input ownership

Reference: [Gen1recomp-Quest-Standalone at
`b6666705f87fca4ee691eda12719a29b08fa74cb`](https://github.com/HimioneGranger/Gen1recomp-Quest-Standalone/tree/b6666705f87fca4ee691eda12719a29b08fa74cb),
on `quest-stable` when checked. Its [root MIT licence](https://github.com/HimioneGranger/Gen1recomp-Quest-Standalone/blob/b6666705f87fca4ee691eda12719a29b08fa74cb/LICENSE.MD)
does not establish the licence of every external mod or bundled dependency.
No Quest source is imported in this change.

| Inspected source | Useful pattern | RubyVR decision |
|---|---|---|
| [HostDisplay.lua](https://github.com/HimioneGranger/Gen1recomp-Quest-Standalone/blob/b6666705f87fca4ee691eda12719a29b08fa74cb/src/core/HostDisplay.lua) | Optional display lifecycle with a no-op default | Preserve a separate presentation boundary; keep simulation independent |
| [QuestOpenXRDisplay.lua](https://github.com/HimioneGranger/Gen1recomp-Quest-Standalone/blob/b6666705f87fca4ee691eda12719a29b08fa74cb/src/host/android/QuestOpenXRDisplay.lua) | Launcher-only pointer, normalized focus rectangles, stable control activation and capability-gated handoff | Use as an M7/M9 design reference when adding VR UI; LÖVE focus handling is not a drop-in ImGui implementation |
| [questxr_bridge.c](https://github.com/HimioneGranger/Gen1recomp-Quest-Standalone/blob/b6666705f87fca4ee691eda12719a29b08fa74cb/mobile/android/love/src/jni/questxr_bridge/questxr_bridge.c) | Validated controller-ray projection, one pointing-hand owner, double-buffered completed panel pixels, zero-layer frames when rendering is suppressed | Review these ownership contracts against our Windows adapter; avoid copying Android/JNI/EGL startup into the PC runtime |

Our existing `integration/runtime/vr_layer.cpp` already separates the frame sink
from the VR thread and hands off world snapshots. Replacing that boundary is
unnecessary for the sky preview. Future integration needs tests for controller
focus, invalid tracking, modal ownership, repeated transitions and shutdown.
The upstream [backend notes](https://github.com/HimioneGranger/Gen1recomp-Quest-Standalone/blob/b6666705f87fca4ee691eda12719a29b08fa74cb/docs/quest-openxr-backend.md)
also describe remaining lifecycle testing; this review ran no Quest build.

The advanced weather shown in newer demonstrations was not located in the
inspected source paths. This change imports no weather system and makes no
claim that those previews are available as a compatible public mod.

## Verification and remaining work

See [environment verification](environment-verification.md) for the actual
five-look renders, neutral comparison, data/geometry invariants and local costs.

M8 still needs a chosen guest-clock policy, outdoor/interior rules, animated
sky bodies, weather and water integration. M2 terrain heights and M5 live state
routing remain prerequisites for a coherent game environment. M3 authoring UX
remains an independent priority: this feature does not close the
[Dramatic Studio parity gaps](reference-parity.md).
