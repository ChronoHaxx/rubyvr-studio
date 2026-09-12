// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_presentation.h"
#include <algorithm>

namespace vr::presentation {
namespace {
uint16_t u16(const uint8_t* p) {return uint16_t(p[0] | unsigned(p[1])<<8);}
uint32_t u32(const uint8_t* p) {return uint32_t(u16(p)) | uint32_t(u16(p+2))<<16;}
bool menu(Mode mode) {return mode==Mode::Bag || mode==Mode::Party || mode==Mode::Options;}
}
Input inspect(const world::live::Memory& m,uint64_t epoch) {
    Input out;out.epoch=epoch;
    if(!m.verified_ruby_rev1 || m.rom.size()!=0x1000000 ||
       m.ewram.size()!=0x40000 || m.iwram.size()!=0x8000)return out;
    const auto* main=m.read(world::live::kMain,0x440);
    const auto* location=m.read(world::kGSaveBlock1+4,2);
    const auto* header=m.read(world::kGMapHeader,28);
    const auto* fade=m.read(0x0202f388,12); // gPaletteFade, Ruby USA rev1
    out.callback=u32(main+4);
    out.identity={location[0],location[1],u32(header)};
    // active clears one frame before the menu callback changes. Its final
    // blend coefficient can still be 16 (black); do not cache that as the world.
    out.fading=(fade[7]&0x80)!=0 || ((u16(fade+4)>>6)&31)!=0;
    // In-battle wins over shared Bag/party callbacks. Initialization can precede
    // the inBattle flag; both exact init callbacks are separately recognized.
    if((main[0x43d]&2) || out.callback==0x0800e7c5 || out.callback==0x0800e7f9) {
        out.mode=Mode::Battle;return out;
    }
    if(out.callback==world::live::kOverworldCallback) {
        out.mode=header[0x17]>=1 && header[0x17]<=3 ? Mode::Field : Mode::Interior;
        return out;
    }
    // Imported rev1 symbols, cross-checked against the pinned source call paths.
    // Names containing older addresses are not their rev1 addresses.
    switch(out.callback) {
    case 0x080a5419: // sub_80A53F8: Start menu's field-Bag entry callback
        out.mode=Mode::Bag;break; // this entry itself assigns RETURN_TO_FIELD_0
    case 0x080a53ed: // sub_80A53CC: ordinary field Bag initialization
    case 0x080a3139: // sub_80A3118: active Bag
        if(*m.read(0x03000701,1)==0)out.mode=Mode::Bag; // RETURN_TO_FIELD_0 only
        break;
    case 0x08089a91: // sub_8089A70: Start menu's ordinary party entry
    case 0x0806b481: // CB2_InitPartyMenu
    case 0x0806aefd: // CB2_PartyMenuMain
        out.mode=Mode::Party;break;
    case 0x0808b65d: // CB2_InitOptionMenu
    case 0x0808b631: // option_menu.c::MainCB
        out.mode=Mode::Options;break;
    case 0x08054605: // CB2_ReturnToField
    case 0x08054631: // CB2_ReturnToFieldLocal
    case 0x080546bd: // c2_exit_to_overworld_1_sub_8080DEC
    case 0x080546f5: // CB2_ReturnToFieldContinueScriptPlayMapMusic
        out.mode=Mode::ReturnToField;break;
    default:break;
    }
    return out;
}
const char* name(Mode m) {
    switch(m) {
    case Mode::Field:return "Field";
    case Mode::Bag:return "Bag";
    case Mode::Party:return "Party";
    case Mode::Options:return "Options";
    case Mode::ReturnToField:return "Returning to field";
    case Mode::Battle:return "Battle - original game";
    case Mode::Interior:return "Interior - original game";
    default:return "Original game";
    }
}
void Lifetime::reset(){identity_={};epoch_=0;cached_=menu_=false;}
Decision Lifetime::next(const Input& in,bool valid_field) {
    if(epoch_!=in.epoch){reset();epoch_=in.epoch;}
    const bool same=cached_ && identity_==in.identity;
    if(in.mode==Mode::Field && valid_field) {
        menu_=false;
        // Preserve the last fully colored material/actor state through the
        // source fade into menus. Revalidate before publishing the return.
        if(in.fading) {
            if(same)return {true,false,true,Overlay::FieldUi};
            cached_=false;identity_={};return {};
        }
        identity_=in.identity;cached_=true;
        return {true,true,false,Overlay::FieldUi};
    }
    if(menu(in.mode) && same) {
        menu_=true;return {true,false,true,Overlay::Original};
    }
    if(in.mode==Mode::ReturnToField && same && menu_)
        return {true,false,true,Overlay::None};
    cached_=menu_=false;identity_={};
    return {};
}

bool field_ui(std::span<const uint8_t> vram,std::span<const uint8_t> io,
              std::span<const uint8_t> rgb,std::vector<uint8_t>& rgba) {
    rgba.clear();
    if(vram.size()<0x10000 || io.size()<0x50 || rgb.size()!=240*160*3)return false;
    const auto display=u16(io.data()),control=u16(io.data()+8);
    if((display&7)!=0 || (display&0x80))return false;
    rgba.assign(240*160*4,0);
    if(!(display&0x100))return true;
    // The supported field template is BG0, char block 2, screen block 31,
    // priority 0, 4bpp, 256x256. OBJ windows/mosaic need a separate ownership pass.
    if(control!=0x1f08 || (display&0x8000)){rgba.clear();return false;}
    const int sx=u16(io.data()+0x10)&255,sy=u16(io.data()+0x12)&255;
    auto inside=[](int p,int first,int last){return first<=last?p>=first&&p<last:p>=first||p<last;};
    for(int y=0;y<160;++y)for(int x=0;x<240;++x) {
        if(display&0x6000) {
            uint8_t flags=io[0x4a]; // WINOUT
            for(int win=1;win>=0;--win)if(display&(0x2000<<win)) {
                const auto horizontal=u16(io.data()+0x40+win*2),vertical=u16(io.data()+0x44+win*2);
                if(inside(x,horizontal>>8,horizontal&255) && inside(y,vertical>>8,vertical&255))
                    flags=io[0x48+win];
            }
            if(!(flags&1))continue;
        }
        const int bx=(x+sx)&255,by=(y+sy)&255;
        const auto tile=u16(vram.data()+0xf800+2*((by/8)*32+bx/8));
        const int tx=(tile&0x400)?7-bx%8:bx%8,ty=(tile&0x800)?7-by%8:by%8;
        const size_t address=0x8000+size_t(tile&1023)*32+ty*4+tx/2;
        if(address>=vram.size()){rgba.clear();return false;}
        const auto index=(vram[address]>>(4*(tx&1)))&15;
        if(!index)continue;
        const size_t pixel=size_t(y)*240+x;
        std::copy_n(rgb.data()+pixel*3,3,rgba.data()+pixel*4);
        rgba[pixel*4+3]=255;
    }
    return true;
}
} // namespace vr::presentation
