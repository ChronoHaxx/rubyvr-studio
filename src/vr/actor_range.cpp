// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor_range.h"
#include <algorithm>

namespace vr::actor {
void RangeCache::reset(){known_.clear();distant_.clear();group_=number_=-1;}
void RangeCache::update(const world::Snapshot& s) {
    distant_.clear();
    if(!s.valid || !s.has_map_identity() || !s.actor_range_safe || s.actor_templates.size()>64) {reset();return;}
    if(group_!=s.map_group || number_!=s.map_number || epoch_!=s.actor_epoch || layout_!=s.layout_ptr) {
        reset();group_=s.map_group;number_=s.map_number;epoch_=s.actor_epoch;layout_=s.layout_ptr;
    }
    std::array<const world::ActorTemplate*,256> definitions{};
    for(const auto& t:s.actor_templates) {
        const auto id=t.bytes[0];
        if(!id || id==255 || definitions[id]){reset();return;}
        definitions[id]=&t;
    }
    const auto inside=[&](int x,int y) {
        // Ruby TrySpawnObjectEvents / RemoveObjectEventIfOutsideView use this
        // rectangle in backup coordinates, including BOTH event positions.
        return x>=s.view_x-2 && x<=s.view_x+17 && y>=s.view_y && y<=s.view_y+16;
    };
    for(auto it=known_.begin();it!=known_.end();) {
        const auto id=it->object.local_id;
        const auto* def=definitions[id];
        bool live=false;
        for(const auto& o:s.objects)
            live|=o.active && !o.is_player && o.local_id==id && o.map_group==s.map_group && o.map_number==s.map_number;
        const auto& o=it->object;
        // A missing event inside its spawn area is a removal, not distance
        // culling. Live slots always replace old poses, including hidden ones.
        if(!def || def->hidden || def->bytes!=it->definition.bytes || live ||
           inside(o.x,o.y) || inside(o.initial_x,o.initial_y))it=known_.erase(it);
        else {distant_.push_back(*it);++it;}
    }
    for(int i=0;i<world::kObjectEventCount;++i) {
        const auto& o=s.objects[i];const auto& source=s.actor_sources[i];
        const auto* def=definitions[o.local_id];
        if(!o.active || o.is_player || o.invisible || o.map_group!=s.map_group || o.map_number!=s.map_number ||
           !def || def->hidden || def->bytes[2]!=0 || def->bytes[1]!=o.graphics_id ||
           source.world_facing<1 || source.world_facing>4 || source.fixed_pose)continue;
        Remembered r;r.definition=*def;r.object=o;r.facing=source.world_facing;
        r.frame=decode(source,s.obj_tiles,s.obj_palette,s.obj_mapping_1d);
        if(r.frame.status!=Status::Visible)continue;
        bool valid=true;
        for(uint8_t d=1;d<=4;++d){r.directions[d-1]=decode_direction(source,s.obj_tiles,s.obj_palette,s.obj_mapping_1d,d);valid&=r.directions[d-1].status==Status::Visible;}
        if(!valid)continue;
        r.position=position(r.frame,s.view_x,s.view_y,s.view_base_x,s.view_base_y,
            s.actor_offset_x,s.actor_offset_y,o.x,o.y);
        if(known_.size()<64)known_.push_back(std::move(r));
    }
}
}
