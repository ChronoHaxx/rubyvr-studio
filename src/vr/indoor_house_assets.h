// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "indoor_house.h"
#include "overrides.h"
#include <algorithm>

namespace vr::indoor_house {
// Missing, partial or changed room recipes keep the original game usable.
// Same structural cell/tileset guards as the normal pattern matcher, restricted
// to the two complete room strips. No per-frame search over unrelated maps.
inline bool authored(const world::Snapshot& s,const overrides::OverrideSet& set) {
    if(!s.valid || !supports(s.map_group,s.map_number,s.width,s.height))return false;
    for(int x0:{0,4}) {
        const auto id="indoor-may-"+std::to_string(s.map_number-1)+"f-"+std::to_string(x0);
        auto it=std::find_if(set.patterns.begin(),set.patterns.end(),[&](const auto& p){return p.id==id;});
        if(it==set.patterns.end())return false;
        const auto& p=*it;
        if(!p.voxel || p.parts.empty() || p.w!=(x0?s.width-19:4) || p.extent!=s.height-14 ||
           p.ids.size()!=size_t(p.cells()) || p.mask.size()!=p.ids.size())return false;
        for(const auto& part:p.parts)
            if(part.kind!=overrides::PartKind::Box || part.transform.angles.x ||
               part.transform.angles.y || part.transform.angles.z)return false;
        for(int i=0;i<p.cells();++i)
            if(p.mask[i]!=1 || s.metatile_id(7+x0+i%p.w,7+i/p.w)!=p.ids[i])return false;
        for(const auto& [tile,def]:p.tiles) {
            if(tile>=s.attributes.size() || size_t(tile)*8+8>s.metatiles.size() ||
               s.attributes[tile]!=def.attr ||
               !std::equal(std::begin(def.entries),std::end(def.entries),s.metatiles.begin()+tile*8))return false;
        }
    }
    return true;
}
// The pilot's unrotated boxes also bound furniture collision in free modes.
// This uses the actual loaded parts, so editing a chair/bed moves its collider.
// Native tile/NPC rules still run. The stair well is left to native warp logic.
inline bool body_blocked(const overrides::OverrideSet& set,int group,int number,double x,double z) {
    const auto r=room(group,number);if(!r.width)return false;
    const double stair0=number==2?8.75:8.0,stair1=number==2?10.25:9.0;
    for(int x0:{0,4}) {
        const auto id="indoor-may-"+std::to_string(number-1)+"f-"+std::to_string(x0);
        for(const auto& p:set.patterns)if(p.id==id)for(const auto& part:p.parts) {
            const auto& t=part.transform;
            if(t.position.y>=1.25f || t.position.y+t.size.y<=0)continue;
            const double left=7+x0+t.position.x,right=left+t.size.x;
            const double back=7+p.extent-.5+t.position.z-t.size.z/2,front=back+t.size.z;
            if(left>=stair0 && right<=stair1 && front<=r.wall_front)continue;
            constexpr double radius=.22;
            if(x+radius>left && x-radius<right && z+radius>back && z-radius<front)return true;
        }
    }
    return false;
}
} // namespace vr::indoor_house
