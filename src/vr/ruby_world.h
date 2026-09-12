// ruby_world.h — read Pokemon Ruby's field map out of guest memory.
//
// WHY THIS EXISTS, because it is the whole reason a diorama is possible:
//
//   A GBA game has no camera and no geometry. If all you have is the finished
//   240x160 framebuffer (which is all Phases 0-3 had) there is nothing to make
//   3D out of. But this is a STATIC RECOMPILATION, not a screenshot: the guest's
//   EWRAM, IWRAM, VRAM and ROM are all plain arrays in our own process. So we
//   can read the game's own data structures directly and get the map as a grid
//   of "metatile id + collision + elevation" — exactly the shape you need to
//   extrude geometry from.
//
//   We are not decoding what the PPU drew. We are reading what the GAME KNOWS.
//
// WHERE THE ADDRESSES COME FROM:
//
//   variants/ruby/symbols/imported_data_symbols.tsv — the pret/pokeruby decomp
//   symbol import. Struct layouts are in third_party/pokeruby/include/
//   global.fieldmap.h, which comments every field offset; do not guess them.
//
//   These addresses are RUBY REV1 SPECIFIC. Sapphire builds from the same
//   source tree but links at different addresses, so a Sapphire build needs its
//   own table. That day is not today.
//
// THREADING, and it is load-bearing:
//
//   capture() must be called from HostWindow::present()'s frame sink, i.e. on
//   the EMULATION thread with the guest between frames. Guest memory is
//   quiescent exactly there and nowhere else. The VR thread must never touch
//   guest memory; it reads a Snapshot that was filled here.
//
//   So capture() does the MINIMUM: memcpy and integer unpacking, no pixel
//   format conversion. Expanding 4bpp tiles to one byte per pixel is 64 KB of
//   nibble work and belongs on the consumer's thread, not on the game's
//   critical path. See the comment on Snapshot::vram_tiles.

#pragma once

#include <cstdint>
#include <vector>
#include "actor_frame.h"

