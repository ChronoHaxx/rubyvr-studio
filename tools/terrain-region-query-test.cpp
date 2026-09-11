// SPDX-License-Identifier: GPL-3.0-or-later
// Original synthetic fixtures. No renderer, game assets or external test runner.
#include "terrain.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {
namespace terrain = vr::terrain;
namespace world = vr::world;
using terrain::RegionHeight;
using terrain::RegionMapView;
using terrain::Status;
using terrain::TerrainCell;
using terrain::TerrainKind;
using terrain::TerrainMap;
using terrain::TerrainSurface;

struct Check {
    size_t count = 0;
    void expect(bool ok, const std::string& why) {
        ++count;
        if(!ok) throw std::runtime_error(why);
    }
};

bool near(float a, float b) { return std::abs(a-b)<=1e-5f; }
void no_owner(Check& t, const RegionHeight& r, const std::string& why) {
    t.expect(r.height.status==Status::Unresolved && r.height.pixels==0 &&
             r.height.surface==nullptr && !r.height.resolved() &&
             r.map_group==-1 && r.map_number==-1 && r.cell_x==-1 &&
             r.cell_y==-1 && r.u==0 && r.v==0, why);
}
void owned(Check& t, const RegionHeight& r, int group, int number,
           int x, int y, float u, float v, Status status, float pixels) {
    t.expect(r.map_group==group && r.map_number==number && r.cell_x==x &&
             r.cell_y==y && near(r.u,u) && near(r.v,v), "owner and local coordinates");
    t.expect(r.height.status==status && near(r.height.pixels,pixels), "status and pixel height");
    t.expect(r.height.resolved()==(status==Status::Authored || status==Status::LegacyFlat),
             "resolved predicate");
    t.expect((r.height.surface!=nullptr)==(status==Status::Authored), "surface presence");
}
bool equal_result(const RegionHeight& a, const RegionHeight& b) {
    return a.height.status==b.height.status && a.height.pixels==b.height.pixels &&
        a.height.surface==b.height.surface && a.map_group==b.map_group &&
        a.map_number==b.map_number && a.cell_x==b.cell_x && a.cell_y==b.cell_y &&
        a.u==b.u && a.v==b.v;
}

TerrainSurface ground(int height, int layer=3) {
    TerrainSurface p;
    p.layer=layer;p.height=p.thickness=height;p.top=1;p.side=2;
    return p;
}
TerrainSurface water() {
    auto p=ground(8,1);p.kind=TerrainKind::Water;p.thickness=0;p.top=3;
    return p;
}
TerrainSurface deck() {
    auto p=ground(16,3);p.kind=TerrainKind::Deck;p.thickness=4;
    return p;
}

struct Fixture {
    world::Snapshot source;
    std::vector<TerrainMap> document;
    terrain::Resolved resolved;

    Fixture(int group=2, int number=1, int width=19, int height=18) {
        source.valid=true;source.map_group=group;source.map_number=number;
        source.identity_source=world::Snapshot::IdentitySource::SourceTable;
        source.width=width;source.height=height;
        // Padding is deliberately defined: it must never become a region owner.
        source.grid.assign(size_t(width)*size_t(height),0x3001);
        source.metatiles.resize(8192);source.attributes.resize(1024);
        source.vram_tiles.assign(32768,0x11);source.bg_palette.assign(256,0x1234);
        for(int id=1;id<=3;++id) {
            source.attributes[id]=uint16_t(id);
            for(int k=0;k<8;++k) source.metatiles[id*8+k]=uint16_t(id*16+k);
        }
        TerrainMap m;
        m.group=group;m.number=number;m.width=width;m.height=height;
        for(uint16_t id:{1,2,3}) {
            if(!terrain::guard_tile(source,id,&m)) throw std::runtime_error("fixture guard construction");
        }
        document.push_back(std::move(m));
    }
    Fixture(const Fixture&)=delete;
    Fixture& operator=(const Fixture&)=delete;

    void add(int x, int y, std::initializer_list<TerrainSurface> surfaces) {
        TerrainCell c;
        c.x=x;c.y=y;c.expected=source.cell(x,y);c.underlay=3;c.surfaces=surfaces;
        document[0].cells.push_back(std::move(c));
    }
    void fill(int height) {
        for(int y=7;y<source.height-7;++y)
            for(int x=7;x<source.width-8;++x) add(x,y,{ground(height)});
    }
    void finish(Check& t) {
        t.expect(source.valid && source.has_map_identity() && source.valid_connections(),
                 "synthetic snapshot validity");
        t.expect(terrain::valid(document), "real terrain validation accepts fixture");
        resolved=terrain::resolve(source,document);
    }
    RegionMapView view(int x=0, int z=0) const { return {&source,&resolved,x,z}; }
};

