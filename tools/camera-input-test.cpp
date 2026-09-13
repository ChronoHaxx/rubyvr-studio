// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera_input.h"
#include "live_scene.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace vr::camera_input;
static int checks=0;
static void expect(bool result,const char* label) {
    ++checks;if(!result){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}
}
static uint16_t press(uint16_t bits){return uint16_t(0x3ff & ~bits);}
int main() {
    constexpr uint16_t up=0x40,right=0x10,down=0x80,left=0x20;
    // Expected world directions for screen Up, Right, Down, Left at each view.
    constexpr uint16_t expected[4][4]={{up,right,down,left},{left,up,right,down},
        {down,left,up,right},{right,down,left,up}};
    constexpr uint16_t screen[]={up,right,down,left};
    const std::array<uint16_t,4> wasd={
        with_wasd(0x3ff,true,false,false,false),
        with_wasd(0x3ff,false,false,false,true),
        with_wasd(0x3ff,false,false,true,false),
        with_wasd(0x3ff,false,true,false,false)};
    for(int q=0;q<4;++q)for(int i=0;i<4;++i) {
        Mapper alias;
        expect(alias.update(wasd[i],q*1.570796327f,Context::Camera)==press(expected[q][i]),
            "WASD follows all four camera views like arrows");
    }
    expect(with_wasd(press(right|1|2|8),true,false,false,false)==press(up|right|1|2|8),
        "W adds to arrow/controller input without changing action buttons");
    expect(with_wasd(press(up),true,false,false,false)==press(up),"W and Up do not cancel each other");
    expect(with_wasd(press(up),false,false,true,false)==press(up|down),"opposing aliases keep source input semantics");
    expect(with_wasd(0xffff,false,false,false,false)==0xffff,"released aliases preserve every host bit");
    Mapper aliases;
    aliases.update(0x3ff,1.57f,Context::Camera);
    expect(aliases.update(wasd[0],1.57f,Context::Camera)==press(left),"W begins a camera-relative walk");
    expect(aliases.update(wasd[0],1.57f,Context::Menu)==0x3ff,"opening game UI releases held W");
    aliases.update(0x3ff,1.57f,Context::Menu);
    expect(aliases.update(wasd[0],1.57f,Context::Menu)==press(up),"W navigates game menus without camera rotation");
    expect(aliases.update(wasd[0],1.57f,Context::Inactive)==0x3ff,"host panel or focus loss suppresses aliases");
    expect(aliases.update(wasd[0],1.57f,Context::Camera)==0x3ff,"closing panel requires W release before walking");
    aliases.update(0x3ff,1.57f,Context::Camera);
    expect(aliases.update(wasd[0],1.57f,Context::Camera)==press(left),"W resumes normally after release");
    for(int q=0;q<4;++q)for(int i=0;i<4;++i) {
        Mapper m;
        expect(m.update(press(screen[i]|1|8),q*1.570796327f,Context::Camera)==
            press(expected[q][i]|1|8),"four viewpoints preserve action/start and map direction");
    }
    expect(rotate(press(up|right),1)==press(left|up),"diagonal combination retains original grid-input semantics");
    expect(rotate(press(up|down),1)==press(left|right),"opposing input is preserved, not invented movement");
    for(unsigned bits=0;bits<1024;++bits)
        expect((rotate(uint16_t(bits),3)&~directions)==(bits&~directions),"all non-direction bits preserved");
    expect(quadrant(0.785f)==0 && quadrant(0.786f)==1,"nearest quadrant boundary");
    expect(quadrant(-0.786f)==3,"negative orbit");
    expect(quadrant(6.283185307f)==0,"complete revolution");
    TurnLatch turn;
    expect(turn.update(false,true,false)==1,"right press turns one quarter");
    expect(turn.update(false,true,false)==0,"held camera key does not spin");
    turn.update(false,false,false);
    expect(turn.update(true,false,false)==3,"left press turns the opposite quarter");
    turn.update(false,false,false);
    expect(turn.update(false,true,true)==0,"arrow walk defers camera turn");
    expect(turn.update(false,false,true)==0,"queued turn waits while walking");
    expect(turn.update(false,false,false)==1,"arrow release applies the queued quarter");
    expect(turn.update(false,false,false)==0,"queued turn applies only once");
    expect(turn.update(true,true,false)==0,"opposing camera keys cancel");
    turn.update(false,false,false);turn.update(false,true,true);turn.reset();
    expect(turn.update(false,true,false)==0,"focus reset discards pending turn and held key");
    turn.update(false,false,false);
    expect(turn.update(false,true,false)==1,"new camera press works after focus neutral");
    turn.update(false,false,false);
    expect(turn.update(false,true,((~wasd[0])&directions)!=0)==0,"W walk also defers a camera turn");
    expect(turn.update(false,false,false)==1,"W release applies the queued turn once");
    Mapper m;
    expect(m.update(press(up),0,Context::Camera)==press(up),"start held walk");
    expect(m.update(press(up),1.57f,Context::Camera)==press(up),"orbit cannot turn an already held walk");
    m.update(0x3ff,1.57f,Context::Camera);
    expect(m.update(press(up),1.57f,Context::Camera)==press(left),"release permits new camera direction");
    expect(m.update(press(up|1),1.57f,Context::Menu)==press(1),"menu transition releases direction but preserves action");
    m.update(0x3ff,1.57f,Context::Menu);
    expect(m.update(press(up),1.57f,Context::Menu)==press(up),"menu up remains up");
    expect(m.update(press(up),1.57f,Context::Camera)==0x3ff,"closing menu while held cannot walk");
    m.update(0x3ff,1.57f,Context::Original);
    expect(m.update(press(right),1.57f,Context::Original)==press(right),"original view stays map-relative");
    expect(m.update(press(right|1),1.57f,Context::Inactive)==0x3ff,"background releases all game input");
    expect(m.update(press(right),1.57f,Context::Camera)==0x3ff,"focus regain requires direction release");
    m.update(0x3ff,1.57f,Context::Camera);m.reset();
    expect(m.update(press(right),1.57f,Context::Camera)==0x3ff,"checkpoint reset requires direction release");
    m.update(0x3ff,0,Context::Camera);
    expect(m.update(press(up),std::numeric_limits<float>::quiet_NaN(),Context::Camera)==press(up),"invalid camera cannot corrupt input");

    using namespace vr::world::live;
    std::vector<uint8_t> rom(0x1000000),ewram(0x40000),iwram(0x8000);
    Memory memory{rom,ewram,iwram,true};
    auto put=[&](uint32_t address,uint32_t value){
        auto& v=(address>>24)==3?iwram:ewram;size_t p=address&uint32_t(v.size()-1);
        for(int i=0;i<4;++i)v[p+i]=uint8_t(value>>(i*8));
    };
    put(kMain,kOverworldInputCallback);put(kMain+4,kOverworldCallback);
    put(vr::world::kGPlayerAvatar,1);
    expect(field_controls_available(memory),"verified ordinary on-foot field accepts camera input");
    // An indoor overworld callback is still a valid native field. The optional
    // outdoor controller must not swallow its input when a free camera remains
    // selected, including during the frame before the renderer changes views.
    for(unsigned type=0;type<256;++type){
        put(vr::world::kGMapHeader+0x17,type);
        expect(outdoor_controls_available(memory)==(type>=1&&type<=3),
               "only town/city/route maps grant outdoor movement ownership");
    }
    for(unsigned type:{4u,5u,6u,7u,8u,9u}){
        put(vr::world::kGMapHeader+0x17,type);
        expect(field_controls_available(memory)&&!outdoor_controls_available(memory),
               "indoor and other map types keep native field controls");
    }
    put(vr::world::kGMapHeader+0x17,2);
    iwram[kFieldControlsLock&0x7fff]=1;
    expect(!field_controls_available(memory),"start-menu/dialogue field lock retains raw navigation");
    expect(!outdoor_controls_available(memory),"outdoor map does not bypass native input locks");
    iwram[kFieldControlsLock&0x7fff]=0;put(kMain,0);
    expect(!field_controls_available(memory),"other input callback refuses remapping");
    put(kMain,kOverworldInputCallback);put(kMain+4,0);
    expect(!field_controls_available(memory),"Bag/other main callback refuses remapping");
    put(kMain+4,kOverworldCallback);iwram[(kMain+0x43d)&0x7fff]=2;
    expect(!field_controls_available(memory),"battle flag refuses remapping");
    iwram[(kMain+0x43d)&0x7fff]=0;
    for(uint32_t flags:{2u,4u,8u,16u,3u,0u}){
        put(vr::world::kGPlayerAvatar,flags);
        expect(!field_controls_available(memory),"non-foot and mixed movement flags refuse remapping");
    }
    put(vr::world::kGPlayerAvatar,1);memory.verified_ruby_rev1=false;
    expect(!field_controls_available(memory),"unknown ROM keeps original behavior");
    expect(!outdoor_controls_available(memory),"outdoor gate refuses an unverified ROM");
    memory.verified_ruby_rev1=true;memory.iwram={};
    expect(!field_controls_available(memory),"unreadable state refuses remapping");
    expect(!outdoor_controls_available(memory),"outdoor gate refuses incomplete memory");
    std::cout<<"PASS: "<<checks<<" camera input and verified field-control checks\n";
}
