#pragma once

#include <cstdint>
#include <vector>
#include "ruby_world.h"

namespace studio {

// A room image, not a renderer for authored geometry. Release the texture before
// destroying the GL context; its CPU pixels remain useful to future mask tools.
struct Room2D {
    int w = 0, h = 0;
    unsigned texture = 0;
    std::vector<uint32_t> rgba;
    bool build(const vr::world::Snapshot& snapshot);
    bool upload();
    void release();
};

} // namespace studio
