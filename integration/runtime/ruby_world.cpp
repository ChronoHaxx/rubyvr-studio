// ruby_world.cpp — see ruby_world.h for why this is possible at all.
//
// THE ONE PERFORMANCE TRAP, written down because it is silent:
//
//   Do NOT read guest memory here through bus_read_u8/u16/u32. Those are the
//   entry points generated code uses, and they do real work per call:
//   runtime_bus_bridge.cpp's is_idle_safe_read() classifies VRAM, PAL and OAM
//   as NOT idle-safe, so every such read does ++g_idle_disturb_epoch. That
//   counter is how the runtime decides an idle loop can be elided. Bulk-reading
//   32 KB of VRAM through it once a frame would quietly disable that
//   optimisation and cost real speed, with nothing in any log to say why.
//
//   active_bus() hands out the raw region pointers instead (gba_bus.h:176-192).
//   They are the same bytes with none of the bookkeeping. bus_read_* stays fine
//   for the odd one-off scalar; it is bulk reads that must not use it.

#include "ruby_world.h"
#include "live_scene.h"
#include "live_presentation.h"
#include "actor_rules.h"

#include "runtime_bus_bridge.h"   // gbarecomp::active_bus()
#include "gba_bus.h"              // gba::GbaBus region pointers
#include "sha1.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Private runner lifecycle signal: increments for file loads, rewind and TCP
// loads, not just our named checkpoints. Never restore a host world across it.
extern "C" unsigned long long g_runtime_state_epoch;

