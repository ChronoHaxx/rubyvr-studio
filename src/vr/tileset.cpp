// tileset.cpp — see tileset.h for the two pixel formats involved.

#include "tileset.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vr {
namespace tileset {

void expand_palette(const world::Snapshot& s, uint32_t* out) {
    const size_t n = s.bg_palette.size();
    for (int i = 0; i < world::kPaletteEntries; ++i) {
        out[i] = (static_cast<size_t>(i) < n) ? bgr555_to_rgba(s.bg_palette[i])
                                             : 0xFF000000u;
    }
}

void blit_tile(uint32_t* dst, int dst_w, int dst_h, int dx, int dy,
               const world::Snapshot& s, const uint32_t* pal256,
               world::TileEntry e) {
    // A tile index past the end of the sheet is not an error worth shouting
    // about: metatile tables are read as a fixed 512 entries per tileset
    // whether or not the tileset really has that many, so ids the map never
    // references can decode to nonsense. Skip them.
    const size_t base = static_cast<size_t>(e.index) * world::kTileBytes;
    if (base + world::kTileBytes > s.vram_tiles.size()) return;
    const uint8_t* tile = s.vram_tiles.data() + base;

    const uint32_t* pal = pal256 + static_cast<size_t>(e.palette) * 16;

    for (int y = 0; y < kTilePx; ++y) {
        const int ty = dy + y;
        if (ty < 0 || ty >= dst_h) continue;

        // Flips are applied when READING the source, not when writing the
        // destination, so the clip above stays a simple rectangle test.
        const int sy  = e.vflip ? (kTilePx - 1 - y) : y;
        const uint8_t* row = tile + sy * 4;   // 4 bytes == 8 pixels at 4bpp

        for (int x = 0; x < kTilePx; ++x) {
            const int tx = dx + x;
            if (tx < 0 || tx >= dst_w) continue;

            const int sx = e.hflip ? (kTilePx - 1 - x) : x;
            const uint8_t byte = row[sx >> 1];
            // Low nibble is the LEFT pixel of the pair.
            const uint8_t idx = (sx & 1) ? static_cast<uint8_t>(byte >> 4)
                                         : static_cast<uint8_t>(byte & 0x0F);
            if (idx == 0) continue;   // transparent, not black

            dst[static_cast<size_t>(ty) * dst_w + tx] = pal[idx];
        }
    }
}

void blit_metatile_pair(uint32_t* dst, int dst_w, int dst_h, int dx, int dy,
                        const world::Snapshot& s, const uint32_t* pal256,
                        uint16_t metatile_id, Pair pair) {
    const size_t base = static_cast<size_t>(metatile_id) * world::kTilesPerMetatile
                      + static_cast<size_t>(pair) * 4;
    if (base + 4 > s.metatiles.size()) return;

    // TL, TR, BL, BR — pokeruby DrawMetatile writes +0, +1, +0x20, +0x21.
    for (int i = 0; i < 4; ++i) {
        const world::TileEntry e = world::unpack_tile_entry(s.metatiles[base + i]);
        blit_tile(dst, dst_w, dst_h,
                  dx + (i & 1) * kTilePx,
                  dy + (i >> 1) * kTilePx,
                  s, pal256, e);
    }
}

void build_atlas(const world::Snapshot& s, AtlasMode mode,
                 std::vector<uint32_t>& out_rgba) {
    out_rgba.assign(static_cast<size_t>(kAtlasPx) * kAtlasPx, 0u);   // transparent

    uint32_t pal[world::kPaletteEntries];
    expand_palette(s, pal);

    for (int id = 0; id < world::kMetatilesTotal; ++id) {
        const int dx = (id % kAtlasCols) * kMetatilePx;
        const int dy = (id / kAtlasCols) * kMetatilePx;
        const uint16_t mid = static_cast<uint16_t>(id);

        if (mode == kAtlasLower || mode == kAtlasBoth)
            blit_metatile_pair(out_rgba.data(), kAtlasPx, kAtlasPx, dx, dy,
                               s, pal, mid, kPairLower);
        if (mode == kAtlasUpper || mode == kAtlasBoth)
            blit_metatile_pair(out_rgba.data(), kAtlasPx, kAtlasPx, dx, dy,
                               s, pal, mid, kPairUpper);
    }
}

bool dump_ppm(const char* path, const uint32_t* rgba, int w, int h) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    std::fprintf(f, "P6\n%d %d\n255\n", w, h);

    std::vector<uint8_t> row(static_cast<size_t>(w) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t p = rgba[static_cast<size_t>(y) * w + x];
            const uint32_t a = (p >> 24) & 0xFF;

            // Composite over an 8px grey checkerboard. PPM has no alpha, and
            // writing transparent pixels as black would make "no tile here"
            // and "a black tile" identical — which is exactly the distinction
            // this dump exists to check.
            const bool light = (((x >> 3) ^ (y >> 3)) & 1) != 0;
            const uint32_t bg = light ? 0xB0u : 0x60u;

            const uint32_t r = (p >> 0)  & 0xFF;
            const uint32_t g = (p >> 8)  & 0xFF;
            const uint32_t b = (p >> 16) & 0xFF;

            row[x * 3 + 0] = static_cast<uint8_t>((r * a + bg * (255 - a)) / 255);
            row[x * 3 + 1] = static_cast<uint8_t>((g * a + bg * (255 - a)) / 255);
            row[x * 3 + 2] = static_cast<uint8_t>((b * a + bg * (255 - a)) / 255);
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }

    std::fclose(f);
    return true;
}

bool dump_rgb888_ppm(const char* path, const uint8_t* rgb, int w, int h) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    // RGB888 top-down is already exactly PPM's own layout, so this is one write.
    std::fwrite(rgb, 1, static_cast<size_t>(w) * h * 3, f);
    std::fclose(f);
    return true;
}

void dump_atlases_once(const world::Snapshot& s) {
    static bool done = false;
    if (done || !s.valid) return;

    const char* want = std::getenv("RUBYVR_DUMP_ATLAS");
    if (!want || want[0] != '1') { done = true; return; }
    done = true;

    struct { AtlasMode mode; const char* name; } kJobs[] = {
        {kAtlasLower, "atlas_lower.ppm"},
        {kAtlasUpper, "atlas_upper.ppm"},
        {kAtlasBoth,  "atlas_both.ppm"},
    };

    std::vector<uint32_t> img;
    for (const auto& j : kJobs) {
        build_atlas(s, j.mode, img);
        const bool ok = dump_ppm(j.name, img.data(), kAtlasPx, kAtlasPx);
        std::fprintf(stderr, "[tileset] %s %s (%dx%d, layout %08X)\n",
                     ok ? "wrote" : "FAILED to write", j.name,
                     kAtlasPx, kAtlasPx, s.layout_ptr);
    }
}

}  // namespace tileset
}  // namespace vr
