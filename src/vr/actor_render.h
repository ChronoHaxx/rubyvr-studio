// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ruby_world.h"
#include "terrain.h"
#include "vr_math.h"
namespace vr::actor_render {
struct Stats {
    int visible=0,unsupported=0,unresolved=0,distant=0;
    bool player=false;
    float player_x=0,player_y=0,player_z=0;
};
void update(const world::Snapshot&,const terrain::Resolved&);
void clear();
void draw(const math::Mat4& view_projection,const math::Mat4& model);
void shutdown();
const Stats& stats();
}
