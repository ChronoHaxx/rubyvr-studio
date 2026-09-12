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
        layouts=0x08012000, tiles=0x08014000, list=0x08015000, entries=0x08015100,
        border=0x08017002;
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
            word(lay+8,border);
            // Deliberately only two-byte aligned metatile map data.
            word(lay+12,0x08018002); word(lay+16,tiles); word(lay+20,tiles+24);
        }
        word(headers+12,list); word(list,4); word(list+4,entries);
        connection(0,1,-10,1); connection(1,2,2,2);
        connection(2,3,-10,3); connection(3,4,0,4);
        word(kGBackupMapLayout,25); word(kGBackupMapLayout+4,24);
        word(kGBackupMapLayout+8,0x02000000); select(0);
        word(border,0xB456A123); word(border+4,0xD678C567);
    }
};
void refused(const Scene& s,Status status,const char* why) {
    expect(s.status==status,why);
    expect(s.group==-1 && s.number==-1 && s.layout==0 && s.grid==0 &&
        s.width==0 && s.height==0 && s.connections.empty() &&
        s.border==std::array<uint16_t,4>{},"refusal clears all scene provenance");
}
}
int main() {
    std::vector<uint8_t> decoded;
    const std::vector<uint8_t> compressed{0x10,9,0,0,0x10,'A','B','C',0x30,2};
    expect(decode_tiles(compressed,9,decoded) && decoded==std::vector<uint8_t>({'A','B','C','A','B','C','A','B','C'}),
        "LZ77 overlapping copy and exact declared size");
    for(size_t n=0;n<compressed.size();++n)
        expect(!decode_tiles(std::span(compressed).first(n),9,decoded) && decoded.empty(),"truncated compressed data refuses atomically");
    expect(!decode_tiles(compressed,8,decoded),"compressed allocation limit");
    for(const auto& corrupt:std::vector<std::vector<uint8_t>>{
        {0x11,9,0,0},{0x10,0,0,0},{0x10,3,0,0,0x80,0,0}})
        expect(!decode_tiles(corrupt,16384,decoded) && decoded.empty(),"invalid header, back-reference or overrun refused");
    const std::vector<uint8_t> final_token{0x10,2,0,0,0x40,'A',0,0};
    expect(decode_tiles(final_token,4,decoded) && decoded==std::vector<uint8_t>(4,'A'),
        "BIOS completes the final token even beyond the declared size");
    expect(!decode_tiles(final_token,3,decoded) && decoded.empty(),"final token cannot cross destination capacity");
    {
        Fixture source;
        for(int half=0;half<2;++half) {
            const auto ts=Fixture::tiles+uint32_t(half)*24;
            source.at(ts)[1]=uint8_t(half);
            source.word(ts+4,0x08020000+uint32_t(half)*16384);
            source.word(ts+8,0x08046000);source.word(ts+12,0x08040000);
            source.word(ts+16,0x08045000);
            std::memset(source.at(0x08020000+uint32_t(half)*16384),half?0x76:0x21,16384);
        }
        for(int i=0;i<192;++i) {
            auto* p=source.at(0x08046000)+i*2;p[0]=uint8_t(i);p[1]=0;
        }
        auto m=source.memory();m.ewram={};m.iwram={};
        Snapshot full;
        expect(source_snapshot(m,0,0,full),"static source scenery needs no active scene or guest RAM");
        expect(full.valid && full.has_map_identity() && full.valid_connections() && full.width==25 && full.height==24,
            "complete source body and connection provenance");
        expect(full.vram_tiles.front()==0x21 && full.vram_tiles[16384]==0x76 &&
            full.bg_palette[95]==95 && full.bg_palette[96]==96 && full.bg_palette[191]==191 && full.bg_palette[192]==0,
            "source-specific primary and secondary tile slots and six-palette halves");
        expect(full.grid[7*25+7]==0 && full.grid[0]==0 && full.grid[23*25+24]==0x0456,
            "body, copied neighbours and remaining odd-phase border reconstructed");
        const auto rom_before=source.rom;
        expect(source_snapshot(m,0,1,full) && full.width==35 && full.height==26 &&
            full.player_index==-1 && source.rom==rom_before,"neighbour loaded in full without actors or ROM writes");
        m.verified_ruby_rev1=false;
        expect(!source_snapshot(m,0,0,full) && !full.valid && full.grid.empty(),"source loader requires independent ROM verification");
        m.verified_ruby_rev1=true;
        source.word(Fixture::tiles+4,0x08fffffc);
        expect(!source_snapshot(m,0,0,full) && !full.valid,"truncated source pixels refuse instead of borrowing current-map art");
    }
    Fixture f;
    auto s=inspect(f.memory());
    expect(s.status==Status::Field && s.group==0 && s.number==0,"verified current map identity");
    expect(s.width==25 && s.height==24,"live backup dimensions");
    expect(s.border==std::array<uint16_t,4>{0xA123,0xB456,0xC567,0xD678},
        "source border read at two-byte alignment, all four quarters retained");
    std::vector<uint16_t> visual;
    const auto grid=0x02000000u;
    for(int i=0;i<600;++i) { f.at(grid)[2*i]=0xff; f.at(grid)[2*i+1]=3; }
    auto set_cell=[&](int x,int y,uint16_t value) {
        auto* p=f.at(grid)+2*(y*25+x);p[0]=uint8_t(value);p[1]=uint8_t(value>>8);
    };
    set_cell(7,7,0x3188); set_cell(17,7,0xE211); set_cell(0,17,0x0C99);
    const auto ram_before=f.ew;
    expect(copy_presentation_grid(f.memory(),s,visual),"presentation copy succeeds");
    expect(visual[0]==0x0678 && visual[1]==0x0567 && visual[25]==0x0456 && visual[26]==0x0523,
        "northwest border uses odd backup phase and preserves blocked zero-elevation semantics");
    expect(visual[24]==0x0678 && visual[23*25]==0x0456 && visual.back()==0x0456,
        "right eighth column and bottom border keep the same repeating phase");
    expect(visual[7*25+7]==0x3188 && visual[7*25+8]==kGridUndefined,
        "map body, including an undefined body hole, remains byte-exact");
    expect(visual[7*25+17]==0xE211 && visual[17*25]==0x0C99,
        "real east and south neighbour data preserve art, elevation and collision");
    expect(f.ew==ram_before,"presentation does not modify guest RAM");
    auto bad=s;bad.status=Status::NonField;
    expect(!copy_presentation_grid(f.memory(),bad,visual) && visual.empty(),"invalid scene drops old border output");
    bad=s;bad.width=std::numeric_limits<int>::max();
    expect(!copy_presentation_grid(f.memory(),bad,visual),"oversized copy refused before allocation");
    bad=s;bad.grid=0x0203fff0;
    expect(!copy_presentation_grid(f.memory(),bad,visual),"truncated grid refused");
    bad=s;bad.border.fill(0xFFFF);
    expect(copy_presentation_grid(f.memory(),bad,visual) && visual[0]==kGridUndefined,
        "undefined source border art stays unresolved");
    bad=s;bad.border.fill(0xF000);
    expect(copy_presentation_grid(f.memory(),bad,visual) && visual[0]==0x0400,
        "plain ground border is not assigned source elevation bits as physical height");
    for(auto pointer:{0u,0x02000000u,0x08017003u,0x08fffffau}) {
        f.word(Fixture::layouts+8,pointer);
        refused(inspect(f.memory()),Status::InvalidLayout,"invalid border ROM span rejected");
    }
    f.word(Fixture::layouts+8,Fixture::border);
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