namespace vr {
namespace world {

// ── Guest symbol addresses (Ruby rev1) ───────────────────────────────────────
// From variants/ruby/symbols/imported_data_symbols.tsv. The size column there
// is a useful sanity check: gObjectEvents is 0x240 == 16 * sizeof(ObjectEvent).
inline constexpr uint32_t kGBackupMapLayout    = 0x03004870;  // iwram, 0x0C
inline constexpr uint32_t kGMapHeader          = 0x0202E828;  // ewram, 0x1C
inline constexpr uint32_t kGObjectEvents       = 0x030048A0;  // iwram, 0x240
inline constexpr uint32_t kGPlayerAvatar       = 0x0202E858;  // ewram, 0x24
inline constexpr uint32_t kGCameraPixelOffsetY = 0x03004898;  // iwram, 0x04
inline constexpr uint32_t kGCameraPixelOffsetX = 0x0300489C;  // iwram, 0x04
inline constexpr uint32_t kGSaveBlock1         = 0x02025734;  // ewram, 0x3AC0
inline constexpr uint32_t kSFieldCameraOffset  = 0x03000590;  // iwram, 0x08

inline constexpr int kObjectEventCount = 16;   // OBJECT_EVENTS_COUNT

// ── Map grid bit layout (global.fieldmap.h:7-11) ─────────────────────────────
// One u16 per cell. An "undefined" cell is all metatile bits set and nothing
// else, which is how the border reads outside the real map.
inline constexpr uint16_t kMetatileIdMask = 0x03FF;
inline constexpr uint16_t kCollisionMask  = 0x0C00;
inline constexpr uint16_t kElevationMask  = 0xF000;
inline constexpr int      kCollisionShift = 10;
inline constexpr int      kElevationShift = 12;
inline constexpr uint16_t kGridUndefined  = kMetatileIdMask;

// The backup map buffer is the LIVE grid: the current map plus any connected
// neighbours stitched in. It is bigger than the map itself, because the game
// needs 7 metatiles of border in every direction to fill the player's view.
//   backup width  = layout width  + MAP_OFFSET_W (15)
//   backup height = layout height + MAP_OFFSET_H (14)
// and the real map's (0,0) lands at backup cell (7,7). Object and camera
// coordinates are already expressed in this offset space, so nothing needs
// shifting as long as we stay in backup coordinates throughout.
inline constexpr int kMapOffset = 7;

// ── Metatiles (fieldmap.h:5-13) ──────────────────────────────────────────────
// A metatile is a 16x16 pixel cell built from 8 tile entries: entries 0-3 are
// the LOWER 2x2 pair, entries 4-7 the UPPER pair. Each entry is a standard GBA
// BG screen entry.
//
// The metatile's ATTRIBUTE word carries an 8-bit behaviour (grass? water? a
// door?) and a 4-bit layer type in bits 12-15. Layer type decides which BG
// layer each pair is drawn on, and therefore whether the upper pair draws ABOVE
// the player. That is our billboard trigger for the pop-up-book diorama: an
// upper pair drawn above the player is tall scenery — a tree, a sign, the front
// of a house — and should stand up off the board.
inline constexpr int kTilesPerMetatile   = 8;
inline constexpr int kMetatilesTotal     = 1024;
inline constexpr int kMetatilesInPrimary = 512;

inline constexpr uint16_t kAttrBehaviorMask = 0x00FF;
inline constexpr uint16_t kAttrLayerMask    = 0xF000;
inline constexpr int      kAttrLayerShift   = 12;

enum LayerType : uint8_t {
    kLayerNormal  = 0,   // lower -> BG2 middle,  upper -> BG1 top
    kLayerCovered = 1,   // lower -> BG3 bottom,  upper -> BG2 middle
    kLayerSplit   = 2,   // lower -> BG3 bottom,  upper -> BG1 top
};

// Does this metatile's UPPER pair draw in front of the player?
//
// Verified against pokeruby's DrawMetatile (src/field_camera.c:250-311), which
// is the function that actually routes the 8 entries to BG layers. BG1 is the
// layer the decomp itself annotates as the one "which covers object event
// sprites", and BOTH kLayerNormal and kLayerSplit send the upper pair there;
// only kLayerCovered keeps it behind, on BG2.
//
// This is the pop-up-book billboard trigger, and it is easy to get wrong: an
// earlier draft of the plan said "billboard when layer type is NORMAL", which
// would have left every SPLIT metatile lying flat on the board — cliff tops and
// the seams where terrain meets scenery, exactly the places the eye checks.
inline bool draws_above_player(uint8_t layer_type) {
    return layer_type != kLayerCovered;
}

// A tile entry, unpacked.
struct TileEntry {
    uint16_t index;    // 0..1023 into the tile sheet
    uint8_t  palette;  // 0..15 into BG palette RAM
    bool     hflip;
    bool     vflip;
};

inline TileEntry unpack_tile_entry(uint16_t raw) {
    TileEntry e;
    e.index   = static_cast<uint16_t>(raw & 0x03FF);
    e.hflip   = (raw & 0x0400) != 0;
    e.vflip   = (raw & 0x0800) != 0;
    e.palette = static_cast<uint8_t>(raw >> 12);
    return e;
}

// ── Tile pixel data ──────────────────────────────────────────────────────────
// The game decompresses both tilesets into VRAM itself (fieldmap.c:806-816), so
// we never have to touch LZ77: tiles 0..1023 are a flat contiguous 32 KB block
// at the start of VRAM, 32 bytes each, 4 bits per pixel.
//
//   primary   tiles 0..511    VRAM 0x0000 + id * 32
//   secondary tiles 512..1023 VRAM 0x4000 + (id - 512) * 32   (== id * 32)
//
// 4bpp means two pixels per byte, LOW NIBBLE FIRST. Colour index 0 is
// TRANSPARENT, not black — that is what lets the upper pair sit over the lower
// one without blanking it.
inline constexpr int      kTileCount     = 1024;
inline constexpr int      kTileBytes     = 32;      // 8x8 at 4bpp
inline constexpr uint32_t kTileSheetSize = kTileCount * kTileBytes;   // 32 KB

// BG palette RAM: 16 palettes of 16 colours, BGR555.
inline constexpr int kPaletteCount   = 16;
inline constexpr int kPaletteEntries = kPaletteCount * 16;   // 256 u16 == 512 B

// ── Snapshot ─────────────────────────────────────────────────────────────────

struct ObjectSnapshot {
    bool    active      = false;
    bool    invisible   = false;
    bool    is_player   = false;
    uint8_t graphics_id = 0;
    uint8_t sprite_id   = 0;
    uint8_t elevation   = 0;
    uint8_t facing      = 0;   // DIR_SOUTH=1 NORTH=2 WEST=3 EAST=4
    int16_t x = 0, y = 0;      // backup-map coordinates
};

// A copied neighbour rectangle, in the same backup coordinates as each map's
// authored cells. This is provenance for the visible border, not a second
// editable copy of the neighbour or a world-height inferred from layer bits.
struct ConnectionSlice {
    int group=0, number=0, width=0, height=0;
    int x=0, y=0, source_x=0, source_y=0, w=0, h=0;
    bool compatible_art=false;
    bool operator==(const ConnectionSlice&) const = default;
};

struct Snapshot {
    bool valid = false;
    // Stable source-table identity. Legacy/live adapters without a verified
    // identity leave both fields unknown; layout_ptr is never a public map key.
    int map_group = -1, map_number = -1;
    enum class IdentitySource : uint8_t { Unknown, SourceTable, LiveCapture };
    IdentitySource identity_source = IdentitySource::Unknown;
    bool has_map_identity() const {
        return identity_source != IdentitySource::Unknown && map_group >= 0 &&
               map_group < 256 && map_number >= 0 && map_number < 256;
    }