namespace vr {
namespace world {
namespace {
const uint8_t* g_verified_rom = nullptr;
size_t g_verified_size = 0;
bool g_supported_rom = false;
int g_last_status = -1;
uint64_t g_presentation_epoch=0, g_seen_state_epoch=~uint64_t(0);

// ── Guest → host address resolution ──────────────────────────────────────────
//
// The guest's regions mirror, and the mirroring is real and visible to games
// (gba_memory.h:16-19). EWRAM's 256 KB repeats every 0x40000 through
// 0x02FFFFFF; IWRAM's 32 KB repeats every 0x8000 through 0x03FFFFFF; the
// cartridge appears three times, at 0x08/0x0A/0x0C, with different waitstates
// but the same bytes. Pointers stored by the game are ordinary guest addresses
// and may legitimately be in any mirror, so resolve rather than assume.
//
// Returns nullptr when `addr` is outside the regions we know about or when the
// requested span would run off the end. Callers treat nullptr as "the field map
// is not up yet", which is the normal state in the launcher and during battles.
const uint8_t* host_ptr(gba::GbaBus* bus, uint32_t addr, size_t bytes) {
    if (!bus || bytes == 0) return nullptr;

    const uint32_t region = addr >> 24;
    switch (region) {
        case 0x02: {   // EWRAM, 256 KB
            const uint32_t off = addr & 0x0003FFFFu;
            if (off + bytes > 0x40000u) return nullptr;
            return bus->ewram_ptr() + off;
        }
        case 0x03: {   // IWRAM, 32 KB
            const uint32_t off = addr & 0x00007FFFu;
            if (off + bytes > 0x8000u) return nullptr;
            return bus->iwram_ptr() + off;
        }
        case 0x08: case 0x09:   // cartridge, waitstate 0
        case 0x0A: case 0x0B:   // waitstate 1
        case 0x0C: case 0x0D: { // waitstate 2 — all the same bytes
            const uint8_t* rom = bus->rom_ptr();
            if (!rom) return nullptr;
            const uint32_t off = addr & 0x01FFFFFFu;
            if (off + bytes > bus->rom_size()) return nullptr;
            return rom + off;
        }
        default:
            return nullptr;
    }
}

// Little-endian scalar reads. The guest is little-endian ARM and the host is
// little-endian x64, so these are byte copies — but go through memcpy rather
// than a pointer cast, because guest structs are not guaranteed to sit at an
// alignment the host would accept for a u32 load.
uint16_t rd16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
int32_t  rds32(const uint8_t* p) { int32_t v; std::memcpy(&v, p, 4); return v; }
int16_t  rds16(const uint8_t* p) { int16_t v; std::memcpy(&v, p, 2); return v; }

// ── Struct field offsets, from third_party/pokeruby/include/global.fieldmap.h ─
// Every one of these is a commented offset in that header. They are named here
// so the reads below say what they mean instead of scattering magic numbers.

// struct Tileset { isCompressed; isSecondary; tiles; palettes; metatiles;
//                  metatileAttributes; callback; }
constexpr uint32_t kTilesetMetatiles  = 0x0C;
constexpr uint32_t kTilesetAttributes = 0x10;
constexpr uint32_t kTilesetSize       = 0x18;

// struct ObjectEvent, 0x24 bytes.
constexpr uint32_t kObjSize          = 0x24;
constexpr uint32_t kObjFlags0        = 0x00;   // bit 0 = active
constexpr uint32_t kObjFlags1        = 0x01;   // bit 5 = invisible
constexpr uint32_t kObjFlags2        = 0x02;   // bit 0 = isPlayer
constexpr uint32_t kObjSpriteId      = 0x04;
constexpr uint32_t kObjGraphicsId    = 0x05;
constexpr uint32_t kObjElevation     = 0x0B;   // low nibble = current
constexpr uint32_t kObjCurrentCoords = 0x10;   // struct Coords16 { s16 x, y; }
constexpr uint32_t kObjDirection     = 0x18;   // low nibble = facing

// struct PlayerAvatar — objectEventId is the index into gObjectEvents.
constexpr uint32_t kAvatarObjectEventId = 0x05;

// struct SaveBlock1 — only the leading `struct Coords16 pos` matters here.
// It is the backup-map cell drawn at the top-left of the screen, and it is what
// MapPosToBgTilemapOffset subtracts (pokeruby src/field_camera.c:314).
constexpr uint32_t kSaveBlockPos = 0x00;

// struct FieldCameraOffset (pokeruby src/field_camera.c:12) — five u8s.
// Note the order: PIXEL offsets come first, then TILE offsets. Getting that
// backwards silently yields a plausible-but-wrong sub-tile scroll.
constexpr uint32_t kFieldCamXPixel = 0x00;
constexpr uint32_t kFieldCamYPixel = 0x01;
constexpr uint32_t kFieldCamXTile  = 0x02;
constexpr uint32_t kFieldCamYTile  = 0x03;
constexpr uint32_t kFieldCamSize   = 0x08;

// Camera pan, added to the scroll registers alongside the offsets above.
constexpr uint32_t kSHorizontalCameraPan = 0x03000598;   // iwram, 0x02
constexpr uint32_t kSVerticalCameraPan   = 0x0300059A;   // iwram, 0x02

// Copy one tileset's metatile table out of ROM.
//
// A tileset does not record how many metatiles it has; the engine simply
// assumes 512 per tileset (fieldmap.h:7-8, and CopyTilesetToVram's comment
// that the primary and secondary counts are assumed equal). So we read a fixed
// 512 entries and may run past the true end of a smaller table into whatever
// ROM sits after it. That is harmless — those ids are ones the map never
// references — and host_ptr's bounds check keeps it inside the cartridge.
// A tileset pointer of 0 is legitimate: plenty of maps have no secondary.
void read_tileset_table(gba::GbaBus* bus, uint32_t tileset_ptr,
                        uint32_t field_offset, uint16_t* dst, size_t count) {
    std::memset(dst, 0, count * sizeof(uint16_t));
    if (!tileset_ptr) return;

    const uint8_t* ts = host_ptr(bus, tileset_ptr, kTilesetSize);
    if (!ts) return;

    const uint32_t table = rd32(ts + field_offset);
    if (!table) return;

    const uint8_t* src = host_ptr(bus, table, count * sizeof(uint16_t));
    if (!src) return;

    std::memcpy(dst, src, count * sizeof(uint16_t));
}

const char* dir_name(uint8_t d) {
    switch (d) {
        case 1:  return "S";
        case 2:  return "N";
        case 3:  return "W";
        case 4:  return "E";
        default: return "-";
    }
}

}  // namespace

void reset_capture() {
    g_verified_rom=nullptr; g_verified_size=0; g_supported_rom=false; g_last_status=-1;
    ++g_presentation_epoch;
}

bool source_map(int group,int number,Snapshot& out) {
    auto* bus=gbarecomp::active_bus();
    if(!bus || !g_supported_rom || bus->rom_ptr()!=g_verified_rom || bus->rom_size()!=g_verified_size) {
        out={};return false;
    }
    return live::source_snapshot({{g_verified_rom,g_verified_size},{},{},true},group,number,out);
}

bool capture(Snapshot& out, uint32_t previous_layout_ptr) {
    if(g_seen_state_epoch!=g_runtime_state_epoch) {
        g_seen_state_epoch=g_runtime_state_epoch;++g_presentation_epoch;
    }
    out.valid = false;
    out.map_group=out.map_number=-1;
    out.identity_source=Snapshot::IdentitySource::Unknown;
    out.connections.clear();
    out.layout_ptr=0; out.width=out.height=0; out.grid.clear();
    out.player_index=-1;
    for (auto& object:out.objects) object={};
    for (auto& source:out.actor_sources) source={};
    out.obj_tiles.clear();out.obj_palette.clear();out.obj_mapping_1d=false;
    out.actor_offset_x=out.actor_offset_y=0;
    out.actor_range_safe=false;out.actor_templates.clear();

    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus || !bus->rom_ptr()) return false;
    if (g_verified_rom!=bus->rom_ptr() || g_verified_size!=bus->rom_size()) {
        g_verified_rom=bus->rom_ptr(); g_verified_size=bus->rom_size();
        g_supported_rom=g_verified_size==0x1000000 &&
            gba::sha1(g_verified_rom,g_verified_size).hex()==live::kRubySha1;
    }
    const live::Memory memory{{bus->rom_ptr(),bus->rom_size()},
        {bus->ewram_ptr(),0x40000},{bus->iwram_ptr(),0x8000},g_supported_rom};
    const auto scene=live::inspect(memory);
    if (int(scene.status)!=g_last_status) {
        g_last_status=int(scene.status);
        std::fprintf(stderr,"[world] scene %s callback2=%08X map=%d.%d connections=%zu\n",
            live::status_name(scene.status),scene.callback2,scene.group,scene.number,scene.connections.size());
    }
    if (scene.status!=live::Status::Field) return false;
    const auto layout_ptr=scene.layout;
    const auto w=scene.width, h=scene.height;
    if (!live::copy_presentation_grid(memory,scene,out.grid)) return false;
    out.map_group=scene.group; out.map_number=scene.number;
    out.identity_source=Snapshot::IdentitySource::LiveCapture;
    out.connections=scene.connections;
    out.layout_ptr = layout_ptr;
    out.width      = w;
    out.height     = h;

