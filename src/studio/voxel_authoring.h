#pragma once

// Pixel-space authoring operations shared by the widgets and their checks.
// These edit recipes only; diorama.cpp remains the only mesh implementation.
#include <algorithm>
#include <string>
#include <vector>
#include "cutout.h"
#include "overrides.h"

namespace studio::voxel_edit {
using namespace vr::overrides;

inline ArtRegion region(const Pattern& p, const Part& part) {
    return part.art_region == ArtRegion{} ? ArtRegion{0,0,p.w*16,p.extent*16} : part.art_region;
}
inline bool inside(const ArtRegion& r, int x, int y) {
    return x>=r[0] && y>=r[1] && x<r[0]+r[2] && y<r[1]+r[3];
}
inline bool member(const Pattern& p,int x,int y) {
    return x>=0 && y>=0 && x<p.w*16 && y<p.extent*16 && p.mask[size_t(y/16)*p.w+x/16];
}
inline bool valid_region(const Pattern& p,const ArtRegion& r) {
    return r[0]>=0 && r[1]>=0 && r[2]>0 && r[3]>0 && r[0]+r[2]<=p.w*16 && r[1]+r[3]<=p.extent*16;
}
inline bool object_pixel(const Pattern& p,const vr::cutout::Art& art,int x,int y) {
    return member(p,x,y) && p.cutout && p.cutout->opacity[size_t(y)*art.w+x] && (art.pixels[size_t(y)*art.w+x].rgba>>24);
}
inline void ownership_from_mask(Pattern& p,const vr::cutout::Art& art) {
    if(!p.cutout) p.cutout=vr::cutout::original_opacity(art);
    p.voxel=Voxel{16,Cutout{art.w,art.h,std::vector<uint8_t>(art.pixels.size())},
                       Cutout{art.w,art.h,std::vector<uint8_t>(art.pixels.size())}};
    for(int y=0;y<art.h;++y) for(int x=0;x<art.w;++x)
        p.voxel->ground.opacity[size_t(y)*art.w+x]=member(p,x,y) && !p.cutout->opacity[size_t(y)*art.w+x];
}
inline bool material_region(const Pattern& p,const vr::cutout::Art& art,const ArtRegion& r) {
    if(!p.voxel || !valid_region(p,r)) return false;
    for(int y=r[1];y<r[1]+r[3];++y) for(int x=r[0];x<r[0]+r[2];++x)
        if(!object_pixel(p,art,x,y)) return false;
    return true;
}
inline Cutout local_mask(const Pattern& p,const Part& part) {
    if(part.local_mask) return *part.local_mask;
    const auto r=region(p,part);
    return {r[2],r[3],std::vector<uint8_t>(size_t(r[2])*r[3],1)};
}
inline float front_pixels(const Part& part) {
    const auto normal=vr::part_geometry::rotate({0,0,1},part.transform.angles);
    return (vr::part_geometry::dot(part.transform.position,normal)+part.transform.size.z/2)*16;
}
inline void set_front(Part& part,float pixels) {
    const auto normal=vr::part_geometry::rotate({0,0,1},part.transform.angles);
    part.transform.position=part.transform.position+normal*((pixels-front_pixels(part))/16);
}
inline void set_depth(Part& part,int pixels) {
    const float depth=std::clamp(pixels,1,1024)/16.f;
    const auto normal=vr::part_geometry::rotate({0,0,1},part.transform.angles);
    part.transform.position=part.transform.position+normal*((part.transform.size.z-depth)/2);
    part.transform.size.z=depth;
}

// Tools: 0 selection, 1 object, 2 ground, 3 shadow, 4 local erase, 5 local restore.
// Object/ground/shadow are an exact partition, including transparent source pixels.
inline bool paint(Pattern& p,const vr::cutout::Art& art,const std::string& part_id,int tool,int x,int y) {
    if(!p.voxel || !member(p,x,y)) return false;
    const size_t at=size_t(y)*art.w+x;
    if(tool>=1 && tool<=3) {
        if(tool==1 && !(art.pixels[at].rgba>>24)) return false;
        const uint8_t value=tool==1;
        if(p.cutout->opacity[at]==value && p.voxel->ground.opacity[at]==(tool==2) && p.voxel->shadow.opacity[at]==(tool==3)) return false;
        p.cutout->opacity[at]=value;
        p.voxel->ground.opacity[at]=tool==2;
        p.voxel->shadow.opacity[at]=tool==3;
        return true;
    }
    for(auto& part:p.parts) if(part.id==part_id && part.kind==PartKind::Billboard && (tool==4 || tool==5)) {
        const auto r=region(p,part);
        if(!inside(r,x,y) || !object_pixel(p,art,x,y)) return false;
        if(!part.local_mask) part.local_mask=local_mask(p,part);
        auto& value=part.local_mask->opacity[size_t(y-r[1])*r[2]+x-r[0]];
        const uint8_t next=tool==5;
        if(value==next) return false;
        value=next;return true;
    }
    return false;
}
inline void flood(Pattern& p,const vr::cutout::Art& art,const std::string& part_id,int tool,int x,int y) {
    if(!member(p,x,y)) return;
    const uint32_t color=art.pixels[size_t(y)*art.w+x].rgba;
    std::vector<uint8_t> seen(art.pixels.size());
    std::vector<int> stack{y*art.w+x};
    while(!stack.empty()) {
        const int at=stack.back();stack.pop_back();
        if(seen[size_t(at)]) continue;
        seen[size_t(at)]=1;
        const int px=at%art.w,py=at/art.w;
        if(!member(p,px,py) || art.pixels[size_t(at)].rgba!=color) continue;
        paint(p,art,part_id,tool,px,py);
        if(px) stack.push_back(at-1);
        if(px+1<art.w) stack.push_back(at+1);
        if(py) stack.push_back(at-art.w);
        if(py+1<art.h) stack.push_back(at+art.w);
    }
}

inline bool make_part(const Pattern& p,const vr::cutout::Art& art,PartKind kind,const ArtRegion& r,
                      const std::string& id,Part* out,std::string* error) {
    if(!p.voxel || !valid_region(p,r)) { *error="Drag a rectangle on the source drawing first.";return false; }
    Part part;part.id=id;part.kind=kind;
    part.name=kind==PartKind::Billboard?"Relief":kind==PartKind::Wedge?"Roof":"Box";
    const float depth=kind==PartKind::Billboard?2.f:16.f;
    part.transform.position={r[0]/16.f,(art.h-r[1]-r[3])/16.f,-depth/32.f};
    part.transform.size={r[2]/16.f,r[3]/16.f,depth/16.f};
    if(kind==PartKind::Wedge) {part.wedge_axis=2;part.wedge_direction=-1;}
    if(kind==PartKind::Billboard) {
        part.art_region=r;part.local_mask=Cutout{r[2],r[3],std::vector<uint8_t>(size_t(r[2])*r[3])};
        bool any=false;
        for(int y=0;y<r[3];++y) for(int x=0;x<r[2];++x)
            any|=part.local_mask->opacity[size_t(y)*r[2]+x]=object_pixel(p,art,x+r[0],y+r[1]);
        if(!any) { *error="The selection has no object pixels. Mark the object in MASK first.";return false; }
    } else {
        if(!material_region(p,art,r)) { *error="For a box or roof, select a rectangle containing only object pixels. Its art will repeat on the faces.";return false; }
        part.surfaces.assign(6,Surface{r});
    }
    *out=std::move(part);return true;
}

// Split keeps every source pixel in exactly one relief. The new part starts at
// precisely the old transform, including rotations; depth can then be changed.
inline bool split(Pattern& p,const vr::cutout::Art& art,const std::string& selected,const ArtRegion& selection,
                  const std::string& id,std::string* error) {
    if(p.parts.size()>=64) { *error="This model already has 64 parts.";return false; }
    auto it=std::find_if(p.parts.begin(),p.parts.end(),[&](const Part& part){return part.id==selected;});
    if(it==p.parts.end() || it->kind!=PartKind::Billboard || !valid_region(p,selection)) {
        *error="Select a relief part, then drag a source rectangle to split.";return false;
    }
    const auto r=region(p,*it);
    const int x=std::max(r[0],selection[0]),y=std::max(r[1],selection[1]);
    const int w=std::min(r[0]+r[2],selection[0]+selection[2])-x;
    const int h=std::min(r[1]+r[3],selection[1]+selection[3])-y;
    if(w<=0 || h<=0) { *error="The rectangle must overlap the selected relief's source art.";return false; }
    Part remainder=*it,part=*it;
    remainder.local_mask=local_mask(p,*it);
    part.id=id;part.name="Split relief";part.art_region={x,y,w,h};
    part.local_mask=Cutout{w,h,std::vector<uint8_t>(size_t(w)*h)};
    bool any=false;
    for(int yy=0;yy<h;++yy) for(int xx=0;xx<w;++xx) {
        auto& from=remainder.local_mask->opacity[size_t(y+yy-r[1])*r[2]+x+xx-r[0]];
        const bool take=from && object_pixel(p,art,x+xx,y+yy);
        part.local_mask->opacity[size_t(yy)*w+xx]=take;any|=take;
        if(take) from=0;
    }
    if(!any) { *error="No visible pixels of this relief are inside the rectangle.";return false; }
    part.transform.position=vr::part_geometry::to_group({(x-r[0])/16.f,(r[1]+r[3]-y-h)/16.f,0},it->transform);
    part.transform.size.x=w/16.f;part.transform.size.y=h/16.f;
    *it=std::move(remainder);p.parts.push_back(std::move(part));return true;
}
} // namespace studio::voxel_edit
