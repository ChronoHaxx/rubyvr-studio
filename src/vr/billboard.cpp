// SPDX-License-Identifier: GPL-3.0-or-later
#include "billboard.h"
#include <algorithm>
#include <cmath>

namespace vr::billboard {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kPitchLimit = 1.50f;
constexpr float kTolerance = 1e-3f; // accepts small floating error in a basis

bool finite3(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// A usable basis is finite, both axes unit length and mutually orthogonal.
bool usable(const Basis& b) {
    if(!finite3(b.right) || !finite3(b.up)) return false;
    const float rr = b.right.x*b.right.x + b.right.y*b.right.y + b.right.z*b.right.z;
    const float uu = b.up.x*b.up.x + b.up.y*b.up.y + b.up.z*b.up.z;
    const float ru = b.right.x*b.up.x + b.right.y*b.up.y + b.right.z*b.up.z;
    return std::fabs(rr-1.f) <= kTolerance && std::fabs(uu-1.f) <= kTolerance &&
           std::fabs(ru) <= kTolerance;
}

}

Basis basis(float yaw, float pitch) {
    Basis b;
    if(!std::isfinite(yaw) || !std::isfinite(pitch)) return b;
    yaw = std::remainder(yaw, 2.f*kPi); // wrap to [-pi, pi]
    pitch = std::clamp(pitch, -kPitchLimit, kPitchLimit);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    // right stays horizontal; up tilts about that right axis by pitch,
    // so the top leans away from the eye (north at yaw zero).
    b.right = {cy, 0.f, -sy};
    b.up = {-sy*sp, cp, -cy*sp};
    return b;
}

bool vertices(const Basis& b, Vec3 foot, float left, float width, float height,
              std::array<float,30>& out) {
    if(!usable(b) || !finite3(foot)) return false;
    if(!std::isfinite(left) || !std::isfinite(width) || !std::isfinite(height)) return false;
    if(!(width > 0.f) || !(height > 0.f)) return false;
    const float right_edge = left + width;
    if(!std::isfinite(right_edge)) return false;

    // Bottom-left, bottom-right then the matching top corners.
    const float blx = foot.x + left*b.right.x;
    const float bly = foot.y + left*b.right.y;
    const float blz = foot.z + left*b.right.z;
    const float brx = foot.x + right_edge*b.right.x;
    const float bry = foot.y + right_edge*b.right.y;
    const float brz = foot.z + right_edge*b.right.z;
    const float tlx = blx + height*b.up.x, tly = bly + height*b.up.y, tlz = blz + height*b.up.z;
    const float trx = brx + height*b.up.x, try_ = bry + height*b.up.y, trz = brz + height*b.up.z;

    const float coords[18] = {tlx,tly,tlz, trx,try_,trz, brx,bry,brz,
                              tlx,tly,tlz, brx,bry,brz, blx,bly,blz};
    for(float c : coords) if(!std::isfinite(c)) return false;

    // Triangle order and UVs match actor_render.cpp: top-left, top-right,
    // bottom-right, then top-left, bottom-right, bottom-left.
    const float uv[12] = {0,0, 1,0, 1,1, 0,0, 1,1, 0,1};
    std::array<float,30> v{};
    for(int k=0;k<6;++k) {
        v[k*5+0]=coords[k*3+0];
        v[k*5+1]=coords[k*3+1];
        v[k*5+2]=coords[k*3+2];
        v[k*5+3]=uv[k*2+0];
        v[k*5+4]=uv[k*2+1];
    }
    out = v;
    return true;
}

}