    // ── Sub-tile camera ──────────────────────────────────────────────────────
    if (const uint8_t* p = host_ptr(bus, kGCameraPixelOffsetX, 4))
        out.cam_px = rds32(p);
    if (const uint8_t* p = host_ptr(bus, kGCameraPixelOffsetY, 4))
        out.cam_py = rds32(p);

    // Where the game's own view sits — see the Snapshot::view_* comment.
    out.view_x = out.view_y = 0;
    out.view_base_x = out.view_base_y = 0;
    if (const uint8_t* p = host_ptr(bus, kGSaveBlock1 + kSaveBlockPos, 4)) {
        out.view_x = rds16(p + 0);
        out.view_y = rds16(p + 2);
    }
    if (const uint8_t* p = host_ptr(bus, kSFieldCameraOffset, kFieldCamSize)) {
        // Camera pan is nonzero only during shakes and scripted moves, but it
        // is two bytes each and folding it in here means those scenes track
        // instead of visibly detaching from the game's own screen.
        int pan_x = 0, pan_y = 0;
        if (const uint8_t* h = host_ptr(bus, kSHorizontalCameraPan, 2))
            pan_x = static_cast<int16_t>(rd16(h));
        if (const uint8_t* v = host_ptr(bus, kSVerticalCameraPan, 2))
            pan_y = static_cast<int16_t>(rd16(v));

        // Everything below is mod 256 — the tilemap is a 256x256 px ring.
        const int bx = p[kFieldCamXTile] * 8 - p[kFieldCamXPixel] - pan_x;
        const int by = p[kFieldCamYTile] * 8 - p[kFieldCamYPixel] - pan_y - 8;
        out.view_base_x = static_cast<int16_t>(((bx % 256) + 256) % 256);
        out.view_base_y = static_cast<int16_t>(((by % 256) + 256) % 256);
    }

