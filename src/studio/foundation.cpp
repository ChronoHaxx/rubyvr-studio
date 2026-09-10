#include "foundation.h"
#include "diorama.h"
#include "terrain.h"
#include <algorithm>
#include <cmath>

namespace studio::foundation {
bool level(const vr::world::Snapshot& s,const vr::overrides::OverrideSet& original,
           const vr::overrides::Match& selected,vr::overrides::OverrideSet* out,Result* result) {
    using namespace vr;
    using namespace overrides;
    if(!out || !result) return false;
    *result={};
    auto fail=[&](const char* message){result->message=message;return false;};
    if(!s.has_map_identity() || !s.valid_connections() || !terrain::valid(original.terrain))
        return fail("Map identity or terrain is unresolved. Review its source first.");
    if(selected.pattern<0 || size_t(selected.pattern)>=original.patterns.size())
        return fail("Select an applied model instance in this map.");
    const auto& p=original.patterns[selected.pattern];
    if(!p.voxel || p.follow_ground || p.parts.empty())
        return fail("Choose a rigid voxel model. Flexible grass follows the ground.");
    const auto claims=resolve(s,original);
    const auto same=[&](const Match& c){return c.pattern==selected.pattern && c.x==selected.x && c.y==selected.y;};
    if(std::none_of(claims.accepted.begin(),claims.accepted.end(),same))
        return fail("This model instance no longer matches its source.");
    std::vector<diorama::AuthoredVertex> mesh;
    if(!diorama::inspect_authored_mesh(s,p,&mesh) || mesh.empty()) return fail("The model has no valid solid base.");
    float bottom=mesh.front().position.y;
    for(const auto& v:mesh) bottom=std::min(bottom,v.position.y);
    if(std::abs(bottom)>1e-5f) return fail("Set the model base to 0 px in Model before levelling its ground.");
    float x0=1e6f,x1=-1e6f,z0=1e6f,z1=-1e6f;
    for(size_t i=0;i+2<mesh.size();i+=3) {
        const auto a=mesh[i].position,b=mesh[i+1].position,c=mesh[i+2].position;
        if(std::abs(a.y)>1e-5f || std::abs(b.y)>1e-5f || std::abs(c.y)>1e-5f ||
           std::abs((b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x))<1e-6f) continue;
        for(const auto v:{a,b,c}) {x0=std::min(x0,v.x);x1=std::max(x1,v.x);z0=std::min(z0,v.z);z1=std::max(z1,v.z);}
    }
    if(x0>=x1 || z0>=z1) return fail("The model needs horizontal base faces to define a foundation.");
    const int ax=selected.x+p.w/2,ay=selected.y+p.extent-1;
    result->x0=std::min(ax,selected.x+int(std::floor(x0+1e-5f)));
    result->x1=std::max(ax,selected.x+int(std::ceil(x1-1e-5f))-1);
    result->y0=std::min(ay,int(std::floor(selected.y+p.extent-.5f+z0+1e-5f)));
    result->y1=std::max(ay,int(std::ceil(selected.y+p.extent-.5f+z1-1e-5f))-1);
    const int count=(result->x1-result->x0+1)*(result->y1-result->y0+1);
    if(count<1 || count>1024) return fail("This foundation is too large; author its terrain as a region.");
    auto inside=[&](int x,int y){return x>=result->x0 && x<=result->x1 && y>=result->y0 && y<=result->y1;};
    for(const auto& c:claims.accepted) if(!same(c)) {
        const auto& other=original.patterns[c.pattern];
        if(other.ground_only() || other.follow_ground || (!other.cutout && other.parts.empty())) continue;
        for(int i=0;i<other.cells();++i) if(other.mask[i] && inside(c.x+i%other.w,c.y+i/other.w))
            return fail("The foundation overlaps another rigid object. Author a shared region in Terrain instead.");
    }
    const auto land=terrain::resolve(s,original.terrain);
    for(int y=7;y<s.height-7;++y) for(int x=7;x<s.width-8;++x)
        if(land.query(x,y,s.elevation(x,y)).status==terrain::Status::SourceMismatch)
            return fail("Terrain source changed. Review affected cells before levelling a foundation.");
    int authored=0;
    for(int y=result->y0;y<=result->y1;++y) for(int x=result->x0;x<=result->x1;++x) {
        if(!terrain::primary_cell(s,x,y)) return fail("The foundation crosses the map border. Edit its owning region first.");
        const auto* cell=land.cell(x,y);
        if(!cell) continue;
        if(cell->surfaces.size()!=1 || cell->surfaces.front().kind!=TerrainKind::Ground)
            return fail("A foundation needs single ground surfaces; water and decks are preserved.");
        if(!land.query(x,y,s.elevation(x,y)).resolved()) return fail("The ground layer under this model is unresolved.");
        result->height=std::max(result->height,terrain::maximum_height(cell->surfaces.front()));
        ++authored;
    }
    if(!authored) {result->message="No authored slope under this model. The original floor is already flat.";*out=original;return true;}
    if(authored!=count) return fail("Author ground under the whole foundation in Terrain first.");
    auto candidate=original;
    auto map=std::find_if(candidate.terrain.begin(),candidate.terrain.end(),[&](const auto& m){return m.group==s.map_group && m.number==s.map_number;});
    if(map==candidate.terrain.end()) return fail("Edit this terrain in its owning map.");
    for(auto& c:map->cells) if(inside(c.x,c.y)) {
        auto& surface=c.surfaces.front();
        auto flat=surface;flat.height=flat.thickness=result->height;flat.rise_x=flat.rise_z=flat.corner_delta=0;
        result->changed_cells+=flat!=surface;surface=flat;
    }
    if(!terrain::valid(candidate.terrain)) return fail("The foundation exceeds terrain limits. No changes applied.");
    candidate.version=kTerrainVersion;
    result->message=result->changed_cells?"Foundation levelled at "+std::to_string(result->height)+" px. Ctrl+Z restores the slope; Ctrl+S saves.":"This foundation is already level.";
    *out=std::move(candidate);return true;
}
}
