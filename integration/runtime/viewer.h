// SPDX-License-Identifier: GPL-3.0-or-later
// Live desktop presentation over the shared scenery and original game.
// Grid: WASD/arrows, J/L quarter turns. Free modes: continuous on-foot
// movement, right-click mouse-look toggle, J/L smooth turn and I/K pitch.
// Escape/menu/focus loss release capture. First person hides only the player
// card. The native movement adapter is separate; the editor/XR keep their own
// camera policy. All calls run on the thread that owns the GL context.

#pragma once

#include <SDL2/SDL.h>

#include "ruby_world.h"
#include "live_region.h"
#include "live_presentation.h"

namespace vr {
namespace viewer {

// Take over `win`: resize it, show it, and switch to viewer rendering.
bool init(SDL_Window* win, bool visible = true, world::live::SourceLoader loader = nullptr);
void shutdown();
// Published scenery only, for native/GL regression checks and diagnostics.
size_t connected_maps();
bool map_origin(int group,int number,int* x,int* z);

// True when the viewer owns the window and the frame sink should drive it.
bool active();
bool focused();
float yaw_radians();
float pitch_radians();
float camera_distance();
void set_camera_distance(float distance);
// Grid mode quantizes yaw; free modes retain the continuous angle.
void set_yaw_radians(float yaw);
void set_pitch_radians(float pitch);
enum class CameraMode { Grid, ThirdPerson, FirstPerson };
CameraMode camera_mode();
void set_camera_mode(CameraMode);
bool continuous_movement();
bool mouse_look();
int mouse_speed();
void set_mouse_speed(int preset);
void release_mouse();
bool event(const SDL_Event&);
void reset_camera();
bool camera_relative();
void set_camera_relative(bool enabled);
// Host overlay is optional; standalone rendering/tests keep no runtime dependency.
void set_overlay(void (*draw)(SDL_Window*), bool (*owns_input)(), void (*shutdown)());

// Update the mesh from `s`, draw one frame, and present. Must be called on the
// thread that owns the GL context — which, in viewer mode, is the emulation
// thread, because no VR thread was ever started.
// present=false leaves the rendered back buffer available to local GL probes.
void frame(const world::Snapshot& s, bool present = true);

// Complete native desktop presentation. Original RGB and transparent field UI
// belong to the same quiescent frame; only recognized menus can retain scenery.
void game_frame(const world::Snapshot&, const presentation::Input&,
                std::span<const uint8_t> rgb, int width, int height,
                std::span<const uint8_t> field_ui={}, bool present=true);
bool uses_world_controls();
bool controls_for_scene(const presentation::Input&);
presentation::Decision presentation_state();

}  // namespace viewer
}  // namespace vr