    // ── Object events: the player and up to 15 NPCs ──────────────────────────
    out.player_index = -1;
    for (int i = 0; i < kObjectEventCount; ++i) {
        ObjectSnapshot& o = out.objects[i];
        o = ObjectSnapshot{};

        const uint8_t* e = host_ptr(bus, kGObjectEvents + i * kObjSize, kObjSize);
        if (!e) continue;

        // The first four bytes are a run of :1 bitfields. GCC packs those into
        // the u32 LSB-first on little-endian, so bit N of byte B is the (8B+N)th
        // declared flag — which is exactly what the /*0x00*/ /*0x01*/ comments
        // in global.fieldmap.h are telling you.
        o.active    = (e[kObjFlags0] & 0x01) != 0;
        o.invisible = (e[kObjFlags1] & 0x20) != 0;
        o.is_player = (e[kObjFlags2] & 0x01) != 0;
        if (!o.active) continue;

        o.sprite_id   = e[kObjSpriteId];
        o.graphics_id = e[kObjGraphicsId];
        o.elevation   = static_cast<uint8_t>(e[kObjElevation] & 0x0F);
        o.facing      = static_cast<uint8_t>(e[kObjDirection] & 0x0F);
        o.x           = rds16(e + kObjCurrentCoords + 0);
        o.y           = rds16(e + kObjCurrentCoords + 2);
        o.local_id=e[8];o.map_number=e[9];o.map_group=e[10];
        o.initial_x=rds16(e+12);o.initial_y=rds16(e+14);
        // Pinned Ruby gSprites (0x02020004), 64 entries of 0x44 bytes.
        // Sprite.data[0] must still identify this object event. Do not bind a
        // recycled sprite slot or a UI sprite to an old actor.
        if(o.sprite_id<64 && !o.invisible) {
            if(const auto* sprite=host_ptr(bus,0x02020004+o.sprite_id*0x44,0x44);
               sprite && rds16(sprite+0x2e)==i) {
                auto& source=out.actor_sources[i];source.present=true;
                std::memcpy(source.sprite.data(),sprite,source.sprite.size());
                if(!actor::bind_event(source,{e,kObjSize},unsigned(i)))continue;
                if(sprite[0x42]>>6) {
                    const uint32_t tables=rd32(sprite+0x18);
                    // These field profiles are immutable ROM data, eight-byte
                    // table entries and six-byte records on the target ARM ABI.
                    if((tables>>24)==8)if(const auto* table=host_ptr(bus,tables+(sprite[0x42]&63)*8,8)) {
                        const auto count=table[0];const uint32_t parts=rd32(table+4);
                        if(count>0 && count<=64 && (parts>>24)==8)
                            if(const auto* raw=host_ptr(bus,parts,size_t(count)*6))
                                source.subsprites.assign(raw,raw+size_t(count)*6);
                    }
                }
            }
        }
    }

    // gPlayerAvatar names the player's slot outright, which beats scanning for
    // the isPlayer bit — but cross-check the bit anyway, because a stale avatar
    // during a map transition would otherwise silently point at an NPC.
    if (const uint8_t* av = host_ptr(bus, kGPlayerAvatar, 0x24)) {
        const uint8_t idx = av[kAvatarObjectEventId];
        if (idx < kObjectEventCount && out.objects[idx].active &&
            out.objects[idx].is_player) {
            out.player_index = idx;
        }
    }

