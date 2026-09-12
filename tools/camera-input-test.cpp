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
    iwram[kFieldControlsLock&0x7fff]=1;
    expect(!field_controls_available(memory),"start-menu/dialogue field lock retains raw navigation");
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
    memory.verified_ruby_rev1=true;memory.iwram={};
    expect(!field_controls_available(memory),"unreadable state refuses remapping");
    std::cout<<"PASS: "<<checks<<" camera input and verified field-control checks\n";
}
