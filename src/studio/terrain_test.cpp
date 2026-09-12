// Original synthetic input only; callable without source art or a GL context.
#include "terrain.h"
#include "pattern_io.h"
#include "world_io.h"
#include "snapshot_build.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>

namespace studio {
int terrain_source_selftest(const char* source,const char* pack) {
    Decomp decomp;if(!decomp.open(source)) return 1;
    vr::overrides::OverrideSet set;if(!vr::overrides::load(pack,&set) || set.terrain.empty()) return 1;
    using Key=std::pair<int,int>;
    std::map<Key,vr::world::Snapshot> snapshots;
    for(const auto& id:decomp.map_ids()) {
        vr::world::Snapshot s;
        if(!build_snapshot(decomp,id,&s) || !s.has_map_identity() || !s.valid_connections() ||
            !snapshots.emplace(Key{s.map_group,s.map_number},std::move(s)).second) return 1;
    }
    size_t copies=0,heights=0,joins=0,continuous_edges=0;
    for(const auto& [key,s]:snapshots) {
        const auto resolved=vr::terrain::resolve(s,set.terrain);
        for(const auto& slice:s.connections) {
            const auto target=snapshots.find({slice.group,slice.number});if(target==snapshots.end()) return 1;
            const auto& other=target->second;const auto canonical=vr::terrain::resolve(other,set.terrain);
            if(slice.width!=other.width || slice.height!=other.height) return 1;
            for(int y=0;y<slice.h;++y) for(int x=0;x<slice.w;++x) {
                const int dx=slice.x+x,dy=slice.y+y,sx=slice.source_x+x,sy=slice.source_y+y;
                // Adjacent connections can overlap at a corner; last writer owns it.
                const vr::world::ConnectionSlice* owner=nullptr;
                for(const auto& c:s.connections) if(dx>=c.x && dy>=c.y && dx<c.x+c.w && dy<c.y+c.h) owner=&c;
                if(owner!=&slice) continue;
                if(s.cell(dx,dy)!=other.cell(sx,sy)) return 1;++copies;
                if(!canonical.cell(sx,sy)) continue;
                const auto a=canonical.query(sx,sy,other.elevation(sx,sy)),b=resolved.query(dx,dy,s.elevation(dx,dy));
                if(slice.compatible_art ? (a.status!=b.status || a.pixels!=b.pixels) : b.status!=vr::terrain::Status::SourceMismatch) return 1;
                if(slice.compatible_art) for(float u:{0.f,.5f,1.f}) for(float v:{0.f,.5f,1.f})
                    if(canonical.query(sx,sy,other.elevation(sx,sy),u,v).pixels!=
                        resolved.query(dx,dy,s.elevation(dx,dy),u,v).pixels) return 1;
                ++heights;
            }
        }
        const auto* definition=decomp.map("MAP_OLDALE_TOWN");
        const auto* route=decomp.map("MAP_ROUTE101");
        if(key==Key{route->group,route->number}) {
            auto at=[&](int x,int y,float u,float v){return resolved.query(x+7,y+7,s.elevation(x+7,y+7),u,v).pixels;};
            for(int y=0;y<20;++y) for(int x=0;x<20;++x) {
                const bool cliff_e=x==5 && y==7;
                const bool cliff_s=(y==7 && x>=2 && x<6) || (y==6 && x>=6 && x<11) || (y==13 && x>=8 && x<12);
                if(x<19 && !cliff_e) {
                    for(float v:{0.f,.5f,1.f}) if(at(x,y,1,v)!=at(x+1,y,0,v)) return 1;
                    ++continuous_edges;
                }
                if(y<19 && !cliff_s) {
                    for(float u:{0.f,.5f,1.f}) if(at(x,y,u,1)!=at(x,y+1,u,0)) return 1;
                    ++continuous_edges;
                }
            }
            for(int x=0;x<20;++x) if(at(x,0,0,0)!=16 || at(x,19,0,1)!=0) return 1;
            if(at(8,6,.5f,1)-at(8,7,.5f,0)!=8 || at(9,13,.5f,1)-at(9,14,.5f,0)!=8) return 1;
        }
        if(key!=Key{definition->group,definition->number} && key!=Key{route->group,route->number}) continue;
        const int seam=key==Key{definition->group,definition->number}?s.height-7:7;
        for(int x=7;x<s.width-8;++x) {
            const auto a=resolved.query(x,seam-1,s.elevation(x,seam-1)),b=resolved.query(x,seam,s.elevation(x,seam));
            if(a.status!=vr::terrain::Status::Authored || b.status!=vr::terrain::Status::Authored || a.pixels!=16 || b.pixels!=16) return 1;
            ++joins;
        }
        std::vector<vr::diorama::AuthoredVertex> mesh;vr::diorama::DioramaStats stats;
        if(!vr::diorama::inspect_diorama_mesh(s,set,&mesh,&stats) || stats.terrain_rejected) return 1;
        for(size_t i=0;i<stats.flat_vertices;i+=3) {
            const auto& a=mesh[i].position;const auto& b=mesh[i+1].position;const auto& c=mesh[i+2].position;
            if(a.x<7 || a.x>s.width-8 || b.x<7 || b.x>s.width-8 || c.x<7 || c.x>s.width-8) continue;
            if(a.z==seam && b.z==seam && c.z==seam && (a.y!=b.y || a.y!=c.y)) return 1;
        }
    }
    if(!heights || joins!=40 || continuous_edges!=746) return 1;
    // Check every included connection in both source views, including offset
    // east/west joins. An unselected neighbour remains an explicit frontier;
    // a selected neighbour must resolve and meet at both corners and midpoint.
    std::set<Key> authored;
    for(const auto& m:set.terrain) authored.emplace(m.group,m.number);
    std::set<std::pair<Key,Key>> frontiers;
    size_t region_joins=0,water_cells=0,water_triangles=0;
    struct Join {bool east_west;int plane,along;};
    for(const auto& key:authored) {
        const auto found=snapshots.find(key);if(found==snapshots.end()) return 1;
        const auto& s=found->second;const auto resolved=vr::terrain::resolve(s,set.terrain);
        if(resolved.rejected) return 1;
        size_t map_water_cells=0;
        for(int y=7;y<s.height-7;++y) for(int x=7;x<s.width-8;++x) {
            const auto* cell=resolved.cell(x,y);
            if(!cell || cell->surfaces.size()!=1 || cell->surfaces[0].kind!=vr::overrides::TerrainKind::Water) continue;
            const auto& p=cell->surfaces[0];++water_cells;++map_water_cells;
            for(float u:{0.f,.5f,1.f}) for(float v:{0.f,.5f,1.f}) {
                const auto height=resolved.query(x,y,s.elevation(x,y),u,v);
                if(height.status!=vr::terrain::Status::Authored || height.pixels!=p.height) return 1;
            }
            for(const auto [dx,dy]:{std::pair{1,0},std::pair{0,1}}) {
                const auto* next=resolved.cell(x+dx,y+dy);
                if(next && next->surfaces.size()==1 && next->surfaces[0].kind==vr::overrides::TerrainKind::Water &&
                    next->surfaces[0].layer==p.layer && next->surfaces[0].height!=p.height) {
                    std::fprintf(stderr,"[terrain-water-test] adjacent water levels differ at map %d:%d cell %d,%d\n",key.first,key.second,x,y);
                    return 1;
                }
            }
        }
        std::vector<Join> checked;
        for(int side=0;side<4;++side) {
            const bool ew=side==1 || side==3;
            const int count=ew?s.height-14:s.width-15;
            for(int i=0;i<count;++i) {
                const int x=ew?(side==1?s.width-9:7):7+i;
                const int y=ew?7+i:(side==0?7:s.height-8);
                const int nx=x+(side==1)-(side==3),ny=y+(side==2)-(side==0);
                const vr::world::ConnectionSlice* owner=nullptr;
                for(const auto& c:s.connections)
                    if(nx>=c.x && ny>=c.y && nx<c.x+c.w && ny<c.y+c.h) owner=&c;
                if(!owner) continue;
                const Key other{owner->group,owner->number};
                if(!authored.count(other)) {frontiers.emplace(key,other);continue;}
                for(float t:{0.f,.5f,1.f}) {
                    const auto a=resolved.query(x,y,s.elevation(x,y),ew?(side==1?1.f:0.f):t,ew?t:(side==0?0.f:1.f));
                    const auto b=resolved.query(nx,ny,s.elevation(nx,ny),ew?(side==1?0.f:1.f):t,ew?t:(side==0?1.f:0.f));
                    if(a.status!=vr::terrain::Status::Authored || b.status!=vr::terrain::Status::Authored || a.pixels!=b.pixels) {
                        std::fprintf(stderr,"[terrain-region-test] gap/unresolved: map %d:%d at %d,%d side %d\n",key.first,key.second,x,y,side);
                        return 1;
                    }
                }
                checked.push_back({ew,ew?std::max(x,nx):std::max(y,ny),ew?y:x});
                ++region_joins;
            }
        }
        if(checked.empty() && !map_water_cells) continue;
        std::set<std::pair<int,int>> water_mesh_cells;
        std::vector<vr::diorama::AuthoredVertex> mesh;vr::diorama::DioramaStats stats;
        if(!vr::diorama::inspect_diorama_mesh(s,set,&mesh,&stats) || stats.terrain_rejected) return 1;
        for(size_t i=0;i<stats.flat_vertices;i+=3) {
            const auto& a=mesh[i].position;const auto& b=mesh[i+1].position;const auto& c=mesh[i+2].position;
            const int x=int(std::floor((a.x+b.x+c.x)/3)),y=int(std::floor((a.z+b.z+c.z)/3));
            const auto* cell=resolved.cell(x,y);
            const float area=(b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x);
            if(vr::terrain::primary_cell(s,x,y) && cell && cell->surfaces.size()==1 &&
                cell->surfaces[0].kind==vr::overrides::TerrainKind::Water && std::abs(area)>1e-6f) {
                const float plane=cell->surfaces[0].height/16.f;
                // Source overlay planes have small depth offsets. Shore walls
                // at cell edges are legitimate; a water bottom or grade is not.
                if(std::abs(a.y-b.y)>1e-5f || std::abs(a.y-c.y)>1e-5f || a.y<plane-1e-5f || a.y>plane+.004f) {
                    std::fprintf(stderr,"[terrain-water-test] non-level/buried water face at map %d:%d cell %d,%d\n",key.first,key.second,x,y);
                    return 1;
                }
                ++water_triangles;
                water_mesh_cells.emplace(x,y);
            }
            if(a.y==b.y && a.y==c.y) continue;
            for(const auto& j:checked) {
                const float ap=j.east_west?a.x:a.z,bp=j.east_west?b.x:b.z,cp=j.east_west?c.x:c.z;
                if(ap!=j.plane || bp!=j.plane || cp!=j.plane) continue;
                const float lo=std::min({j.east_west?a.z:a.x,j.east_west?b.z:b.x,j.east_west?c.z:c.x});
                const float hi=std::max({j.east_west?a.z:a.x,j.east_west?b.z:b.x,j.east_west?c.z:c.x});
                if(hi>j.along && lo<j.along+1) {
                    std::fprintf(stderr,"[terrain-region-test] internal terrain wall at map %d:%d\n",key.first,key.second);
                    return 1;
                }
            }
        }
        if(water_mesh_cells.size()!=map_water_cells) {
            std::fprintf(stderr,"[terrain-water-test] missing water surface at map %d:%d\n",key.first,key.second);
            return 1;
        }
    }
    std::printf("[terrain-region-test] PASS: %zu authored maps, %zu joined edges checked in both views; no internal map walls; %zu unreviewed connections\n",
                authored.size(),region_joins,frontiers.size());
    if(water_cells) std::printf("[terrain-water-test] PASS: %zu source-body water cells, %zu level surface triangles; no water bottom faces\n",water_cells,water_triangles);
    std::printf("[terrain-source-test] PASS: %zu source maps, %zu copied cells, %zu canonical terrain copies, %zu equal-height seam edges; %zu continuous Route 101 edges and two 8px ledges; no internal seam walls\n",snapshots.size(),copies,heights,joins,continuous_edges);
    return 0;
}

int terrain_selftest(const char* output) {
    using namespace vr;
    using namespace overrides;
    int checks=0;
    auto check=[&](bool ok,const char* reason) {
        if(ok) ++checks;else std::fprintf(stderr,"[terrain-test] FAIL: %s\n",reason);return ok;
    };
    world::Snapshot s;s.valid=true;s.width=19;s.height=18;s.layout_ptr=123;
    s.map_group=0;s.map_number=1;s.identity_source=world::Snapshot::IdentitySource::SourceTable;
    s.grid.assign(s.width*s.height,world::kGridUndefined);
    for(int y=7;y<11;++y) for(int x=7;x<11;++x) s.grid[y*s.width+x]=0x3001;
    s.grid[7*s.width+7]=0x3003;
    s.metatiles.resize(8192);s.attributes.resize(1024);s.vram_tiles.resize(32768);s.bg_palette.resize(256);
    for(int id=1;id<=3;++id) for(int k=0;k<4;++k) s.metatiles[id*8+k]=uint16_t(id|(id==2?0xC00:0));
    std::fill(s.vram_tiles.begin()+32,s.vram_tiles.begin()+128,0x11);s.bg_palette[1]=0x7fff;
    TerrainMap m;m.group=0;m.number=1;m.width=s.width;m.height=s.height;
    terrain::guard_tile(s,1,&m);terrain::guard_tile(s,2,&m);
    terrain::guard_tile(s,3,&m);
    TerrainCell cell;cell.x=7;cell.y=7;cell.expected=0x3003;cell.underlay=1;
    TerrainSurface ground;ground.layer=3;ground.height=ground.thickness=32;ground.side=2;
    cell.surfaces={ground};m.cells={cell};
    OverrideSet set;set.version=kTerrainVersion;set.terrain={m};
    if(!check(terrain::valid(set.terrain),"two-level source-guarded terrain is valid")) return 1;
    auto resolved=terrain::resolve(s,set.terrain);
    if(!check(resolved.matched==1 && resolved.rejected==0 && resolved.query(7,7,3).pixels==32,"exact layer resolves authored pixels") ||
       !check(resolved.query(7,7,0).pixels==32 && resolved.query(7,7,15).pixels==32,"sole surface permits transition layers without converting bits to height") ||
       !check(!resolved.query(7,7,4).resolved() && !resolved.query(7,7,16).resolved(),"unknown nonmatching layers stay unresolved") ||
       !check(resolved.query(8,8,9).status==terrain::Status::LegacyFlat,"unmodified floor keeps legacy height")) return 1;
    auto changed=s;changed.map_number=2;
    if(!check(terrain::resolve(changed,set.terrain).matched==0,"same layout with another map identity cannot activate terrain")) return 1;
    changed=s;changed.identity_source=world::Snapshot::IdentitySource::Unknown;
    if(!check(terrain::resolve(changed,set.terrain).matched==0,"unknown snapshot identity never guessed from layout pointer")) return 1;
    for(int mode=0;mode<4;++mode) {
        changed=s;
        if(mode==0) changed.grid[7*s.width+7]^=0x400; // collision guard
        if(mode==1) changed.metatiles[8]^=0x400;
        if(mode==2) changed.attributes[2]^=1; // side art guard
        if(mode==3) {changed.width++;changed.grid.resize(changed.width*changed.height);}
        const auto rejected=terrain::resolve(changed,set.terrain);
        if(!check(rejected.matched==0 && rejected.rejected==1 && !rejected.query(7,7,3).resolved(),"changed source/layout/material refuses cell without mutation")) return 1;
    }
    for(int mode=0;mode<8;++mode) {
        auto bad=set.terrain;
        if(mode==0) bad[0].cells.push_back(cell);
        if(mode==1) bad.push_back(m);
        if(mode==2) bad[0].cells[0].x=6;
        if(mode==3) bad[0].cells[0].surfaces[0].height=257;
        if(mode==4) bad[0].cells[0].surfaces[0].layer=16;
        if(mode==5) bad[0].tiles.erase(2);
        if(mode==6) bad[0].cells[0].surfaces[0].thickness=31;
        if(mode==7) bad[0].cells[0].underlay=1024;
        if(!check(!terrain::valid(bad),"invalid identity/cell/material/solid rejected")) return 1;
    }
    // One canonical neighbour surface must occupy the copied border coordinate
    // from every direction. Never edit four duplicated padding recipes.
    for(const auto [x,y]:{std::pair{8,3},std::pair{8,12},std::pair{3,8},std::pair{12,8}}) {
        auto view=s;view.map_number=2;
        view.connections={{0,1,s.width,s.height,x,y,7,7,2,2,true}};
        view.grid[y*s.width+x]=cell.expected;
        auto heights=terrain::resolve(view,set.terrain);
        if(!check(view.valid_connections() && heights.matched==1 && heights.query(x,y,3).pixels==32 &&
            !heights.cell(7,7),"north/south/west/east copies read canonical neighbour heights")) return 1;
        std::vector<diorama::AuthoredVertex> border_mesh;diorama::DioramaStats border_stats;
        if(!check(diorama::inspect_diorama_mesh(view,set,&border_mesh,&border_stats) &&
            std::any_of(border_mesh.begin(),border_mesh.end(),[&](const auto& v){return v.position.x==float(x) && v.position.z==float(y) && v.position.y==2.f;}),
            "neighbour terrain is meshed at copied coordinates, not source coordinates")) return 1;
        view.connections[0].compatible_art=false;
        if(!check(terrain::resolve(view,set.terrain).query(x,y,3).status==terrain::Status::SourceMismatch,
            "different tileset pairs require their own atlas, never silently reuse current art")) return 1;
        view.connections[0].compatible_art=true;view.grid[y*s.width+x]^=0x400;
        if(!check(terrain::resolve(view,set.terrain).rejected==1,"copied source cells retain their packed source guards")) return 1;
        view.grid[y*s.width+x]=cell.expected;
        auto later=view.connections[0];later.number=3;view.connections.push_back(later);
        if(!check(terrain::resolve(view,set.terrain).matched==0,"later source connection owns overlapping padding")) return 1;
    }
    {
        auto large=s;large.width=large.height=100;large.grid.assign(10000,0x3001);
        large.connections={{0,2,100,100,7,0,7,7,85,7,true}};
        auto first=m;first.width=first.height=100;first.cells.clear();
        for(int i=0;i<600;++i) {
            auto c=cell;c.x=7+i%85;c.y=7+i/85;c.expected=0x3001;
            c.surfaces[0].height=c.surfaces[0].thickness=256;first.cells.push_back(c);
        }
        auto second=first;second.number=2;second.cells.resize(100);
        std::vector<TerrainMap> combined{first,second};const auto bounded=terrain::resolve(large,combined);
        if(!check(terrain::valid(combined) && bounded.matched==0 && bounded.rejected==700,
            "combined neighbour terrain obeys the visible mesh budget without clipping a region")) return 1;
    }
    auto stacked=set;auto& surfaces=stacked.terrain[0].cells[0].surfaces;
    surfaces[0].kind=TerrainKind::Water;surfaces[0].height=surfaces[0].thickness=0;
    auto deck=ground;deck.kind=TerrainKind::Deck;deck.layer=4;deck.thickness=4;surfaces.push_back(deck);
    resolved=terrain::resolve(s,stacked.terrain);
    if(!check(terrain::valid(stacked.terrain) && resolved.query(7,7,3).pixels==0 && resolved.query(7,7,4).pixels==32,"water and deck preserve separate heights/layers") ||
       !check(!resolved.query(7,7,0).resolved() && !resolved.query(7,7,15).resolved() && !resolved.query(7,7,-1).resolved(),"ambiguous transition never chooses highest deck")) return 1;
    for(int mode=0;mode<4;++mode) {
        auto bad=stacked;
        auto& p=bad.terrain[0].cells[0].surfaces;
        if(mode==0) p[1].layer=3;
        if(mode==1) p[1].layer=0;
        if(mode==2) {p[0].height=30;}
        if(mode==3) p[1].thickness=0;
        if(!check(!terrain::valid(bad.terrain),"duplicate/transition/overlapping/empty deck rejected")) return 1;
    }
    std::vector<diorama::AuthoredVertex> mesh;diorama::DioramaStats stats;
    if(!check(diorama::inspect_diorama_mesh(s,set,&mesh,&stats) && stats.terrain_cells==1 && stats.terrain_vertices>0,"shared production mesher emits plateau")) return 1;
    int strips=0;
    for(size_t i=0;i<mesh.size();i+=3) {
        const auto& a=mesh[i];const auto& b=mesh[i+1];const auto& c=mesh[i+2];
        if(a.tile!=2 || std::abs(a.position.y-c.position.y)<1e-6f) continue;
        ++strips;
        if(!check(std::abs(std::abs(a.position.y-c.position.y)-std::abs(a.v-c.v)*.5f)<1e-6f,"cliff UV strip retains native pixel scale through source flips")) return 1;
    }
    if(!check(strips>0,"side-art strips inspected")) return 1;
    auto neighbor=cell;neighbor.x=8;neighbor.expected=0x3001;set.terrain[0].cells.push_back(neighbor);
    if(!check(diorama::inspect_diorama_mesh(s,set,&mesh,&stats),"adjacent plateau builds")) return 1;
    int buried=0;
    for(size_t i=0;i<mesh.size();i+=3) {
        const auto& a=mesh[i].position;const auto& b=mesh[i+1].position;const auto& c=mesh[i+2].position;
        if(std::abs(a.x-8)<.001f && std::abs(b.x-8)<.001f && std::abs(c.x-8)<.001f &&
           std::max({a.z,b.z,c.z})<=8 && std::min({a.z,b.z,c.z})>=7 && std::abs(a.y-c.y)>.001f) ++buried;
    }
    if(!check(buried==0,"equal-height neighbors remove their shared interior walls")) return 1;
    {
        auto grade=set;auto& g=grade.terrain[0].cells[0].surfaces[0];
        g.height=g.thickness=16;g.rise_x=-4;g.rise_z=-8;g.corner_delta=2;g.top=1;g.side_offset=8;
        auto& next=grade.terrain[0].cells[1].surfaces[0];
        next=g;next.height=next.thickness=12;next.rise_x=0;next.rise_z=-6;next.corner_delta=0;
        const auto land=terrain::resolve(s,grade.terrain);
        if(!check(terrain::valid(grade.terrain) && land.query(7,7,3,1,1).pixels==6 &&
            land.query(7,7,3,.5f,.5f).pixels==11 && land.query(7,7,3,.75f,.25f).pixels==11.5f,
            "corner heights interpolate the actual NW-SE triangles")) return 1;
        if(!check(!land.query(7,7,3,-.1f,.5f).resolved() && !land.query(7,7,3,NAN,0).resolved(),
            "invalid local coordinates cannot extrapolate terrain")) return 1;
        for(float z:{0.f,.25f,.5f,.75f,1.f})
            if(!check(land.query(7,7,3,1,z).pixels==land.query(8,7,3,0,z).pixels,
                "unequal corner grades share a continuous edge")) return 1;
        if(!check(diorama::inspect_diorama_mesh(s,grade,&mesh,&stats),"graded terrain builds")) return 1;
        int sides=0,top=0,internal=0;
        for(size_t i=0;i<stats.flat_vertices;i+=3) {
            const auto& a=mesh[i];const auto& b=mesh[i+1];const auto& c=mesh[i+2];
            if(a.position.x<7 || b.position.x<7 || c.position.x<7 ||
                a.position.x>9 || b.position.x>9 || c.position.x>9 ||
                a.position.z<7 || b.position.z<7 || c.position.z<7 ||
                a.position.z>8 || b.position.z>8 || c.position.z>8) continue;
            if(std::abs(a.position.x-8)<.001f && std::abs(b.position.x-8)<.001f && std::abs(c.position.x-8)<.001f &&
                std::max({a.position.y,b.position.y,c.position.y})-std::min({a.position.y,b.position.y,c.position.y})>.001f) ++internal;
            if(a.tile==2 && std::abs(a.position.y-c.position.y)>.0001f) {
                ++sides;
                if(!check(std::abs(std::abs(a.position.y-c.position.y)-std::abs(a.v-c.v)*.5f)<1e-6f,
                    "clipped slope sides preserve native vertical texels")) return 1;
            }
            if(a.tile==1 && a.position.y>0 && b.position.y>0 && c.position.y>0 &&
                a.position.x<=8 && b.position.x<=8 && c.position.x<=8) {
                ++top;
                const float u=(a.position.x+b.position.x+c.position.x)/3-7,v=(a.position.z+b.position.z+c.position.z)/3-7;
                const float h=(a.position.y+b.position.y+c.position.y)*16/3;
                if(!check(std::abs(h-land.query(7,7,3,u,v).pixels)<.0001f,
                    "ground queries agree with mesh triangle interiors, not just corners")) return 1;
            }
        }
        if(!check(!internal && sides && top,"graded joins suppress internal walls and retain exposed faces")) return 1;
        for(int mode=0;mode<6;++mode) {
            auto bad=grade;auto& p=bad.terrain[0].cells[0].surfaces[0];
            if(mode==0) p.rise_x=-17;
            if(mode==1) p.rise_z=257;
            if(mode==2) p.corner_delta=512;
            if(mode==3) p.kind=TerrainKind::Deck;
            if(mode==4) p.side_offset=16;
            if(mode==5) bad.terrain[0].cells[0].surfaces.push_back(ground);
            if(!check(!terrain::valid(bad.terrain),"invalid corners, materials and stacked grades reject")) return 1;
        }
        const std::string graded_path=std::string(output)+"-grade.json";OverrideSet copy;
        if(!check(pattern_io::write(graded_path.c_str(),grade) && overrides::load(graded_path.c_str(),&copy) && copy==grade,
            "all grade and material-phase fields round-trip exactly")) return 1;
        Pattern cover;if(!pattern_io::from_cells(s,{{7,7}},"synthetic grass",&cover)) return 1;
        cover.source.room="MAP_SYNTHETIC";cover.cutout=Cutout{16,16,std::vector<uint8_t>(256,1)};
        cover.voxel=Voxel{};cover.voxel->ground=cover.voxel->shadow=Cutout{16,16,std::vector<uint8_t>(256,0)};
        Part blade;blade.id="blade";blade.kind=PartKind::Box;
        blade.transform.position={0,0,0};blade.transform.size={1,.25f,1};
        for(int k=0;k<6;++k) {Surface art;art.region={0,0,16,16};blade.surfaces.push_back(art);}
        cover.parts={blade};cover.follow_ground=true;
        // A source-derived closed slab supplies independent local geometry.
        OverrideSet plain;plain.version=6;plain.patterns={cover};grade.patterns={cover};
        std::vector<diorama::AuthoredVertex> before;diorama::DioramaStats before_stats;
        if(!check(diorama::inspect_diorama_mesh(s,plain,&before,&before_stats) &&
            diorama::inspect_diorama_mesh(s,grade,&mesh,&stats) && stats.authored_vertices==before_stats.authored_vertices,
            "flexible cover builds through the shared production mesher")) return 1;
        bool contact=stats.authored_vertices>0;
        for(size_t i=0;i<stats.authored_vertices;++i) {
            const auto& a=before[before_stats.flat_vertices+i];const auto& b=mesh[stats.flat_vertices+i];
            const float height=land.query(7,7,3,std::clamp(a.position.x-7,0.f,1.f),std::clamp(a.position.z-7,0.f,1.f)).pixels/16;
            contact &= std::abs(b.position.y-a.position.y-height)<1e-6f && a.tile==b.tile && a.u==b.u && a.v==b.v;
        }
        if(!check(contact,"every cover vertex follows the ground without changing source UVs") ||
            !check(pattern_io::write(graded_path.c_str(),grade) && overrides::load(graded_path.c_str(),&copy) && copy==grade,
            "follow-ground authoring round-trips with terrain")) return 1;
    }
    if(!check(diorama::inspect_diorama_mesh(s,stacked,&mesh,&stats) &&
        std::any_of(mesh.begin(),mesh.end(),[](const auto& v){return std::abs(v.position.y-1.75f)<1e-6f;}),"deck has a real underside above its lower surface")) return 1;
    // Independently compare all authored model vertices before/after placement.
    Pattern p;if(!pattern_io::from_cells(s,{{7,7}},"synthetic marker",&p)) return 1;
    p.source.room="MAP_SYNTHETIC";
    p.cutout=Cutout{16,16,std::vector<uint8_t>(256,1)};
    OverrideSet flat;flat.patterns={p};std::vector<diorama::AuthoredVertex> old;
    diorama::DioramaStats oldstats;
    if(!diorama::inspect_diorama_mesh(s,flat,&old,&oldstats)) return 1;
    set.patterns={p};if(!diorama::inspect_diorama_mesh(s,set,&mesh,&stats)) return 1;
    bool moved=oldstats.authored_vertices==stats.authored_vertices && stats.unresolved_placements==0;
    for(size_t i=0;moved && i<oldstats.authored_vertices;++i) {
        const auto& a=old[oldstats.flat_vertices+i];const auto& b=mesh[stats.flat_vertices+i];
        moved &= a.position.x==b.position.x && a.position.z==b.position.z && std::abs(b.position.y-a.position.y-2)<1e-6f && a.tile==b.tile && a.u==b.u && a.v==b.v;
    }
    if(!check(moved && stats.raised_instances>0,"every model vertex uses the same queried surface height without changing art")) return 1;
    set.terrain[0].cells[0].surfaces[0].layer=4;
    if(!check(diorama::inspect_diorama_mesh(s,set,&mesh,&stats) && stats.unresolved_placements==1,"unresolved object foundation is reported")) return 1;
    const std::string path=std::string(output)+".json";OverrideSet reopened;
    if(!check(pattern_io::write(path.c_str(),set) && overrides::load(path.c_str(),&reopened) && reopened==set,"v7 terrain and models save/reopen exactly")) return 1;
    auto bad=set;bad.terrain[0].cells[0].surfaces[0].height=-1;
    if(!check(!pattern_io::write(path.c_str(),bad) && overrides::load(path.c_str(),&reopened) && reopened==set,"failed terrain save preserves previous file")) return 1;
    for(uint32_t version:{5u,6u}) {
        flat.version=version;
        if(!check(pattern_io::write(path.c_str(),flat) && overrides::load(path.c_str(),&reopened) && reopened==flat,"legacy document meaning survives save/reopen")) return 1;
    }
    auto read_bytes=[](const std::string& p) {std::ifstream f(p,std::ios::binary);return std::vector<char>(std::istreambuf_iterator<char>(f),{});};
    auto write_bytes=[](const std::string& p,const std::vector<char>& b) {std::ofstream f(p,std::ios::binary);f.write(b.data(),std::streamsize(b.size()));};
    const std::string snap=std::string(output)+".snap";world::Snapshot restored;
    if(!check(world_io::write(s,snap.c_str()) && world_io::read(restored,snap.c_str()) && restored.has_map_identity() && restored.map_number==1 && restored.grid==s.grid,"snapshot v2 preserves source identity and grid")) return 1;
    const auto before_actor_bytes=read_bytes(snap);
    auto live=s;live.actor_sources[0].present=true;live.obj_tiles={0xa5};live.obj_palette={31};
    live.actor_offset_x=12;live.obj_mapping_1d=true;restored=live;
    if(!check(world_io::write(live,snap.c_str()) && read_bytes(snap)==before_actor_bytes &&
        world_io::read(restored,snap.c_str()) && !restored.actor_sources[0].present && restored.obj_tiles.empty() &&
        restored.obj_palette.empty() && restored.actor_offset_x==0 && !restored.obj_mapping_1d,
        "transient actor art never changes disk format or survives disk reload")) return 1;
    const auto bytes=read_bytes(snap);auto legacy=bytes;legacy[8]=1;legacy.resize(legacy.size()-6);write_bytes(snap,legacy);
    if(!check(world_io::read(restored,snap.c_str()) && !restored.has_map_identity() && restored.map_number==-1 && restored.grid==s.grid,"legacy snapshot reads with explicit unknown identity")) return 1;
    for(int mode=0;mode<5;++mode) {
        auto corrupt=bytes;
        if(mode==0) corrupt[8]=3;
        if(mode==1) corrupt.pop_back();
        if(mode==2) corrupt[corrupt.size()-6]=4;
        if(mode==3) corrupt.push_back(0);
        if(mode==4) corrupt[12]=1;
        write_bytes(snap,corrupt);restored.layout_ptr=999;
        if(!check(!world_io::read(restored,snap.c_str()) && restored.layout_ptr==999,"invalid/future/truncated snapshot rejects atomically")) return 1;
    }
    changed=s;changed.connections={{0,2,s.width,s.height,8,3,7,7,2,2,true}};
    if(!check(world_io::write(changed,snap.c_str()) && world_io::read(restored,snap.c_str()) && restored.connections==changed.connections,
        "snapshot v2 round-trips neighbour identity, source rectangle and atlas compatibility")) return 1;
    for(int mode=0;mode<4;++mode) {
        auto invalid=changed;
        if(mode==0) invalid.connections[0].source_x=6;
        if(mode==1) invalid.connections[0].x=7,invalid.connections[0].y=7;
        if(mode==2) invalid.connections[0].w=10000;
        if(mode==3) invalid.connections[0].number=256;
        if(!check(!world_io::write(invalid,snap.c_str()),"invalid/overlapping-primary neighbour provenance cannot be saved")) return 1;
    }
    std::printf("[terrain-test] PASS: %d synthetic checks; shared mesh, native UVs, layers, source guards and persistence\n",checks);
    return 0;
}
}
