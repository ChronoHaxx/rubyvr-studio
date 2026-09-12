// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor_frame.h"
#include <cmath>
#include <cstring>

namespace vr::actor {
Frame decode(const Source& source,std::span<const uint8_t> tiles,
             std::span<const uint16_t> pal,bool mapping_1d) {
    Frame f;
    if(!source.present)return f;
    const auto& s=source.sprite;
    auto u16=[&](int p){return unsigned(s[p])|(unsigned(s[p+1])<<8);};
    auto i16=[&](int p){const int v=int(u16(p));return v>=32768?v-65536:v;};
    auto i8=[&](int p){return s[p]>=128?int(s[p])-256:int(s[p]);};
    const auto a0=u16(0),a1=u16(2),a2=u16(4);
    if(!(s[0x3e]&1) || (s[0x3e]&4) || ((a0&0x300)==0x200)) {
        f.status=Status::Hidden;return f;
    }
    const unsigned shape=a0>>14,size=a1>>14;
    // No approximated affine, object-window/blended, mosaic or 8bpp sprites.
    // Field actors require world-coordinate offsets, unlike screen-space UI.
    if((a0&0x3d00) || shape==3 || !(s[0x3e]&2)) {
        f.status=Status::Unsupported;return f;
    }
    constexpr int widths[3][4]={{8,16,32,64},{16,32,32,64},{8,8,16,32}};
    constexpr int heights[3][4]={{8,16,32,64},{8,8,16,32},{16,32,32,64}};
    f.width=widths[shape][size];f.height=heights[shape][size];
    f.x=i16(0x20);f.y=i16(0x22);f.x2=i16(0x24);f.y2=i16(0x26);
    f.corner_x=i8(0x28);f.corner_y=i8(0x29);
    if(s[0x42]>>6) {
        if(source.subsprites.empty() || source.subsprites.size()%6 || source.subsprites.size()>64*6) {
            f.status=Status::Unsupported;return f;
        }
        // The native field profiles split a body for BG priority. Recompose
        // its exact pixels; 3D depth handles scenery occlusion in this slice.
        f.width=-2*f.corner_x;f.height=-2*f.corner_y;
        if(f.width<=0 || f.height<=0 || f.width>64 || f.height>64){f.status=Status::Unsupported;return f;}
        f.rgba.assign(size_t(f.width)*f.height*4,0);
        for(size_t p=0;p<source.subsprites.size();p+=6) {
            auto word=[&](size_t q){return unsigned(source.subsprites[q])|(unsigned(source.subsprites[q+1])<<8);};
            auto signedword=[&](size_t q){int v=int(word(q));return v>=32768?v-65536:v;};
            const unsigned bits=word(p+4),partshape=bits&3,partsize=(bits>>2)&3;
            if(partshape==3){f.status=Status::Unsupported;f.rgba.clear();return f;}
            const unsigned parttile=(a2&1023)+((bits>>4)&1023);
            if(parttile>=1024){f.status=Status::Truncated;f.rgba.clear();return f;}
            Source part=source;part.sprite[0x42]=0;part.subsprites.clear();
            auto put=[&](int q,unsigned v){part.sprite[q]=uint8_t(v);part.sprite[q+1]=uint8_t(v>>8);};
            put(0,(a0&0x3fff)|(partshape<<14));put(2,(a1&0x3fff)|(partsize<<14));put(4,(a2&0xfc00)|parttile);
            auto image=decode(part,tiles,pal,mapping_1d);
            if(image.status!=Status::Visible){f.status=image.status;f.rgba.clear();return f;}
            int x=signedword(p),y=signedword(p+2);
            if(a1&0x1000)x=-x-image.width;
            if(a1&0x2000)y=-y-image.height;
            x-=f.corner_x;y-=f.corner_y;
            if(x<0 || y<0 || x+image.width>f.width || y+image.height>f.height) {
                f.status=Status::Unsupported;f.rgba.clear();return f;
            }
            for(int yy=0;yy<image.height;++yy)for(int xx=0;xx<image.width;++xx) {
                const auto src=(size_t(yy)*image.width+xx)*4,dst=(size_t(y+yy)*f.width+x+xx)*4;
                if(!f.rgba[dst+3])std::memcpy(f.rgba.data()+dst,image.rgba.data()+src,4);
            }
        }
        f.status=Status::Visible;return f;
    }
    const unsigned tile=a2&1023, bank=(a2>>12)*16;
    const unsigned stride=mapping_1d?unsigned(f.width/8):32;
    const size_t end=(tile+(unsigned(f.height)/8-1)*stride+unsigned(f.width)/8)*32;
    if(end>tiles.size() || bank+16>pal.size()) {
        f.status=Status::Truncated;return f;
    }
    f.rgba.resize(size_t(f.width)*f.height*4);
    for(int y=0;y<f.height;++y)for(int x=0;x<f.width;++x) {
        const unsigned sx=(a1&0x1000)?f.width-1-x:x;
        const unsigned sy=(a1&0x2000)?f.height-1-y:y;
        const auto b=tiles[(tile+(sy/8)*stride+sx/8)*32+(sy%8)*4+(sx%8)/2];
        const unsigned index=(b>>((sx&1)*4))&15;
        const auto color=pal[bank+index];
        const size_t p=(size_t(y)*f.width+x)*4;
        for(int c=0;c<3;++c) {
            const unsigned v=(color>>(c*5))&31;
            f.rgba[p+c]=uint8_t((v<<3)|(v>>2));
        }
        f.rgba[p+3]=index?255:0;
    }
    f.status=Status::Visible;return f;
}
Position position(const Frame& f,int view_x,int view_y,int base_x,int base_y,
                  int offset_x,int offset_y,int cell_x,int cell_y) {
    // Invert the same 256px field ring used for map drawing. Choose its
    // incarnation nearest the event's destination tile, preserving step pixels.
    float x=view_x+(f.x+offset_x+f.x2-base_x)/16.f;
    float z=view_y+(f.y+offset_y-f.corner_y-8-base_y)/16.f;
    x+=16*std::round((cell_x+.5f-x)/16.f);
    z+=16*std::round((cell_y+.5f-z)/16.f);
    return {x,z,-f.y2/16.f};
}
}