    out.obj_tiles.resize(0x8000);
    std::memcpy(out.obj_tiles.data(),bus->vram_ptr()+0x10000,0x8000);
    out.obj_palette.resize(256);
    std::memcpy(out.obj_palette.data(),bus->pal_ptr()+0x200,512);
    // DISPCNT bit 6 selects OBJ 1D mapping. Read through the bus at capture.
    out.obj_mapping_1d=(bus->read16(0x04000000)&0x40)!=0;
    // Pinned sprite.c queue: consume at this capture boundary, never retain
    // guest pointers in the snapshot. A flag alone cannot authorize a frame.
    const auto* iwram=bus->iwram_ptr();
    const unsigned copies=iwram[0x24dc];
    const std::span<const uint8_t> pending_copies=iwram[0x28f0] && copies>0 && copies<=64
        ? std::span<const uint8_t>(iwram+0x24e0,copies*12) : std::span<const uint8_t>{};
    for(int i=0;i<kObjectEventCount;++i)if(out.actor_sources[i].present) {
        auto& source=out.actor_sources[i];
        actor::capture_object_directions(source,
            {bus->rom_ptr(),bus->rom_size()},out.obj_tiles,out.obj_mapping_1d,out.objects[i].graphics_id,pending_copies);
        if(std::getenv("RUBYVR_ACTOR_TRACE")) {
            std::fprintf(stderr,"ACTOR_VIEW_CAPTURE slot=%d player=%d graphics=%u culled=%d anim=%u phase=%u source_facing=%u matched=%u displayed_anim=%u displayed_phase=%u pending_flip=%d\n",
                i,i==out.player_index,unsigned(out.objects[i].graphics_id),source.viewport_culled,
                unsigned(source.sprite[0x2a]),unsigned(source.sprite[0x2b]),
                unsigned(out.objects[i].facing),unsigned(source.world_facing),
                unsigned(source.displayed_anim),unsigned(source.displayed_phase),source.pending_flip_transition);
        }
    }
    out.actor_offset_x=rds16(bus->iwram_ptr()+0x24d0);
    out.actor_offset_y=rds16(bus->iwram_ptr()+0x27e0);
    live::capture_actor_rules(memory,out,g_presentation_epoch);

    // ── Metatile tables: ROM data, so only on a map change ───────────────────
    if (layout_ptr != previous_layout_ptr ||
        out.metatiles.size() != static_cast<size_t>(kMetatilesTotal * kTilesPerMetatile)) {
        out.metatiles.resize(kMetatilesTotal * kTilesPerMetatile);
        out.attributes.resize(kMetatilesTotal);

        const uint32_t primary   = scene.primary_tileset;
        const uint32_t secondary = scene.secondary_tileset;

        constexpr size_t kHalfEntries = static_cast<size_t>(kMetatilesInPrimary) * kTilesPerMetatile;
        read_tileset_table(bus, primary, kTilesetMetatiles,
                           out.metatiles.data(), kHalfEntries);
        read_tileset_table(bus, secondary, kTilesetMetatiles,
                           out.metatiles.data() + kHalfEntries, kHalfEntries);

        read_tileset_table(bus, primary, kTilesetAttributes,
                           out.attributes.data(), kMetatilesInPrimary);
        read_tileset_table(bus, secondary, kTilesetAttributes,
                           out.attributes.data() + kMetatilesInPrimary,
                           kMetatilesInPrimary);
    }

    // ── Tile pixels and palettes: every frame, so animation is free ──────────
    // Raw and unconverted on purpose; see the Snapshot::vram_tiles comment.
    out.vram_tiles.resize(kTileSheetSize);
    std::memcpy(out.vram_tiles.data(), bus->vram_ptr(), kTileSheetSize);

    out.bg_palette.resize(kPaletteEntries);
    std::memcpy(out.bg_palette.data(), bus->pal_ptr(),
                kPaletteEntries * sizeof(uint16_t));

