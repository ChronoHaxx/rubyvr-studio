// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_presentation.h"
#include <cstdlib>
#include <iostream>
using namespace vr;
using namespace vr::presentation;
namespace {
int checks=0;
void check(bool pass,const char* what){++checks;if(!pass){std::cerr<<"FAIL: "<<what<<'\n';std::exit(1);}}
void word(std::vector<uint8_t>& v,size_t p,uint32_t n){for(int i=0;i<4;++i)v[p+i]=uint8_t(n>>(8*i));}
}
int main() {
    std::vector<uint8_t> rom(0x1000000),ew(0x40000),iw(0x8000);
    world::live::Memory memory{rom,ew,iw,true};
    const size_t main=world::live::kMain&0x7fff,header=world::kGMapHeader&0x3ffff;
    // gMapHeader is IWRAM; use the address-selected region, not host assumptions.
    auto* hdr=const_cast<uint8_t*>(memory.read(world::kGMapHeader,28));
    hdr[0]=0x00;hdr[1]=0x10;hdr[3]=8;hdr[0x17]=3;
    (void)header;
    ew[(world::kGSaveBlock1+4)&0x3ffff]=0;ew[(world::kGSaveBlock1+5)&0x3ffff]=16;
    auto callback=[&](uint32_t value){word(iw,main+4,value);return inspect(memory,7);};
    auto field=callback(world::live::kOverworldCallback);
    check(field.mode==Mode::Field && field.identity==Identity{0,16,0x08001000},"pinned outdoor identity");
    memory.verified_ruby_rev1=false;check(presentation::inspect(memory).mode==Mode::Original,"unverified ROM refuses classification");
    memory.verified_ruby_rev1=true;
    check(callback(0x080a3139).mode==Mode::Bag,"ordinary field Bag callback");
    iw[0x701]=1;check(presentation::inspect(memory).mode==Mode::Original,"battle Bag context is not field Bag");iw[0x701]=0;
    for(const auto cb:{0x080a5419u,0x080a53edu,0x080a3139u,0x08089a91u,0x0806b481u,0x0806aefdu,0x0808b65du,0x0808b631u}) {
        check(callback(cb).mode!=Mode::Original,"recognized menu init and running callback");
        iw[main+0x43d]=2;check(presentation::inspect(memory).mode==Mode::Battle,"battle wins over shared menu callback");
        iw[main+0x43d]=0;
    }
    check(callback(0x0800e7c5).mode==Mode::Battle,"battle initialization precedes inBattle bit");
    check(callback(0x080a3138).mode==Mode::Original,"missing Thumb bit refuses menu");
    check(callback(0x08123457).mode==Mode::Original,"arbitrary nonfield is never menu");
    hdr[0x17]=8;check(callback(world::live::kOverworldCallback).mode==Mode::Interior,"interior uses original presentation");hdr[0x17]=3;
    field=callback(world::live::kOverworldCallback);
    ew[0x2f38c]=0;ew[0x2f38d]=4; // y=16, active=0: completed fade is still black
    check(presentation::inspect(memory).fading,"completed black fade is not fresh field material");
    ew[0x2f38d]=0;
    auto bag=callback(0x080a3139),party=callback(0x0806aefd),returning=callback(0x08054631);
    auto unknown=callback(0x08123457);
    Lifetime life;
    check(!life.next(bag,false).world,"cold-start menu cannot borrow an imaginary world");
    check(life.next(field,true).update,"valid field publishes");
    field.fading=true;
    auto d=life.next(field,true);check(d.world && d.retained && !d.update,"fade preserves fully colored source materials");
    d=life.next(bag,false);check(d.world && d.retained && d.overlay==Overlay::Original,"Bag keeps world and uses complete original UI");
    d=life.next(party,false);check(d.retained,"nested recognized menu retains same origin");
    d=life.next(returning,false);check(d.retained && d.overlay==Overlay::None,"menu return waits for valid field without drawing half-loaded tiles");
    field.fading=false;check(life.next(field,true).update,"valid return refreshes field");
    check(!life.next(field,false).world,"invalid field never passes retention");
    check(!life.next(bag,false).world,"invalid field invalidates later menu retention");
    life.next(field,true);bag.epoch=8;check(!life.next(bag,false).world,"same-map checkpoint load discards old host world");bag.epoch=7;
    life.next(field,true);auto other=bag;++other.identity.number;
    check(!life.next(other,false).world,"different map cannot retain previous menu origin");
    life.next(field,true);auto other_fade=field;++other_fade.identity.number;other_fade.fading=true;
    life.next(other_fade,true);check(!life.next(bag,false).world,"different-map fade invalidates old menu origin");
    life.next(field,true);other=bag;other.identity.layout+=4;
    check(!life.next(other,false).world,"same map changed layout cannot retain");
    life.next(field,true);life.next(unknown,false);check(!life.next(bag,false).world,"unknown transition breaks menu chain");
    life.next(field,true);check(!life.next(returning,false).world,"return callback alone is not evidence of a field menu");
    life.next(field,true);auto battle=bag;battle.mode=Mode::Battle;
    check(!life.next(battle,false).world && !life.next(bag,false).world,"battle drops world and does not reuse it in battle Bag");
    life.next(field,true);life.reset();check(!life.next(bag,false).world,"explicit reset drops retained scene");

    std::vector<uint8_t> vram(0x18000),io(0x400),rgb(240*160*3),rgba;
    word(io,0,0x100);word(io,8,0x1f08);
    // One UI tile, opaque even when its composited color is black. Palette zero
    // stays transparent even when the underlying field pixel is bright.
    word(vram,0xf800,1);vram[0x8020]=0x01;
    rgb[3]=255;
    check(field_ui(vram,io,rgb,rgba),"supported BG0 UI extraction");
    check(rgba[3]==255 && rgba[0]==0 && rgba[7]==0,"ownership comes from BG0 indices, not color matching");
    rgb[0]=37;rgb[1]=89;rgb[2]=140;field_ui(vram,io,rgb,rgba);
    check(rgba[0]==37 && rgba[1]==89 && rgba[2]==140,"composited original colors are preserved");
    word(vram,0xf800,1|0x400);field_ui(vram,io,rgb,rgba);
    check(rgba[3]==0 && rgba[7*4+3]==255,"tile horizontal flip");
    word(vram,0xf800,1|0x800);field_ui(vram,io,rgb,rgba);
    check(rgba[3]==0 && rgba[7*240*4+3]==255,"tile vertical flip");
    word(vram,0xf800,1);word(io,0,0x2100);word(io,0x40,0x0102);word(io,0x44,0x0001);
    word(io,0x48,1);field_ui(vram,io,rgb,rgba);
    check(rgba[3]==0,"window clipping excludes BG0 outside window");
    word(io,0,0);check(field_ui(vram,io,rgb,rgba) && rgba[3]==0,"disabled BG0 clears previous UI");
    word(io,0,0x8100);check(!field_ui(vram,io,rgb,rgba) && rgba.empty(),"unsupported OBJ window falls back explicitly");
    word(io,0,0x100);word(io,8,0x1f88);check(!field_ui(vram,io,rgb,rgba),"8bpp layout is not guessed");
    word(io,8,0x1f08);check(!field_ui(vram,io,std::span<const uint8_t>(rgb.data(),10),rgba),"truncated original frame refuses");
    check(!field_ui(std::span<const uint8_t>{},io,rgb,rgba),"truncated video memory refuses");
    std::cout<<"PASS: "<<checks<<" presentation classification/lifetime/UI ownership checks (synthetic, no graphics/assets)\n";
}
