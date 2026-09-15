// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "indoor_house_assets.h"

namespace vr::indoor_scene {
using Room=overrides::IndoorScope;
// Asset-driven capability, deliberately independent of SDL and native hooks.
// No guessed partial room can take control from Ruby's original presentation.
inline Room describe(const world::Snapshot& s,const overrides::OverrideSet& set) {
    if(!s.valid || s.width<16 || s.height<15 || s.width>143 || s.height>142)return {};
    Room room;
    std::vector<uint8_t> covered(size_t(s.width)*s.height,0);
    for(const auto& p:set.patterns) {
        if(!p.indoor || p.indoor->group!=s.map_group || p.indoor->number!=s.map_number)continue;
        if(set.version<overrides::kIndoorVersion || p.indoor->width!=s.width || p.indoor->height!=s.height ||
           (room.width && room!=*p.indoor))return {};
        room=*p.indoor;
        if(room.wall_front<7 || room.wall_front>=s.height-7 || !p.voxel || p.parts.empty() ||
           p.w<1 || p.w>64 || p.extent<1 || p.extent>64 || p.source.x<7 || p.source.y<7 ||
           p.source.x>s.width-8 || p.source.y>s.height-7 ||
           p.source.x+p.w>s.width-8 || p.source.y+p.extent>s.height-7 ||
           p.ids.size()!=size_t(p.cells()) || p.mask.size()!=p.ids.size())return {};
        for(const auto& part:p.parts)
            if(part.kind!=overrides::PartKind::Box || part.transform.angles.x ||
               part.transform.angles.y || part.transform.angles.z)return {};
        for(int i=0;i<p.cells();++i) {
            const int x=p.source.x+i%p.w,y=p.source.y+i/p.w;
            if(p.mask[i]!=1 || s.metatile_id(x,y)!=p.ids[i] || !p.tiles.count(p.ids[i]) ||
               covered[size_t(y)*s.width+x]++)return {};
        }
        for(const auto& [id,def]:p.tiles)
            if(id>=s.attributes.size() || size_t(id)*8+8>s.metatiles.size() ||
               s.attributes[id]!=def.attr ||
               !std::equal(std::begin(def.entries),std::end(def.entries),s.metatiles.begin()+id*8))return {};
    }
    if(!room.width) {
        // Previously saved v7 house packs keep their accepted geometry.
        if(indoor_house::authored(s,set)) {
            const auto old=indoor_house::room(s.map_group,s.map_number);
            return {s.map_group,s.map_number,old.width,old.height,old.wall_front};
        }
        return {};
    }
    for(int y=7;y<s.height-7;++y)for(int x=7;x<s.width-8;++x)
        if(!covered[size_t(y)*s.width+x])return {};
    // Layer bits are sprite/collision priority, not indoor height. Require the
    // complete, source-guarded flat terrain profile accompanying the furniture.
    const auto it=std::find_if(set.terrain.begin(),set.terrain.end(),[&](const auto& t){
        return t.group==s.map_group && t.number==s.map_number && t.width==s.width && t.height==s.height;});
    if(it==set.terrain.end())return {};
    std::fill(covered.begin(),covered.end(),0);
    for(const auto& c:it->cells) {
        if(c.x<7 || c.y<7 || c.x>=s.width-8 || c.y>=s.height-7 ||
           s.cell(c.x,c.y)!=c.expected || c.surfaces.size()!=1)return {};
        const auto& f=c.surfaces.front();
        if(f.layer!=(c.expected>>12) || f.kind!=overrides::TerrainKind::Ground ||
           f.height || f.thickness || f.rise_x || f.rise_z || f.corner_delta ||
           covered[size_t(c.y)*s.width+c.x]++)return {};
    }
    for(const auto& [id,def]:it->tiles)
        if(id>=s.attributes.size() || size_t(id)*8+8>s.metatiles.size() ||
           s.attributes[id]!=def.attr ||
           !std::equal(std::begin(def.entries),std::end(def.entries),s.metatiles.begin()+id*8))return {};
    for(int y=7;y<s.height-7;++y)for(int x=7;x<s.width-8;++x)
        if(!covered[size_t(y)*s.width+x])return {};
    return room;
}
inline bool body_blocked(const overrides::OverrideSet& set,int group,int number,double x,double z) {
    bool scoped=false;
    for(const auto& p:set.patterns)if(p.indoor && p.indoor->group==group && p.indoor->number==number) {
        scoped=true;
        for(const auto& part:p.parts) {
            const auto& t=part.transform;
            if(t.position.y>=1.25f || t.position.y+t.size.y<=0)continue;
            const double left=p.source.x+t.position.x,right=left+t.size.x;
            const double back=p.source.y+p.extent-.5+t.position.z-t.size.z/2,front=back+t.size.z;
            constexpr double radius=.22;
            if(x+radius>left && x-radius<right && z+radius>back && z-radius<front)return true;
        }
    }
    return !scoped && indoor_house::body_blocked(set,group,number,x,z);
}
} // namespace vr::indoor_scene
