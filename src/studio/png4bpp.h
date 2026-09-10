// png4bpp.h — pokeruby's tiles.png as PALETTE INDICES, and why nothing else
// will do.
//
// WHY NOT stb_image, OR ANY ORDINARY DECODER:
//
//   A tileset's tiles.png is a 4-bit INDEXED image (bit depth 4, colour type
//   3). The palette indices ARE the tile pixel data — the GBA stores exactly
//   these nibbles in VRAM and its PPU looks each one up in palette RAM at draw
//   time. What we need out of the file is the index, not the colour.
//
//   Every general-purpose decoder resolves the palette for you and hands back
//   RGB. Going back the other way — matching each pixel's colour against the
//   PLTE chunk to recover its index — is AMBIGUOUS, because GBA palettes
//   duplicate colours freely (several entries are pure black, and a tileset's
//   unused slots are routinely all the same). A pixel that decodes to black
//   could be index 0, 12 or 15, and index 0 is the one the shader treats as
//   TRANSPARENT. Guessing wrong there does not corrupt a colour; it punches
//   holes in geometry, because build_silhouette reads index 0 as "always
//   background".
//
//   So we read the indices directly. The narrow case is small: bit depth 4,
//   colour type 3, no interlace. Everything else is REFUSED rather than
//   handled — a tileset PNG that is not this shape is a surprise worth
//   stopping for, not a variation to accommodate.
//
// THE NIBBLE ORDER IS OPPOSITE TO THE GBA'S, and this is the trap:
//
//   PNG packs sub-byte samples MOST-significant first, so in a 4bpp PNG the
//   LEFT pixel of a pair is the HIGH nibble. The GBA is the other way round —
//   ruby_world.h says it outright: "4bpp means two pixels per byte, LOW NIBBLE
//   FIRST". Copying a PNG row into VRAM bytes unswapped transposes every pair
//   of pixels horizontally, which does not look like corruption. It looks like
//   slightly wrong art, and it would sail past a diff of the geometry counts
//   while quietly changing every silhouette.
//
//   decode() returns ONE INDEX PER BYTE, so the ordering question is settled
//   once, here, and pack_gba_tiles() below is the only place that re-packs.

#pragma once

#include <cstdint>
#include <vector>

namespace studio {
namespace png {

// One palette index per byte, row-major, `width * height` of them.
struct Indexed {
    int                  width = 0;
    int                  height = 0;
    std::vector<uint8_t> index;   // 0..15
};

// Decode `path`. Returns false, having explained itself on stderr, for any file
// that is not a non-interlaced 4bpp indexed PNG, and for any zlib or structural
// failure.
bool decode(const char* path, Indexed* out);

// Re-pack an 8x8-tiled indexed image into the GBA's own 4bpp tile format: 32
// bytes per tile, 8 rows of 4 bytes, LOW nibble first, tiles in reading order
// across the sheet.
//
// The image's width must be a multiple of 8 and so must its height; the tile
// count is (width/8) * (height/8). pokeruby's sheets are 128 px wide, so 16
// tiles per row — gTileset_General is 128x256 (512 tiles), gTileset_Petalburg
// 128x80 (160). The CALLER decides where those land in the 1024-tile VRAM
// sheet, because that depends on whether the tileset is primary or secondary.
bool pack_gba_tiles(const Indexed& img, std::vector<uint8_t>* out,
                    int* out_tile_count);

}  // namespace png
}  // namespace studio
