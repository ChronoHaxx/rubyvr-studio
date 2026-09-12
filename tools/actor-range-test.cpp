// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic source pixels and memory; no SDL, GL, ROM or saved game required.
#include "actor_range.h"
#include "actor_rules.h"
#include <iostream>
#include <cstdlib>
using namespace vr;
int checks=0;
void expect(bool ok,const char* label){++checks;if(!ok){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
world::Snapshot fixture() {
    world::Snapshot s;s.valid=true;s.map_group=0;s.map_number=9;s.layout_ptr=100;
    s.identity_source=world::Snapshot::IdentitySource::LiveCapture;s.actor_range_safe=true;
    world::ActorTemplate t;t.bytes[0]=1;t.bytes[1]=2;t.hidden=false;s.actor_templates.push_back(t);
    auto& o=s.objects[1];o.active=true;o.local_id=1;o.graphics_id=2;o.map_number=9;
    o.x=o.initial_x=12;o.y=o.initial_y=12;
    auto& source=s.actor_sources[1];source.present=true;source.world_facing=2;
    source.sprite[3]=0x40;source.sprite[0x3e]=3;source.sprite[0x28]=source.sprite[0x29]=248;
    source.sprite[0x20]=200;source.sprite[0x22]=200;
    for(auto& d:source.directions){d.byte_count=128;d.tiles.fill(0x11);}
    s.obj_tiles.assign(32768,0x11);s.obj_palette.assign(256,0);s.obj_palette[1]=31;s.obj_mapping_1d=true;
    return s;
}
void depart(world::Snapshot& s){s.view_x=40;s.objects[1].active=false;s.actor_sources[1]={};s.obj_tiles.clear();s.obj_palette.clear();}
int main(){
    actor::RangeCache cache;auto s=fixture();cache.update(s);
    expect(cache.distant().empty(),"live NPC is not duplicated");depart(s);cache.update(s);
    expect(cache.distant().size()==1,"known NPC survives actual source distance removal");
    const auto before=cache.distant()[0];
    expect(before.frame.width==16 && before.frame.rgba[0]==255,"owned pixels survive recycled OBJ memory");
    expect(before.position.x==12.5f && before.position.z==12.5f,"last observed world position survives moving source camera");
    s.view_x=60;cache.update(s);expect(cache.distant()[0].position.x==before.position.x,"no extra offscreen motion clock");
    for(int reason=0;reason<10;++reason) {
        s=fixture();cache.reset();cache.update(s);depart(s);
        switch(reason){
        case 0:s.actor_templates[0].hidden=true;break;
        case 1:s.actor_templates.clear();break;
        case 2:s.actor_templates[0].bytes[4]=3;break;
        case 3:s.actor_epoch++;break;
        case 4:s.map_number++;break;
        case 5:s.actor_range_safe=false;break;
        case 6:s.view_x=0;break;
        case 7:s.objects[1].active=true;s.objects[1].invisible=true;break;
        case 8:s.actor_templates.push_back(s.actor_templates[0]);break;
        case 9:s.layout_ptr++;break;
        }
        cache.update(s);expect(cache.distant().empty(),"hiding/removal/template/load/map/script/inside/duplicate invalidation");
    }
    s=fixture();cache.reset();s.objects[1].initial_x=45;cache.update(s);depart(s);cache.update(s);
    expect(cache.distant().empty(),"initial coordinates also keep the guest slot in range");
    s=fixture();cache.reset();cache.update(s);depart(s);cache.update(s);
    s=fixture();cache.update(s);expect(cache.distant().empty(),"live respawn takes precedence");
    cache.reset();s=fixture();depart(s);cache.update(s);expect(cache.distant().empty(),"unseen templates never fabricate NPCs");
    for(bool fixed:{false,true}) {
        s=fixture();cache.reset();s.actor_sources[1].fixed_pose=fixed;
        if(!fixed)s.actor_sources[1].world_facing=0;
        cache.update(s);depart(s);cache.update(s);expect(cache.distant().empty(),"unverified or scripted pose not retained");
    }
    std::vector<uint8_t> rom(0x1000000),ew(0x40000),iw(0x8000);
    world::live::Memory m{rom,ew,iw,true};
    auto byte=[&](uint32_t a)->uint8_t&{return *const_cast<uint8_t*>(m.read(a,1));};
    auto word=[&](uint32_t a,uint32_t v){for(int i=0;i<4;++i)byte(a+i)=uint8_t(v>>(i*8));};
    word(world::live::kMain,world::live::kOverworldInputCallback);word(world::live::kMain+4,world::live::kOverworldCallback);
    byte(world::kGPlayerAvatar)=1;word(world::kGMapHeader+4,0x08010000);rom[0x10000]=1;
    byte(world::kGSaveBlock1+0xc20)=1;byte(world::kGSaveBlock1+0xc20+20)=9;
    world::live::capture_actor_rules(m,s,8);expect(s.actor_range_safe && !s.actor_templates[0].hidden && s.actor_epoch==8,"normal flag is captured");
    byte(world::kGSaveBlock1+0x1220+1)=2;world::live::capture_actor_rules(m,s,8);
    expect(s.actor_templates[0].hidden,"set source flag hides the event");
    byte(world::kGSaveBlock1+0xc20+21)=0x40;world::live::capture_actor_rules(m,s,8);
    expect(s.actor_templates[0].hidden,"special flag is not misread as normal flag");
    byte(world::live::kFieldControlsLock)=1;world::live::capture_actor_rules(m,s,8);
    expect(!s.actor_range_safe && s.actor_templates.empty(),"scripts clear capture rules");
    byte(world::live::kFieldControlsLock)=0;m.verified_ruby_rev1=false;world::live::capture_actor_rules(m,s,8);
    expect(!s.actor_range_safe && s.actor_templates.empty(),"unverified ROM cannot retain actors");
    std::cout<<"PASS "<<checks<<" actor-range/capture checks\n";
}
