#include "cutout.h"
#include "tileset.h"

namespace vr::cutout {
bool compose(const world::Snapshot& s, const overrides::Pattern& p, Art* out) {
    if (!out || p.w < 1 || p.w > 64 || p.extent < 1 || p.extent > 64 ||
        p.mask.size() != size_t(p.cells()) || p.ids.size() != p.mask.size()) return false;
    for (const auto& [id, expected] : p.tiles) {
        const size_t base = size_t(id) * world::kTilesPerMetatile;
        if (id >= s.attributes.size() || base + world::kTilesPerMetatile > s.metatiles.size() ||
            s.attributes[id] != expected.attr) return false;
        for (int k=0; k<world::kTilesPerMetatile; ++k)
            if (s.metatiles[base+k] != expected.entries[k]) return false;
    }
    Art art; art.w = p.w * 16; art.h = p.extent * 16;
    art.pixels.resize(size_t(art.w) * art.h);
    uint32_t palette[world::kPaletteEntries]; tileset::expand_palette(s, palette);
    for (int pass = 0; pass < 2; ++pass)
        for (int y = 0; y < art.h; ++y) for (int x = 0; x < art.w; ++x) {
            const size_t cell = size_t(y / 16) * p.w + x / 16;
            if (!p.mask[cell]) continue;
            const size_t entry = size_t(p.ids[cell]) * world::kTilesPerMetatile + pass * 4 +
                                 (y % 16 / 8) * 2 + x % 16 / 8;
            if (entry >= s.metatiles.size()) return false;
            const auto tile = world::unpack_tile_entry(s.metatiles[entry]);
            const int tx = tile.hflip ? 7 - x % 8 : x % 8;
            const int ty = tile.vflip ? 7 - y % 8 : y % 8;
            const size_t offset = size_t(tile.index) * world::kTileBytes + ty * 4 + tx / 2;
            if (offset >= s.vram_tiles.size()) return false;
            const int index = (s.vram_tiles[offset] >> ((tx & 1) * 4)) & 15;
            if (index) art.pixels[size_t(y) * art.w + x] =
                {tile, uint8_t(tx), uint8_t(ty), palette[tile.palette * 16 + index]};
        }
    *out = std::move(art); return true;
}
overrides::Cutout original_opacity(const Art& art) {
    overrides::Cutout out; out.w = art.w; out.h = art.h;
    for (const auto& pixel : art.pixels) out.opacity.push_back(pixel.rgba >> 24 ? 1 : 0);
    return out;
}
bool valid(const overrides::Pattern& p) {
    if (!p.cutout) return true;
    const auto& c = *p.cutout;
    if (p.w < 1 || p.w > 64 || p.extent < 1 || p.extent > 64 ||
        c.w != p.w * 16 || c.h != p.extent * 16 ||
        c.opacity.size() != size_t(c.w) * c.h || p.mask.size() != size_t(p.cells())) return false;
    for (int y = 0; y < c.h; ++y) for (int x = 0; x < c.w; ++x) {
        const auto value = c.opacity[size_t(y) * c.w + x];
        if (value > 1 || (value && !p.mask[size_t(y / 16) * p.w + x / 16])) return false;
    }
    return true;
}
bool decode(int w, int h, int first, const std::vector<uint32_t>& runs, overrides::Cutout* out) {
    if (!out || w < 1 || h < 1 || w > 1024 || h > 1024 || first < 0 || first > 1 || runs.empty()) return false;
    const size_t pixels = size_t(w) * h;
    size_t total = 0;
    for (uint32_t run : runs) {
        if (!run || run > pixels - total) return false;
        total += run;
    }
    if (total != pixels) return false;
    overrides::Cutout result; result.w = w; result.h = h; result.opacity.reserve(pixels);
    uint8_t bit = uint8_t(first);
    for (uint32_t run : runs) { result.opacity.insert(result.opacity.end(), run, bit); bit ^= 1; }
    *out = std::move(result); return true;
}
std::vector<uint32_t> encode(const overrides::Cutout& c) {
    std::vector<uint32_t> runs;
    if (c.opacity.empty()) return runs;
    uint8_t previous = c.opacity.front(); uint32_t length = 0;
    for (uint8_t bit : c.opacity) {
        if (bit != previous) { runs.push_back(length); length = 0; previous = bit; }
        ++length;
    }
    runs.push_back(length); return runs;
}
} // namespace vr::cutout
