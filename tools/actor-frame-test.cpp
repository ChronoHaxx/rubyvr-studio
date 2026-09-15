// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor_frame.h"
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <limits>
#include <utility>
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
    Motion precise{true,16.544194f,21.455806f};
    auto continuous=position(f,9,16,0,0,0,0,16,21,&precise);
    expect(continuous.x==precise.x&&continuous.z==precise.z&&continuous.lift==p.lift,"precise foot survives pixel rounding and preserves source lift");
    precise.valid=false;auto inactive_pose=position(f,9,16,0,0,0,0,16,21,&precise);
    expect(inactive_pose.x==p.x&&inactive_pose.z==p.z,"ordinary grid motion retains exact native placement");
    precise.valid=true;precise.x=80;auto stale=position(f,9,16,0,0,0,0,16,21,&precise);
    expect(stale.x==p.x&&stale.z==p.z,"another cell cannot supply a stale precise foot");
    precise.x=std::numeric_limits<float>::quiet_NaN();auto invalid_motion=position(f,9,16,0,0,0,0,16,21,&precise);
    expect(invalid_motion.x==p.x&&invalid_motion.z==p.z,"non-finite precise motion falls back to native placement");
    f.x=40;f.y=-80;f.y2=0;
    auto offset=position(f,9,16,0,216,80,152,16,23);
    expect(offset.x==16.5f && offset.z==23.5f,"global sprite offset and vertical ring phase");
    // Original synthetic ROM tables at the pinned ABI addresses. Each image
    // has asymmetric pixels, so a wrong view, phase or double mirror is visible.
    std::vector<uint8_t> rom(0x380000,0);
    auto rom16=[&](uint32_t address,unsigned value){size_t n=address-0x08000000;rom[n]=uint8_t(value);rom[n+1]=uint8_t(value>>8);};
    auto rom32=[&](uint32_t address,uint32_t value){rom16(address,value);rom16(address+2,value>>16);};
    auto sprite32=[&](Source& t,int n,uint32_t value){put(t,n,int(value&65535));put(t,n+2,int(value>>16));};
    constexpr unsigned walking[4][4]={{3,0,4,0},{5,1,6,1},{7,2,8,2},{7,2,8,2}};
    for(unsigned person=0;person<2;++person) {
        const uint32_t table=person?0x0836f720:0x0836e068;
        const uint32_t images=person?0x0831a5c0:0x0830fd60;
        for(unsigned image=0;image<18;++image) {
            rom32(table+image*8,images+image*256);rom16(table+image*8+4,256);
            const auto off=images-0x08000000+image*256;
            std::fill_n(rom.begin()+off,256,uint8_t((image%14+1)*17));
            rom[off]=0xf0; // transparent left edge, distinct next pixel
        }
        for(unsigned anim=0;anim<24;++anim) {
            const unsigned d=anim%4;const uint32_t commands=0x08360000+anim*32;
            rom32(0x08370fc8+anim*4,commands);
            for(unsigned phase=0;phase<(anim<4?1u:4u);++phase) {
                const unsigned image=anim<4?(d<3?d:2):walking[d][phase];
                rom16(commands+phase*4,image);rom16(commands+phase*4+2,8|(d==3?64:0));
            }
        }
        for(unsigned anim=0;anim<24;++anim)for(unsigned phase=0;phase<(anim<4?1u:4u);++phase) {
            Source t=s;put(t,2,(2<<14)|(anim%4==3?0x1000:0));put(t,4,(3<<12)|16);
            sprite32(t,8,0x08370fc8);sprite32(t,12,table);
            t.sprite[0x2a]=uint8_t(anim);t.sprite[0x2b]=uint8_t(phase);
            const unsigned expected=anim<4?(anim%4<3?anim%4:2):walking[anim%4][phase];
            std::copy_n(rom.begin()+images-0x08000000+expected*256,256,tiles.begin()+512);
            const auto original=decode(t,tiles,palette,true);
            const auto original_sprite=t.sprite;
            expect(capture_player_directions(t,rom,tiles,true),"both player profiles capture idle/walk/run phases");
            expect(t.sprite==original_sprite && t.world_facing==anim%4+1,"capture preserves authoritative sprite state");
            expect(decode_direction(t,tiles,palette,true,t.world_facing).rgba==original.rgba,"unrotated directional art exactly matches resident pixels");
            for(uint8_t d=1;d<=4;++d) {
                const unsigned image=anim<4?(d<4?d-1:2):walking[d-1][phase];
                expect(t.directions[d-1].image==image,"camera changes direction without resetting walking phase");
                const auto view=decode_direction(t,tiles,palette,true,d);
                expect(view.status==Status::Visible && view.x==original.x && view.y==original.y &&
                    view.y2==original.y2 && view.corner_x==original.corner_x,"directional art keeps motion and foot pivot");
                expect(view.rgba[(d==4?15:0)*4+3]==0,"side view mirror is applied once");
            }
        }
    }
    constexpr float right[4][2]={{1,0},{0,-1},{-1,0},{0,1}};
    constexpr uint8_t expected_views[4][4]={{1,2,3,4},{3,4,2,1},{2,1,4,3},{4,3,1,2}};
    for(int q=0;q<4;++q)for(uint8_t d=1;d<=4;++d)
        expect(apparent_facing(d,right[q][0],right[q][1])==expected_views[q][d-1],"all sixteen world/view directions");
    expect(apparent_facing(2,0,0)==2 && apparent_facing(2,std::numeric_limits<float>::quiet_NaN(),0)==2,"invalid view basis keeps source direction");
    Source t=s;put(t,4,3<<12);sprite32(t,8,0x08370fc8);sprite32(t,12,0x0836e068);
    t.sprite[0x2a]=4;t.sprite[0x2b]=0;
    std::copy_n(rom.begin()+0x30fd60+3*256,256,tiles.begin());
    expect(capture_player_directions(t,rom,tiles,true),"fixture before refusal checks");
    auto reject=[&](Source rejected,std::span<const uint8_t> bytes,std::span<const uint8_t> vram) {
        expect(!capture_player_directions(rejected,bytes,vram,true) && !rejected.world_facing,"unsupported or inconsistent state cannot retain old directional art");
        expect(decode_direction(rejected,tiles,palette,true,4).rgba==decode(rejected,tiles,palette,true).rgba,"unsupported pose retains original captured frame");
    };
    bad=t;bad.sprite[0x2a]=24;reject(bad,rom,tiles);
    bad=t;bad.sprite[0x2b]=4;reject(bad,rom,tiles);
    bad=t;bad.sprite[0x3f]|=64;reject(bad,rom,tiles);
    bad=t;bad.sprite[0x3e]|=4;reject(bad,rom,tiles);
    bad=t;sprite32(bad,12,0x0836e0f8);reject(bad,rom,tiles);
    reject(t,std::span(rom).first(0x370fcf),tiles);
    reject(t,rom,std::span(tiles).first(255));
    tiles[0]^=1;reject(t,rom,tiles);tiles[0]^=1;
    bad=t;put(bad,2,(2<<14)|0x1000);reject(bad,rom,tiles);
    // A global script mirror remains distinct from the animation's own mirror.
    bad=t;bad.sprite[0x3f]|=1;put(bad,2,(2<<14)|0x1000);
    expect(capture_player_directions(bad,rom,tiles,true) && bad.directions[2].hflip && !bad.directions[3].hflip,"preserve extra sprite mirror without double flipping");
    bad=t;bad.sprite[0x2b]=2;
    expect(capture_player_directions(bad,rom,tiles,true) && bad.displayed_phase==0,"lagging image DMA keeps the resident walking phase");
    bad=t;bad.sprite[0x2a]=5;
    expect(capture_player_directions(bad,rom,tiles,true) && bad.world_facing==1,"turn metadata cannot relabel the previous resident direction");
    bad=t;bad.sprite[0x2a]=0;bad.sprite[0x3f]|=4;
    expect(capture_player_directions(bad,rom,tiles,true) && bad.displayed_anim==4,"idle transition retains matching walking pose until copied");
    bad=t;bad.sprite[0x2c]|=64;
    expect(capture_player_directions(bad,rom,tiles,true) && bad.displayed_phase==0,"paused animation uses the same displayed phase");
    // Original Jump2 moves 32 pixels in 32 ticks. Binding an independently
    // captured middle frame (including after load) must recover both contacts.
    for(unsigned direction=1;direction<=4;++direction)for(unsigned tick=1;tick<=32;++tick) {
        Source jumping;jumping.present=true;put(jumping,0x2e,3);
        std::array<uint8_t,36> owner{};owner[0]=0x41;owner[0x1c]=uint8_t(0x0b+direction);
        put(jumping,0x32,1);put(jumping,0x34,int(direction));put(jumping,0x36,2);put(jumping,0x38,0);put(jumping,0x3a,int(tick));
        const auto original=jumping.sprite;
        expect(bind_event(jumping,owner,3),"original active jump owner binds");
        const float dx=direction==3?-1.f:direction==4?1.f:0.f,dz=direction==1?1.f:direction==2?-1.f:0.f;
        const Position feet{16.5f+dx*tick/16.f,19.5f+dz*tick/16.f,.75f};
        const auto span=jump_span(jumping,feet);
        expect(span.active && span.progress==tick/32.f,"source jump progress including midpoint and last tick");
        expect(span.start_x==16.5f && span.start_z==19.5f && span.end_x==16.5f+2*dx && span.end_z==19.5f+2*dz,
               "midjump capture recovers fixed takeoff and landing in every direction");
        expect(jumping.sprite==original && feet.lift==.75f,"jump presentation does not write the source arc");
        owner[0x1c]=0;expect(bind_event(jumping,owner,3) && !jump_span(jumping,feet).active,"ordinary movement clears prior jump metadata");
    }
    Source jump;jump.present=true;put(jump,0x2e,3);
    std::array<uint8_t,36> event{};event[0]=0x41;event[0x1c]=0x0c;
    put(jump,0x32,1);put(jump,0x34,1);put(jump,0x36,2);put(jump,0x38,0);put(jump,0x3a,12);
    expect(bind_event(jump,event,3) && jump_span(jump,{16.5f,20.25f,1}).active,"active takeoff fixture");
    for(auto [offset,value]:{std::pair{0x32,2},{0x34,2},{0x36,1},{0x38,1},{0x3a,0},{0x3a,33}}) {
        auto invalid=jump;put(invalid,offset,value);
        expect(bind_event(invalid,event,3) && !jump_span(invalid,{16.5f,20.25f,1}).active,"finished/stale/unrelated jump data refused");
    }
    auto inactive=event;inactive[0]=1;
    expect(bind_event(jump,inactive,3) && !jump_span(jump,{16.5f,20.25f,1}).active,"inactive movement cannot reuse jump clock");
    auto approach=t;approach.jump_direction=1;approach.jump_ticks=18;
    // Original synthetic arc, deliberately unlike the cartridge's values.
    const std::array<uint8_t,16> test_arc={2,3,4,5,6,7,8,9,8,7,6,5,4,3,1,0};
    for(unsigned n=0;n<16;++n)rom[0x3761b6 + n]=uint8_t(-int(test_arc[n]));
    expect(capture_player_directions(approach,rom,tiles,true) && approach.jump_arc_valid && approach.jump_arc==test_arc,
           "validated on-foot profile reads the actual ROM arc");
    rom32(0x0836dc70,0x08371080);rom16(0x08371088,16);rom16(0x0837108a,32);
    rom32(0x08371098,0x08370fc8);rom32(0x0837109c,0x0836e068);
    expect(capture_object_directions(approach,rom,tiles,true,0) && approach.jump_arc_valid && approach.jump_arc==test_arc,
           "runtime unified object capture also reads the on-foot player arc");
    for(unsigned direction=1;direction<=4;++direction)for(unsigned tick=1;tick<=32;++tick) {
        auto frame=approach;frame.jump_direction=uint8_t(direction);frame.jump_ticks=uint8_t(tick);
        const auto value=approach_jump_lift(frame,.5f);
        if(tick<=12 || tick==32)expect(value==0,"approach and exact landing remain grounded in every direction");
        else expect(value>0 && value<=9.f/16,"retimed flight stays within captured source arc");
        expect(approach_jump_lift(frame,.5f)==value,"independent repeated capture has no secondary animation clock");
    }
    approach.jump_ticks=17;expect(std::abs(approach_jump_lift(approach,.5f)-5.f/16)<1e-6f,"later takeoff samples captured arc at the new source phase");
    auto unsupported=approach;unsupported.jump_arc_valid=false;
    expect(approach_jump_lift(unsupported,.5f)==.5f,"missing arc preserves original source lift");
    unsupported.jump_arc_valid=true;unsupported.jump_ticks=33;
    expect(approach_jump_lift(unsupported,.5f)==.5f,"invalid clock preserves original source lift");
    rom[0x3761b6]=1;
    expect(capture_player_directions(approach,rom,tiles,true) && !approach.jump_arc_valid,"corrupt arc cannot retain prior data");
    std::cout<<"PASS: actor frame "<<checks<<" checks (original synthetic pixels)\n";
}