// A valid copy from an unloaded third map supplies a deliberately distracting
// 96px surface in padding. It is resolved through production code, not patched
// into Resolved by the test. Region ownership must ignore it in either order.
void padding_copy(Fixture& f, int x, int y) {
    auto ghost=f.document[0];ghost.group=200;ghost.number=201;
    for(auto& c:ghost.cells) c.surfaces={ground(96)};
    f.document.push_back(std::move(ghost));
    f.source.connections.push_back({200,201,19,18,x,y,7,7,4,4,true});
}

void bridge_layers(Check& t) {
    Fixture f;f.add(7,7,{water(),deck()});f.finish(t);
    const std::vector<RegionMapView> maps{f.view()};
    t.expect(f.resolved.matched==1 && f.resolved.rejected==0, "real stacked terrain resolves");
    for(int layer=-1;layer<=15;++layer) {
        const auto r=terrain::query_region(maps,7.25,7.75,layer);
        const bool selected=layer==1 || layer==3;
        owned(t,r,2,1,7,7,.25f,.75f,selected?Status::Authored:Status::Unresolved,
              layer==1?8.f:layer==3?16.f:0.f);
        if(selected) t.expect(r.height.surface==&f.document[0].cells[0].surfaces[layer==1?0:1],
                              "bridge pointer borrows exact original surface");
    }
}

void sole_and_legacy(Check& t) {
    Fixture f;
    f.add(7,7,{ground(16,3)});f.add(8,7,{ground(24,0)});f.add(9,7,{ground(32,15)});
    f.finish(t);
    const std::vector<RegionMapView> maps{f.view()};
    for(int x=7;x<=9;++x) for(int layer:{-1,0,15}) {
        const auto r=terrain::query_region(maps,x+.5,7.5,layer);
        owned(t,r,2,1,x,7,.5f,.5f,Status::Authored,float(16+(x-7)*8));
        t.expect(r.height.surface==&f.document[0].cells[size_t(x-7)].surfaces[0],
                 "sole-surface pointer identity");
    }
    owned(t,terrain::query_region(maps,7.5,7.5,2),2,1,7,7,.5f,.5f,Status::Unresolved,0);
    for(int layer=-1;layer<=15;++layer)
        owned(t,terrain::query_region(maps,10.5,10.5,layer),2,1,10,10,.5f,.5f,Status::LegacyFlat,0);
    // An entirely unauthored document is also valid; no -16px preview floor.
    f.document.clear();f.finish(t);
    owned(t,terrain::query_region(maps,7.5,7.5,-1),2,1,7,7,.5f,.5f,Status::LegacyFlat,0);
}

void undefined_owned_cells(Check& t) {
    Fixture f;f.add(7,7,{ground(16)});
    f.source.grid[size_t(8)*f.source.width+8]=world::kGridUndefined;
    f.source.grid[size_t(7)*f.source.width+7]=world::kGridUndefined;
    f.finish(t);
    const std::vector<RegionMapView> maps{f.view(-10,-10)};
    t.expect(f.resolved.query(8,8,3).status==Status::LegacyFlat, "legacy query has no source-cell check");
    t.expect(f.resolved.query(7,7,3).status==Status::SourceMismatch, "undefined formerly authored cell rejects guard");
    for(int layer:{-1,0,1,3,15}) {
        owned(t,terrain::query_region(maps,-1.75,-1.25,layer),2,1,8,8,.25f,.75f,Status::Unresolved,0);
        owned(t,terrain::query_region(maps,-2.75,-2.25,layer),2,1,7,7,.25f,.75f,Status::Unresolved,0);
    }
}

void triangular_samples(Check& t) {
    Fixture f;auto p=ground(16);p.rise_x=8;p.rise_z=-4;p.corner_delta=4;
    f.add(7,7,{p});f.finish(t);
    const std::vector<RegionMapView> maps{f.view(-9,-12)};
    struct Sample {double u,v;float expected;};
    // Independent constants, not surface_height() or another query as oracle.
    // Bilinear interpolation would instead give 15.75, 21.75 and 19 pixels.
    for(const auto& s:std::array<Sample,3>{{{.25,.75,16},{.75,.25,22},{.5,.5,20}}}) {
        const auto r=terrain::query_region(maps,-2+s.u,-5+s.v,3);
        owned(t,r,2,1,7,7,float(s.u),float(s.v),Status::Authored,s.expected);
        t.expect(r.height.surface==&f.document[0].cells[0].surfaces[0], "grade pointer is original");
    }
}

