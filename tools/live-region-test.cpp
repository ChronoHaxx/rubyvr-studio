// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_region.h"
#include <cstdlib>
#include <iostream>
using namespace vr::world;
using namespace vr::world::live;
namespace {
int checks=0,loads=0;
void expect(bool b,const char* why){++checks;if(!b){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
Snapshot fixture(int n) {
    Snapshot s;s.valid=true;s.map_group=0;s.map_number=n;
    s.identity_source=Snapshot::IdentitySource::SourceTable;s.layout_ptr=1;
    s.width=35;s.height=34;s.grid.assign(1190,0);return s;
}
bool load(int g,int n,Snapshot& s) {
    ++loads;if(g || n>9)return false;s=fixture(n);
    // A north/south chain, with Ruby's real seven-cell copied rectangles.
    if(n) s.connections.push_back({0,n-1,35,34,7,27,7,7,20,7,true});
    if(n<9) s.connections.push_back({0,n+1,35,34,7,0,7,20,20,7,true});
    return true;
}
}
int main() {
    Snapshot a;load(0,1,a);Neighbourhood r;
    expect(r.refresh(a,load) && r.maps().size()==3,"full unseen neighbours in a bounded local area");
    expect(r.find(0,0)->z==20 && r.find(0,2)->z==-20,"connection-derived translations, including negative north");
    const int calls=loads;const auto revision=r.revision(),space=r.space();
    a.bg_palette.assign(256,12);a.objects[0].x=9;
    expect(!r.refresh(a,load) && r.revision()==revision && loads==calls,"actor/palette animation does not rebuild scenery");
    Snapshot b;load(0,2,b);r.refresh(b,load);
    expect(r.space()==space && r.find(0,2)->z==-20 && r.find(0,1)->z==0,"crossing and look-back preserve world origins");
    expect(r.find(0,1)->z+7==r.find(0,2)->z+27,"shared border occupies identical world coordinates");
    r.refresh(a,load);expect(r.find(0,1)->z==0 && r.find(0,2)->z==-20,"reverse crossing keeps both maps");
    a.grid[10*35+10]=1;expect(r.refresh(a,load),"runtime metatile edit invalidates current scenery");
    expect(!r.refresh({},load) && r.find(0,1),"menu invalidation retains bounded cache");
    for(int n=3;n<=9;++n){load(0,n,b);r.refresh(b,load);expect(r.maps().size()<=3,"cache bounded across a long walk");}
    b=fixture(40);r.refresh(b,load);
    expect(r.space()>space && r.maps().size()==1 && r.find(0,40)->z==0,"disconnected warp discards incompatible placements");
    b=fixture(0);b.connections={{0,1,35,34,27,7,7,7,8,20,true},{0,2,35,34,27,7,7,7,8,20,true}};
    r.refresh(b,load);expect(r.omitted()>0 && (!r.find(0,2) || r.find(0,2)->z!=r.find(0,1)->z),
        "overlapping edge refused even when another valid path later reaches its destination");
    std::cout<<"PASS: "<<checks<<" live neighbourhood checks (no display or assets)\n";
}
