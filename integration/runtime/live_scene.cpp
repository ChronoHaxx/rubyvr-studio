// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_scene.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace vr::world::live {
namespace {
constexpr int kMaxMapDataSize = 10240;

// Bounds of the pinned source table, cross-checked against the hash-gated ROM.
constexpr std::array<int, 34> kGroupCounts{
    54,5,5,6,7,7,8,7,7,13,8,17,10,24,13,13,14,2,2,2,3,1,1,1,86,44,12,2,1,13,1,1,3,1};

uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
int64_t signed32(const uint8_t* p) {
    const uint32_t v = u32(p);
    return v < 0x80000000u ? int64_t(v) : int64_t(v) - 0x100000000ll;
}
bool map_id(int group, int number) {
    return group >= 0 && group < int(kGroupCounts.size()) &&
           number >= 0 && number < kGroupCounts[size_t(group)];
}
const uint8_t* header(const Memory& m, int group, int number) {
    if (!map_id(group, number)) return nullptr;
    const auto* entry = m.read_rom(kMapGroups + uint32_t(group)*4, 4);
    if (!entry) return nullptr;
    const auto* table = m.read_rom(u32(entry), size_t(kGroupCounts[size_t(group)])*4);
    return table ? m.read_rom(u32(table + number*4), 28) : nullptr;
}
struct Layout {
    int width = 0, height = 0;
    uint32_t primary = 0, secondary = 0;
    std::array<uint16_t, 4> border{};
};
bool layout(const Memory& m, uint32_t address, Layout& out) {
    const auto* p = m.read_rom(address, 24);
    if (!p) return false;
    const auto w = signed32(p), h = signed32(p+4);
    if (w < 1 || h < 1 || w > 1009 || h > 1010 || (w+15)*(h+14) > kMaxMapDataSize)
        return false;
    if (!m.read_rom(u32(p+12), size_t(w*h)*2, 2)) return false;
    const auto* border = m.read_rom(u32(p+8), 8, 2);
    if (!border) return false;
    const uint32_t primary = u32(p+16), secondary = u32(p+20);
    if (!m.read_rom(primary, 24) || (secondary && !m.read_rom(secondary, 24))) return false;
    out = {int(w), int(h), primary, secondary};
    for (int i=0; i<4; ++i)
        out.border[i] = uint16_t(border[2*i] | uint16_t(border[2*i+1])<<8);
    return true;
}

bool connections(const Memory& m, const uint8_t* hdr, const Layout& own,
                 Scene& out) {
    const uint32_t pointer = u32(hdr+12);
    if (!pointer) return true; // Interior/no planar connections.
    const auto* list = m.read_rom(pointer, 8);
    if (!list) return false;
    const auto count = signed32(list);
    if (count < 0 || count > 64) return false;
    if (!count) return true;
    // ARM ABI padding matters: direction + 3 pad bytes, s32 offset, u8/u8, 2 pad.
    const auto* entries = m.read_rom(u32(list+4), size_t(count)*12);
    if (!entries) return false;
    for (int i = 0; i < count; ++i) {
        const auto* e = entries + i*12;
        const int direction = e[0], group = e[8], number = e[9];
        if (!map_id(group, number) || direction < 1 || direction > 6) return false;
        if (direction > 4) continue; // Dive/emerge do not copy a planar border.
        const auto* neighbour = header(m, group, number);
        Layout other;
        if (!neighbour || !layout(m, u32(neighbour), other)) return false;
        const int64_t offset = signed32(e+4);
        int64_t x=0, y=0, sx=0, sy=0, w=0, h=0;
        if (direction == 1 || direction == 2) {
            x=offset+7; y=direction==1 ? own.height+7 : 0;
            sy=direction==1 ? 0 : other.height-7;
            w=other.width; h=7;
        } else {
            x=direction==3 ? 0 : own.width+7; y=offset+7;
            sx=direction==3 ? other.width-7 : 0;
            w=direction==3 ? 7 : 8; h=other.height;
        }
        // Same clipped source/destination rectangles as the source adapter.
        // Wide arithmetic also makes corrupt extreme signed offsets harmless.
        const auto left=std::max({int64_t(0),-x,-sx});
        const auto top=std::max({int64_t(0),-y,-sy});
        const auto right=std::min({w,int64_t(out.width)-x,int64_t(other.width)-sx});
        const auto bottom=std::min({h,int64_t(out.height)-y,int64_t(other.height)-sy});
        if (right<=left || bottom<=top) continue;
        const bool same_art = (own.primary & 0x01ffffffu)==(other.primary & 0x01ffffffu) &&
            (own.secondary & 0x01ffffffu)==(other.secondary & 0x01ffffffu);
        out.connections.push_back({group,number,other.width+15,other.height+14,
            int(x+left),int(y+top),int(sx+left+7),int(sy+top+7),
            int(right-left),int(bottom-top),same_art});
    }
    return true; // Preserve source order, including overlapping corner copies.
}
} // namespace

