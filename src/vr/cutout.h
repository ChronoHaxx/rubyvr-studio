#pragma once
#include "overrides.h"

namespace vr::cutout {
struct Pixel {
    world::TileEntry tile{};
    uint8_t tx = 0, ty = 0;
    uint32_t rgba = 0;
};
struct Art {
    int w = 0, h = 0;
    std::vector<Pixel> pixels;
};
// Composite lower then upper, retaining the exact winning texel for meshing.
// Nonmember cells and transparent source pixels stay transparent.
bool compose(const world::Snapshot&, const overrides::Pattern&, Art*);
overrides::Cutout original_opacity(const Art&);
bool valid(const overrides::Pattern&);
bool decode(int w, int h, int first, const std::vector<uint32_t>& runs, overrides::Cutout*);
std::vector<uint32_t> encode(const overrides::Cutout&);
} // namespace vr::cutout
