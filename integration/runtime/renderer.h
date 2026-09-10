// renderer.h — stereo 3D rendering into an XrCompositionLayerProjection.
//
// WHAT CHANGES FROM THE QUAD LAYER, because it is a change in kind:
//
//   XrCompositionLayerQuad (Phases 2-4) is "here is an image and a pose, you
//   place it". The runtime does the reprojection and we never render a scene.
//   That is why it stayed sharp and cost nothing, and why it can only ever show
//   a flat picture.
//
//   XrCompositionLayerProjection is "here is what each eye sees". We render the
//   scene ONCE PER EYE, from two slightly different positions, with a depth
//   buffer, into two swapchain images the runtime hands us. Everything about
//   3D — parallax, occlusion, a world you can lean into — comes from that.
//
//   Both layers can be submitted in the same frame, and are: the projection
//   layer carries the 3D scene and the quad still floats the GBA screen above
//   it. Submission order is composition order, so the projection goes first.
//
// THE TWO THINGS PEOPLE GET WRONG HERE:
//
//   1. The projection matrix is ASYMMETRIC. A headset lens is not centred on
//      the eye, so angleLeft and angleRight have different magnitudes and a
//      textbook symmetric perspective() is subtly wrong — it looks almost
//      right, and gives everyone eye strain. OpenXR hands over four angles;
//      use all four.
//
//   2. Depth is OURS, not the runtime's. The swapchain gives colour images
//      only. Core OpenXR does not want a depth buffer submitted (that is the
//      optional XR_KHR_composition_layer_depth), but we still need one to
//      render with, so we make our own renderbuffer and attach it.

#pragma once

// windows.h and unknwn.h MUST precede the OpenXR platform header, or
// openxr_platform.h fails with four "unknown type name 'IUnknown'" errors from
// the Win32 graphics extensions. Same trap as vr_layer.cpp; see the notes at
// the top of that file.
#include <windows.h>
#include <unknwn.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <vector>

#include "ruby_world.h"

namespace vr {
namespace renderer {

// Create per-eye swapchains, the depth buffers, the FBO and the shaders.
// A GL context must be current and vr::gl::load() must have succeeded.
bool init(XrInstance instance, XrSystemId system, XrSession session);

void shutdown();

// True once init() has succeeded — the frame loop submits a projection layer
// only when this holds, so a renderer failure degrades to the quad alone
// rather than taking VR down with it.
bool ready();

// Render both eyes for this frame and fill in a projection layer.
//
// Call between xrBeginFrame and xrEndFrame. `views` is scratch owned by the
// caller and must stay alive until after xrEndFrame, because the layer points
// into it — a local vector inside this function would be a dangling pointer by
// the time the runtime read it, which is the classic way to make this crash
// somewhere else entirely.
//
// Returns false if the views could not be located this frame (tracking loss),
// in which case nothing was rendered and no layer should be submitted.
bool render_frame(XrSpace space, XrTime display_time,
                  XrCompositionLayerProjection& out_layer,
                  std::vector<XrCompositionLayerProjectionView>& views);

// ── Offline self-test ────────────────────────────────────────────────────────
//
// Render the scene to an offscreen buffer with a MADE-UP camera and write it as
// a PPM. Needs a current GL context and gl::load(), and nothing else — no
// headset, no OpenXR session, no runtime.
//
// This exists because the projection matrix is the single most likely thing in
// Phase 5 to be quietly wrong, and the natural way to find out is to put a
// headset on and squint. That is a slow loop and a bad instrument: a transposed
// or mirrored matrix still produces a picture. Rendering two known views to
// disk instead makes the failure legible — a symmetric view should put the
// cube dead centre, and an asymmetric one should shift it by an amount you can
// predict from the angles.
//
// `asymmetric` selects the second case: deliberately unequal half-angles, which
// is what a real lens gives and what a textbook perspective() would get wrong.
bool selftest_dump(const char* path, int w, int h, bool asymmetric);

// Same idea, but rendering the real diorama from a synthetic camera placed
// above and in front of the board, looking down at it.
//
// This is the Phase 6 equivalent of Phase 4's map A/B: it turns "put the
// headset on and see whether the town looks right" into an image on disk that
// can be examined a frame at a time. Needs a valid world snapshot, a current GL
// context, and no OpenXR at all.
bool selftest_diorama(const char* path, int w, int h, const world::Snapshot& s);

// ── The inspector ────────────────────────────────────────────────────────────
//
// Render the live mesh from six cameras into one contact sheet PPM: overhead,
// oblique, oblique-with-classification-colours, and three close shots down to
// eye level.
//
// This exists to close a feedback loop that was costing a headset session per
// iteration. Geometry bugs are visual, and judging them from a description of a
// screenshot is guesswork — the naive run-merge that fused forests into walls
// would have been obvious in one glance at the classification tile.
//
// Needs a current GL context and a valid snapshot; no OpenXR, no headset.
bool inspect(const char* path, const world::Snapshot& s);

// One rectangle of the map, alone on empty ground, through the same six
// cameras. For working on a single structure without its neighbours confusing
// the picture. Writes to `path`; destructive to the live mesh, offline only.
bool inspect_isolate(const char* path, const world::Snapshot& s,
                     int x, int y, int w, int h);

// The same idea aimed at the TILESET instead of the map: every distinct
// metatile alone on its own plot, four yaws each.
//
// The contact sheet above shows what the map looks like; this shows what the
// RULES do, one metatile at a time, with no neighbours to blame. Four yaws
// because the fold puts one drawing on all four sides of a unit, so judging it
// from one angle is judging a quarter of it.
//
// DESTRUCTIVE: it replaces the live mesh with the turntable's. Offline only.
// Prints a manifest to stderr for tools/turntable.py to label from.
bool inspect_turntable(const char* path, const world::Snapshot& s);

}  // namespace renderer
}  // namespace vr
