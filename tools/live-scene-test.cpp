// SPDX-License-Identifier: GPL-3.0-or-later
// Original synthetic memory; contains no ROM or saved-game bytes.
#include "live_scene.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace vr::world;
using namespace vr::world::live;
namespace {
int checks = 0;
void expect(bool ok, const char* what) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); }
}
struct Fixture {
    std::vector<uint8_t> rom=std::vector<uint8_t>(0x1000000);
    std::vector<uint8_t> ew=std::vector<uint8_t>(0x40000), iw=std::vector<uint8_t>(0x8000);
    static constexpr uint32_t table=0x08010000, headers=0x08011000,
        layouts=0x08012000, tiles=0x08014000, list=0x08015000, entries=0x08015100;
    Memory memory() const { return {rom,ew,iw,true}; }
    uint8_t* at(uint32_t a) {
        return const_cast<uint8_t*>(memory().read(a,1));
    }
    void word(uint32_t a,uint32_t v) {
        for(int i=0;i<4;++i) at(a)[i]=uint8_t(v>>(i*8));
    }
    void select(int number) {
        at(kGSaveBlock1+4)[0]=0; at(kGSaveBlock1+5)[0]=uint8_t(number);
        std::memcpy(at(kGMapHeader),at(headers+uint32_t(number)*28),28);
    }
    void connection(int index,int direction,int32_t offset,int number) {
        const auto a=entries+uint32_t(index)*12;
        at(a)[0]=uint8_t(direction); word(a+4,uint32_t(offset));
        at(a)[8]=0; at(a)[9]=uint8_t(number);
    }
    Fixture() {
        word(kMain+4,kOverworldCallback);
        word(kMapGroups,table);
        for(int n=0;n<6;++n) {
            const auto hdr=headers+uint32_t(n)*28, lay=layouts+uint32_t(n)*24;
            word(table+uint32_t(n)*4,hdr); word(hdr,lay);
            word(lay,n==0?10:20); word(lay+4,n==0?10:12);
            // Deliberately only two-byte aligned metatile map data.
            word(lay+12,0x08018002); word(lay+16,tiles); word(lay+20,tiles+24);
        }
        word(headers+12,list); word(list,4); word(list+4,entries);
        connection(0,1,-10,1); connection(1,2,2,2);
        connection(2,3,-10,3); connection(3,4,0,4);
        word(kGBackupMapLayout,25); word(kGBackupMapLayout+4,24);
        word(kGBackupMapLayout+8,0x02000000); select(0);
    }
};
void refused(const Scene& s,Status status,const char* why) {
    expect(s.status==status,why);
    expect(s.group==-1 && s.number==-1 && s.layout==0 && s.grid==0 &&
        s.width==0 && s.height==0 && s.connections.empty(),"refusal clears all scene provenance");
}
}
int main() {
    Fixture f;
    auto s=inspect(f.memory());
    expect(s.status==Status::Field && s.group==0 && s.number==0,"verified current map identity");
    expect(s.width==25 && s.height==24,"live backup dimensions");
    // Hand-calculated clipping in backup coordinates, including the eighth east column.
    const std::vector<ConnectionSlice> expected{
        {0,1,35,26,0,17,10,7,17,7,true},
        {0,2,35,26,9,0,7,12,16,7,true},
        {0,3,35,26,0,0,20,10,7,9,true},
        {0,4,35,26,17,7,7,7,8,12,true}};
    expect(s.connections==expected,"four directional copies, negative offsets and source order");
    Snapshot snap; snap.valid=true; snap.map_group=s.group; snap.map_number=s.number;
    snap.identity_source=Snapshot::IdentitySource::LiveCapture;
    snap.width=s.width; snap.height=s.height; snap.connections=s.connections;
    expect(snap.valid_connections(),"provenance accepted by shared snapshot contract");
    auto m=f.memory(); m.verified_ruby_rev1=false;
    refused(inspect(m),Status::UnsupportedRom,"ROM must be independently verified");
    m=f.memory(); m.iwram=m.iwram.first(12);
    refused(inspect(m),Status::Unreadable,"truncated RAM");
    m=f.memory(); m.rom=m.rom.first(m.rom.size()-1);
    refused(inspect(m),Status::Unreadable,"wrong ROM size");
    for(auto cb:{0u,0x08054605u,kOverworldCallback-1}) {
        f.word(kMain+4,cb);
        refused(inspect(f.memory()),Status::NonField,"nonfield or invalid Thumb callback");
    }
    f.word(kMain+4,kOverworldCallback); f.at(kMain+0x43d)[0]=2;
    refused(inspect(f.memory()),Status::NonField,"battle keeps old field RAM but must not use it");
    f.at(kMain+0x43d)[0]=1;
    expect(inspect(f.memory()).status==Status::Field,"unrelated OAM bit does not disable field");
    f.at(kGSaveBlock1+4)[0]=34;
    refused(inspect(f.memory()),Status::InvalidMap,"group beyond table");
    f.select(0); f.at(kGSaveBlock1+5)[0]=54;
    refused(inspect(f.memory()),Status::InvalidMap,"map beyond group");
    f.select(0); f.word(kGMapHeader,Fixture::layouts+24);
    refused(inspect(f.memory()),Status::HeaderMismatch,"transition has new header with old location");
    f.select(0); f.at(kGMapHeader+18)[0]=1;
    refused(inspect(f.memory()),Status::HeaderMismatch,"layout id mismatch");
    f.select(0); f.at(kGMapHeader+22)[0]=7;
    expect(inspect(f.memory()).status==Status::Field,"mutable weather is not a map key");
    f.word(kGBackupMapLayout,26);
    refused(inspect(f.memory()),Status::InvalidLayout,"transition has inconsistent backup dimensions");
    f.word(kGBackupMapLayout,25); f.word(kGBackupMapLayout+8,0x0203fff0);
    refused(inspect(f.memory()),Status::InvalidLayout,"grid crossing RAM end");
    f.word(kGBackupMapLayout+8,0x08000000);
    refused(inspect(f.memory()),Status::InvalidLayout,"live grid must be RAM");
    f.word(kGBackupMapLayout+8,0x02000000);
    f.word(Fixture::layouts,0xffffffff);
    refused(inspect(f.memory()),Status::InvalidLayout,"negative layout dimension");
    f.word(Fixture::layouts,10);
    for(auto count:{0xffffffffu,65u,0x7fffffffu}) {
        f.word(Fixture::list,count);
        refused(inspect(f.memory()),Status::InvalidConnections,"bad connection count");
    }
    f.word(Fixture::list,4); f.word(Fixture::list+4,0x08fffff0);
    refused(inspect(f.memory()),Status::InvalidConnections,"connection table crosses ROM end");
    f.word(Fixture::list+4,Fixture::entries); f.connection(3,0,0,4);
    refused(inspect(f.memory()),Status::InvalidConnections,"partial connection list cannot leak");
    f.connection(3,4,0,4); f.word(Fixture::layouts+4*24+20,Fixture::tiles+48);
    s=inspect(f.memory());
    expect(s.status==Status::Field && !s.connections[3].compatible_art,"different neighbour art retained as incompatible");
    f.connection(0,5,0,1); f.connection(1,6,0,2);
    expect(inspect(f.memory()).connections.size()==2,"dive and emerge are not planar copies");
    f.connection(2,3,std::numeric_limits<int32_t>::min(),3);
    f.connection(3,4,std::numeric_limits<int32_t>::max(),4);
    expect(inspect(f.memory()).connections.empty(),"extreme offsets clip safely");
    // A different map can share a layout; returning to the old map is not cached identity.
    f.word(Fixture::headers+5*28,Fixture::layouts); f.select(5);
    s=inspect(f.memory());
    expect(s.status==Status::Field && s.number==5 && s.connections.empty(),"interior with shared layout has own identity");
    f.select(0); expect(inspect(f.memory()).number==0,"return to original identity");
    m=f.memory();
    expect(m.read_rom(0x0a000000,4)==m.rom.data(),"ROM waitstate mirror");
    expect(!m.read(0x02000001,std::numeric_limits<size_t>::max()),"span overflow rejected");
    expect(!m.read_rom(0x08000001,4),"unaligned header pointer rejected");
    expect(!m.read_rom(0x02000000,4),"ROM structural pointer cannot refer to RAM");
    std::cout << "PASS: live scene contract (" << checks << " checks; synthetic memory, no graphics or game assets)\n";
}
