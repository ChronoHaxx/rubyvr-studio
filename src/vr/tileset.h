// tileset.h — turn a world Snapshot's raw guest bytes into RGBA pixels.
//
// The CONSUMER side of ruby_world.h. capture() deliberately hands over raw
// 4bpp VRAM bytes and raw BGR555 palette entries, because it runs on the
// emulation thread and must not spend the game's frame budget on pixel format
// conversion. Everything here is that conversion, and it runs on whichever
// thread is drawing.
//
// The pixel formats, since neither is a format anything modern uses:
//
//   TILES — 4 bits per pixel, 32 bytes per 8x8 tile, rows top to bottom, and
//   within a row the LOW nibble of each byte is the LEFT pixel. The value is an
//   index 0..15 into one of the 16 BG palettes, not a colour. Index 0 is
//   TRANSPARENT rather than black, which is what lets a metatile's upper pair
//   sit over its lower pair without blanking it.
//
//   PALETTES — BGR555: 5 bits each of blue, green, red packed into a u16, with
//   RED IN THE LOW BITS. Note the channel order is the reverse of the name most
//   people expect. Scaling 5 bits to 8 is (v << 3) | (v >> 2), which replicates
//   the high bits into the low ones so 31 maps to 255 rather than 248.
//
// Output is RGBA8 packed little-endian as 0xAABBGGRR — i.e. byte order R,G,B,A
// in memory, which is what GL_RGBA/GL_UNSIGNED_BYTE wants.

#pragma once

#include <cstdint>
#include <vector>

#include "ruby_world.h"

namespace vr {
namespace tileset {

// Which half of a metatile's 8 entries to draw.
//
// Entries are ordered TL, TR, BL, BR within each pair — verified against
// pokeruby's DrawMetatile, which writes them to tilemap offsets +0, +1, +0x20
// and +0x21, and 0x20 is one tilemap row.
enum Pair : int { kPairLower = 0, kPairUpper = 1 };

inline constexpr int kMetatilePx = 16;   // a metatile is 16x16 pixels
inline constexpr int kTilePx     = 8;

// One BGR555 entry to packed RGBA8, always opaque. Transparency is decided by
// the tile's colour INDEX, not by the palette entry, so it cannot live here.
inline uint32_t bgr555_to_rgba(uint16_t c) {
    const uint32_t r5 = (c >> 0) & 0x1F;
    const uint32_t g5 = (c >> 5) & 0x1F;
    const uint32_t b5 = (c >> 10) & 0x1F;
    const uint32_t r = (r5 << 3) | (r5 >> 2);
    const uint32_t g = (g5 << 3) | (g5 >> 2);
    const uint32_t b = (b5 << 3) | (b5 >> 2);
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

// Expand the snapshot's 256 BG palette entries to RGBA. `out` must hold 256.
void expand_palette(const world::Snapshot& s, uint32_t* out);

// Draw one 8x8 tile at (dx, dy) into an RGBA target, honouring flips and
// skipping colour index 0. Clipped against the target; out-of-range tile
// indices are ignored rather than read past the end of the sheet.
void blit_tile(uint32_t* dst, int dst_w, int dst_h, int dx, int dy,
               const world::Snapshot& s, const uint32_t* pal256,
               world::TileEntry e);

// Draw one 16x16 metatile pair at (dx, dy). Does nothing for an id the
// snapshot's tables do not cover.
void blit_metatile_pair(uint32_t* dst, int dst_w, int dst_h, int dx, int dy,
                        const world::Snapshot& s, const uint32_t* pal256,
                        uint16_t metatile_id, Pair pair);

// ── Debug dumps, for the Phase 4.2 eyeball check ─────────────────────────────

inline constexpr int kAtlasCols = 32;   // 1024 metatiles as 32x32 of 16x16 px
inline constexpr int kAtlasPx   = kAtlasCols * kMetatilePx;   // 512

// Render all 1024 metatiles into a 512x512 RGBA image. `pairs` selects which
// halves to composite: lower only, upper only, or both (lower then upper).
enum AtlasMode : int { kAtlasLower, kAtlasUpper, kAtlasBoth };
void build_atlas(const world::Snapshot& s, AtlasMode mode,
                 std::vector<uint32_t>& out_rgba);

// Write RGBA as a binary PPM (P6), compositing over a grey checkerboard so
// transparency is visibly transparent rather than silently black. PPM because
// it is about twelve lines of code and every image viewer opens it; there is no
// reason to link a PNG encoder to look at a texture once.
bool dump_ppm(const char* path, const uint32_t* rgba, int w, int h);

// Same, for a raw RGB888 buffer — i.e. exactly what HostWindow::present hands
// the sink. Lets the GBA's own frame be written beside our map view at the same
// instant, which is the whole point of rendering the map at the game's size.
bool dump_rgb888_ppm(const char* path, const uint8_t* rgb, int w, int h);

// Dump the three atlases (lower / upper / both) next to the executable.
// Called once when the field map first comes up, under RUBYVR_DUMP_ATLAS=1.
void dump_atlases_once(const world::Snapshot& s);

}  // namespace tileset
}  // namespace vr
