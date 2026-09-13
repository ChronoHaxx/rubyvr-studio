// SPDX-License-Identifier: GPL-3.0-or-later
#include "free_walk.h"
#include <algorithm>
#include <cmath>

namespace vr::free_walk {
Point direction(uint16_t keys,double yaw) {
    if(!std::isfinite(yaw))return {};
    const double x=(!(keys&0x10))-(!(keys&0x20));
    const double z=(!(keys&0x80))-(!(keys&0x40));
    const double length=std::hypot(x,z);
    if(!length)return {};
    const double c=std::cos(yaw),s=std::sin(yaw);
    return {(x*c+z*s)/length,(-x*s+z*c)/length};
}
int facing(Point v) {
    if(!std::isfinite(v.x)||!std::isfinite(v.z)||std::hypot(v.x,v.z)<1e-9)return 0;
    return std::abs(v.x)>std::abs(v.z)?(v.x>0?4:3):(v.z>0?1:2);
}
Step advance(Point p,Point v,double distance,Blocked blocked,void* context) {
    Step out{p};
    if(!std::isfinite(p.x)||!std::isfinite(p.z)||std::abs(p.x)>32760||std::abs(p.z)>32760||
       !std::isfinite(distance)||distance<=0||!blocked)return out;
    const double length=std::hypot(v.x,v.z);
    if(!std::isfinite(length)||length<1e-9)return out;
    distance=std::min(distance,1.0/16); // No large-delta tunnelling or event skips.
    v.x*=distance/length;v.z*=distance/length;
    constexpr double radius=0.22;
    const int old_x=int(std::floor(p.x)),old_z=int(std::floor(p.z));
    auto sweep=[&](bool horizontal,double delta) {
        if(std::abs(delta)<1e-12)return;
        Point candidate=out.position;
        (horizontal?candidate.x:candidate.z)+=delta;
        const int dir=horizontal?(delta>0?4:3):(delta>0?1:2);
        const int edge=int(std::floor((horizontal?candidate.x:candidate.z)+(delta>0?radius:-radius)));
        const double across=horizontal?candidate.z:candidate.x;
        bool hit=false;
        for(int side=int(std::floor(across-radius));side<=int(std::floor(across+radius));++side) {
            const int x=horizontal?edge:side,z=horizontal?side:edge;
            if(x==old_x && z==old_z)continue;
            if(blocked(x,z,dir,context)){hit=true;break;}
        }
        if(hit){out.blocked=true;return;}
        out.position=candidate;
        out.crossed=int(std::floor(candidate.x))!=old_x || int(std::floor(candidate.z))!=old_z;
    };
    // Resolve both components of this bounded step. Dropping the second axis
    // at a tile boundary bends a straight diagonal and jerks its follow camera.
    // The caller notifies the game once, at the resulting cell, before another
    // step; body checks still prevent cutting through a blocked corner.
    const bool x_first=std::abs(v.x)>=std::abs(v.z);
    sweep(x_first,x_first?v.x:v.z);
    sweep(!x_first,x_first?v.z:v.x);
    return out;
}
}
