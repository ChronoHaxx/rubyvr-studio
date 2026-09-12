// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor_frame.h"
#include <cstdlib>
#include <iostream>
#include <cmath>
using namespace vr::actor;
int checks=0;
void expect(bool ok,const char* label){++checks;if(!ok){std::cerr<<"FAIL: "<<label<<'\n';std::exit(1);}}
void put(Source& s,int p,int v){s.sprite[p]=uint8_t(v);s.sprite[p+1]=uint8_t(unsigned(v)>>8);}
int main(){
    Source s;s.present=true;s.sprite[0x3e]=3;
    put(s,0,2<<14);put(s,2,2<<14); // 16x32
    put(s,4,3<<12);s.sprite[0x28]=248;s.sprite[0x29]=240;
    std::vector<uint8_t> tiles(32768,0);std::vector<uint16_t> palette(256,0);
    palette[49]=31;palette[50]=31<<5;palette[51]=31<<10;
    tiles[0]=0x21;tiles[32]=3;tiles[64]=0x11;
    auto f=decode(s,tiles,palette,true);
    expect(f.status==Status::Visible && f.width==16 && f.height==32,"rectangular dimensions");
    expect(f.rgba[0]==255 && f.rgba[3]==255,"low nibble and palette bank");
    expect(f.rgba[5]==255,"high nibble");expect(f.rgba[8*4+2]==255,"horizontal tile step");
    expect(f.rgba[(8*16)*4]==255,"1D vertical tile stride");
    expect(f.rgba[2*4+3]==0,"index zero transparent");
    put(s,2,(2<<14)|0x1000);auto h=decode(s,tiles,palette,true);
    expect(h.rgba[15*4]==255 && h.rgba[14*4+1]==255,"horizontal flip covers full body");
    put(s,2,(2<<14)|0x2000);auto v=decode(s,tiles,palette,true);
    expect(v.rgba[(31*16)*4]==255,"vertical flip covers full body");put(s,2,2<<14);
    tiles[32*32]=0x22;auto two=decode(s,tiles,palette,false);
    expect(two.rgba[(8*16)*4+1]==255,"2D vertical stride");
    tiles[0]=0x33;auto changed=decode(s,tiles,palette,true);
    expect(changed.rgba[2]==255 && changed.rgba!=f.rgba,"animated tile bytes replace frame");
    expect(decode(s,std::span(tiles).first(255),palette,true).status==Status::Truncated,"last tile truncation");
    expect(decode(s,tiles,std::span(palette).first(63),true).status==Status::Truncated,"palette bank truncation");
    auto bad=s;bad.present=false;expect(decode(bad,tiles,palette,true).status==Status::Missing,"missing");
    bad=s;bad.sprite[0x3e]=2;expect(decode(bad,tiles,palette,true).status==Status::Hidden,"inactive");
    bad=s;bad.sprite[0x3e]|=4;expect(decode(bad,tiles,palette,true).status==Status::Hidden,"invisible");
    for(int bits:{0x100,0x400,0x800,0x1000,0x2000,0xc000}) {
        bad=s;put(bad,0,(2<<14)|bits);auto rejected=decode(bad,tiles,palette,true);
        expect(rejected.status==Status::Unsupported && rejected.rgba.empty(),"unsupported format explicitly refused");
    }
    bad=s;bad.sprite[0x42]=64;expect(decode(bad,tiles,palette,true).status==Status::Unsupported,"missing subsprite table");
    // Two 16x16 pieces, matching the split field body layout without game art.
    bad.subsprites={248,255,240,255,4,0, 248,255,0,0,68,0};
    std::fill(tiles.begin(),tiles.end(),0x11);std::fill(tiles.begin()+128,tiles.begin()+256,0x22);
    auto split=decode(bad,tiles,palette,true);
    expect(split.status==Status::Visible && split.rgba[0]==255 && split.rgba[(16*16)*4+1]==255,"compose split body");
    put(bad,2,(2<<14)|0x2000);auto flipped=decode(bad,tiles,palette,true);
    expect(flipped.status==Status::Visible && flipped.rgba[1]==255 && flipped.rgba[(31*16)*4]==255,"flip parts and pixels together");
    bad.subsprites[0]=200;expect(decode(bad,tiles,palette,true).status==Status::Unsupported,"out-of-body part refused");
    put(s,0x20,-40);put(s,0x22,-128);put(s,0x26,-6);f=decode(s,tiles,palette,true);
    expect(f.x==-40 && f.y==-128 && f.y2==-6 && f.corner_y==-16,"signed source coordinates");
    // Tilemap ring wrap and a step halfway toward its already-updated tile.
    f.x=112;f.y=80;f.x2=0;f.y2=-6;f.corner_y=-16;
    auto p=position(f,9,16,0,0,0,0,16,21);
    expect(p.x==16.f && p.z==21.5f && p.lift==.375f,"subtile motion and separate jump");
    auto wrap=position(f,9,16,256,256,0,0,16,21);
    expect(wrap.x==p.x && wrap.z==p.z,"ring incarnation chosen by object tile");
    f.x=40;f.y=-80;f.y2=0;
    auto offset=position(f,9,16,0,216,80,152,16,23);
    expect(offset.x==16.5f && offset.z==23.5f,"global sprite offset and vertical ring phase");
    std::cout<<"PASS: actor frame "<<checks<<" checks (original synthetic pixels)\n";
}
