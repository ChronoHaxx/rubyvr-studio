// SPDX-License-Identifier: GPL-3.0-or-later
// Pinned local source acceptance through the production reader/query/mesher.
#include "snapshot_build.h"
#include "terrain.h"
#include "diorama.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

int bridge_source_test(const char* source,const char* pack) {
    using namespace vr;
    studio::Decomp decomp;
    overrides::OverrideSet set;
    world::Snapshot route,town;
    auto fail=[](const char* reason) {std::fprintf(stderr,"[terrain-bridge-test] FAIL: %s\n",reason);return 1;};
    if(!decomp.open(source) || !overrides::load(pack,&set) || !terrain::valid(set.terrain) ||
       !studio::build_snapshot(decomp,"MAP_ROUTE104",&route) ||
       !studio::build_snapshot(decomp,"MAP_PETALBURG_CITY",&town)) return fail("source/pack load");
    if(route.width!=55 || route.height!=94) return fail("Route 104 dimensions");
    const auto r=terrain::resolve(route,set.terrain),t=terrain::resolve(town,set.terrain);
    auto deck=[](int x,int y) {
        return ((x==24 || x==25) && y>=9 && y<=16) ||
               ((x==30 || x==31) && y>=15 && y<=21) ||
               (x>=26 && x<=29 && (y==15 || y==16));
    };
    auto pond=[](int x,int y) {
        return y>=9 && y<=21 && x<38 && x>=(y<=11?20:y==12?16:y<=14?14:13);
    };
    auto height=[](const terrain::Resolved& terrain,int x,int y,int layer,float value,float u=.5f,float v=.5f) {
        const auto h=terrain.query(x,y,layer,u,v);
        return h.status==terrain::Status::Authored && h.pixels==value;
    };
    size_t decks=0,waters=0;
    for(int y=0;y<80;++y) for(int x=0;x<40;++x) {
        const auto* c=r.cell(x+7,y+7);
        if(!c || c->expected!=route.cell(x+7,y+7)) return fail("missing/mismatched primary terrain");
        const bool is_deck=deck(x,y),is_pond=pond(x,y);
        if(c->surfaces.size()!=(is_deck?2u:1u)) return fail("surface count");
        const auto& first=c->surfaces[0];
        if(is_pond) {
            ++waters;
            if(first.kind!=terrain::TerrainKind::Water || first.height!=8 || first.thickness ||
               first.layer!=1 || first.top!=(is_deck?520:int(route.metatile_id(x+7,y+7))))
                return fail("water level/material/layer");
        }
        else if(first.kind!=terrain::TerrainKind::Ground || first.height!=16 || first.thickness!=16)
            return fail("outside context changed");
        if(is_deck) {
            ++decks;
            const auto& d=c->surfaces[1];
            if(d.kind!=terrain::TerrainKind::Deck || d.layer!=3 || d.height!=16 || d.thickness!=4 ||
               d.top!=route.metatile_id(x+7,y+7) || d.side!=836 || c->underlay!=520)
                return fail("deck shape/material/layer");
            for(int layer:{-1,0,15}) if(r.query(x+7,y+7,layer).status!=terrain::Status::Unresolved)
                return fail("ambiguous layer resolved incorrectly");
        }
        for(float u:{0.f,.5f,1.f}) for(float v:{0.f,.5f,1.f}) {
            if(!height(r,x+7,y+7,route.elevation(x+7,y+7),is_pond&&!is_deck?8.f:16.f,u,v))
                return fail("primary layer query");
            if(is_deck && !height(r,x+7,y+7,1,8.f,u,v)) return fail("water under deck query");
        }
    }
    if(decks!=38 || waters!=299) return fail("reviewed footprint count");
    // Both two-cell-wide entrances meet ordinary land, without raised approach tiles.
    for(int x:{24,25}) for(float u:{0.f,.5f,1.f})
        if(!height(r,x+7,15,3,16.f,u,1.f) || !height(r,x+7,16,3,16.f,u,0.f)) return fail("north bank contact");
    for(int x:{30,31}) for(float u:{0.f,.5f,1.f})
        if(!height(r,x+7,28,3,16.f,u,1.f) || !height(r,x+7,29,3,16.f,u,0.f)) return fail("south bank contact");
    // Different-atlas padding remains explicitly unresolved in the single-map
    // view. Compare canonical owner bodies, as the connected renderer does.
    const auto* connection=static_cast<const world::ConnectionSlice*>(nullptr);
    for(const auto& c:route.connections) if(c.group==town.map_group && c.number==town.map_number) connection=&c;
    if(!connection || connection->compatible_art) return fail("expected Rustboro/Petalburg atlas boundary");
    size_t joins=0;
    for(int y=0;y<town.height-14;++y) {
        for(float v:{0.f,.5f,1.f})
            if(!height(r,46,y+57,route.elevation(46,y+57),16,1,v) ||
               !height(t,7,y+7,town.elevation(7,y+7),16,0,v)) return fail("Petalburg owner height seam");
        ++joins;
    }
    // Inspect actual shared mesh, including the closed-base connected path.
    // Empty patterns isolate terrain; accepted scenery is checked by the GUI suite.
    auto terrain_only=set;terrain_only.patterns.clear();
    std::vector<diorama::RegionMesh> meshes;diorama::RegionStats stats;std::string error;
    if(!diorama::inspect_region_mesh({{&route,0,0},{&town,40,50}},terrain_only,&meshes,&stats,&error) ||
       meshes.size()!=2 || stats.terrain_rejected) return fail("connected production mesh");
    std::set<std::pair<int,int>> water_faces,deck_faces,bottom_faces;
    size_t triangles=0;
    for(const auto& mesh:meshes) for(size_t i=0;i<mesh.stats.flat_vertices;i+=3) {
        const auto& a=mesh.vertices[i].position;
        const auto& b=mesh.vertices[i+1].position;
        const auto& c=mesh.vertices[i+2].position;
        if(a.x==47.f && b.x==47.f && c.x==47.f &&
           std::min({a.z,b.z,c.z})<57+joins && std::max({a.z,b.z,c.z})>57 &&
           (a.y!=b.y || a.y!=c.y)) return fail("buried wall at different-atlas seam");
        const float area=(b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x);
        if(std::abs(area)<1e-6f) continue;
        const int x=int(std::floor((a.x+b.x+c.x)/3))-7,y=int(std::floor((a.z+b.z+c.z)/3))-7;
        if(!pond(x,y)) continue;
        if(std::abs(a.y-b.y)>1e-5f || std::abs(a.y-c.y)>1e-5f) return fail("graded pond/deck face");
        const std::pair<int,int> pos{x,y};
        int material=-1;
        if(a.y>=.5f-1e-5f && a.y<=.504f) {
            water_faces.insert(pos);material=deck(x,y)?520:route.metatile_id(x+7,y+7);
        } else if(deck(x,y) && a.y>=1.f-1e-5f && a.y<=1.004f) {
            deck_faces.insert(pos);material=route.metatile_id(x+7,y+7);
        } else if(deck(x,y) && a.y>=.746f && a.y<=.75f+1e-5f) {
            // The existing mesher uses the authored edge material underneath.
            bottom_faces.insert(pos);material=836;
        } else return fail("extra/misplaced solid or water bottom in pond");
        for(size_t k=i;k<i+3;++k) {
            const auto& vertex=mesh.vertices[k];bool found=false;
            for(int entry=0;entry<8;++entry) {
                const auto tile=world::unpack_tile_entry(route.metatiles[material*8+entry]);
                found|=vertex.tile==tile.index && vertex.palette==tile.palette;
            }
            if(!found || vertex.u<0 || vertex.u>1 || vertex.v<0 || vertex.v>1) {
                std::fprintf(stderr,"cell %d,%d h=%g material=%d tile=%u palette=%u uv=%g,%g found=%d\n",x,y,a.y,material,vertex.tile,vertex.palette,vertex.u,vertex.v,int(found));
                return fail("source texel identity");
            }
        }
        ++triangles;
    }
    if(water_faces.size()!=299 || deck_faces.size()!=38 || bottom_faces.size()!=38)
        return fail("missing water/deck/underside mesh");
    auto changed=route;changed.grid[16*55+31]^=0x400;
    if(terrain::resolve(changed,set.terrain).query(31,16,3).status!=terrain::Status::SourceMismatch)
        return fail("packed source guard not enforced");
    changed=route;changed.metatiles[520*8]^=1;
    if(terrain::resolve(changed,set.terrain).query(31,16,3).status!=terrain::Status::SourceMismatch)
        return fail("hidden water material guard not enforced");
    std::printf("[terrain-bridge-test] PASS: %zu decks, %zu water planes, 4 bank contacts, %zu owner seam edges, %zu source-matched terrain triangles; layer/guard checks pass\n",
                decks,waters,joins,triangles);
    return 0;
}