void east_west_join(Check& t) {
    Fixture west(4,1),east(4,2);
    west.fill(12);east.fill(28);padding_copy(west,11,7);padding_copy(east,3,7);
    west.finish(t);east.finish(t);
    std::vector<RegionMapView> maps{west.view(-11,-10),east.view(-7,-10)};
    t.expect(west.resolved.query(11,8,3).pixels==96 && east.resolved.query(6,8,3).pixels==96,
             "real copied-padding distractors are higher than both bodies");
    for(int order=0;order<2;++order) {
        owned(t,terrain::query_region(maps,-.25,-1.25,3),4,1,10,8,.75f,.75f,Status::Authored,12);
        owned(t,terrain::query_region(maps,0,-1.25,3),4,2,7,8,0,.75f,Status::Authored,28);
        owned(t,terrain::query_region(maps,.25,-1.25,3),4,2,7,8,.25f,.75f,Status::Authored,28);
        owned(t,terrain::query_region(maps,-4,-3,3),4,1,7,7,0,0,Status::Authored,12);
        owned(t,terrain::query_region(maps,0,-3,3),4,2,7,7,0,0,Status::Authored,28);
        // These doubles would round onto another local cell if the integer
        // translation were applied before flooring. Float u may round to 1.
        owned(t,terrain::query_region(maps,std::nextafter(0.,-1.),-1.25,3),
              4,1,10,8,1,.75f,Status::Authored,12);
        owned(t,terrain::query_region(maps,std::nextafter(0.,1.),-1.25,3),
              4,2,7,8,0,.75f,Status::Authored,28);
        owned(t,terrain::query_region(maps,std::nextafter(4.,0.),-1.25,3),
              4,2,10,8,1,.75f,Status::Authored,28);
        no_owner(t,terrain::query_region(maps,std::nextafter(-4.,-5.),-1.25,3), "just west of body");
        no_owner(t,terrain::query_region(maps,4,-1.25,3), "exclusive east edge");
        no_owner(t,terrain::query_region(maps,0,1,3), "exclusive south edge");
        no_owner(t,terrain::query_region(maps,4.5,-1.25,3), "defined padding outside all bodies");
        std::reverse(maps.begin(),maps.end());
    }
    // The copied 96px surface cannot rescue an undefined owning body cell.
    east.source.grid[size_t(8)*east.source.width+7]=world::kGridUndefined;
    east.finish(t);
    owned(t,terrain::query_region(maps,.25,-1.25,3),4,2,7,8,.25f,.75f,Status::Unresolved,0);
}

void offset_north_south_join(Check& t) {
    Fixture north(5,1),south(5,2);
    north.fill(8);south.fill(24);padding_copy(north,7,11);padding_copy(south,7,3);
    north.finish(t);south.finish(t);
    // South is shifted two columns east, as a nonzero connected_scene offset.
    std::vector<RegionMapView> maps{north.view(-10,-11),south.view(-8,-7)};
    t.expect(north.resolved.query(9,11,3).pixels==96 && south.resolved.query(7,6,3).pixels==96,
             "north/south padding distractors resolve");
    for(int order=0;order<2;++order) {
        owned(t,terrain::query_region(maps,-.25,-.25,3),5,1,9,10,.75f,.75f,Status::Authored,8);
        owned(t,terrain::query_region(maps,-.25,0,3),5,2,7,7,.75f,0,Status::Authored,24);
        owned(t,terrain::query_region(maps,.25,.25,3),5,2,8,7,.25f,.25f,Status::Authored,24);
        owned(t,terrain::query_region(maps,-.25,std::nextafter(0.,-1.),3),
              5,1,9,10,.75f,1,Status::Authored,8);
        owned(t,terrain::query_region(maps,-.25,std::nextafter(0.,1.),3),
              5,2,7,7,.75f,0,Status::Authored,24);
        owned(t,terrain::query_region(maps,-1,0,3),5,2,7,7,0,0,Status::Authored,24);
        owned(t,terrain::query_region(maps,1,0,3),5,2,9,7,0,0,Status::Authored,24);
        no_owner(t,terrain::query_region(maps,-2,0,3), "offset leaves west frontier unowned");
        no_owner(t,terrain::query_region(maps,2,-.25,3), "offset leaves east frontier unowned");
        no_owner(t,terrain::query_region(maps,-.25,4,3), "exclusive south edge after offset join");
        std::reverse(maps.begin(),maps.end());
    }
}

