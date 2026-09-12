// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor_frame.h"
#include <cmath>
#include <cstring>

namespace vr::actor {
namespace {
unsigned word(const uint8_t* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
uint32_t ptr(const uint8_t* p){return uint32_t(word(p))|(uint32_t(word(p+2))<<16);}
const uint8_t* read(std::span<const uint8_t> rom,uint32_t address,size_t size) {
    if(address<0x08000000u || address>=0x0a000000u)return nullptr;
    const size_t offset=address-0x08000000u;
    return offset<=rom.size() && size<=rom.size()-offset ? rom.data()+offset : nullptr;
}
struct Profile { uint32_t anims,images,image_start;unsigned width,height,anim_count,image_count; };
bool capture_directions(Source& source,std::span<const uint8_t> rom,
                        std::span<const uint8_t> tiles,bool mapping_1d,const Profile& profile,
                        std::span<const uint8_t> pending_copies={}) {
    source.world_facing=0;
    source.pending_flip_transition=false;
    const auto& s=source.sprite;
    // Pinned pokeruby Sprite ABI, imported_data_symbols.tsv and
    // object_event_{anims,pic_tables}.h. No guessed directions for special poses.
    const auto& [anims,images,image_start,width,height,anim_count,image_count]=profile;
    const unsigned anim=s[0x2a],phase=s[0x2b];
    const unsigned a0=word(s.data()),a1=word(s.data()+2),a2=word(s.data()+4);
    const unsigned shape=width==height?0:2,size=width==height?(width==16?1:2):2;
    const unsigned bytes=width*height/2,row_bytes=width*4;
    if(!source.present || source.fixed_pose || (s[0x3e]&3)!=3 || ((s[0x3e]&4) && !source.viewport_culled) ||
       (s[0x3f]&0x40) || !image_start || ptr(s.data()+8)!=anims || ptr(s.data()+12)!=images ||
       (a0&0xff00)!=(shape<<14) || (a1&0xc000)!=(size<<14) ||
       s[0x28]!=256-width/2 || s[0x29]!=256-height/2 ||
       anim>=anim_count || phase>=4 || (anim<4 && phase && !(s[0x3f]&4)))return false;
    auto capture_at=[&](unsigned candidate,unsigned command,bool pending_flip=false)->bool {
        std::array<DirectionImage,4> directions;
        for(unsigned d=0;d<4;++d) {
            const auto* entry=read(rom,anims+(candidate/4*4+d)*4,4);
            if(!entry)return false;
            const auto* cmd=read(rom,ptr(entry)+command*4,4);
            if(!cmd || word(cmd)>=image_count || (word(cmd+2)&0xff00))return false;
            const auto* image=read(rom,images+word(cmd)*8,8);
            if(!image || word(image+4)!=bytes || ptr(image)!=image_start+word(cmd)*bytes)return false;
            const auto* data=read(rom,ptr(image),bytes);
            if(!data)return false;
            auto& v=directions[d];v.image=uint16_t(word(cmd));v.byte_count=uint16_t(bytes);
            v.hflip=((cmd[2]>>6)^(s[0x3f]))&1;
            v.vflip=((cmd[2]>>7)^(s[0x3f]>>1))&1;
            std::memcpy(v.tiles.data(),data,bytes);
        }
        auto& current=directions[candidate%4];
        const bool different_flip=bool(a1&0x1000)!=current.hflip || bool(a1&0x2000)!=current.vflip;
        if(different_flip && !pending_flip)return false;
        // The game may have advanced animation metadata before an image DMA. Only
        // publish alternatives for the phase actually resident in this snapshot.
        const unsigned tile=a2&1023,stride=mapping_1d?width/8:32;
        for(unsigned row=0;row<height/8;++row) {
            const size_t offset=(tile+row*stride)*32;
            if(offset>tiles.size() || row_bytes>tiles.size()-offset ||
               std::memcmp(tiles.data()+offset,current.tiles.data()+row*row_bytes,row_bytes))return false;
        }
        // In a verified queued-copy transition, only this resident view has
        // the new flip already applied. Keep its exact pixels while alternate
        // views retain the original phase's own mirrors, without a double flip.
        if(different_flip){current.hflip=bool(a1&0x1000);current.vflip=bool(a1&0x2000);}
        source.directions=std::move(directions);
        source.world_facing=uint8_t(candidate%4+1);
        source.displayed_anim=uint8_t(candidate);source.displayed_phase=uint8_t(command);
        source.pending_flip_transition=different_flip;
        return true;
    };
    if(phase<(anim<4?1u:4u) && capture_at(anim,phase))return true;
    // Metadata can lead the displayed image at a step/turn/animation change.
    // Identify the resident pose by exact bytes AND flips; never reset a timer,
    // reuse an actor from an older snapshot, or flash an unrotated fallback.
    for(unsigned c=0;c<(anim<4?1u:4u);++c)if(capture_at(anim,c))return true;
    for(unsigned d=0;d<4;++d)for(unsigned c=0;c<(anim<4?1u:4u);++c)
        if(capture_at(anim/4*4+d,c))return true;
    for(unsigned a=0;a<anim_count;++a)for(unsigned c=0;c<(a<4?1u:4u);++c)
        if(capture_at(a,c))return true;
    // BeginAnim/AnimCmd_frame update mirror bits before RequestSpriteFrameImageCopy.
    // Recover that mixed boundary only with a matching bounded pending request:
    // exact destination, size, next ROM image and current command's flip bits.
    if(!mapping_1d || phase>=(anim<4?1u:4u) || pending_copies.empty() ||
       pending_copies.size()>64*12 || pending_copies.size()%12)return false;
    const auto* entry=read(rom,anims+anim*4,4);
    const auto* command=entry?read(rom,ptr(entry)+phase*4,4):nullptr;
    if(!command || word(command)>=image_count || (word(command+2)&0xff00) ||
       bool(a1&0x1000)!=bool(((command[2]>>6)^s[0x3f])&1) ||
       bool(a1&0x2000)!=bool(((command[2]>>7)^(s[0x3f]>>1))&1))return false;
    const uint32_t next=image_start+word(command)*bytes;
    const auto* next_entry=read(rom,images+word(command)*8,8);
    if(!next_entry || ptr(next_entry)!=next || word(next_entry+4)!=bytes || !read(rom,next,bytes))return false;
    // Refuse overlapping/repeated writes to this actor's image. The queue is
    // a witness for a particular transition, not permission to repair arbitrary
    // corrupted flips or an image already replaced by a different request.
    const uint32_t dest=0x06010000u+(a2&1023)*32;
    unsigned requests=0;
    for(size_t p=0;p<pending_copies.size();p+=12) {
        const auto* q=pending_copies.data()+p;
        const uint64_t target=ptr(q+4),size=word(q+8);
        if(target<dest+bytes && target+size>dest) {
            if(target!=dest || size!=bytes || ptr(q)!=next)return false;
            ++requests;
        }
    }
    if(requests!=1)return false;
    for(unsigned d=0;d<4;++d)for(unsigned c=0;c<(anim<4?1u:4u);++c)
        if(capture_at(anim/4*4+d,c,true))return true;
    for(unsigned a=0;a<anim_count;++a)for(unsigned c=0;c<(a<4?1u:4u);++c)
        if(capture_at(a,c,true))return true;
    return false;
}
}

bool capture_player_directions(Source& source,std::span<const uint8_t> rom,
                               std::span<const uint8_t> tiles,bool mapping_1d) {
    const uint32_t images=ptr(source.sprite.data()+12);
    const uint32_t start=images==0x0836e068u?0x0830fd60u:images==0x0836f720u?0x0831a5c0u:0;
    return capture_directions(source,rom,tiles,mapping_1d,{0x08370fc8u,images,start,16,32,24,18});
}

bool capture_object_directions(Source& source,std::span<const uint8_t> rom,
                               std::span<const uint8_t> tiles,bool mapping_1d,uint8_t graphics_id,
                               std::span<const uint8_t> pending_copies) {
    source.world_facing=0;
    source.pending_flip_transition=false;
    // Ruby rev1: 218 graphics records. Variable graphics must already have
    // resolved to a real event ID; do not guess a profile from a sheet's shape.
    if(graphics_id>=218)return false;
    const auto* entry=read(rom,0x0836dc70u+graphics_id*4,4);
    const auto* info=entry?read(rom,ptr(entry),0x24):nullptr;
    if(!info || (info[12]&0x40))return false;
    const auto width=word(info+8),height=word(info+10);
    if(!((width==16 && (height==16 || height==32)) || (width==32 && height==32)))return false;
    const uint32_t anims=ptr(info+24),images=ptr(info+28);
    // Only the three audited four-direction tables. Nurse bows, berry growth,
    // fishing, acro-bike tricks and fixed-frame objects retain their source art.
    if(anims!=0x08370f78u && anims!=0x08370f28u && anims!=0x08370fc8u)return false;
    const auto* first=read(rom,images,8);
    if(!first)return false;
    return capture_directions(source,rom,tiles,mapping_1d,{anims,images,ptr(first),width,height,
        anims==0x08370fc8u?24u:20u,anims==0x08370fc8u?18u:anims==0x08370f28u?7u:9u},pending_copies);
}

bool bind_event(Source& source,std::span<const uint8_t> event,unsigned slot) {
    source.viewport_culled=false;
    if(!source.present || event.size()!=0x24 || slot>=16 || !(event[0]&1) ||
       (event[1]&0x20) || word(source.sprite.data()+0x2e)!=slot) {
        source={};return false;
    }
    source.viewport_culled=(event[1]&0x40)!=0;
    source.fixed_pose=(event[1]&0x10)!=0;
    return true;
}

uint8_t apparent_facing(uint8_t facing,float rx,float rz) {
    if(facing<1 || facing>4 || !std::isfinite(rx) || !std::isfinite(rz) ||
       std::hypot(rx,rz)<1e-6f)return facing;
    const float dx=facing==3?-1.f:facing==4?1.f:0.f;
    const float dz=facing==1?1.f:facing==2?-1.f:0.f;
    const float side=dx*rx+dz*rz,toward=-dx*rz+dz*rx;
    return std::abs(side)>std::abs(toward) ? (side>0?4:3) : (toward>0?1:2);
}

Frame decode_direction(const Source& source,std::span<const uint8_t> tiles,
                       std::span<const uint16_t> pal,bool mapping_1d,uint8_t facing) {
    if(!source.world_facing || facing<1 || facing>4)return decode(source,tiles,pal,mapping_1d);
    Source adjusted=source;adjusted.world_facing=0;
    const auto& v=source.directions[facing-1];
    // Palette, pivot, split-body ownership and motion stay exactly as captured.
    adjusted.sprite[3]=uint8_t((adjusted.sprite[3]&0xcf)|(v.hflip?0x10:0)|(v.vflip?0x20:0));
    adjusted.sprite[4]=0;adjusted.sprite[5]&=0xfc;
    if(v.byte_count>v.tiles.size())return decode(source,tiles,pal,mapping_1d);
    auto result=decode(adjusted,std::span(v.tiles).first(v.byte_count),pal,true);
    return result.status==Status::Visible?result:decode(source,tiles,pal,mapping_1d);
}

Frame decode(const Source& source,std::span<const uint8_t> tiles,
             std::span<const uint16_t> pal,bool mapping_1d) {
    Frame f;
    if(!source.present)return f;
    const auto& s=source.sprite;
    auto u16=[&](int p){return unsigned(s[p])|(unsigned(s[p+1])<<8);};
    auto i16=[&](int p){const int v=int(u16(p));return v>=32768?v-65536:v;};
    auto i8=[&](int p){return s[p]>=128?int(s[p])-256:int(s[p]);};
    const auto a0=u16(0),a1=u16(2),a2=u16(4);
    if(!(s[0x3e]&1) || ((s[0x3e]&4) && !source.viewport_culled) || ((a0&0x300)==0x200)) {
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
