// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

namespace vr::billboard {

// Minimal world-space types. X east, Y up, Z south.
struct Vec3 {
    float x=0,y=0,z=0;
};

// Foot-anchored card axes. right is horizontal screen-right; up lies in the
// camera's vertical plane, tilted by pitch. Both are unit and orthogonal.
struct Basis {
    Vec3 right{1,0,0}, up{0,1,0};
};

// Camera yaw zero is an eye south of the target looking north; positive yaw
// moves the eye east. Pitch is radians above the horizon and positive looks
// down at the target. Nonfinite angles yield the default horizon/north basis.
Basis basis(float yaw, float pitch);

// Build six interleaved x,y,z,u,v vertices (two triangles, same order as
// actor_render.cpp) for a card whose bottom-left source anchor is
// foot + left*right, extending width*right and height*up.
// Returns false for invalid dimensions, nonfinite/overflowing coordinates or a
// degenerate/nonorthonormal basis, leaving out unchanged.
bool vertices(const Basis&, Vec3 foot, float left, float width, float height,
              std::array<float,30>& out);

}