void corners_and_reordering(Check& t) {
    std::array<std::unique_ptr<Fixture>,4> f;
    std::vector<RegionMapView> maps;
    for(int i=0;i<4;++i) {
        f[size_t(i)]=std::make_unique<Fixture>(6,i);
        f[size_t(i)]->fill(8*(i+1));
        if(i==3) f[size_t(i)]->document[0].cells[0].surfaces={water(),deck()};
        if(i==1) f[size_t(i)]->source.grid[size_t(7)*19+7]^=0x400;
        f[size_t(i)]->finish(t);
        maps.push_back(f[size_t(i)]->view(-11+4*(i%2),-11+4*(i/2)));
    }
    owned(t,terrain::query_region(maps,0,0,1),6,3,7,7,0,0,Status::Authored,8);
    owned(t,terrain::query_region(maps,-.25,0,3),6,2,10,7,.75f,0,Status::Authored,24);
    owned(t,terrain::query_region(maps,0,-.25,3),6,1,7,10,0,.75f,Status::Authored,16);
    owned(t,terrain::query_region(maps,-.25,-.25,3),6,0,10,10,.75f,.75f,Status::Authored,8);
    struct Probe {double x,z;int layer;};
    const std::array<Probe,8> probes{{{0,0,1},{0,0,3},{0,0,-1},{-3.5,-3.5,3},
                                     {.25,-3.75,3},{-.25,0,3},{0,-.25,3},{4,4,3}}};
    std::array<RegionHeight,8> expected;
    for(size_t i=0;i<probes.size();++i)
        expected[i]=terrain::query_region(maps,probes[i].x,probes[i].z,probes[i].layer);
    t.expect(expected[2].height.status==Status::Unresolved && expected[2].map_number==3,
             "ambiguous corner stays owned and unresolved");
    t.expect(expected[4].height.status==Status::SourceMismatch && expected[4].map_number==1,
             "source mismatch probe is owned");
    std::array<int,4> order{{0,1,2,3}};
    do {
        std::vector<RegionMapView> reordered;
        for(int i:order) reordered.push_back(maps[size_t(i)]);
        for(size_t i=0;i<probes.size();++i)
            t.expect(equal_result(expected[i],terrain::query_region(reordered,probes[i].x,probes[i].z,probes[i].layer)),
                     "all 24 orders preserve status, height, owner, fractions and pointer");
    } while(std::next_permutation(order.begin(),order.end()));
}

void source_and_material_guards(Check& t) {
    for(int mode=0;mode<6;++mode) {
        Fixture west(7,1),east(7,2);
        west.fill(12);east.fill(28);padding_copy(west,11,7);west.finish(t);
        if(mode==0) east.source.grid[size_t(8)*19+7]^=0x400; // collision
        if(mode==1) east.source.grid[size_t(8)*19+7]^=0x1000; // packed gameplay layer
        if(mode==2) east.source.grid[size_t(8)*19+7]^=1; // metatile id
        if(mode==3) east.source.metatiles[8]^=0x400; // source/top definition
        if(mode==4) east.source.attributes[2]^=1; // side material
        if(mode==5) east.source.metatiles[24]^=1; // explicit underlay
        east.finish(t);
        t.expect(east.resolved.rejected>0, "real resolve rejects source/material guard mutation");
        std::vector<RegionMapView> maps{west.view(-11,-10),east.view(-7,-10)};
        for(int order=0;order<2;++order) {
            for(int layer:{-1,0,1,3,15})
                owned(t,terrain::query_region(maps,.25,-1.25,layer),7,2,7,8,.25f,.75f,Status::SourceMismatch,0);
            t.expect(west.resolved.query(11,8,3).pixels==96, "a tempting padding fallback exists");
            std::reverse(maps.begin(),maps.end());
        }
    }
}

void invalid_coordinates_and_layers(Check& t) {
    Fixture f;f.add(7,7,{ground(16)});f.finish(t);
    const std::vector<RegionMapView> maps{f.view()};
    const double inf=std::numeric_limits<double>::infinity();
    for(double value:{std::numeric_limits<double>::quiet_NaN(),inf,-inf,
                      std::numeric_limits<double>::max(),-std::numeric_limits<double>::max(),1e100,-1e100}) {
        no_owner(t,terrain::query_region(maps,value,7.5,3), "nonfinite/huge world x");
        no_owner(t,terrain::query_region(maps,7.5,value,3), "nonfinite/huge world z");
        no_owner(t,terrain::query_region(maps,value,value,3), "nonfinite/huge both coordinates");
    }
    for(int layer:{std::numeric_limits<int>::min(),-2,16,std::numeric_limits<int>::max()})
        no_owner(t,terrain::query_region(maps,7.5,7.5,layer), "invalid gameplay layer");
    no_owner(t,terrain::query_region({},7.5,7.5,3), "empty region");
    no_owner(t,terrain::query_region(maps,6.5,7.5,3), "defined west padding is not a body");
    no_owner(t,terrain::query_region(maps,7.5,6.5,3), "defined north padding is not a body");
    no_owner(t,terrain::query_region(maps,11,7.5,3), "exclusive east body edge");
    no_owner(t,terrain::query_region(maps,7.5,11,3), "exclusive south body edge");
}

