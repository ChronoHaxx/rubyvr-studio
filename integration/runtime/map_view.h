// map_view.h — draw the field map ourselves, from the Snapshot, on the CPU.
//
// Phase 4.3, and it is a TEST INSTRUMENT more than a feature. The diorama in
// Phase 6 draws this same data as 3D geometry on the GPU; this draws it as a
// flat image so the decode can be checked before any of that exists.
//
// WHY 240x160, the game's own resolution, rather than a big standalone map:
//
//   Because then it is a direct A/B. The GBA is already drawing this exact
//   region at this exact size, so toggling between its framebuffer and ours
//   puts two images of the same thing in the same place. If the metatile
//   decode, the tile flips, the palette mapping or the layer order are wrong,
//   the difference is obvious at a glance. A bigger, prettier map view would
//   look plausible while being subtly wrong, which is the one thing a test
//   instrument must not do.
//
//   It also means zero changes to the working swapchain code: the map view is
//   just a different image of the size that is already there.
//
// What it will NOT match, by design: the game's sprites (we draw markers
// instead) and the last few pixels of scroll alignment. Metatile GEOMETRY and
// COLOUR lining up is the thing being checked.

#pragma once

#include <cstdint>

#include "ruby_world.h"

namespace vr {
namespace map_view {

// Render the map around the player into an RGBA8 buffer of w*h pixels.
// Safe to call with an invalid snapshot; it fills the buffer with a flat
// colour so the toggle still visibly does something.
void render(const world::Snapshot& s, uint32_t* dst, int w, int h);

}  // namespace map_view
}  // namespace vr