    out.valid = true;
    return true;
}

void debug_dump(const Snapshot& s) {
    // Rate-limited and change-triggered. At 60 fps an unconditional print would
    // be 60 lines a second of identical text, which hides the one line that
    // matters — the moment the map changes.
    static uint32_t last_layout = 0;
    static float    last_cx = -1.0f, last_cy = -1.0f;
    static int      countdown = 0;

    if (!s.valid) return;

    const ObjectSnapshot* p =
        (s.player_index >= 0) ? &s.objects[s.player_index] : nullptr;

    // Per-frame printing is OPT-IN. Triggering on the fractional camera prints
    // ~60 lines a second while walking, which is what you want when checking
    // that sub-cell motion is smooth — and is otherwise a real cost, because
    // this runs on the EMULATION thread and an unbuffered write to a Windows
    // console is not cheap. Default back to whole-tile changes.
    static const bool verbose = [] {
        const char* e = std::getenv("RUBYVR_WORLD_VERBOSE");
        return e && e[0] == '1';
    }();

    const float cx = s.camera_cell_x(), cy = s.camera_cell_y();
    const ObjectSnapshot* pp =
        (s.player_index >= 0) ? &s.objects[s.player_index] : nullptr;
    const float tick_x = verbose ? cx : (pp ? static_cast<float>(pp->x) : cx);
    const float tick_y = verbose ? cy : (pp ? static_cast<float>(pp->y) : cy);

    const bool map_changed = (s.layout_ptr != last_layout);
    const bool moved       = (tick_x != last_cx || tick_y != last_cy);
    if (!map_changed && !moved && --countdown > 0) return;
    countdown = 120;   // a heartbeat every ~2 s even when standing still

    last_layout = s.layout_ptr;
    last_cx = tick_x; last_cy = tick_y;

    if (p) {
        std::fprintf(stderr,
                     "[world] %dx%d layout %08X  player (%d,%d) elev %u %s  "
                     "cam %.2f,%.2f  metatile %u coll %u\n",
                     s.width, s.height, s.layout_ptr, p->x, p->y, p->elevation,
                     dir_name(p->facing), cx, cy,
                     s.metatile_id(p->x, p->y), s.collision(p->x, p->y));
    } else {
        std::fprintf(stderr, "[world] %dx%d layout %08X  (no player object)\n",
                     s.width, s.height, s.layout_ptr);
    }
}

}  // namespace world
}  // namespace vr

namespace vr::presentation {
Input capture_input() {
    auto* bus=gbarecomp::active_bus();
    if(world::g_seen_state_epoch!=g_runtime_state_epoch) {
        world::g_seen_state_epoch=g_runtime_state_epoch;++world::g_presentation_epoch;
    }
    if(!bus)return Input{};
    const world::live::Memory memory{{bus->rom_ptr(),bus->rom_size()},
        {bus->ewram_ptr(),0x40000},{bus->iwram_ptr(),0x8000},
        world::g_supported_rom && world::g_verified_rom==bus->rom_ptr()};
    const auto input=inspect(memory,world::g_presentation_epoch);
    static uint32_t last_callback=~uint32_t(0);
    static uint64_t last_epoch=~uint64_t(0);
    static bool last_fade=false;
    if(input.callback!=last_callback || input.epoch!=last_epoch || input.fading!=last_fade) {
        std::fprintf(stderr,"[presentation] %s callback=%08X epoch=%llu map=%d.%d fading=%d\n",
            name(input.mode),input.callback,static_cast<unsigned long long>(input.epoch),
            input.identity.group,input.identity.number,input.fading);
        last_callback=input.callback;last_epoch=input.epoch;last_fade=input.fading;
    }
    return input;
}
bool capture_field_ui(std::span<const uint8_t> rgb,std::vector<uint8_t>& rgba) {
    auto* bus=gbarecomp::active_bus();
    if(!bus){rgba.clear();return false;}
    return field_ui({bus->vram_ptr(),0x18000},{bus->io().raw(),0x400},rgb,rgba);
}
}