void null_and_storage_guards(Check& t) {
    Fixture good(8,1);good.add(7,7,{ground(16)});good.finish(t);
    no_owner(t,terrain::query_region({RegionMapView{}},7.5,7.5,3), "both pointers null");
    no_owner(t,terrain::query_region({{nullptr,&good.resolved,0,0}},7.5,7.5,3), "source pointer null");
    no_owner(t,terrain::query_region({{&good.source,nullptr,0,0}},7.5,7.5,3), "resolved pointer null");
    for(int mode=0;mode<13;++mode) {
        Fixture bad(8,2);bad.add(7,7,{ground(24)});bad.finish(t);
        // Deliberate malformed-storage views; never hand these to resolve().
        if(mode==0) bad.source.grid.clear();
        if(mode==1) bad.source.grid.pop_back();
        if(mode==2) bad.source.grid.push_back(0);
        if(mode==3) bad.resolved.cells.clear();
        if(mode==4) bad.resolved.cells.pop_back();
        if(mode==5) bad.resolved.cells.push_back(nullptr);
        if(mode==6) bad.resolved.mismatched.clear();
        if(mode==7) bad.resolved.mismatched.pop_back();
        if(mode==8) bad.resolved.mismatched.push_back(false);
        if(mode==9) ++bad.resolved.width;
        if(mode==10) ++bad.resolved.height;
        if(mode==11) bad.resolved.width=std::numeric_limits<int>::min();
        if(mode==12) bad.resolved.height=std::numeric_limits<int>::max();
        no_owner(t,terrain::query_region({bad.view()},7.5,7.5,3), "malformed storage mode "+std::to_string(mode));
        // A valid early owner must not hide an invalid later map, even far away.
        std::vector<RegionMapView> maps{good.view(),bad.view(100,100)};
        for(int order=0;order<2;++order) {
            no_owner(t,terrain::query_region(maps,7.5,7.5,3), "whole-region storage refusal");
            std::reverse(maps.begin(),maps.end());
        }
    }
}

void invalid_dimensions_and_offsets(Check& t) {
    Fixture f;f.add(7,7,{ground(16)});f.finish(t);
    const int lo=std::numeric_limits<int>::min(),hi=std::numeric_limits<int>::max();
    const std::array<std::pair<int,int>,16> dimensions{{
        {15,18},{19,14},{0,18},{19,0},{-1,18},{19,-1},{1025,15},{16,1025},
        {1024,15},{16,1024},{101,102},{lo,18},{19,lo},{hi,hi},{641,16},{16,641}}};
    for(const auto& [w,h]:dimensions) {
        auto bad=f.source;bad.width=w;bad.height=h;
        // Exercises dimension ordering before connection subtraction too.
        bad.connections={{3,4,19,18,7,0,7,7,1,1,true}};
        no_owner(t,terrain::query_region({{&bad,&f.resolved,0,0}},7.5,7.5,3), "invalid dimension bounds/product");
    }
    // Isolate the product bound with otherwise exact, coherent-sized storage.
    auto large=f.source;large.width=128;large.height=128;large.grid.resize(16384);
    auto resolved=f.resolved;resolved.width=128;resolved.height=128;
    resolved.cells.assign(16384,nullptr);resolved.mismatched.assign(16384,false);
    no_owner(t,terrain::query_region({{&large,&resolved,0,0}},7.5,7.5,3), "cell-count bound despite exact lengths");
    for(int offset:{lo,-8193,8193,hi}) {
        no_owner(t,terrain::query_region({f.view(offset,0)},7.5,7.5,3), "invalid x offset");
        no_owner(t,terrain::query_region({f.view(0,offset)},7.5,7.5,3), "invalid z offset");
    }
}

void identity_and_snapshot_guards(Check& t) {
    Fixture f;f.add(7,7,{ground(16)});f.finish(t);
    for(int mode=0;mode<7;++mode) {
        auto bad=f.source;
        if(mode==0) bad.valid=false;
        if(mode==1) bad.identity_source=world::Snapshot::IdentitySource::Unknown;
        if(mode==2) bad.identity_source=static_cast<world::Snapshot::IdentitySource>(255);
        if(mode==3) bad.map_group=-1;
        if(mode==4) bad.map_group=256;
        if(mode==5) bad.map_number=-1;
        if(mode==6) bad.map_number=256;
        no_owner(t,terrain::query_region({{&bad,&f.resolved,0,0}},7.5,7.5,3), "invalid snapshot/identity");
    }
    f.source.identity_source=world::Snapshot::IdentitySource::LiveCapture;f.finish(t);
    owned(t,terrain::query_region({f.view()},7.5,7.5,3),2,1,7,7,.5f,.5f,Status::Authored,16);
}

