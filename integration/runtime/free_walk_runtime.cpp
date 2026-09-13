// SPDX-License-Identifier: GPL-3.0-or-later
#include "free_walk_runtime.h"
#include "viewer.h"
#include "live_scene.h"
#include "runtime_bus_bridge.h"
#include "gba_bus.h"
#include "sha1.h"
#include "actor_frame.h"
#include "dev_runtime.h"
#include "mod_function_hooks.h"
#include "runtime_arm.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace vr::free_walk::runtime {
namespace {
constexpr uint32_t step_entry=0x080587FC, transition_entry=0x08059224,camera_entry=0x0805810C;
Point intent{},position{};
bool owned=false,entry_pending=false,calling=false;
bool viewer_controls=true;
int tracked_object=-1,cell_x=0,cell_z=0;
uint64_t ticks=0,entries=0,handoffs=0;
uint64_t epoch=~uint64_t(0);
const uint8_t* checked_rom=nullptr;
bool verified=false;
uint8_t* writable(uint32_t address,size_t size) {
    auto* b=gbarecomp::active_bus();if(!b)return nullptr;
    if(address>=0x02000000 && uint64_t(address)+size<=0x02040000)return b->ewram_ptr()+address-0x02000000;
    if(address>=0x03000000 && uint64_t(address)+size<=0x03008000)return b->iwram_ptr()+address-0x03000000;
    return nullptr;
}
int s16(const uint8_t* p){return int16_t(uint16_t(p[0])|uint16_t(p[1])<<8);}
void put16(uint8_t* p,int v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
int32_t s32(const uint8_t* p){return int32_t(uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24);}
void put32(uint8_t* p,int32_t v){for(int i=0;i<4;++i)p[i]=uint8_t(uint32_t(v)>>(i*8));}
world::live::Memory memory() {
    auto* b=gbarecomp::active_bus();if(!b)return {};
    if(checked_rom!=b->rom_ptr()) {
        checked_rom=b->rom_ptr();verified=checked_rom && gba::sha1(checked_rom,b->rom_size()).hex()==world::live::kRubySha1;
    }
    return {{b->rom_ptr(),b->rom_size()},{b->ewram_ptr(),0x40000},{b->iwram_ptr(),0x8000},verified};
}
// Small original-game routines are interpreted with an explicit return and
// instruction bound. Their memory effects are real; CPU and call frames belong
// to the intercepted caller and are restored. Never call an overworld loop.
uint32_t guest(uint32_t address,uint32_t a=0,uint32_t b=0,uint32_t c=0,uint32_t d=0) {
    const auto saved=g_cpu;
    const uint32_t depth=runtime_call_stack_depth();
    const auto* data=runtime_call_stack_data();
    const std::vector<uint32_t> stack(data,data+depth);
    calling=true;
    g_cpu.R[0]=a;g_cpu.R[1]=b;g_cpu.R[2]=c;g_cpu.R[3]=d;
    constexpr uint32_t stop=0x08000000;
    g_cpu.R[14]=stop|1;
    runtime_call_push_return(stop);
    const bool ok=runtime_bridge_interpret(address,true,stop,100000)!=0;
    const uint32_t result=g_cpu.R[0];
    g_cpu=saved;runtime_call_stack_restore(stack.data(),depth);calling=false;
    if(!ok)throw std::runtime_error("RubyVR free walking: bounded game helper failed; stopped to protect the session");
    return result;
}
void shift_sprite(uint8_t* sprite,Point before,Point after) {
    put16(sprite+0x20,s16(sprite+0x20)+int(std::lround(after.x*16)-std::lround(before.x*16)));
    put16(sprite+0x22,s16(sprite+0x22)+int(std::lround(after.z*16)-std::lround(before.z*16)));
}
Point source_position(const world::live::Memory& m,const uint8_t* sprite,int x,int z) {
    const auto* view=m.read(world::kGSaveBlock1,4);
    const auto* camera=m.read(world::kSFieldCameraOffset,8);
    const auto* pan=m.read(0x03000598,4);
    const auto* ox=m.read(0x030024d0,2);const auto* oy=m.read(0x030027e0,2);
    if(!view || !camera || !pan || !ox || !oy)return {x+.5,z+.5};
    actor::Frame frame;frame.x=s16(sprite+0x20);frame.y=s16(sprite+0x22);
    frame.x2=s16(sprite+0x24);frame.corner_y=int8_t(sprite[0x29]);
    const int bx=(camera[2]*8-camera[0]-s16(pan)+65536)%256;
    const int by=(camera[3]*8-camera[1]-s16(pan+2)-8+65536)%256;
    const auto p=actor::position(frame,s16(view),s16(view+2),bx,by,s16(ox),s16(oy),x,z);
    return {p.x,p.z};
}
bool blocked(int x,int z,int direction,void* context) {
    if(dev::obstacles_bypassed()) {
        const auto scene=world::live::inspect(memory());
        if(scene.status==world::live::Status::Field && x>=7 && z>=7 && x<scene.width-8 && z<scene.height-7)return false;
    }
    return guest(0x0805FF80,uint32_t(reinterpret_cast<uintptr_t>(context)),uint16_t(x),uint16_t(z),direction)!=0;
}
void release(uint8_t* sprite,bool centre) {
    if(owned && centre && sprite)shift_sprite(sprite,position,{cell_x+0.5,cell_z+0.5});
    if(owned)++handoffs;
    owned=false;entry_pending=false;tracked_object=-1;
}
int intercept(uint32_t address,int thumb,ArmCpuState* cpu) {
    if(calling || !thumb || !cpu || !viewer::active())return 0;
    if(epoch!=g_runtime_state_epoch){epoch=g_runtime_state_epoch;reset();}
    const auto m=memory();
    if(!world::live::field_controls_available(m)){release(nullptr,false);return 0;}
    if(address==camera_entry){
        auto* camera=writable(0x03004880,0x18);
        if(!camera || s32(camera+4)<=0 || s32(camera+4)>=64)return 0;
        const auto* follow=m.read(0x02020004+s32(camera+4)*0x44,0x44);
        if(!follow)return 0;
        const int offset=s32(camera+0x14),delta=s16(follow+0x34);
        // Ruby's otherwise-unreachable partial Y reversal writes its map delta
        // to X. Route this one case through the correct start-of-Y-scroll path,
        // then restore the completed phase. All tile drawing/connection logic
        // still executes in the original CameraUpdate routine.
        if(offset!=0 && offset==-delta){
            put32(camera+0x14,0);guest(camera_entry);put32(camera+0x14,0);
            cpu->R[15]=cpu->R[14]&~1u;return 1;
        }
        return 0;
    }
    auto* avatar=writable(world::kGPlayerAvatar,0x24);
    if(!avatar || avatar[5]>=16)return 0;
    const uint32_t object_address=world::kGObjectEvents+avatar[5]*0x24;
    auto* object=writable(object_address,0x24);
    auto* sprite=object && object[4]<64?writable(0x02020004+object[4]*0x44,0x44):nullptr;
    if(!object || !sprite || !(object[0]&1) || !(object[2]&1))return 0;
    const int x=s16(object+0x10),z=s16(object+0x12);
    if(owned && (tracked_object!=avatar[5] || x!=cell_x || z!=cell_z))release(nullptr,false);
    if(!viewer::continuous_movement() || !viewer_controls) { release(sprite,true);return 0; }
    if(avatar[6] || avatar[1] || (object[1]&1)) {release(sprite,true);return 0;}
    if(address==transition_entry) {
        if(!owned)return 0;
        avatar[3]=entry_pending?2:0;
        avatar[2]=entry_pending?2:0;
        entry_pending=false;
        cpu->R[15]=cpu->R[14]&~1u;return 1;
    }
    if(address!=step_entry)return 0;
    // Ongoing game-authored actions (ledge jumps, scripts, surf, forced tiles)
    // retain ownership until completion. Ordinary idle/turn actions can finish.
    if((object[0]&0x40) && !(object[0]&0x80)) {release(nullptr,false);return 0;}
    if(guest(0x08058964)!=0) {release(sprite,true);return 0;}
    if(!owned) {
        // Checkpoints can be saved between pixels. Adopt the original sprite's
        // real foot instead of inventing a centred position on resume.
        position=source_position(m,sprite,x,z);cell_x=x;cell_z=z;tracked_object=avatar[5];owned=true;
        guest(0x08060634,object_address);
    }
    const int dir=free_walk::facing(intent);
    if(!dir) {
        sprite[0x2c]|=0x40;avatar[2]=0;
        cpu->R[15]=cpu->R[14]&~1u;return 1;
    }
    const Point before=position;
    const auto moved=advance(position,intent,1.0/16,blocked,reinterpret_cast<void*>(uintptr_t(object_address)));
    position=moved.position;++ticks;
    if(moved.blocked && std::hypot(position.x-before.x,position.z-before.z)<1e-8) {
        const int nx=x+(dir==4)-(dir==3),nz=z+(dir==1)-(dir==2);
        const auto collision=guest(0x0805FF80,object_address,uint16_t(nx),uint16_t(nz),dir);
        const bool ledge=guest(0x08063BE4,uint16_t(nx),uint16_t(nz),dir)!=0;
        const auto scene=world::live::inspect(m);
        const bool border=nx<7 || nz<7 || nx>=scene.width-8 || nz>=scene.height-7;
        // A normal wall stops the small body exactly where it is. Only special
        // collisions need Ruby's centred movement action (ledge, object push or
        // border). Its dialogue/door handler already ran before this hook.
        if(ledge || collision==4 || border){release(sprite,true);return 0;}
        guest(0x0805C530,object_address,dir);sprite[0x2c]|=0x40;avatar[2]=0;
        cpu->R[15]=cpu->R[14]&~1u;return 1;
    }
    guest(0x0805C530,object_address,dir);
    const auto anim=guest(0x0805FD68,dir);
    guest(0x08001F70,0x02020004+object[4]*0x44,anim);
    sprite[0x2c]&=uint8_t(~0x40);object[1]&=uint8_t(~4);avatar[2]=2;
    shift_sprite(sprite,before,position);
    if(moved.crossed) {
        cell_x=int(std::floor(position.x));cell_z=int(std::floor(position.z));
        guest(0x0805C054,object_address,uint16_t(cell_x),uint16_t(cell_z));
        object[0]|=0x0c;object[0]&=uint8_t(~0x10);
        entry_pending=true;++entries;
    }
    cpu->R[15]=cpu->R[14]&~1u;return 1;
}
const bool registered_step=gba_mod_register_function_entry_plugin("rubyvr.free-walk.step",step_entry,1,intercept)!=0;
const bool registered_transition=gba_mod_register_function_entry_plugin("rubyvr.free-walk.transition",transition_entry,1,intercept)!=0;
const bool registered_camera=gba_mod_register_function_entry_plugin("rubyvr.free-walk.camera",camera_entry,1,intercept)!=0;
}
void input(Point direction,bool controls){
    intent=direction;viewer_controls=controls;
    // The host resets plugins on state load. These guards are safe while grid
    // mode is selected too, and must run once to return fractional motion home.
    if(available()) {
        gba_mod_set_function_hook_enabled("rubyvr.free-walk.step",1);
        gba_mod_set_function_hook_enabled("rubyvr.free-walk.transition",1);
        gba_mod_set_function_hook_enabled("rubyvr.free-walk.camera",1);
    }
}
void reset(){intent={};owned=false;entry_pending=false;tracked_object=-1;}
bool available(){return registered_step&&registered_transition&&registered_camera;}
Stats stats(){return {owned,position.x,position.z,ticks,entries,handoffs};}
}
