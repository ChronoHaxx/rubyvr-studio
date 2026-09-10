#include "connected_scene.h"
#include "pattern_io.h"
#include "terrain.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

int connected_selftest() {
    using namespace vr;using namespace overrides;
    int checks=0;
    auto check=[&](bool ok,const char* reason){if(ok)++checks;else std::fprintf(stderr,"[connected-test] FAIL %s\n",reason);return ok;};
    auto make=[](int number,int offset) {
        world::Snapshot s;s.valid=true;s.width=19;s.height=18;s.layout_ptr=100+number;
        s.identity_source=world::Snapshot::IdentitySource::SourceTable;s.map_group=0;s.map_number=number;
        s.grid.assign(19*18,world::kGridUndefined);
        for(int y=7;y<11;++y)for(int wx=7;wx<15;++wx)s.grid[y*19+wx-offset]=uint16_t(0x3001+((wx==10 && y==8)?2:0));
        s.metatiles.resize(8192);s.attributes.resize(1024);s.vram_tiles.assign(32768,0x11);s.bg_palette.resize(256,0x7fff);
        for(int id:{1,3})for(int k=0;k<8;++k)s.metatiles[id*8+k]=uint16_t(id);
        return s;
    };
    auto a=make(1,0),b=make(2,4);const auto grid_a=a.grid,grid_b=b.grid;
    OverrideSet set;set.version=kTerrainVersion;
    for(const auto* s:{&a,&b}) {
        TerrainMap m;m.group=0;m.number=s->map_number;m.width=19;m.height=18;
        terrain::guard_tile(*s,1,&m);terrain::guard_tile(*s,3,&m);
        for(int y=7;y<11;++y)for(int x=7;x<11;++x) {
            TerrainCell c;c.x=x;c.y=y;c.expected=s->cell(x,y);
            TerrainSurface p;p.layer=3;p.height=p.thickness=16;c.surfaces={p};m.cells.push_back(c);
        }
        set.terrain.push_back(m);
    }
    std::vector<diorama::RegionMap> inputs{{&a,0,0},{&b,4,0}};
    std::vector<diorama::RegionMesh> mesh;diorama::RegionStats stats;std::string error;
    if(!check(diorama::inspect_region_mesh(inputs,set,&mesh,&stats,&error),"adjacent source maps build") ||
       !check(stats.maps==2 && stats.primary_cells==32 && stats.padding_cells==0 && stats.hidden_cells==32,"body ownership removes both padding copies") ||
       !check(!stats.terrain_rejected && !stats.unresolved_placements,"owner terrain resolves across the edge"))return 1;
    bool walls=false,wrong_owner=false;
    for(size_t c=0;c<mesh.size();++c)for(size_t k=0;k<mesh[c].vertices.size();k+=3) {
        const auto p=mesh[c].vertices[k].position,q=mesh[c].vertices[k+1].position,r=mesh[c].vertices[k+2].position;
        walls|=p.x==11 && q.x==11 && r.x==11 && (p.y!=q.y || q.y!=r.y);
        const float centre=(p.x+q.x+r.x)/3;
        wrong_owner|=c==0?centre>11.0001f:centre<10.9999f;
    }
    if(!check(!walls,"equal-height join has no internal vertical wall") || !check(!wrong_owner,"each floor draws only its own body"))return 1;
    Pattern model;if(!studio::pattern_io::from_cells(a,{{10,8},{11,8}},"boundary building",&model))return 1;
    model.cutout=Cutout{32,16,std::vector<uint8_t>(512,1)};model.voxel=Voxel{};
    model.voxel->ground=model.voxel->shadow=Cutout{32,16,std::vector<uint8_t>(512,0)};
    Part box;box.id="overhang";box.transform.position={0,0,-.5f};box.transform.size={2,1,1};
    Surface art;art.region={0,0,16,16};box.surfaces.assign(6,art);model.parts={box};set.patterns={model};
    const auto before=set;
    if(!check(diorama::inspect_region_mesh(inputs,set,&mesh,&stats,&error) && stats.raised_instances==1,"boundary model is emitted once by its anchor owner"))return 1;
    bool overhang=false;const auto& first=mesh[0];
    for(size_t k=first.stats.flat_vertices;k<first.vertices.size();++k)overhang|=first.vertices[k].position.x>11;
    if(!check(overhang,"owned model geometry is not clipped at the map boundary") ||
       !check(set==before && a.grid==grid_a && b.grid==grid_b,"source grids and authored documents remain unchanged"))return 1;
    const auto hash=stats.geometry_hash;
    for(int mode=0;mode<6;++mode) {
        auto bad=inputs;auto source=b;
        if(mode==0)bad[1].x=3;
        if(mode==1)bad[1].source=&a;
        if(mode==2){source.grid.pop_back();bad[1].source=&source;}
        if(mode==3)bad[1].z=9000;
        if(mode==4)bad.resize(10,bad[0]);
        if(mode==5)bad[1].x=std::numeric_limits<int>::min();
        if(!check(!diorama::inspect_region_mesh(bad,set,&mesh,&stats,&error) && stats.geometry_hash==hash,"invalid/overlapping/duplicate/oversized regions refuse atomically"))return 1;
    }
    // Different numeric tile/palette slots must remain map-local.
    auto separate=set;separate.patterns.clear();
    for(int id:{1,3})for(int k=0;k<8;++k)b.metatiles[id*8+k]=uint16_t(7|(1<<12));
    separate.terrain[1].tiles.clear();terrain::guard_tile(b,1,&separate.terrain[1]);terrain::guard_tile(b,3,&separate.terrain[1]);
    if(!check(diorama::inspect_region_mesh(inputs,separate,&mesh,&stats,&error) && !stats.terrain_rejected,"different source atlases share only validated heights"))return 1;
    bool native=true;for(const auto& v:mesh[1].vertices)native&=v.tile==7 && v.palette==1;
    if(!check(native,"second map preserves its own tile/palette identity"))return 1;
    auto again=mesh;const auto count=stats.vertices;
    if(!check(diorama::inspect_region_mesh(inputs,separate,&again,&stats,&error) && stats.vertices==count,"region build is deterministic"))return 1;
    // Compare compact storage against the unchanged single-map production
    // mesher. Include rigid repeats, a sloped per-vertex deformation and a
    // changed model, then check ordered world vertices after a negative offset.
    auto repeated=make(3,0);repeated.grid.assign(19*18,world::kGridUndefined);
    for(int y=7;y<11;++y)for(int x=7;x<11;++x)repeated.grid[y*19+x]=0x3001;
    for(int y:{7,9})for(int x:{7,9})repeated.grid[y*19+x]=0x3003;
    repeated.grid[8*19+8]=repeated.grid[10*19+10]=0x3004;
    for(int k=0;k<8;++k)repeated.metatiles[4*8+k]=4;
    OverrideSet repeated_set;repeated_set.version=kTerrainVersion;
    TerrainMap slope;slope.group=0;slope.number=3;slope.width=19;slope.height=18;
    for(int id:{1,3,4})terrain::guard_tile(repeated,id,&slope);
    for(int y=7;y<11;++y)for(int x=7;x<11;++x) {
        TerrainCell c;c.x=x;c.y=y;c.expected=repeated.cell(x,y);
        TerrainSurface p;p.layer=3;p.height=p.thickness=16+4*(x-7);p.rise_x=4;
        c.surfaces={p};slope.cells.push_back(c);
    }
    repeated_set.terrain={slope};
    for(int i=0;i<2;++i) {
        Pattern p;if(!studio::pattern_io::from_cells(repeated,{{7+i,7+i}},i?"ground following":"rigid repeats",&p))return 1;
        p.cutout=Cutout{16,16,std::vector<uint8_t>(256,1)};p.voxel=Voxel{};
        p.voxel->ground=p.voxel->shadow=Cutout{16,16,std::vector<uint8_t>(256,0)};
        auto part=box;part.transform.size={1,1,1};p.parts={part};p.follow_ground=i!=0;
        repeated_set.patterns.push_back(p);
    }
    uint64_t previous=0;
    for(int edit=0;edit<2;++edit) {
        if(edit)repeated_set.patterns[0].parts[0].transform.size.y=1.5f;
        std::vector<diorama::AuthoredVertex> reference;diorama::DioramaStats reference_stats;
        // The connected region closes ground to the -16px preview base, so the
        // shared single-map mesher is asked for the same option here; the
        // editor's default output is checked separately below.
        if(!check(diorama::inspect_diorama_mesh(repeated,repeated_set,&reference,&reference_stats,true) &&
            diorama::inspect_region_mesh({{&repeated,0,0}},repeated_set,&mesh,&stats,&error),"reference and compact mixed models build"))return 1;
        if(!check(mesh[0].stats.geometry_hash==reference_stats.geometry_hash && stats.vertices==reference.size(),
            "ordered positions, UVs, source indices and face shades equal the original mesher"))return 1;
        if(!check(stats.models==1 && stats.model_instances==4 && stats.deformed_instances==2 && stats.raised_instances==6 &&
            stats.stored_vertices<stats.vertices,"rigid repeats share storage; ground-following placements retain deformation"))return 1;
        if(edit && !check(previous!=stats.geometry_hash,"editing model geometry invalidates rebuilt region content"))return 1;
        previous=stats.geometry_hash;
        if(!check(diorama::inspect_region_mesh({{&repeated,-50,37}},repeated_set,&mesh,&stats,&error),"translated compact region builds"))return 1;
        bool identical=mesh[0].vertices.size()==reference.size();
        for(size_t k=0;identical && k<reference.size();++k) {
            const auto& a=reference[k];const auto& b=mesh[0].vertices[k];
            identical=a.position.x-50==b.position.x && a.position.y==b.position.y && a.position.z+37==b.position.z &&
                a.u==b.u && a.v==b.v && a.tile==b.tile && a.palette==b.palette;
        }
        if(!check(identical,"compact decoding preserves every ordered world vertex and material after translation"))return 1;
    }
    // ── Connected ground base: one closed slab at -16 source pixels ────────
    // Authored Ground and legacy floors close one map cell below the floor.
    // Outward walls exist only where that solid meets empty space or a
    // non-ground neighbour, so adjoining cells and maps share no buried seam.
    const float base=-1.f;
    auto close_to=[](float p,float q){return std::abs(p-q)<1.5e-3f;};
    auto wall=[](const diorama::AuthoredVertex& p,const diorama::AuthoredVertex& q,
                 const diorama::AuthoredVertex& r,float* at,bool* xplane) {
        if(std::abs(p.position.y-q.position.y)<1e-3f && std::abs(q.position.y-r.position.y)<1e-3f) return false;
        if(std::abs(p.position.x-q.position.x)<1e-4f && std::abs(q.position.x-r.position.x)<1e-4f) {*xplane=true;*at=p.position.x;return true;}
        if(std::abs(p.position.z-q.position.z)<1e-4f && std::abs(q.position.z-r.position.z)<1e-4f) {*xplane=false;*at=p.position.z;return true;}
        return false;
    };
    auto base_triangle=[](const diorama::AuthoredVertex& p,const diorama::AuthoredVertex& q,
                          const diorama::AuthoredVertex& r,float y) {
        return std::abs(p.position.y-y)<2.5e-3f && std::abs(q.position.y-y)<2.5e-3f && std::abs(r.position.y-y)<2.5e-3f;
    };
    auto normal_x=[](const diorama::AuthoredVertex& p,const diorama::AuthoredVertex& q,const diorama::AuthoredVertex& r) {
        return (q.position.y-p.position.y)*(r.position.z-p.position.z)-(q.position.z-p.position.z)*(r.position.y-p.position.y);
    };
    auto normal_y=[](const diorama::AuthoredVertex& p,const diorama::AuthoredVertex& q,const diorama::AuthoredVertex& r) {
        return (q.position.z-p.position.z)*(r.position.x-p.position.x)-(q.position.x-p.position.x)*(r.position.z-p.position.z);
    };
    auto normal_z=[](const diorama::AuthoredVertex& p,const diorama::AuthoredVertex& q,const diorama::AuthoredVertex& r) {
        return (q.position.x-p.position.x)*(r.position.y-p.position.y)-(q.position.y-p.position.y)*(r.position.x-p.position.x);
    };
    // Native density on a wall: 16 source pixels per cell and 8 texels per
    // tile means U and V each advance two units per world unit of wall.
    auto wall_density=[](const diorama::AuthoredVertex& p,const diorama::AuthoredVertex& q,const diorama::AuthoredVertex& r) {
        const diorama::AuthoredVertex* v[3]={&p,&q,&r};float bu=0,bu_dist=0,bv=0,bv_dist=0;
        for(int i=0;i<3;++i)for(int j=i+1;j<3;++j) {
            const float du=std::abs(v[i]->u-v[j]->u),dv=std::abs(v[i]->v-v[j]->v);
            const float dx=v[i]->position.x-v[j]->position.x,dz=v[i]->position.z-v[j]->position.z;
            if(du>bu){bu=du;bu_dist=std::sqrt(dx*dx+dz*dz);}
            if(dv>bv){bv=dv;bv_dist=std::abs(v[i]->position.y-v[j]->position.y);}
        }
        return std::abs(bu_dist-.5f*bu)<1e-4f && std::abs(bv_dist-.5f*bv)<1e-4f;
    };
    // A legacy floor with no authored terrain: tops stay at 0, the base sits a
    // full cell lower, and the body's undefined neighbours become four walls.
    OverrideSet legacy_set;legacy_set.version=kTerrainVersion;
    std::vector<diorama::RegionMesh> floor_mesh;diorama::RegionStats floor_stats;
    if(!check(diorama::inspect_region_mesh({{&repeated,0,0}},legacy_set,&floor_mesh,&floor_stats,&error) &&
        floor_mesh.size()==1 && floor_mesh[0].stats.terrain_cells==0 && !floor_stats.terrain_rejected,
        "flat legacy floor region builds without authored terrain"))return 1;
    {
        float lowest=1e9f,highest=-1e9f,wall_low=1e9f;int bases=0,bad_base_wind=0,walls=0,dense=0,bad_wall_wind=0;
        for(size_t k=0;k+2<floor_mesh[0].vertices.size();k+=3) {
            const auto& p=floor_mesh[0].vertices[k];const auto& q=floor_mesh[0].vertices[k+1];const auto& r=floor_mesh[0].vertices[k+2];
            lowest=std::min({lowest,p.position.y,q.position.y,r.position.y});
            highest=std::max({highest,p.position.y,q.position.y,r.position.y});
            if(base_triangle(p,q,r,base)) {++bases;if(normal_y(p,q,r)<=0)++bad_base_wind;}
            float at=0;bool xplane=false;
            if(wall(p,q,r,&at,&xplane)) {
                ++walls;wall_low=std::min({wall_low,p.position.y,q.position.y,r.position.y});
                if(!wall_density(p,q,r))++dense;
                if((xplane&&close_to(at,7)&&normal_x(p,q,r)<=0) || (xplane&&close_to(at,11)&&normal_x(p,q,r)>=0) ||
                   (!xplane&&close_to(at,7)&&normal_z(p,q,r)<=0) || (!xplane&&close_to(at,11)&&normal_z(p,q,r)>=0))++bad_wall_wind;
            }
        }
        if(!check(close_to(lowest,base-.002f) && close_to(highest,.002f),"legacy tops stay at 0 and close at -16px"))return 1;
        if(!check(bases==16*16 && !bad_base_wind,"every legacy cell gets a downward-facing base plane (clockwise winding)"))return 1;
        if(!check(walls>0 && close_to(wall_low,base) && !dense && !bad_wall_wind,
            "legacy boundary walls reach the base with outward winding and native UV density"))return 1;
    }
    {
        bool interior=false,west=false,east=false,north=false,south=false;
        for(size_t k=0;k+2<floor_mesh[0].vertices.size();k+=3) {
            const auto& p=floor_mesh[0].vertices[k];const auto& q=floor_mesh[0].vertices[k+1];const auto& r=floor_mesh[0].vertices[k+2];
            float at=0;bool xplane=false;if(!wall(p,q,r,&at,&xplane))continue;
            const bool inside=xplane?(close_to(at,8)||close_to(at,9)||close_to(at,10)):(close_to(at,8)||close_to(at,9)||close_to(at,10));
            interior|=inside;
            west|=xplane&&close_to(at,7);east|=xplane&&close_to(at,11);
            north|=!xplane&&close_to(at,7);south|=!xplane&&close_to(at,11);
        }
        if(!check(!interior,"adjoining legacy cells emit no buried vertical seam"))return 1;
        if(!check(west&&east&&north&&south,"every undefined exterior side has an outward wall"))return 1;
    }
    // Raised/graded authored ground keeps its top and closes to the same base.
    OverrideSet terrain_only=repeated_set;terrain_only.patterns.clear();
    std::vector<diorama::AuthoredVertex> slope_mesh;diorama::DioramaStats slope_stats;
    if(!check(diorama::inspect_diorama_mesh(repeated,terrain_only,&slope_mesh,&slope_stats,true) &&
        slope_stats.terrain_cells==16,"graded authored ground builds with the connected base"))return 1;
    {
        float lowest=1e9f,highest=-1e9f;int bases=0,bad=0,walls=0,dense=0,interior=0,exterior=0;
        for(size_t k=0;k+2<slope_mesh.size();k+=3) {
            const auto& p=slope_mesh[k];const auto& q=slope_mesh[k+1];const auto& r=slope_mesh[k+2];
            lowest=std::min({lowest,p.position.y,q.position.y,r.position.y});
            highest=std::max({highest,p.position.y,q.position.y,r.position.y});
            if(base_triangle(p,q,r,base)) {++bases;if(normal_y(p,q,r)<=0)++bad;}
            float at=0;bool xplane=false;
            if(wall(p,q,r,&at,&xplane)) {
                ++walls;if(!wall_density(p,q,r))++dense;
                if(xplane?(close_to(at,8)||close_to(at,9)||close_to(at,10)):(close_to(at,8)||close_to(at,9)||close_to(at,10)))++interior;
                if(xplane?(close_to(at,7)||close_to(at,11)):(close_to(at,7)||close_to(at,11)))++exterior;
            }
        }
        if(!check(close_to(lowest,base-.002f) && std::abs(highest-2.002f)<3e-3f,"graded tops are unchanged and close at -16px"))return 1;
        if(!check(bases>0 && !bad,"authored ground base faces downward with clockwise winding"))return 1;
        if(!check(walls>0 && !dense && !interior && exterior>0,
            "graded walls repeat native source pixels with no equal-height interior seam"))return 1;
    }
    {
        std::vector<diorama::AuthoredVertex> editor_mesh;diorama::DioramaStats editor_stats;
        if(!check(diorama::inspect_diorama_mesh(repeated,terrain_only,&editor_mesh,&editor_stats),"single-map editor mesh builds"))return 1;
        float lowest=1e9f;for(const auto& v:editor_mesh)lowest=std::min(lowest,v.position.y);
        if(!check(lowest>-.01f && editor_mesh.size()<slope_mesh.size() &&
            editor_stats.geometry_hash!=slope_stats.geometry_hash,
            "single-map output keeps its existing shallow underside without the connected base"))return 1;
    }
    // A raised neighbour must not fill the lower solid's base seam. `separate`
    // carries b's rebuilt atlas guards after the map-local material check.
    auto raised=separate;
    for(auto& c:raised.terrain[1].cells) for(auto& p:c.surfaces) {p.height=32;p.thickness=32;}
    std::vector<diorama::RegionMesh> raised_mesh;diorama::RegionStats raised_stats;
    if(!check(diorama::inspect_region_mesh(inputs,raised,&raised_mesh,&raised_stats,&error) &&
        !raised_stats.terrain_rejected,"raised adjacent map region builds"))return 1;
    {
        int join=0,buried=0;
        for(const auto& chunk:raised_mesh) for(size_t k=0;k+2<chunk.vertices.size();k+=3) {
            const auto& p=chunk.vertices[k];const auto& q=chunk.vertices[k+1];const auto& r=chunk.vertices[k+2];
            float at=0;bool xplane=false;
            if(!wall(p,q,r,&at,&xplane) || !xplane || !close_to(at,11))continue;
            ++join;
            if(std::min({p.position.y,q.position.y,r.position.y})<1.f-1e-3f)++buried;
        }
        if(!check(join>0 && !buried,"a raised join exposes only the wall above the lower ground and no buried base seam"))return 1;
    }
    // Offset and reversed map ordering must produce the same world geometry.
    std::vector<diorama::RegionMap> reversed{{&b,4,0},{&a,0,0}};
    std::vector<diorama::RegionMesh> order_mesh;diorama::RegionStats order_stats;
    auto canonical=[](const std::vector<diorama::RegionMesh>& chunks) {
        std::vector<std::array<float,7>> out;
        for(const auto& chunk:chunks) for(const auto& v:chunk.vertices)
            out.push_back({v.position.x,v.position.y,v.position.z,v.u,v.v,float(v.tile),float(v.palette)});
        std::sort(out.begin(),out.end());return out;
    };
    if(!check(diorama::inspect_region_mesh(inputs,separate,&mesh,&stats,&error) &&
        diorama::inspect_region_mesh(reversed,separate,&order_mesh,&order_stats,&error) &&
        canonical(mesh)==canonical(order_mesh),"offset and reversed map ordering emit identical world geometry"))return 1;
    // Explicit water and deck cells keep their semantics: no inferred
    // underwater floor and no base under a floating span.
    auto mixed=make(4,0);
    OverrideSet mixed_set;mixed_set.version=kTerrainVersion;
    TerrainMap mixed_map;mixed_map.group=0;mixed_map.number=4;mixed_map.width=19;mixed_map.height=18;
    terrain::guard_tile(mixed,1,&mixed_map);
    auto add_cell=[&](int x,int y,std::vector<TerrainSurface> surfaces) {
        TerrainCell c;c.x=x;c.y=y;c.expected=mixed.cell(x,y);c.surfaces=std::move(surfaces);mixed_map.cells.push_back(c);
    };
    TerrainSurface ground;ground.layer=3;ground.height=ground.thickness=8;ground.kind=TerrainKind::Ground;
    TerrainSurface water;water.layer=3;water.height=16;water.thickness=0;water.kind=TerrainKind::Water;
    TerrainSurface deck;deck.layer=3;deck.height=32;deck.thickness=16;deck.kind=TerrainKind::Deck;
    TerrainSurface mixed_ground=ground;mixed_ground.layer=2;
    TerrainSurface mixed_deck=deck;mixed_deck.layer=4;
    add_cell(7,7,{ground});
    add_cell(8,7,{water});
    add_cell(9,7,{deck});
    add_cell(10,7,{mixed_ground,mixed_deck});
    mixed_set.terrain={mixed_map};
    const auto mixed_grid=mixed.grid;const auto mixed_before=mixed_set;
    std::vector<diorama::RegionMesh> mixed_mesh;diorama::RegionStats mixed_stats;
    if(!check(diorama::inspect_region_mesh({{&mixed,0,0}},mixed_set,&mixed_mesh,&mixed_stats,&error) &&
        mixed_mesh[0].stats.terrain_cells==4 && !mixed_stats.terrain_rejected,"water/deck fixture builds"))return 1;
    {
        int water_floor=0,deck_floor=0,ground_base=0,mixed_base=0,deck_bottom=0;
        for(size_t k=0;k+2<mixed_mesh[0].vertices.size();k+=3) {
            const auto& p=mixed_mesh[0].vertices[k];const auto& q=mixed_mesh[0].vertices[k+1];const auto& r=mixed_mesh[0].vertices[k+2];
            const float cx=(p.position.x+q.position.x+r.position.x)/3,cz=(p.position.z+q.position.z+r.position.z)/3;
            if(base_triangle(p,q,r,base)) {
                if(cx>7.01f&&cx<7.99f&&cz>7.01f&&cz<7.99f)++ground_base;
                if(cx>8.01f&&cx<8.99f&&cz>7.01f&&cz<7.99f)++water_floor;
                if(cx>9.01f&&cx<9.99f&&cz>7.01f&&cz<7.99f)++deck_floor;
                if(cx>10.01f&&cx<10.99f&&cz>7.01f&&cz<7.99f)++mixed_base;
            }
            if(cx>9.01f&&cx<9.99f&&cz>7.01f&&cz<7.99f&&base_triangle(p,q,r,1.f))++deck_bottom;
        }
        if(!check(ground_base>0 && mixed_base>0,"explicit ground gets the base in plain and mixed cells"))return 1;
        if(!check(!water_floor&&!deck_floor,"water/deck-only cells get no inferred base floor"))return 1;
        if(!check(deck_bottom>0,"deck underside stays at its authored span"))return 1;
    }
    if(!check(mixed.grid==mixed_grid && mixed_set==mixed_before,"water/deck document and source grid remain unchanged"))return 1;
    // Independent acceptance: neighbouring owners need not exist in this
    // snapshot's padding, and can lie beyond its complete backup-map bounds.
    int ownership_failures=0;
    for(int edge=0;edge<2;++edge) for(int reverse=0;reverse<2;++reverse) {
        auto left_source=a,right_source=b;
        left_source.grid.assign(19*18,world::kGridUndefined);
        right_source.grid.assign(19*18,world::kGridUndefined);
        if(edge==0) {
            for(int y=7;y<11;++y)for(int x=7;x<11;++x) {
                left_source.grid[y*19+x]=right_source.grid[y*19+x]=0x3001;
            }
        } else {
            left_source.grid[8*19+18]=right_source.grid[8*19]=0x3001;
        }
        const int offset=edge?19:4,join=edge?19:11;
        std::vector<diorama::RegionMap> border{{&left_source,-50,37},{&right_source,-50+offset,37}};
        if(reverse)std::reverse(border.begin(),border.end());
        if(!check(diorama::inspect_region_mesh(border,legacy_set,&mesh,&stats,&error),
                  "owner-only neighbouring floors build with missing padding and negative origins"))return 1;
        bool buried=false;
        for(const auto& chunk:mesh)for(size_t k=0;k<chunk.vertices.size();k+=3) {
            float at=0;bool xplane=false;
            buried|=wall(chunk.vertices[k],chunk.vertices[k+1],chunk.vertices[k+2],&at,&xplane) &&
                    xplane && close_to(at,float(join-50));
        }
        if(!check(!buried,edge?"no buried wall beyond snapshot bounds in either map order":
                              "no buried wall through undefined copied padding in either map order"))++ownership_failures;
    }
    if(ownership_failures)return 1;
    auto mismatched=repeated;
    for(int id:{1,3,4})mismatched.metatiles[id*8]^=1;
    if(!check(diorama::inspect_region_mesh({{&mismatched,0,0}},terrain_only,&mesh,&stats,&error) &&
        stats.terrain_rejected==16 && mesh[0].stats.terrain_cells==0,
        "source mismatches stay rejected rather than becoming authored ground"))return 1;
    float mismatch_low=0,mismatch_high=0;
    for(const auto& v:mesh[0].vertices) {
        mismatch_low=std::min(mismatch_low,v.position.y);mismatch_high=std::max(mismatch_high,v.position.y);
    }
    if(!check(close_to(mismatch_low,base-.002f) && close_to(mismatch_high,.002f),
        "rejected terrain keeps its legacy top and only receives the preview base"))return 1;
    studio::connected::Scene scene;
    studio::connected::Map left;left.id="A";left.source=a;
    auto right=left;right.id="B";right.x=4;right.source=b;scene.maps={left,right};
    using studio::connected::camera_map;
    if(!check(camera_map(scene,"A",12,9)=="B","camera enters the next primary map body") ||
       !check(camera_map(scene,"A",11.2f,9)=="A" && camera_map(scene,"B",10.8f,9)=="B","inset bounds prevent seam chatter in both directions") ||
       !check(camera_map(scene,"B",9,9)=="A","return travel changes the anchor back") ||
       !check(camera_map(scene,"A",9000,9000)=="A" && camera_map(scene,"A",std::nanf(""),9)=="A","overview, outside and nonfinite cameras do not chase void space"))return 1;
    auto shifted=scene;for(auto& m:shifted.maps)m.x-=4;
    if(!check(studio::connected::rebase(&shifted,scene,"B",&error) && shifted.maps[0].x==0 && shifted.maps[1].x==4,"window recentering preserves the original world coordinates"))return 1;
    auto negative=scene;for(auto& m:negative.maps){m.x-=50;m.z+=37;}
    shifted=scene;for(auto& m:shifted.maps)m.x-=4;
    if(!check(studio::connected::rebase(&shifted,negative,"B",&error) && shifted.maps[0].x==-50 && shifted.maps[1].x==-46 && shifted.maps[1].z==37,"negative and offset joins preserve their fixed world origin"))return 1;
    shifted=scene;shifted.maps[0].x=1;
    if(!check(!studio::connected::rebase(&shifted,scene,"B",&error) && shifted.maps[0].x==1,"conflicting shared placement refuses before changing offsets") ||
       !check(!studio::connected::rebase(&shifted,scene,"missing",&error),"unloaded travel anchor refuses"))return 1;
    auto distant=scene;distant.maps.erase(distant.maps.begin());distant.maps[0].x=8192;
    shifted=scene;shifted.maps[0].id="new";shifted.maps[0].x=8;
    if(!check(!studio::connected::rebase(&shifted,distant,"B",&error) && shifted.maps[1].x==4,"world bound failure retains the original candidate"))return 1;
    auto history=scene;history.known={{"A",0,0},{"B",4,0},{"unloaded",12,0}};
    shifted=scene;auto revisit=left;revisit.id="unloaded";revisit.x=13;shifted.maps.push_back(revisit);
    if(!check(!studio::connected::rebase(&shifted,history,"A",&error) && shifted.maps.back().x==13,"unloaded map identities still guard world placement on a later loop"))return 1;
    std::atomic<bool> cancelled{true};
    if(!check(!diorama::prepare_region(inputs,separate,&error,&cancelled),"cancelled CPU preparation produces no publishable region"))return 1;
    std::printf("[connected-test] PASS: %d checks; ownership, streaming coordinates, reuse, seams and atomic refusal\n",checks);return 0;
}