const uint8_t* Memory::read(uint32_t address, size_t bytes) const {
    std::span<const uint8_t> region;
    size_t offset = 0;
    switch (address >> 24) {
    case 2: region=ewram; offset=address & 0x3ffffu; break;
    case 3: region=iwram; offset=address & 0x7fffu; break;
    case 8: case 9: case 10: case 11: case 12: case 13:
        region=rom; offset=address & 0x01ffffffu; break;
    default: return nullptr;
    }
    if (!bytes || offset>region.size() || bytes>region.size()-offset) return nullptr;
    return region.data()+offset;
}
const uint8_t* Memory::read_rom(uint32_t address, size_t bytes, unsigned alignment) const {
    if ((address>>24) < 8 || (address>>24) > 13 ||
        (alignment != 2 && alignment != 4) || address % alignment) return nullptr;
    return read(address,bytes);
}

bool field_controls_available(const Memory& m) {
    if (!m.verified_ruby_rev1 || m.ewram.size()!=0x40000 ||
        m.iwram.size()!=0x8000 || m.rom.size()!=0x1000000) return false;
    const auto* main=m.read(kMain,0x440);
    const auto* lock=m.read(kFieldControlsLock,1);
    const auto* avatar=m.read(kGPlayerAvatar,6);
    return main && lock && avatar && !*lock &&
        u32(main)==kOverworldInputCallback && u32(main+4)==kOverworldCallback &&
        !(main[0x43d]&2) && (avatar[0]&1) && !(avatar[0]&0x1e);
}

Scene inspect(const Memory& m) {
    Scene out;
    auto refuse = [&](Status status) {
        Scene invalid;
        invalid.status=status; invalid.callback1=out.callback1; invalid.callback2=out.callback2;
        return invalid;
    };
    if (!m.verified_ruby_rev1) return refuse(Status::UnsupportedRom);
    if (m.ewram.size()!=0x40000 || m.iwram.size()!=0x8000 || m.rom.size()!=0x1000000)
        return refuse(Status::Unreadable);
    const auto* main = m.read(kMain,0x440);
    out.callback1=u32(main); out.callback2=u32(main+4);
    if (out.callback2!=kOverworldCallback || (main[0x43d]&2)) return refuse(Status::NonField);
    const auto* location=m.read(kGSaveBlock1+4,2);
    const int group=location[0], number=location[1];
    const auto* source=header(m,group,number);
    if (!source) return refuse(Status::InvalidMap);
    const auto* current=m.read(kGMapHeader,28);
    // Structural fields + layout ID. Mutable music/weather/flags are not keys.
    if (std::memcmp(source,current,16)!=0 || source[18]!=current[18] || source[19]!=current[19])
        return refuse(Status::HeaderMismatch);
    Layout own;
    if (!layout(m,u32(current),own)) return refuse(Status::InvalidLayout);
    const auto* backup=m.read(kGBackupMapLayout,12);
    const auto w=signed32(backup), h=signed32(backup+4);
    const uint32_t grid=u32(backup+8);
    if (w!=own.width+15 || h!=own.height+14 || (grid>>24!=2 && grid>>24!=3) ||
        !m.read(grid,size_t(w*h)*2)) return refuse(Status::InvalidLayout);
    out.group=group; out.number=number; out.layout=u32(current); out.grid=grid;
    out.width=int(w); out.height=int(h);
    out.primary_tileset=own.primary; out.secondary_tileset=own.secondary;
    out.border=own.border;
    if (!connections(m,source,own,out)) return refuse(Status::InvalidConnections);
    out.status=Status::Field;
    return out;
}

bool copy_presentation_grid(const Memory& m, const Scene& scene,
                            std::vector<uint16_t>& out) {
    out.clear();
    if (!m.verified_ruby_rev1 || scene.status!=Status::Field ||
        scene.width<16 || scene.height<15 ||
        int64_t(scene.width)*scene.height>kMaxMapDataSize) return false;
    const size_t cells=size_t(scene.width)*scene.height;
    const auto* raw=m.read(scene.grid,cells*2);
    if (!raw || (scene.grid>>24!=2 && scene.grid>>24!=3)) return false;
    out.resize(cells);
    for (int y=0; y<scene.height; ++y) for (int x=0; x<scene.width; ++x) {
        const size_t i=size_t(y)*scene.width+x;
        uint16_t cell=uint16_t(raw[2*i] | uint16_t(raw[2*i+1])<<8);
        const bool padding=x<7 || y<7 || x>=scene.width-8 || y>=scene.height-7;
        if (padding && cell==kGridUndefined) {
            // GetBorderBlockAt uses backup coordinates, not distance from the
            // nearest edge. Its odd phase keeps all four tree quarters aligned.
            const auto id=scene.border[((y+1)&1)*2+((x+1)&1)] & kMetatileIdMask;
            if (id!=kGridUndefined) cell=uint16_t(id | (1u<<kCollisionShift));
        }
        out[i]=cell;
    }
    return true;
}

const char* status_name(Status status) {
    switch (status) {
    case Status::UnsupportedRom: return "unsupported-rom";
    case Status::Unreadable: return "unreadable";
    case Status::NonField: return "non-field";
    case Status::InvalidMap: return "invalid-map";
    case Status::HeaderMismatch: return "header-mismatch";
    case Status::InvalidLayout: return "invalid-layout";
    case Status::InvalidConnections: return "invalid-connections";
    case Status::Field: return "field";
    }
    return "unknown";
}
} // namespace vr::world::live
