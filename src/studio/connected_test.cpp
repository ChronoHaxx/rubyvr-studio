#include "connected_scene.h"
#include "pattern_io.h"
#include "terrain.h"
#include <algorithm>
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
        if(!check(diorama::inspect_diorama_mesh(repeated,repeated_set,&reference,&reference_stats) &&
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
    std::printf("[connected-test] PASS: %d checks; ownership, reuse, deformation, atlas identity, seams and atomic refusal\n",checks);return 0;
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
