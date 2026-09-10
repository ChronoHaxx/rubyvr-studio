// map_view.cpp — see map_view.h for why this renders at the game's own size.

#include "map_view.h"
#include "tileset.h"

#include <cstring>

namespace vr {
namespace map_view {
namespace {

constexpr int kCell = tileset::kMetatilePx;   // 16 px per metatile

// Markers, so the coordinate mapping is checkable and not merely plausible.
// A map that decodes correctly but is offset by one cell looks fine on its own;
// it stops looking fine the moment the player marker is standing in a wall.
constexpr uint32_t kPlayerColour = 0xFF00FFFFu;   // yellow  (0xAABBGGRR)
constexpr uint32_t kNpcColour    = 0xFF0080FFu;   // orange
constexpr uint32_t kNoMapColour  = 0xFF201818u;   // dark blue-grey

// The game keeps a 16x16-metatile window loaded in a 32x32-tile ring buffer.
constexpr int kWindowCells = 16;
constexpr int kRingPx      = kWindowCells * kCell;   // 256

// Screen position of the cell `index` steps from the window's origin.
//
// The ring makes this only meaningful modulo 256, so a cell can be reached
// either by walking forwards from the origin or by wrapping around the far
// side. Normalise into [-kCell, extent) so the caller's single "is it on
// screen" test is correct in both directions; without the wrap, whichever edge
// the ring seam happens to fall on loses a column of tiles.
int place(int base, int index, int extent) {
    int p = (base + index * kCell) % kRingPx;
    if (p < 0) p += kRingPx;
    if (p >= extent) p -= kRingPx;   // the wrapped-around representative
    return p;
}

void draw_box(uint32_t* dst, int w, int h, int x0, int y0, int size,
              uint32_t colour, int thickness) {
    for (int y = y0; y < y0 + size; ++y) {
        if (y < 0 || y >= h) continue;
        for (int x = x0; x < x0 + size; ++x) {
            if (x < 0 || x >= w) continue;
            const bool edge = (x - x0) < thickness || (x0 + size - 1 - x) < thickness ||
                              (y - y0) < thickness || (y0 + size - 1 - y) < thickness;
            if (edge) dst[static_cast<size_t>(y) * w + x] = colour;
        }
    }
}

}  // namespace

void render(const world::Snapshot& s, uint32_t* dst, int w, int h) {
    if (!s.valid || w <= 0 || h <= 0) {
        for (int i = 0; i < w * h; ++i) dst[i] = kNoMapColour;
        return;
    }

    // Opaque background. Colour index 0 is transparent everywhere in this data,
    // so without a base the untouched pixels would be whatever was in the
    // buffer last frame.
    for (int i = 0; i < w * h; ++i) dst[i] = 0xFF000000u;

    uint32_t pal[world::kPaletteEntries];
    tileset::expand_palette(s, pal);

    // Reproduce the game's own view rather than centring on the player — see
    // Snapshot::view_* for the derivation. Iterate the 16x16-metatile window
    // the game keeps loaded and place each cell by the ring-buffer arithmetic;
    // screen_pos(cell) is only defined modulo 256, so every placement has to go
    // through place().
    for (int pass = 0; pass < 2; ++pass) {
        // Two passes, because a metatile's upper pair must be able to overlap
        // the cell BELOW it — that is how a tree's canopy covers the trunk in
        // front of it. Drawing lower-then-upper per cell would let the next
        // cell's lower pair paint back over the previous cell's upper pair.
        const tileset::Pair pair =
            (pass == 0) ? tileset::kPairLower : tileset::kPairUpper;

        for (int j = 0; j < kWindowCells; ++j) {
            const int py = place(s.view_base_y, j, h);
            if (py <= -kCell || py >= h) continue;

            for (int i = 0; i < kWindowCells; ++i) {
                const int px = place(s.view_base_x, i, w);
                if (px <= -kCell || px >= w) continue;

                const uint16_t raw = s.cell(s.view_x + i, s.view_y + j);
                if (raw == world::kGridUndefined) continue;   // outside the map

                tileset::blit_metatile_pair(
                    dst, w, h, px, py, s, pal,
                    static_cast<uint16_t>(raw & world::kMetatileIdMask), pair);
            }
        }
    }

    // Markers last, over everything.
    for (int n = 0; n < world::kObjectEventCount; ++n) {
        const world::ObjectSnapshot& o = s.objects[n];
        if (!o.active || o.invisible) continue;

        const int px = place(s.view_base_x, o.x - s.view_x, w);
        const int py = place(s.view_base_y, o.y - s.view_y, h);
        if (px <= -kCell || px >= w || py <= -kCell || py >= h) continue;

        if (n == s.player_index)
            draw_box(dst, w, h, px, py, kCell, kPlayerColour, 2);
        else
            draw_box(dst, w, h, px, py, kCell, kNpcColour, 1);
    }
}

}  // namespace map_view
}  // namespace vr
