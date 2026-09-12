// SPDX-License-Identifier: GPL-3.0-or-later
// Original synthetic art and pinned ABI tables; no ROM, SDL or graphics device.
#include "actor_frame.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
using namespace vr::actor;
static int checks=0;
static void expect(bool ok,const char* text){++checks;if(!ok){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
int main(){
    std::vector<uint8_t> rom(0x380000),tiles(32768);
    std::vector<uint16_t> palette(256);
    for(unsigned i=1;i<16;++i)palette[3*16+i]=uint16_t(i|(i<<5)|(i<<10));
    auto word=[](uint8_t* p,unsigned v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);};
    auto dword=[&](uint8_t* p,uint32_t v){word(p,v);word(p+2,v>>16);};
    auto r=[&](uint32_t a){return rom.data()+a-0x08000000u;};
    constexpr uint32_t info=0x083719c4,images=0x08360000,pixels=0x08300000;
    dword(r(0x0836dc70+5*4),info);
    // Standard walkers have nine images. Quinty-style walkers alternate a
    // mirrored front/back stride and have seven; camera remapping must keep it.
    constexpr unsigned standard[4][4]={{3,0,4,0},{5,1,6,1},{7,2,8,2},{7,2,8,2}};
    constexpr unsigned quinty[4][4]={{3,0,3,0},{4,1,4,1},{5,2,6,2},{5,2,6,2}};
    Source last;unsigned last_bytes=0;
    for(unsigned type=0;type<2;++type)for(auto dims:{std::pair{16u,16u},{16u,32u},{32u,32u}}) {
        const auto [w,h]=dims;const unsigned bytes=w*h/2,shape=w==h?0:2,size=w==h?(w==16?1:2):2;
        const uint32_t anims=type?0x08370f28:0x08370f78;
        word(r(info+8),w);word(r(info+10),h);dword(r(info+24),anims);dword(r(info+28),images);
        for(unsigned image=0;image<(type?7u:9u);++image) {
            dword(r(images+image*8),pixels+image*bytes);word(r(images+image*8+4),bytes);
            std::fill_n(r(pixels+image*bytes),bytes,uint8_t((image+1)*17));
            r(pixels+image*bytes)[0]=0xf0; // asymmetric alpha/index witness
        }
        for(unsigned anim=0;anim<20;++anim) {
            const unsigned d=anim%4;const uint32_t command=0x08350000+anim*32;
            dword(r(anims+anim*4),command);
            for(unsigned phase=0;phase<(anim<4?1u:4u);++phase) {
                const unsigned image=anim<4?(d==3?2:d):(type?quinty[d][phase]:standard[d][phase]);
                const bool mirror=d==3 || (type && anim>=4 && d<2 && phase==2);
                word(r(command+phase*4),image);word(r(command+phase*4+2),8|(mirror?64:0));
            }
        }
        for(bool mapping:{false,true})for(unsigned anim=0;anim<20;++anim)
        for(unsigned phase=0;phase<(anim<4?1u:4u);++phase) {
            Source s;s.present=true;s.sprite[0x3e]=3;
            word(s.sprite.data(),shape<<14);word(s.sprite.data()+2,(size<<14));
            word(s.sprite.data()+4,(3<<12)|32);
            dword(s.sprite.data()+8,anims);dword(s.sprite.data()+12,images);
            s.sprite[0x28]=uint8_t(256-w/2);s.sprite[0x29]=uint8_t(256-h/2);
            s.sprite[0x2a]=uint8_t(anim);s.sprite[0x2b]=uint8_t(phase);
            const unsigned d=anim%4;
            const unsigned image=anim<4?(d==3?2:d):(type?quinty[d][phase]:standard[d][phase]);
            const bool mirror=d==3 || (type && anim>=4 && d<2 && phase==2);
            s.sprite[3]|=mirror?16:0;
            const unsigned stride=mapping?w/8:32;
            std::fill(tiles.begin(),tiles.end(),0);
            for(unsigned row=0;row<h/8;++row)
                std::copy_n(r(pixels+image*bytes+row*w*4),w*4,tiles.begin()+(32+row*stride)*32);
            const auto sprite_before=s.sprite;const auto tiles_before=tiles;
            const auto original=decode(s,tiles,palette,mapping);
            expect(capture_object_directions(s,rom,tiles,mapping,5),"ordinary NPC dimensions, directions and stride variants captured");
            expect(s.sprite==sprite_before && tiles==tiles_before,"capture never mutates source sprite or OBJ RAM");
            expect(s.world_facing==d+1 && s.displayed_phase==phase,"resident NPC direction and walking phase retained");
            expect(decode_direction(s,tiles,palette,mapping,s.world_facing).rgba==original.rgba,"north-up NPC remains pixel-identical to original");
            for(unsigned view=0;view<4;++view) {
                const unsigned expected=anim<4?(view==3?2:view):(type?quinty[view][phase]:standard[view][phase]);
                const bool flip=view==3 || (type && anim>=4 && view<2 && phase==2);
                auto frame=decode_direction(s,tiles,palette,mapping,uint8_t(view+1));
                expect(frame.status==Status::Visible && frame.width==int(w) && frame.height==int(h),"camera art preserves full short/tall/wide body");
                expect(s.directions[view].image==expected && s.directions[view].hflip==flip,"same-phase alternative has expected original image and mirror");
                expect(frame.rgba[(flip?w-1:0)*4+3]==0 && frame.rgba[(flip?w-2:1)*4]==123,"alternate pixels retain original palette and one horizontal flip");
                expect(frame.corner_x==original.corner_x && frame.corner_y==original.corner_y,"changing view preserves foot pivot");
            }
            last=s;last_bytes=bytes;
        }
    }
    auto reject=[&](Source s,std::span<const uint8_t> bytes,uint8_t id=5){
        expect(!capture_object_directions(s,bytes,tiles,true,id) && !s.world_facing,"unsafe profile cannot retain old directional data");
        expect(decode_direction(s,tiles,palette,true,4).rgba==decode(s,tiles,palette,true).rgba,"refusal preserves actually captured art");
    };
    reject(last,rom,218);reject(last,std::span(rom).first(0x36dc86));
    auto bad=last;bad.fixed_pose=true;reject(bad,rom);
    bad=last;bad.sprite[0x2a]=20;reject(bad,rom);
    bad=last;dword(bad.sprite.data()+12,images+8);reject(bad,rom);
    dword(r(info+24),0x08371128);reject(last,rom);dword(r(info+24),0x08370f28);
    word(r(info+8),64);reject(last,rom);word(r(info+8),32);
    r(info)[12]=64;reject(last,rom);r(info)[12]=0;
    word(r(images+4),last_bytes-1);reject(last,rom);word(r(images+4),last_bytes);
    // Native witness: an east-turn command sets hflip before its image copy.
    // OBJ still contains the previous north stand frame. Match that old phase
    // only when exactly one queued request confirms the upcoming image.
    last.sprite[0x2a]=7;last.sprite[0x2b]=0;
    std::copy_n(r(pixels+last_bytes),last_bytes,tiles.begin()+32*32);
    const auto mixed=decode(last,tiles,palette,true);reject(last,rom);
    std::array<uint8_t,12> copy{};
    dword(copy.data(),pixels+5*last_bytes);dword(copy.data()+4,0x06010400);word(copy.data()+8,last_bytes);
    expect(capture_object_directions(last,rom,tiles,true,5,copy) && last.pending_flip_transition &&
        last.world_facing==2 && last.displayed_phase==1,"verified pending copy recovers old resident pose with new mirror");
    expect(decode_direction(last,tiles,palette,true,2).rgba==mixed.rgba,"mixed original view stays pixel-exact");
    expect(!last.directions[2].hflip && last.directions[3].hflip,"transition mirror cannot reverse alternative side views");
    auto refuse_copy=[&](std::span<const uint8_t> queue){auto test=last;
        expect(!capture_object_directions(test,rom,tiles,true,5,queue) && !test.world_facing && !test.pending_flip_transition,
            "unproven pending copy cannot authorize mixed pose");};
    auto wrong=copy;dword(wrong.data()+4,0x06010420);refuse_copy(wrong);
    wrong=copy;dword(wrong.data(),pixels+4*last_bytes);refuse_copy(wrong);
    wrong=copy;word(wrong.data()+8,last_bytes-32);refuse_copy(wrong);
    refuse_copy(std::span(copy).first(11));
    std::array<uint8_t,24> duplicate{};std::copy(copy.begin(),copy.end(),duplicate.begin());
    std::copy(copy.begin(),copy.end(),duplicate.begin()+12);refuse_copy(duplicate);
    // Return to a fully copied source image before the visibility checks.
    std::copy_n(r(pixels+5*last_bytes),last_bytes,tiles.begin()+32*32);
    expect(capture_object_directions(last,rom,tiles,true,5) && !last.pending_flip_transition,"completed copy needs no transition exception");
    // Bind a live actor which Ruby has hidden solely for its 2D viewport.
    std::array<uint8_t,0x24> event{};event[0]=1;event[1]=0x40;
    word(last.sprite.data()+0x2e,3);last.sprite[0x3e]|=4;
    const auto original_sprite=last.sprite;const auto event_before=event;
    expect(bind_event(last,event,3) && last.viewport_culled,"owning active event distinguishes original screen culling");
    expect(last.sprite==original_sprite && event==event_before,"visibility permission never changes game state");
    expect(decode(last,tiles,palette,true).status==Status::Visible,"live off-screen NPC remains visible in 3D");
    expect(capture_object_directions(last,rom,tiles,true,5),"culled NPC still gets matching directional art");
    event[1]=0;expect(bind_event(last,event,3) && !last.viewport_culled,"cull permission does not persist after flag clears");
    expect(decode(last,tiles,palette,true).status==Status::Hidden,"unexplained sprite hiding is preserved");
    event[1]=0x40;expect(bind_event(last,event,3),"restore cull fixture");
    bad=last;event[1]=0x60;
    expect(!bind_event(bad,event,3) && !bad.present && !bad.world_facing,"script hide outranks viewport flag and clears old art");
    bad=last;event[1]=0x40;event[0]=0;
    expect(!bind_event(bad,event,3) && !bad.present,"despawn clears presentation instead of inventing a live actor");
    bad=last;event[0]=1;
    expect(!bind_event(bad,event,4) && !bad.present,"recycled sprite slot cannot borrow old actor visibility");
    bad=last;expect(!bind_event(bad,std::span(event).first(35),3),"truncated owner record refused");
    bad=last;event[1]=0x50;expect(bind_event(bad,event,3) && bad.fixed_pose,"fixed-frame script state captured");reject(bad,rom);
    std::cout<<"PASS: NPC frame "<<checks<<" checks (original synthetic art, no game assets)\n";
}
