#include "foundation.h"
#include "diorama.h"
#include "pattern_io.h"
#include "terrain.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

int foundation_selftest(const char* output) {
    using namespace vr;using namespace overrides;
    int checks=0;
    auto check=[&](bool ok,const char* reason){if(ok) ++checks;else std::fprintf(stderr,"[foundation-test] FAIL: %s\n",reason);return ok;};
    world::Snapshot s;s.valid=true;s.width=19;s.height=18;s.layout_ptr=123;
    s.map_group=0;s.map_number=1;s.identity_source=world::Snapshot::IdentitySource::SourceTable;
    s.grid.assign(s.width*s.height,world::kGridUndefined);
    for(int y=7;y<11;++y) for(int x=7;x<11;++x) s.grid[y*s.width+x]=0x3001;
    s.grid[7*s.width+7]=0x3003;
    s.metatiles.resize(8192);s.attributes.resize(1024);s.vram_tiles.resize(32768);s.bg_palette.resize(256);
    for(int id=1;id<=4;++id) for(int k=0;k<4;++k) s.metatiles[id*8+k]=uint16_t(id);
    std::fill(s.vram_tiles.begin()+32,s.vram_tiles.begin()+160,0x11);s.bg_palette[1]=0x7fff;
    Pattern model;if(!studio::pattern_io::from_cells(s,{{7,7},{8,7},{7,8},{8,8}},"synthetic building",&model)) return 1;
    model.source.room="MAP_SYNTHETIC";model.cutout=Cutout{32,32,std::vector<uint8_t>(1024,1)};
    model.voxel=Voxel{};model.voxel->ground=model.voxel->shadow=Cutout{32,32,std::vector<uint8_t>(1024,0)};
    Part body;body.id="body";body.transform.position={.125f,0,-.5f};body.transform.size={1.75f,1,1.5f};
    Surface art;art.region={0,0,16,16};body.surfaces.assign(6,art);
    auto roof=body;roof.id="roof";roof.transform.position={-.5f,1,-.5f};roof.transform.size={3,.5f,2.5f};
    model.parts={body,roof};
    OverrideSet original;original.version=kTerrainVersion;original.patterns={model};
    TerrainMap map;map.group=0;map.number=1;map.width=s.width;map.height=s.height;
    for(int id:{1,2,3}) terrain::guard_tile(s,id,&map);
    for(int y=7;y<11;++y) for(int x=7;x<11;++x) {
        TerrainCell c;c.x=x;c.y=y;c.expected=s.cell(x,y);c.underlay=1;
        TerrainSurface ground;ground.layer=3;ground.height=ground.thickness=32+(x-7)*4-(y-7)*4;
        ground.rise_x=4;ground.rise_z=-4;ground.top=1;ground.side=2;ground.side_offset=8;c.surfaces={ground};map.cells.push_back(c);
    }
    original.terrain={map};OverrideSet fitted;studio::foundation::Result result;
    if(!check(studio::foundation::level(s,original,{0,7,7},&fitted,&result),"level the accepted rigid instance") ||
       !check(result.x0==7 && result.x1==8 && result.y0==7 && result.y1==8 && result.height==40 && result.changed_cells==4,
              "foundation bounds the base, excludes roof overhang and uses the highest corner") ||
       !check(fitted.patterns==original.patterns && fitted.terrain[0].tiles==map.tiles,"models and source material guards remain exact")) return 1;
    const auto before_land=terrain::resolve(s,original.terrain),after_land=terrain::resolve(s,fitted.terrain);
    bool unchanged=true,flat=true;
    for(size_t i=0;i<map.cells.size();++i) {
        const auto& before=map.cells[i];const auto& after=fitted.terrain[0].cells[i];
        if(before.x>8 || before.y>8) unchanged &= before==after;
        else {
            auto expected=before;auto& p=expected.surfaces[0];p.height=p.thickness=40;p.rise_x=p.rise_z=0;
            unchanged &= expected==after;
            for(float u:{0.f,.5f,1.f}) for(float v:{0.f,.5f,1.f}) flat &= after_land.query(after.x,after.y,3,u,v).pixels==40;
        }
    }
    if(!check(unchanged,"only pad heights change; outside terrain, layers, top/side phase, underlays and source cells preserved") ||
       !check(flat,"all pad corners and interiors query at the chosen height")) return 1;
    std::vector<diorama::AuthoredVertex> before,after;diorama::DioramaStats a,b;
    if(!check(diorama::inspect_diorama_mesh(s,original,&before,&a) && diorama::inspect_diorama_mesh(s,fitted,&after,&b),"both states use the production mesher")) return 1;
    const float move=(40-before_land.query(8,8,3,0,.5f).pixels)/16.f;
    bool rigid=a.authored_vertices==b.authored_vertices && a.raised_instances==1 && b.raised_instances==1,contact=true,old_gap=false;
    float minimum=1e6f;for(size_t i=0;i<b.authored_vertices;++i) minimum=std::min(minimum,after[b.flat_vertices+i].position.y);
    for(size_t i=0;rigid && i<a.authored_vertices;++i) {
        const auto& p=before[a.flat_vertices+i];const auto& q=after[b.flat_vertices+i];
        rigid &= p.position.x==q.position.x && p.position.z==q.position.z && std::abs(q.position.y-p.position.y-move)<1e-5f &&
                 p.tile==q.tile && p.palette==q.palette && p.u==q.u && p.v==q.v;
        if(std::abs(q.position.y-minimum)<1e-5f) {
            const int x=int(std::floor(q.position.x)),y=int(std::floor(q.position.z));
            contact &= std::abs(q.position.y-after_land.query(x,y,3,q.position.x-x,q.position.z-y).pixels/16-.002f)<1e-5f;
            old_gap |= std::abs(p.position.y-before_land.query(x,y,3,p.position.x-x,p.position.z-y).pixels/16-.002f)>.01f;
        }
    }
    if(!check(rigid,"every model vertex moves rigidly without changed dimensions or source UV/palette") ||
       !check(contact && old_gap,"the reproduced floating/buried base now meets the actual ground")) return 1;
    OverrideSet twice;
    if(!check(studio::foundation::level(s,fitted,{0,7,7},&twice,&result) && !result.changed_cells && twice==fitted,"a second click is an exact no-op")) return 1;
    const std::string path=std::string(output)+".json";OverrideSet reopened;
    if(!check(studio::pattern_io::write(path.c_str(),fitted) && load(path.c_str(),&reopened) && reopened==fitted,"foundation uses existing exact v7 persistence")) return 1;
    for(int mode=0;mode<11;++mode) {
        auto bad=original;auto source=s;
        if(mode==0) {auto& p=bad.terrain[0].cells[0].surfaces[0];p.kind=TerrainKind::Water;p.thickness=p.rise_x=p.rise_z=0;}
        if(mode==1) {auto& p=bad.terrain[0].cells[0].surfaces[0];p.kind=TerrainKind::Deck;p.thickness=4;p.rise_x=p.rise_z=0;}
        if(mode==2) bad.terrain[0].cells.erase(bad.terrain[0].cells.begin()+1);
        if(mode==3) source.metatiles[2*8]^=1;
        if(mode==4) bad.terrain[0].cells[0].surfaces[0].layer=4;
        if(mode==5) bad.patterns[0].follow_ground=true;
        if(mode==6) for(auto& part:bad.patterns[0].parts) part.transform.position.y+=.5f;
        if(mode==7) bad.patterns[0].parts[0].transform.position.x=-.5f;
        if(mode==8) source.grid[7*source.width+7]^=1;
        if(mode==9) {
            auto& ground=bad.terrain[0].cells[0].surfaces[0];ground.rise_x=ground.rise_z=0;
            auto deck=ground;deck.layer=4;deck.height=64;deck.thickness=4;deck.kind=TerrainKind::Deck;
            bad.terrain[0].cells[0].surfaces.push_back(deck);
        }
        if(mode==10) source.identity_source=world::Snapshot::IdentitySource::Unknown;
        OverrideSet destination=fitted;
        if(!check(!studio::foundation::level(source,bad,{0,7,7},&destination,&result) && destination==fitted,
                  "unsafe water/deck/partial/stale/layer/flexible/elevated/border/source edits refuse atomically")) return 1;
    }
    auto legacy=original;legacy.version=kVoxelVersion;legacy.terrain.clear();
    if(!check(studio::foundation::level(s,legacy,{0,7,7},&twice,&result) && !result.changed_cells && twice==legacy,"legacy flat scenes remain unchanged")) return 1;
    auto shared=original;auto with_prop=s;with_prop.grid[7*s.width+8]=0x3004;
    shared.terrain[0].cells[1].expected=0x3004;terrain::guard_tile(with_prop,4,&shared.terrain[0]);
    shared.patterns[0].mask[1]=0;
    for(int y=0;y<16;++y) for(int x=16;x<32;++x) shared.patterns[0].cutout->opacity[y*32+x]=0;
    Pattern prop;if(!studio::pattern_io::from_cells(with_prop,{{8,7}},"other prop",&prop)) return 1;
    prop.source.room="MAP_SYNTHETIC";prop.cutout=Cutout{16,16,std::vector<uint8_t>(256,1)};prop.voxel=Voxel{};
    prop.voxel->ground=prop.voxel->shadow=Cutout{16,16,std::vector<uint8_t>(256,0)};
    auto small=body;small.transform.position={0,0,0};small.transform.size={1,1,1};prop.parts={small};shared.patterns.push_back(prop);
    twice=fitted;
    if(!check(!studio::foundation::level(with_prop,shared,{0,7,7},&twice,&result) && twice==fitted &&
              result.message.find("overlaps another")!=std::string::npos,"another rigid object inside the pad is preserved")) return 1;
    std::printf("[foundation-test] PASS: %d checks; rigid base contact, native art, atomic refusal and exact persistence\n",checks);
    return 0;
}