    // A layout/material cache key, not a unique map identity: different maps
    // can share it. Scene consumers also compare the explicit group/number.
    uint32_t layout_ptr = 0;

    int32_t               width = 0, height = 0;   // backup-map dimensions
    std::vector<uint16_t> grid;                    // width * height cells
    std::vector<ConnectionSlice> connections;
    bool valid_connections() const {
        if(connections.size()>64 || (!connections.empty() && !has_map_identity())) return false;
        for(const auto& c:connections) {
            if(c.group<0 || c.group>255 || c.number<0 || c.number>255 ||
               c.width<16 || c.height<15 || c.width>1024 || c.height>1024 ||
               int64_t(c.width)*c.height>10240 || c.w<1 || c.h<1 || c.w>1024 || c.h>1024 ||
               c.x<0 || c.y<0 || c.x>width-c.w || c.y>height-c.h ||
               c.source_x<7 || c.source_y<7 || c.source_x>c.width-8-c.w || c.source_y>c.height-7-c.h ||
               !(c.x+c.w<=7 || c.y+c.h<=7 || c.x>=width-8 || c.y>=height-7)) return false;
        }
        return true;
    }

    // Free-running total camera movement in pixels. Not used for placing the
    // view (see view_* below); kept because it is the natural smooth-motion
    // signal for a first-person camera in Phase 7.
    int32_t cam_px = 0, cam_py = 0;

    // WHERE THE GAME'S OWN VIEW IS, derived rather than guessed.
    //
    // view_x/view_y is gSaveBlock1.pos: the top-left cell of the 16x16-metatile
    // window the game keeps loaded. view_base_x/y is where that cell's left/top
    // edge lands on screen, so cell (mx, my) sits at
    //
    //     screen_x = view_base_x + 16 * (mx - view_x)     (mod 256)
    //     screen_y = view_base_y + 16 * (my - view_y)     (mod 256)
    //
    // THE MOD 256 IS NOT DECORATION. The field BG is a 32x32-tile ring buffer
    // (256x256 px) holding 16x16 metatiles: MapPosToBgTilemapOffset writes cell
    // (pos + i) at tile (tileOffset + 2i) mod 32, and sub_8057A58 then scrolls
    // to BGxHOFS = xPixelOffset + hPan, BGxVOFS = yPixelOffset + vPan + 8. So
    //
    //     view_base = tileOffset * 8 - pixelOffset - pan   (-8 more vertically)
    //
    // An earlier version of this assumed the tile and pixel offsets stay in
    // lockstep, making that difference a small sub-metatile scroll. They do not:
    // the pixel offset is the FULL scroll position mod 256 and only agrees with
    // tileOffset*8 at rest. Horizontally the two happened to coincide and the
    // view looked right; vertically it came out two metatiles low. Keep the
    // full modular arithmetic and the class of bug cannot come back.
    //
    // Deriving all this beats centring on the player: the game moves the
    // player's tile coordinates at the START of a step and then slides the
    // sprite across, so a player-centred view lurches a whole cell every step.
    int16_t view_x = 0, view_y = 0;
    int16_t view_base_x = 0, view_base_y = 0;   // both in [0, 256)

    int            player_index = -1;      // index into objects, or -1
    ObjectSnapshot objects[kObjectEventCount];

    // Transient live actor art; source-built/disk snapshots leave it empty.
    // Single 32 KB OBJ allocation and 512 B palette, copied at the safe boundary.
    actor::Source actor_sources[kObjectEventCount];
    std::vector<uint8_t> obj_tiles;
    std::vector<uint16_t> obj_palette;
    bool obj_mapping_1d=false;
    int16_t actor_offset_x=0,actor_offset_y=0;

    // Metatile definitions, read from ROM via gMapHeader.mapLayout. Only
    // refreshed when layout_ptr changes — these are ROM tables and cannot move
    // underneath us within a map.
    std::vector<uint16_t> metatiles;    // kMetatilesTotal * kTilesPerMetatile
    std::vector<uint16_t> attributes;   // kMetatilesTotal