void connection_guards(Check& t) {
    Fixture f;f.add(7,7,{ground(16)});f.finish(t);
    const world::ConnectionSlice valid{3,4,19,18,7,0,7,7,1,1,false};
    struct Mutation {int world::ConnectionSlice::* field;int value;};
    const int hi=std::numeric_limits<int>::max(),lo=std::numeric_limits<int>::min();
    const Mutation mutations[]{
        {&world::ConnectionSlice::group,-1},{&world::ConnectionSlice::group,256},
        {&world::ConnectionSlice::number,-1},{&world::ConnectionSlice::number,256},
        {&world::ConnectionSlice::width,15},{&world::ConnectionSlice::width,1025},
        {&world::ConnectionSlice::width,lo},{&world::ConnectionSlice::width,hi},
        {&world::ConnectionSlice::height,14},{&world::ConnectionSlice::height,1025},
        {&world::ConnectionSlice::height,lo},{&world::ConnectionSlice::height,hi},
        {&world::ConnectionSlice::x,-1},{&world::ConnectionSlice::x,hi},
        {&world::ConnectionSlice::y,-1},{&world::ConnectionSlice::y,hi},
        {&world::ConnectionSlice::source_x,6},{&world::ConnectionSlice::source_x,hi},
        {&world::ConnectionSlice::source_y,6},{&world::ConnectionSlice::source_y,hi},
        {&world::ConnectionSlice::w,0},{&world::ConnectionSlice::w,lo},{&world::ConnectionSlice::w,hi},
        {&world::ConnectionSlice::h,0},{&world::ConnectionSlice::h,lo},{&world::ConnectionSlice::h,hi},
        {&world::ConnectionSlice::y,7} // destination intersects primary body
    };
    for(const auto& m:mutations) {
        auto bad=f.source;auto c=valid;c.*(m.field)=m.value;bad.connections={c};
        no_owner(t,terrain::query_region({{&bad,&f.resolved,0,0}},7.5,7.5,3), "invalid connection rectangle");
    }
    auto bad=f.source;auto c=valid;c.width=101;c.height=102;bad.connections={c};
    no_owner(t,terrain::query_region({{&bad,&f.resolved,0,0}},7.5,7.5,3), "connection source cell budget");
    bad=f.source;bad.connections.assign(65,valid);
    no_owner(t,terrain::query_region({{&bad,&f.resolved,0,0}},7.5,7.5,3), "connection count over 64");
    // Repeated padding slices are permitted; the existing resolver owns their
    // last-writer semantics. No loaded adjacency graph is inferred by this API.
    f.source.connections.assign(64,valid);f.finish(t);
    owned(t,terrain::query_region({f.view()},7.5,7.5,3),2,1,7,7,.5f,.5f,Status::Authored,16);
}

void duplicate_and_overlap_guards(Check& t) {
    Fixture a(9,1),b(9,2),duplicate(9,1);
    a.fill(8);b.fill(16);duplicate.fill(24);a.finish(t);b.finish(t);duplicate.finish(t);
    std::vector<RegionMapView> maps{a.view(),duplicate.view(100,100)};
    for(int order=0;order<2;++order) {
        no_owner(t,terrain::query_region(maps,7.5,7.5,3), "duplicate identity even when bodies disjoint");
        std::reverse(maps.begin(),maps.end());
    }
    for(const auto& [x,z]:std::array<std::pair<int,int>,5>{{{0,0},{1,0},{0,1},{3,3},{-3,-3}}}) {
        maps={a.view(),b.view(x,z)};
        for(int order=0;order<2;++order) {
            no_owner(t,terrain::query_region(maps,7.5,7.5,3), "positive-area overlap rejects entire region");
            no_owner(t,terrain::query_region(maps,1000,1000,3), "invalid overlapping region outside probe");
            std::reverse(maps.begin(),maps.end());
        }
    }
    // Even two bodies containing undefined cells are still overlapping bodies.
    std::fill(a.source.grid.begin(),a.source.grid.end(),world::kGridUndefined);a.finish(t);
    no_owner(t,terrain::query_region({a.view(),b.view(1,1)},8.5,8.5,3), "undefined body cannot evade overlap guard");
    // Any invalid late map (including null) must prevent a valid early result.
    no_owner(t,terrain::query_region({b.view(),RegionMapView{}},7.5,7.5,3), "null later view is not ignored");
}

