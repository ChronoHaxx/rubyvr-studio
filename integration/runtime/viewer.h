// viewer.h — a live, orbitable desktop window showing the diorama mesh.
//
// WHY THIS EXISTS:
//
//   Geometry bugs are visual, and until now judging one cost a headset session
//   plus a description of a screenshot. The contact-sheet inspector fixed the
//   worst of that, but a fixed set of six angles still cannot answer "what does
//   the BACK of that house look like" or "is that tree's silhouette right"
//   without another build.
//
//   This is the same mesh, in a window, that you can fly around — and because
//   it renders from the emulation thread's own frame sink, it is LIVE: walk in
//   the game and the world updates under the camera.
//
//   It is a development instrument, not a feature. It never runs alongside a VR
//   session, because the two would fight over the GL context.
//
// CONTROLS (all keys the GBA does not have, so nothing collides with play):
//   J / L      orbit left / right          U / O   zoom out / in
//   I / K      orbit up / down             T / G   raise / lower the target
//   N / M      min unit height  - / +      (re-meshes live)
//   B          cycle: textured -> classification colours
//   H          follow the player  <->  hold still over the map centre
//   R          reset north-up camera
//   Arrows     play while focused: camera-relative in the normal field,
//              original screen directions in menus (not free/diagonal walking).

#pragma once

#include <SDL2/SDL.h>

#include "ruby_world.h"

namespace vr {
namespace viewer {

// Take over `win`: resize it, show it, and switch to viewer rendering.
bool init(SDL_Window* win, bool visible = true);

// True when the viewer owns the window and the frame sink should drive it.
bool active();
bool focused();
float yaw_radians();
void set_yaw_radians(float yaw);
void reset_camera();
bool camera_relative();
void set_camera_relative(bool enabled);

// Update the mesh from `s`, draw one frame, and present. Must be called on the
// thread that owns the GL context — which, in viewer mode, is the emulation
// thread, because no VR thread was ever started.
// present=false leaves the rendered back buffer available to local GL probes.
void frame(const world::Snapshot& s, bool present = true);

}  // namespace viewer
}  // namespace vr
