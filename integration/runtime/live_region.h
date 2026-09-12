// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ruby_world.h"

namespace vr::world::live {
using SourceLoader = bool (*)(int group,int number,Snapshot&);
struct RegionEntry { Snapshot source; int x=0,z=0,depth=0; };

// Bounded, display-independent neighbourhood. Backup coordinates and real
// connection offsets are used throughout; a crossing never recentres the world.
class Neighbourhood {
public:
    static constexpr size_t max_maps=3, max_cells=24000;
    bool refresh(const Snapshot& live,SourceLoader);
    const RegionEntry* find(int group,int number) const;
    const std::vector<RegionEntry>& maps() const {return maps_;}
    uint64_t revision() const {return revision_;}
    uint64_t space() const {return space_;} // changes only on disconnected warp
    size_t omitted() const {return omitted_;}
private:
    std::vector<RegionEntry> maps_;
    int group_=-1,number_=-1;
    int sector_x_=0,sector_z_=0;
    uint64_t revision_=0,space_=0;
    size_t omitted_=0;
};
} // namespace vr::world::live
