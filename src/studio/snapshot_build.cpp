// snapshot_build.cpp — see snapshot_build.h for what has to be reproduced.

#include "snapshot_build.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace studio {
namespace {

using vr::world::Snapshot;

// pokeruby's include/fieldmap.h. Repeated here as named constants because the
// arithmetic below is a port of that file's own code and reads correctly only
// with its own names.
constexpr int kMapOffset  = 7;                    // MAP_OFFSET
constexpr int kOffsetW    = kMapOffset * 2 + 1;   // MAP_OFFSET_W == 15
constexpr int kOffsetH    = kMapOffset * 2;       // MAP_OFFSET_H == 14
constexpr int kPalsInPrimary = 6;                 // NUM_PALS_IN_PRIMARY
constexpr int kPalsTotal     = 12;                // NUM_PALS_TOTAL

constexpr uint16_t kUndefined = vr::world::kGridUndefined;   // MAPGRID_UNDEFINED

// The backup grid, with its own dimensions, so the port below can read the way
// fieldmap.c does — dest[width * y + x] — without threading three arguments
// through every function.
struct Backup {
    std::vector<uint16_t>* g = nullptr;
    int w = 0, h = 0;
    Snapshot* snapshot=nullptr;
    const MapDef* source=nullptr;
    bool compatible_art=false;

    // FillConnection, with the bounds checks the game does not need and we do.
    //
    // The game trusts its own arithmetic; we are running the same arithmetic on
    // data we parsed ourselves, so a clamp here means either our parse or our
    // port is wrong. It says so rather than reading off the end of a vector.
    void copy(int x, int y, const Layout& src, int x2, int y2,
              int width, int height, const char* what) {
        bool clamped = false;
        for (int i = 0; i < height; ++i) {
            const int sy = y2 + i;
            const int dy = y + i;
            if (sy < 0 || sy >= src.height || dy < 0 || dy >= h) { clamped = true; continue; }
            for (int k = 0; k < width; ++k) {
                const int sx = x2 + k;
                const int dx = x + k;
                if (sx < 0 || sx >= src.width || dx < 0 || dx >= w) { clamped = true; continue; }
                (*g)[static_cast<size_t>(dy) * w + dx] =
                    src.map[static_cast<size_t>(sy) * src.width + sx];
            }
        }
        if (clamped)
            std::fprintf(stderr,
                         "[build] %s connection was clamped — the port and the "
                         "data disagree about a bound\n", what);
        if(snapshot && source && source->group>=0 && source->number>=0) {
            const int left=std::max({0,-x,-x2}),top=std::max({0,-y,-y2});
            const int right=std::min({width,w-x,src.width-x2});
            const int bottom=std::min({height,h-y,src.height-y2});
            if(right>left && bottom>top) snapshot->connections.push_back({
                source->group,source->number,src.width+kOffsetW,src.height+kOffsetH,
                x+left,y+top,x2+left+kMapOffset,y2+top+kMapOffset,right-left,bottom-top,compatible_art});
        }
    }
};

// FillNorthConnection / FillSouthConnection share this x/width clamp verbatim;
// factoring it out keeps the two ports honest about being the same code.
void horizontal_span(int offset, int backup_w, int c_width,
                     int* x, int* x2, int* width) {
    *x = offset + kMapOffset;
    if (*x < 0) {
        *x2 = -*x;
        *x += c_width;
        *width = (*x < backup_w) ? *x : backup_w;
        *x = 0;
    } else {
        *x2 = 0;
        *width = (*x + c_width < backup_w) ? c_width : backup_w - *x;
    }
}

// FillWestConnection / FillEastConnection's y/height clamp, likewise.
void vertical_span(int offset, int backup_h, int c_height,
                   int* y, int* y2, int* height) {
    *y = offset + kMapOffset;
    if (*y < 0) {
        *y2 = -*y;
        *height = (*y + c_height < backup_h) ? (*y + c_height) : backup_h;
        *y = 0;
    } else {
        *y2 = 0;
        *height = (*y + c_height < backup_h) ? c_height : backup_h - *y;
    }
}

// A stable, non-zero stand-in for gMapHeader.mapLayout.
//
// NON-ZERO MATTERS. diorama::update() rebuilds when s.layout_ptr differs from
// the mesh it already has, and its "no mesh yet" state is 0 — a Snapshot
// claiming layout_ptr 0 would never trigger a build. Distinct per layout
// matters for the same reason: the comparison harness meshes two Snapshots in
// one process and needs the second to be recognised as a different map.
uint32_t synthetic_layout_ptr(const std::string& id) {
    uint64_t h = 1469598103934665603ULL;
    for (char c : id) {
        h ^= static_cast<uint8_t>(c);
        h *= 1099511628211ULL;
    }
    // Fold to 32 bits and keep it clear of 0 and of anything that looks like a
    // real ROM pointer, so a stray one in a log is obviously synthetic.
    const uint32_t v = static_cast<uint32_t>(h ^ (h >> 32));
    return 0xDEC0'0000u | (v & 0x000F'FFFFu);
}

}  // namespace

