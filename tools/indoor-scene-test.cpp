// SPDX-License-Identifier: GPL-3.0-or-later
#include "indoor_scene.h"
#include "pattern_io.h"
#include <cstdlib>
#include <iostream>
#include <set>
using namespace vr;
using namespace vr::overrides;
int checks=0;
void expect(bool ok,const char* what) {
    ++checks;if(!ok){std::cerr<<"FAIL: "<<what<<'\n';std::exit(1);}
}
world::Snapshot snapshot(const TerrainMap& t) {
    world::Snapshot s;s.valid=true;s.width=t.width;s.height=t.height;s.map_group=t.group;s.map_number=t.number;
    s.grid.assign(size_t(s.width)*s.height,world::kGridUndefined);s.metatiles.resize(8192);s.attributes.resize(1024);
    for(const auto& c:t.cells)s.grid[size_t(c.y)*s.width+c.x]=c.expected;
    for(const auto& [id,def]:t.tiles){s.attributes[id]=def.attr;std::copy_n(def.entries,8,s.metatiles.begin()+id*8);}
    return s;
}
OverrideSet fixture() {
    OverrideSet set;set.version=kIndoorVersion;
    TerrainMap m;m.group=4;m.number=6;m.width=19;m.height=18;m.tiles[1]={};
    for(int y=7;y<11;++y)for(int x=7;x<11;++x)m.cells.push_back({x,y,0x3001,-1,{{3,0,0,TerrainKind::Ground,-1,1}}});
    set.terrain.push_back(m);
    for(int x:{7,9}) {
        Pattern p;p.id="room-"+std::to_string(x);p.source={"MAP_SYNTHETIC",x,7};
        p.indoor=IndoorScope{4,6,19,18,7};p.w=2;p.extent=4;p.anchor=0;
        p.ids.assign(8,1);p.mask.assign(8,1);p.tiles[1]={};p.cutout=Cutout{32,64,std::vector<uint8_t>(2048,1)};
        p.voxel=Voxel{16,{32,64,std::vector<uint8_t>(2048,0)},{32,64,std::vector<uint8_t>(2048,0)}};
        Part part;part.id="box";part.kind=PartKind::Box;part.transform.position={0,0,0};part.transform.size={1,1,1};
        part.surfaces.assign(6,Surface{{0,0,1,1}});p.parts.push_back(part);set.patterns.push_back(p);
    }
    return set;
}
int main(int argc,char** argv) {
    auto set=fixture();auto s=snapshot(set.terrain[0]);
    expect(indoor_scene::describe(s,set).width==19,"new map identity enables without a C++ map whitelist");
    auto bad=set;bad.patterns.pop_back();expect(!indoor_scene::describe(s,bad).width,"partial room cannot enable controls");
    bad=set;bad.patterns.push_back(bad.patterns[0]);expect(!indoor_scene::describe(s,bad).width,"overlapping fragments refuse");
    bad=set;bad.patterns[0].mask[3]=0;expect(!indoor_scene::describe(s,bad).width,"masked hole refuses");
    bad=set;bad.patterns[1].indoor->wall_front=8;expect(!indoor_scene::describe(s,bad).width,"inconsistent room shell refuses");
    bad=set;bad.patterns[0].parts.clear();expect(!indoor_scene::describe(s,bad).width,"missing geometry refuses");
    bad=set;bad.terrain[0].cells.pop_back();expect(!indoor_scene::describe(s,bad).width,"missing floor refuses");
    bad=set;bad.terrain[0].cells[0].surfaces[0].height=48;expect(!indoor_scene::describe(s,bad).width,"priority layer is not physical room height");
    auto wrong=s;wrong.map_number=7;expect(!indoor_scene::describe(wrong,set).width,"shared art does not promote another room");
    expect(find(wrong,set).empty(),"room furniture never repeats in another map");
    auto matches=find(s,set);expect(matches.size()==2&&matches[0].x==7&&matches[1].x==9,"fixed origins never repeat over identical floor tiles");
    auto competing=set;auto motif=set.patterns[0];motif.id="repeating";motif.indoor.reset();
    competing.patterns.insert(competing.patterns.begin(),motif);
    const auto claims=resolve(s,competing);
    expect(claims.owner[7*19+7]==1&&claims.owner[7*19+9]==2,"explicit room placements win over repeating catalog art");
    auto invalid=set.patterns[0];invalid.source.x=2147483647;
    expect(!valid_parts(invalid),"invalid source origin is rejected before arithmetic overflow");
    wrong=s;wrong.grid[7*19+7]=0x3002;expect(!indoor_scene::describe(wrong,set).width,"changed furniture refuses stale room");
    wrong=s;wrong.grid[7*19+7]=0x3401;expect(!indoor_scene::describe(wrong,set).width,"changed collision refuses stale floor");
    wrong=s;wrong.metatiles[8]=1;expect(!indoor_scene::describe(wrong,set).width,"changed tileset refuses numeric alias");
    auto other=set.patterns[0];other.indoor->number=7;
    expect(!studio::pattern_io::same_key(set.patterns[0],other),"editor keeps separate rooms with identical art");
    expect(studio::pattern_io::write("build/indoor-scene-roundtrip.json",set),"v8 publishes through Studio serializer");
    OverrideSet read;expect(load("build/indoor-scene-roundtrip.json",&read)&&read==set,"v8 roundtrip preserves scope parts floor and origin");
    bad=set;bad.version=kTerrainVersion;
    expect(!studio::pattern_io::write("build/indoor-scene-roundtrip.json",bad),"legacy version cannot silently lose room scope");
    expect(load("build/indoor-scene-roundtrip.json",&read)&&read==set,"refused downgrade preserves previous file");
    if(argc==2) {
        OverrideSet pack;expect(load(argv[1],&pack),"generated pack passes production loader");
        std::set<std::pair<int,int>> rooms;
        for(const auto& p:pack.patterns)if(p.indoor)rooms.emplace(p.indoor->group,p.indoor->number);
        for(const auto& t:pack.terrain)if(rooms.count({t.group,t.number})) {
            const auto snap=snapshot(t);
            expect(indoor_scene::describe(snap,pack).width==t.width,"generated room complete guards and zero floor");
            const auto claims=resolve(snap,pack);
            for(int y=7;y<t.height-7;++y)for(int x=7;x<t.width-8;++x)
                expect(claims.owner[size_t(y)*t.width+x]>=0 && pack.patterns[claims.owner[size_t(y)*t.width+x]].indoor.has_value(),
                       "production claims cover room using scoped recipes");
        }
        expect(studio::pattern_io::write("build/interior-scenes/roundtrip.json",pack),"entire expanded pack saves through Studio");
        expect(load("build/interior-scenes/roundtrip.json",&read)&&read==pack,"expanded pack survives exact save reload");
        std::cout<<"Generated rooms checked: "<<rooms.size()<<'\n';
    }
    std::cout<<"PASS: indoor scene ("<<checks<<" checks; no graphics or input)\n";
}
