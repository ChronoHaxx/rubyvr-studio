// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ruby_world.h"

namespace vr::actor {
struct Remembered {
    world::ActorTemplate definition;
    world::ObjectSnapshot object;
    Frame frame;
    std::array<Frame,4> directions;
    Position position;
    uint8_t facing=0;
};
// Consumer-owned, bounded presentation memory. No guest pointers or writes;
// only the last observed pose, not an off-screen NPC simulation.
class RangeCache {
public:
    void update(const world::Snapshot&);
    void reset();
    const std::vector<Remembered>& distant() const {return distant_;}
private:
    int group_=-1,number_=-1;
    uint64_t epoch_=0;
    uint32_t layout_=0;
    std::vector<Remembered> known_,distant_;
};
}