    // RAW 4bpp tile bytes straight out of VRAM, and RAW BGR555 entries straight
    // out of PAL RAM. Deliberately unconverted: this is copied on the emulation
    // thread, and expanding it here would put 64 KB of nibble shuffling on the
    // game's critical path for no reason. The consumer converts, on its own
    // thread, at its own cadence.
    //
    // Re-copied EVERY frame rather than on map change, because the game
    // animates tiles in place via gTilesetAnimDmas — water, flowers, the tops
    // of waterfalls. Copying 32 KB a frame is nothing and gets that animation
    // for free, with no dirty tracking at all.
    std::vector<uint8_t>  vram_tiles;   // kTileSheetSize
    std::vector<uint16_t> bg_palette;   // kPaletteEntries

    // Reduce into [0, 256) — the tilemap ring is 256 px around. C++ '%' rounds
    // toward zero, so a bare % would return a negative for a negative input and
    // put the camera a whole screen away.
    static float wrap256(int v) {
        return static_cast<float>(((v % 256) + 256) % 256);
    }

    // Cell accessors. Out-of-range reads answer "undefined", which is what the
    // border is, so callers never need their own bounds checks.
    uint16_t cell(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return kGridUndefined;
        return grid[static_cast<size_t>(y) * width + x];
    }
    uint16_t metatile_id(int x, int y) const { return cell(x, y) & kMetatileIdMask; }
    uint8_t  collision(int x, int y) const {
        return static_cast<uint8_t>((cell(x, y) & kCollisionMask) >> kCollisionShift);
    }
    uint8_t elevation(int x, int y) const {
        return static_cast<uint8_t>((cell(x, y) & kElevationMask) >> kElevationShift);
    }
    // Where the CENTRE of the screen is, in fractional backup-map cells.
    //
    // This is the first-person camera position, and the fraction is the whole
    // point. The game moves the player's tile coordinates at the START of a
    // step and then slides the view across over several frames, so integer tile
    // coordinates would teleport the camera a whole metre per step. The scroll
    // state carries the in-between.
    //
    // Derived by inverting the placement in the view_* comment: a cell sits at
    // screen_x = view_base_x + 16*(mx - view_x) mod 256, so the cell at screen
    // centre is view_x + ((centre - view_base_x) mod 256) / 16. The modulo is
    // load-bearing for the same ring-buffer reason as everywhere else.
    float camera_cell_x() const { return view_x + wrap256(120 - view_base_x) / 16.0f; }
    float camera_cell_y() const { return view_y + wrap256( 80 - view_base_y) / 16.0f; }

    // Layer type for the metatile at (x, y). Returns kLayerNormal for anything
    // out of range; that is the common case and the safest default.
    uint8_t layer_type(int x, int y) const {
        const uint16_t id = metatile_id(x, y);
        if (id >= attributes.size()) return kLayerNormal;
        return static_cast<uint8_t>((attributes[id] & kAttrLayerMask) >> kAttrLayerShift);
    }

    // The metatile's 8-bit BEHAVIOUR — what the cell IS, as the game itself
    // classifies it (tall grass, a door, deep water, a ledge you hop). Gen 1
    // has no such field, which is why the reference implementation this
    // project borrows from needs a hand-authored height table. 0 for anything
    // out of range.
    uint8_t behaviour(int x, int y) const {
        const uint16_t id = metatile_id(x, y);
        if (id >= attributes.size()) return 0;
        return static_cast<uint8_t>(attributes[id] & kAttrBehaviorMask);
    }
};

// Fill `out` from live guest memory.
//
// EMULATION THREAD ONLY — see the threading note at the top of this file.
//
// `previous_layout_ptr` lets capture() skip re-reading the ROM metatile tables
// when the map has not changed; pass the layout_ptr of the last good snapshot,
// or 0 to force a full read. On refusal the native adapter clears identity,
// connections, grid and actor validity; retained material storage is not a
// valid scene. Full-screen modes are refused; in-field menus/dialogue can
// retain the normal field callback, so this is not a complete UI mode router.
bool capture(Snapshot& out, uint32_t previous_layout_ptr = 0);

// Call before every guest/ROM lifetime, with capture stopped. The runtime
// adapter hashes an immutable ROM once; pointer reuse must not reuse that gate.
void reset_capture();
// Quiescent native adapter only; verified ROM scenery, no guest state writes.
bool source_map(int group,int number,Snapshot&);

// One-line human-readable summary, rate-limited, for the Phase 4.1 check.
void debug_dump(const Snapshot& s);

}  // namespace world
}  // namespace vr
