// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_region.h"
#include <algorithm>
#include <tuple>

namespace vr::world::live {
namespace {
bool valid(const Snapshot& s) {
    return s.valid && s.has_map_identity() && s.valid_connections() &&
        s.width>15 && s.height>14 && s.width<=512 && s.height<=512 &&
        s.grid.size()==size_t(s.width)*s.height;
}
bool same_geometry(const Snapshot& a,const Snapshot& b) {
    return a.layout_ptr==b.layout_ptr && a.width==b.width && a.height==b.height &&
        a.grid==b.grid && a.metatiles==b.metatiles && a.attributes==b.attributes &&
        a.connections==b.connections;
}
bool overlap(const RegionEntry& a,const RegionEntry& b) {
    return std::max(a.x,b.x)<std::min(a.x+a.source.width-15,b.x+b.source.width-15) &&
        std::max(a.z,b.z)<std::min(a.z+a.source.height-14,b.z+b.source.height-14);
}
}
const RegionEntry* Neighbourhood::find(int group,int number) const {
    for(const auto& m:maps_) if(m.source.map_group==group && m.source.map_number==number) return &m;
    return nullptr;
}
bool Neighbourhood::refresh(const Snapshot& live,SourceLoader load) {
    if(!valid(live)) return false; // Menus do not evict the field cache.
    const auto* previous=find(live.map_group,live.map_number);
    const auto* player=live.player_index>=0 && live.player_index<kObjectEventCount?&live.objects[live.player_index]:nullptr;
    const int px=player?player->x:int(live.camera_cell_x()),pz=player?player->y:int(live.camera_cell_y());
    const int sector_x=px/4,sector_z=pz/4;
    const bool same_map=group_==live.map_group && number_==live.map_number;
    const bool same=previous && same_map && same_geometry(previous->source,live);
    if(same && sector_x_==sector_x && sector_z_==sector_z) return false;
    const int x=previous?previous->x:0,z=previous?previous->z:0;
    if(!previous) ++space_;
    std::vector<RegionEntry> next{{live,x,z,0}};
    size_t cells=live.grid.size();omitted_=0;
    for(size_t i=0;i<next.size();++i) {
        // Copy before pushing: vector growth must not invalidate the edge list.
        auto edges=next[i].source.connections;
        const int ox=next[i].x,oz=next[i].z,depth=next[i].depth;
        if(depth>=2) continue;
        auto priority=[&](const ConnectionSlice& c) {
            // Keep the map just crossed during handover. Afterward select by
            // distance to the player's four-cell sector, independent of yaw.
            if(!same_map && c.group==group_ && c.number==number_) return -1;
            const int left=ox+c.x-c.source_x+7,top=oz+c.y-c.source_y+7;
            const int wx=x+sector_x*4+2,wz=z+sector_z*4+2;
            const int dx=std::max({left-wx,0,wx-(left+c.width-15)});
            const int dz=std::max({top-wz,0,wz-(top+c.height-14)});
            return dx+dz;
        };
        std::stable_sort(edges.begin(),edges.end(),[&](const auto& a,const auto& b){return priority(a)<priority(b);});
        for(const auto& c:edges) {
            const int nx=ox+c.x-c.source_x,nz=oz+c.y-c.source_y;
            const auto found=std::find_if(next.begin(),next.end(),[&](const auto& m){
                return m.source.map_group==c.group && m.source.map_number==c.number;});
            if(found!=next.end()) {if(found->x!=nx || found->z!=nz) ++omitted_;continue;}
            if(next.size()>=max_maps || nx < -8192 || nx > 8192 || nz < -8192 || nz > 8192) {++omitted_;continue;}
            RegionEntry e;e.x=nx;e.z=nz;e.depth=depth+1;
            if(const auto* cached=find(c.group,c.number);previous && cached) e.source=cached->source;
            else if(!load || !load(c.group,c.number,e.source)) {++omitted_;continue;}
            if(!valid(e.source) || e.source.map_group!=c.group || e.source.map_number!=c.number ||
               e.source.width!=c.width || e.source.height!=c.height ||
               cells+e.source.grid.size()>max_cells ||
               std::any_of(next.begin(),next.end(),[&](const auto& m){return overlap(m,e);})) {++omitted_;continue;}
            cells+=e.source.grid.size();next.push_back(std::move(e));
        }
    }
    // Border ownership does not swap just because the player crossed an edge.
    std::sort(next.begin(),next.end(),[](const auto& a,const auto& b){
        return std::tie(a.source.map_group,a.source.map_number)<std::tie(b.source.map_group,b.source.map_number);});
    sector_x_=sector_x;sector_z_=sector_z;
    bool changed=!same || next.size()!=maps_.size();
    if(!changed) for(size_t i=0;i<next.size();++i)
        changed|=next[i].source.map_group!=maps_[i].source.map_group || next[i].source.map_number!=maps_[i].source.map_number ||
            next[i].x!=maps_[i].x || next[i].z!=maps_[i].z;
    if(!changed) return false;
    maps_=std::move(next);group_=live.map_group;number_=live.map_number;++revision_;return true;
}
} // namespace vr::world::live