void supported_limits(Check& t) {
    std::vector<std::unique_ptr<Fixture>> fixtures;
    std::vector<RegionMapView> maps;
    for(int i=0;i<10;++i) {
        auto f=std::make_unique<Fixture>(10,i,16,15);
        f->add(7,7,{ground(8+i)});f->finish(t);
        maps.push_back(f->view(i,0));fixtures.push_back(std::move(f));
        if(i==8) {
            t.expect(maps.size()==9, "maximum region size constructed");
            for(int j=0;j<9;++j)
                owned(t,terrain::query_region(maps,7.25+j,7.75,3),10,j,7,7,.25f,.75f,Status::Authored,float(8+j));
        }
    }
    no_owner(t,terrain::query_region(maps,7.5,7.5,3), "ten unique non-overlapping maps exceed limit");
    for(const auto& [width,height]:std::array<std::pair<int,int>,3>{{{16,15},{640,16},{16,640}}}) {
        Fixture f(255,255,width,height);f.add(7,7,{ground(16)});f.finish(t);
        for(int x:{-8192,8192}) for(int z:{-8192,8192}) {
            owned(t,terrain::query_region({f.view(x,z)},x+7.25,z+7.75,3),255,255,7,7,.25f,.75f,Status::Authored,16);
            const auto far=terrain::query_region({f.view(x,z)},x+width-8.25,z+height-7.25,3);
            owned(t,far,255,255,width-9,height-8,.75f,.75f,
                  width==16 && height==15?Status::Authored:Status::LegacyFlat,
                  width==16 && height==15?16.f:0.f);
        }
    }
    Fixture zero(0,0);zero.add(7,7,{ground(16)});zero.finish(t);
    owned(t,terrain::query_region({zero.view(-7,-7)},-0.,0.,3),0,0,7,7,0,0,Status::Authored,16);
}

bool equal_snapshot(const world::Snapshot& a, const world::Snapshot& b) {
    if(std::tie(a.valid,a.map_group,a.map_number,a.identity_source,a.layout_ptr,
                a.width,a.height,a.cam_px,a.cam_py,a.view_x,a.view_y,a.view_base_x,a.view_base_y,a.player_index)!=
       std::tie(b.valid,b.map_group,b.map_number,b.identity_source,b.layout_ptr,
                b.width,b.height,b.cam_px,b.cam_py,b.view_x,b.view_y,b.view_base_x,b.view_base_y,b.player_index)) return false;
    for(int i=0;i<world::kObjectEventCount;++i) {
        const auto& x=a.objects[i];const auto& y=b.objects[i];
        if(std::tie(x.active,x.invisible,x.is_player,x.graphics_id,x.sprite_id,x.elevation,x.facing,x.x,x.y)!=
           std::tie(y.active,y.invisible,y.is_player,y.graphics_id,y.sprite_id,y.elevation,y.facing,y.x,y.y)) return false;
    }
    return a.grid==b.grid && a.connections==b.connections && a.metatiles==b.metatiles &&
           a.attributes==b.attributes && a.vram_tiles==b.vram_tiles && a.bg_palette==b.bg_palette;
}
bool equal_resolved(const terrain::Resolved& a, const terrain::Resolved& b) {
    return a.width==b.width && a.height==b.height && a.matched==b.matched &&
           a.rejected==b.rejected && a.cells==b.cells && a.mismatched==b.mismatched;
}

