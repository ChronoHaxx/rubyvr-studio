// decomp_source.h — pokeruby's data files, as raw structures.
//
// LAYER 0. This reads FILES and knows nothing about world::Snapshot, the
// backup map, connections being stitched, or how a tileset lands in VRAM. It
// answers "what does the decomp say" and stops there.
//
// The split from snapshot_build (layer 1) is not tidiness. The two fail for
// completely different reasons — a wrong path or a PNG we cannot decode here,
// a misplaced connection or a mis-slotted palette there — and keeping them
// apart means a failure names itself. Merging them is how "the map is wrong"
// becomes a single unsplittable symptom.
//
// WHERE THE NAME-TO-DIRECTORY MAPPINGS COME FROM, because guessing them is a
// trap this deliberately avoids:
//
//   A layout names its tilesets by SYMBOL — "gTileset_General",
//   "gTileset_Petalburg" — and the files live in data/tilesets/primary/general
//   and data/tilesets/secondary/petalburg. The transformation looks like
//   "lowercase the suffix and pick a folder", and it is not: primary/ holds
//   building and secret_base as well as general, so the folder is not derivable
//   from the symbol at all.
//
//   headers.inc names the tiles, palettes, metatiles and attributes separately;
//   graphics.inc and metatiles.inc resolve those symbols to their .incbin files;
//   General's graphics use INCBIN declarations in src/data/graphics.c instead.
//   Secret-base variants share metatile tables but use different graphics and
//   palettes, so a single inferred directory cannot represent them. The
//   isSecondary flag matters because
//   LoadTilesetPalette branches on it — see snapshot_build.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace studio {

struct Tileset {
    std::string name;                  // "gTileset_General"
    std::string dir;                   // absolute path to its graphics directory
    bool        is_secondary = false;  // headers.inc's own byte, not the folder
    bool        is_compressed = false;
    // Resolved from the four independent pointers in headers.inc. Paths refer
    // to source PNG/JASC files for generated .4bpp/.gbapal operands.
    std::string metatiles_path, attributes_path, tiles_path;
    std::vector<std::string> palette_paths;

    // metatiles.bin / metatile_attributes.bin, exactly as stored: little-endian
    // u16, 8 tile entries per metatile. The COUNT is whatever the tileset
    // actually has — 512 for General, 144 for Petalburg — not the 512 the
    // game's loader assumes. Padding to the game's assumption is layer 1's job
    // and layer 1 says so.
    std::vector<uint16_t> metatiles;
    std::vector<uint16_t> attributes;
    int                   metatile_count = 0;

    // tiles.png, decoded to indices and re-packed into the GBA's 4bpp tile
    // layout: 32 bytes per tile, low nibble first. Again the tileset's own
    // count, not a padded one.
    std::vector<uint8_t>  tiles;
    int                   tile_count = 0;

    // All sixteen palettes/NN.pal, as BGR555. Sixteen are on disk for every
    // tileset; which SIX of them the game actually loads, and into which BG
    // slots, depends on isSecondary and is decided in layer 1.
    std::vector<uint16_t> palettes;    // 16 * 16 entries
};

struct Connection {
    std::string direction;   // "up" "down" "left" "right" "dive" "emerge"
    int         offset = 0;  // may be negative
    std::string map_id;      // "MAP_OLDALE_TOWN"
};

struct Layout {
    std::string id;          // "LAYOUT_ROUTE101"
    std::string name;        // "Route101_Layout"
    int         width = 0;
    int         height = 0;
    std::string primary_tileset;
    std::string secondary_tileset;
    std::string blockdata_path;
    std::string border_path;

    // map.bin, width * height cells. Loaded on demand by Decomp::layout().
    std::vector<uint16_t> map;
    bool                  map_loaded = false;
};

struct MapDef {
    int group = -1, number = -1;
    std::string             id;          // "MAP_ROUTE101"
    std::string             name;        // "Route101"
    std::string             dir;         // data/maps/Route101
    std::string             layout_id;   // "LAYOUT_ROUTE101"
    std::vector<Connection> connections;
    bool                    loaded = false;
};

// Everything is cached and returned by pointer. Pointers stay valid for the
// lifetime of the Decomp: the containers are node-based (std::map) precisely so
// that loading another map cannot invalidate a reference the caller is holding.
class Decomp {
public:
    // `root` is the pokeruby checkout — the directory containing data/ and
    // include/. Reads layouts.json, the two tileset .inc files, and indexes
    // every data/maps/<Name>/map.json by its MAP_* id. Roughly 400 small files,
    // once, so that a connection naming MAP_OLDALE_TOWN can be resolved without
    // inventing a symbol-to-directory rule.
    bool open(const std::string& root);

    const std::string& root() const { return root_; }
    std::vector<std::string> map_ids() const {
        std::vector<std::string> ids;
        for (const auto& entry : maps_) ids.push_back(entry.first);
        return ids;
    }

    // By LAYOUT_* id. Loads map.bin on first request and validates its length
    // against the layout's declared width * height.
    const Layout* layout(const std::string& id);

    // By MAP_* id, or by directory name ("Route101"). Loads map.json on first
    // request.
    const MapDef* map(const std::string& id);
    const MapDef* map_by_name(const std::string& name);

    // By gTileset_* symbol. Loads every file in the tileset's directory.
    const Tileset* tileset(const std::string& name);

private:
    bool index_layouts();
    bool index_tilesets();
    bool index_maps();
    bool load_map(MapDef& m);
    bool load_layout_blocks(Layout& l);
    bool load_tileset(Tileset& t);

    std::string root_;
    std::map<std::string, Layout>  layouts_;      // by LAYOUT_* id
    std::map<std::string, MapDef>  maps_;         // by MAP_* id
    std::map<std::string, std::string> map_dir_by_name_;   // "Route101" -> id
    std::map<std::string, Tileset> tilesets_;     // by gTileset_* symbol
};

// ── Small shared readers, exposed because layer 1 and the tests want them ────

// Whole file into a byte vector.
bool read_file(const std::string& path, std::vector<uint8_t>* out);

// A file of little-endian u16s. Fails if the size is odd.
bool read_u16_file(const std::string& path, std::vector<uint16_t>* out);

// One JASC-PAL file to sixteen BGR555 entries.
//
// The .pal stores 8-bit components that gbagfx produced by EXPANDING the GBA's
// 5 bits as (v << 3) | (v >> 2) — which is why a channel reads 41 rather than
// 40 for value 5. The exact inverse is therefore a plain >> 3, and it round
// trips every value. tileset.h's bgr555_to_rgba performs the same expansion in
// the other direction, so the two agree by construction.
bool read_jasc_pal(const std::string& path, uint16_t* out16);

}  // namespace studio