int connected_source_test(const char* root,const char* pack,const char* output) {
    studio::Decomp d;vr::overrides::OverrideSet set;
    if(!d.open(root) || !vr::overrides::load(pack,&set))return 1;
    auto* file=std::fopen(output,"wb");if(!file)return 1;
    std::fputs("{\"scenes\":[",file);
    for(int test=0;test<3;++test) {
        const bool floors=test==2;
        auto input=set;if(floors){input.terrain.clear();input.patterns.clear();}
        studio::connected::Scene scene;std::string error;
        if(!studio::connected::build(d,floors?"MAP_PETALBURG_CITY":"MAP_OLDALE_TOWN",input,&scene,&error)) {std::fprintf(stderr,"%s\n",error.c_str());std::fclose(file);return 1;}
        if(test==0)scene.maps.erase(std::remove_if(scene.maps.begin(),scene.maps.end(),[](const auto& m){return m.depth>1;}),scene.maps.end());
        std::vector<vr::diorama::RegionMesh> meshes;vr::diorama::RegionStats stats;
        if(!vr::diorama::inspect_region_mesh(scene.inputs(),input,&meshes,&stats,&error)) {std::fprintf(stderr,"%s\n",error.c_str());std::fclose(file);return 1;}
        std::set<std::string> atlases;for(const auto& m:scene.maps)atlases.insert(m.info.primary_name+"+"+m.info.secondary_name);
        if((!floors && (scene.maps.size()!=(test==0?4:6) || stats.model_instances<=stats.models || stats.stored_vertices>=stats.vertices)) ||
           (floors && atlases.size()<2) || stats.terrain_rejected || stats.unresolved_placements) {std::fprintf(stderr,"[connected-source] unexpected maps/atlases/reuse/rejections\n");std::fclose(file);return 1;}
        std::fprintf(file,"%s{\"maps\":%zu,\"atlases\":%zu,\"vertices\":%zu,\"gpu_bytes\":%zu,\"primary_cells\":%zu,\"padding_cells\":%zu,\"hidden_cells\":%zu,\"raised\":%zu,\"geometry_hash\":\"%016llx\",\"placements\":[",test?",":"",stats.maps,atlases.size(),stats.vertices,stats.gpu_bytes,stats.primary_cells,stats.padding_cells,stats.hidden_cells,stats.raised_instances,static_cast<unsigned long long>(stats.geometry_hash));
        for(size_t i=0;i<scene.maps.size();++i) {const auto& m=scene.maps[i];std::fprintf(file,"%s{\"map\":\"%s\",\"x\":%d,\"z\":%d}",i?",":"",m.id.c_str(),m.x,m.z);}
        std::fprintf(file,"],\"stored_vertices\":%zu,\"models\":%zu,\"model_instances\":%zu,\"deformed_instances\":%zu,\"batches\":%zu}",
            stats.stored_vertices,stats.models,stats.model_instances,stats.deformed_instances,stats.batches);
    }
    std::fputs("],\"status\":\"PASS\"}\n",file);std::fclose(file);
    std::printf("[connected-source] PASS: four/six authored maps with model reuse and a source-floor region with different atlases\n");return 0;
}