bool build_snapshot(Decomp& d, const std::string& map_id, Snapshot* out,
                    BuildInfo* info, const BuildOptions& opt) {
    const MapDef* m = d.map(map_id);
    if (!m) return false;

    const Layout* l = d.layout(m->layout_id);
    if (!l) return false;

    const Tileset* primary   = d.tileset(l->primary_tileset);
    const Tileset* secondary = d.tileset(l->secondary_tileset);
    if (!primary || !secondary) return false;

    Snapshot s;
    s.map_group=m->group;s.map_number=m->number;
    if(m->group>=0 && m->number>=0) s.identity_source=Snapshot::IdentitySource::SourceTable;
    s.layout_ptr = synthetic_layout_ptr(l->id);
    s.width  = l->width  + kOffsetW;
    s.height = l->height + kOffsetH;

    // ── The grid ────────────────────────────────────────────────────────────
    // InitMapLayoutData: undefined everywhere, the layout at (7, 7), then the
    // connections on top.
    s.grid.assign(static_cast<size_t>(s.width) * s.height, kUndefined);

    Backup b{&s.grid, s.width, s.height, &s};

    for (int y = 0; y < l->height; ++y)
        for (int x = 0; x < l->width; ++x)
            s.grid[static_cast<size_t>(y + kMapOffset) * s.width + (x + kMapOffset)] =
                l->map[static_cast<size_t>(y) * l->width + x];

    for (const Connection& c : m->connections) {
        // dive and emerge are vertical in the OTHER sense — a different map
        // entirely, not a neighbour in the plane — and the game's own switch
        // ignores them here too.
        if (c.direction != "up" && c.direction != "down" &&
            c.direction != "left" && c.direction != "right")
            continue;

        const MapDef* cm = d.map(c.map_id);
        if (!cm) continue;
        const Layout* cl = d.layout(cm->layout_id);
        if (!cl) continue;
        b.source=cm;
        b.compatible_art=l->primary_tileset==cl->primary_tileset && l->secondary_tileset==cl->secondary_tileset;

        int x = 0, x2 = 0, width = 0, y = 0, y2 = 0, height = 0;

        if (c.direction == "up") {          // FillNorthConnection
            horizontal_span(c.offset, s.width, cl->width, &x, &x2, &width);
            b.copy(x, 0, *cl, x2, cl->height - kMapOffset, width, kMapOffset, "north");
        } else if (c.direction == "down") { // FillSouthConnection
            horizontal_span(c.offset, s.width, cl->width, &x, &x2, &width);
            b.copy(x, l->height + kMapOffset, *cl, x2, 0, width, kMapOffset, "south");
        } else if (c.direction == "left") { // FillWestConnection
            vertical_span(c.offset, s.height, cl->height, &y, &y2, &height);
            b.copy(0, y, *cl, cl->width - kMapOffset, y2, kMapOffset, height, "west");
        } else {                            // FillEastConnection
            vertical_span(c.offset, s.height, cl->height, &y, &y2, &height);
            // EIGHT columns, not seven. The decomp's own asymmetry — see the
            // header comment and MAP_OFFSET_W.
            b.copy(l->width + kMapOffset, y, *cl, 0, y2, kMapOffset + 1, height, "east");
        }
    }

    if(!s.valid_connections()) {
        std::fprintf(stderr,"[build] invalid connection provenance for %s\n",map_id.c_str());return false;
    }

    // ── Metatile tables ─────────────────────────────────────────────────────
    // Primary at 0, secondary at 512, zeros above whatever each tileset really
    // defines. A live capture holds ROM overrun there instead; see the header.
    constexpr int kTotal   = vr::world::kMetatilesTotal;      // 1024
    constexpr int kPrimary = vr::world::kMetatilesInPrimary;  // 512
    constexpr int kPerMt   = vr::world::kTilesPerMetatile;    // 8

    s.metatiles.assign(static_cast<size_t>(kTotal) * kPerMt, 0);
    s.attributes.assign(kTotal, 0);

    const int p_mt = primary->metatile_count   < kPrimary ? primary->metatile_count   : kPrimary;
    const int s_mt = secondary->metatile_count < kPrimary ? secondary->metatile_count : kPrimary;

    std::memcpy(s.metatiles.data(), primary->metatiles.data(),
                static_cast<size_t>(p_mt) * kPerMt * sizeof(uint16_t));
    std::memcpy(s.metatiles.data() + static_cast<size_t>(kPrimary) * kPerMt,
                secondary->metatiles.data(),
                static_cast<size_t>(s_mt) * kPerMt * sizeof(uint16_t));

    std::memcpy(s.attributes.data(), primary->attributes.data(),
                static_cast<size_t>(p_mt) * sizeof(uint16_t));
    std::memcpy(s.attributes.data() + kPrimary, secondary->attributes.data(),
                static_cast<size_t>(s_mt) * sizeof(uint16_t));

    // ── Tiles and palettes ──────────────────────────────────────────────────
    const int p_tiles = primary->tile_count   < kPrimary ? primary->tile_count   : kPrimary;
    const int s_tiles = secondary->tile_count < kPrimary ? secondary->tile_count : kPrimary;

    if (opt.borrow_tiles_from) {
        // The bisect. Take the pixel half from a known-good capture so a
        // difference downstream can only be the map half.
        s.vram_tiles = opt.borrow_tiles_from->vram_tiles;
        s.bg_palette = opt.borrow_tiles_from->bg_palette;
        std::fprintf(stderr,
                     "[build] BISECT: vram_tiles and bg_palette borrowed from the "
                     "reference; only the map half is from disk\n");
    } else {
        s.vram_tiles.assign(vr::world::kTileSheetSize, 0);
        std::memcpy(s.vram_tiles.data(), primary->tiles.data(),
                    static_cast<size_t>(p_tiles) * vr::world::kTileBytes);
        std::memcpy(s.vram_tiles.data() +
                        static_cast<size_t>(kPrimary) * vr::world::kTileBytes,
                    secondary->tiles.data(),
                    static_cast<size_t>(s_tiles) * vr::world::kTileBytes);

        // LoadTilesetPalette, reproduced exactly. The three facts that matter:
        //
        //   - a PRIMARY tileset contributes BG palettes 0..5, but BG entry 0 is
        //     overwritten with black rather than taken from the file;
        //   - a SECONDARY tileset contributes BG palettes 6..11 from ITS OWN
        //     palettes 6..11 — files 06.pal upwards, not 00.pal;
        //   - BG palettes 12..15 are not tileset data at all (NUM_PALS_TOTAL is
        //     12), so they stay zero here and hold unrelated data live.
        s.bg_palette.assign(vr::world::kPaletteEntries, 0);

        for (int i = 0; i < kPalsInPrimary * 16; ++i)
            s.bg_palette[i] = primary->palettes[i];
        s.bg_palette[0] = 0;   // RGB_BLACK

        for (int i = kPalsInPrimary * 16; i < kPalsTotal * 16; ++i)
            s.bg_palette[i] = secondary->palettes[i];
    }

    s.valid = true;

    if (info) {
        info->backup_w = s.width;
        info->backup_h = s.height;
        info->primary_name   = primary->name;
        info->secondary_name = secondary->name;
        info->primary_metatiles   = primary->metatile_count;
        info->secondary_metatiles = secondary->metatile_count;
        info->primary_tiles   = primary->tile_count;
        info->secondary_tiles = secondary->tile_count;
        info->first_undefined_metatile = kPrimary + s_mt;
        info->first_undefined_tile     = kPrimary + s_tiles;
    }

    std::fprintf(stderr,
                 "[build] %s -> %s: backup %dx%d, layout %dx%d at (7,7), "
                 "%zu connections\n",
                 map_id.c_str(), l->id.c_str(), s.width, s.height,
                 l->width, l->height, m->connections.size());
    std::fprintf(stderr,
                 "[build]   %s %d metatiles / %d tiles, %s %d / %d; "
                 "defined ids 0..%d, tiles 0..%d\n",
                 primary->name.c_str(), primary->metatile_count, primary->tile_count,
                 secondary->name.c_str(), secondary->metatile_count, secondary->tile_count,
                 kPrimary + s_mt - 1, kPrimary + s_tiles - 1);

    *out = std::move(s);
    return true;
}

}  // namespace studio