void immutable_borrowed_results(Check& t) {
    Fixture f;
    f.add(7,7,{water(),deck()});
    auto grade=ground(16);grade.rise_x=8;grade.rise_z=-4;grade.corner_delta=4;
    f.add(8,7,{grade});f.add(9,7,{ground(24)});
    f.source.grid[size_t(7)*19+9]^=0x400;
    f.source.grid[size_t(7)*19+10]=world::kGridUndefined;
    f.source.layout_ptr=123;f.source.cam_px=-35;f.source.view_base_y=24;
    f.source.objects[0].active=true;f.source.objects[0].facing=3;f.source.objects[0].x=7;
    f.finish(t);
    const auto source=f.source;
    const auto document=f.document;
    const auto resolved=f.resolved;
    const auto* grid_address=f.source.grid.data();
    const auto* resolved_address=f.resolved.cells.data();
    const auto* document_address=f.document.data();
    const std::vector<RegionMapView> maps{f.view(-11,-10)};
    const auto views=maps;
    struct Probe {double x,z;int layer;};
    const std::array<Probe,8> probes{{{-3.75,-2.25,1},{-3.75,-2.25,3},{-3.75,-2.25,-1},
        {-2.75,-2.25,3},{-1.75,-2.25,3},{-.75,-2.25,3},{-2.5,-1.5,3},{100,100,3}}};
    std::array<RegionHeight,8> expected;
    for(size_t i=0;i<probes.size();++i)
        expected[i]=terrain::query_region(maps,probes[i].x,probes[i].z,probes[i].layer);
    t.expect(expected[0].height.surface==&f.document[0].cells[0].surfaces[0] &&
             expected[1].height.surface==&f.document[0].cells[0].surfaces[1] &&
             expected[3].height.surface==&f.document[0].cells[1].surfaces[0],
             "authored pointers are into original document, not copies");
    t.expect(expected[0].height.pixels==8 && expected[1].height.pixels==16 &&
             expected[2].height.status==Status::Unresolved && expected[3].height.pixels==16 &&
             expected[4].height.status==Status::SourceMismatch && expected[5].height.status==Status::Unresolved &&
             expected[5].map_number==1 && expected[6].height.status==Status::LegacyFlat && expected[7].map_number==-1,
             "repeated-query probes cover independent values and every status");
    for(int repeat=0;repeat<1000;++repeat) for(size_t i=0;i<probes.size();++i)
        t.expect(equal_result(expected[i],terrain::query_region(maps,probes[i].x,probes[i].z,probes[i].layer)),
                 "8,000 repeated results agree exactly including pointers");
    no_owner(t,terrain::query_region(maps,std::numeric_limits<double>::quiet_NaN(),0,3), "invalid query also preserves inputs");
    t.expect(equal_snapshot(f.source,source), "all snapshot fields and source arrays unchanged");
    t.expect(equal_resolved(f.resolved,resolved), "resolved dimensions, counters, cells and mismatches unchanged");
    t.expect(f.document==document, "entire authored document unchanged");
    t.expect(f.source.grid.data()==grid_address && f.resolved.cells.data()==resolved_address &&
             f.document.data()==document_address, "backing storage addresses retained");
    t.expect(maps.size()==views.size() && maps[0].source==views[0].source && maps[0].resolved==views[0].resolved &&
             maps[0].x==views[0].x && maps[0].z==views[0].z, "input map views unchanged");
    t.expect(expected[0].height.surface->kind==TerrainKind::Water && expected[1].height.surface->kind==TerrainKind::Deck &&
             expected[3].height.surface->corner_delta==4, "borrowed results remain readable after repeated queries");
}
} // namespace

int main() {
    struct Case {const char* name;void (*run)(Check&);};
    const Case cases[]{
        {"bridge_explicit_layers",bridge_layers},
        {"sole_surface_and_legacy_zero",sole_and_legacy},
        {"undefined_cells_retain_owner",undefined_owned_cells},
        {"independent_triangular_interpolation",triangular_samples},
        {"negative_east_west_join_and_copied_padding",east_west_join},
        {"offset_north_south_join_and_copied_padding",offset_north_south_join},
        {"shared_corners_and_all_24_map_orders",corners_and_reordering},
        {"source_and_material_mismatch_no_fallback",source_and_material_guards},
        {"outside_empty_nonfinite_huge_and_invalid_layers",invalid_coordinates_and_layers},
        {"null_exact_storage_and_atomic_region_refusal",null_and_storage_guards},
        {"dimension_product_and_offset_guards",invalid_dimensions_and_offsets},
        {"snapshot_and_identity_guards",identity_and_snapshot_guards},
        {"connection_rectangle_and_count_guards",connection_guards},
        {"duplicate_identity_and_positive_area_overlap",duplicate_and_overlap_guards},
        {"supported_dimensions_offsets_and_nine_maps",supported_limits},
        {"immutable_fixtures_borrowed_pointers_and_repeatability",immutable_borrowed_results}
    };
    size_t passed=0,failed=0,checks=0;
    for(const auto& c:cases) {
        Check t;
        try {
            c.run(t);++passed;
            std::cout<<"PASS "<<c.name<<" ("<<t.count<<" checks)\n";
        } catch(const std::exception& e) {
            ++failed;std::cerr<<"FAIL "<<c.name<<": "<<e.what()<<'\n';
        } catch(...) {
            ++failed;std::cerr<<"FAIL "<<c.name<<": unexpected exception\n";
        }
        checks+=t.count;
    }
    std::cout<<"SUMMARY "<<passed<<" passed, "<<failed<<" failed; "<<checks<<" checks\n";
    return failed?EXIT_FAILURE:EXIT_SUCCESS;
}
