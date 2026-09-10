// decomp_source.cpp — see decomp_source.h for the layer boundary this keeps.

#include "decomp_source.h"

#include "json_scan.h"
#include "png4bpp.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>

namespace studio {
namespace {

namespace fs = std::filesystem;

std::string join(const std::string& a, const std::string& b) {
    return (fs::path(a) / b).string();
}

// Read a whole text file. Used for the two .inc files, which are small.
bool read_text(const std::string& path, std::string* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "[decomp] cannot open %s\n", path.c_str());
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

// Trim ASCII whitespace from both ends.
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' ||
                     s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

// The quoted operand of an .incbin line, or "" if there is not one.
std::string incbin_path(const std::string& line) {
    const size_t a = line.find('"');
    if (a == std::string::npos) return {};
    const size_t b = line.find('"', a + 1);
    if (b == std::string::npos) return {};
    return line.substr(a + 1, b - a - 1);
}

}  // namespace

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::fprintf(stderr, "[decomp] cannot open %s\n", path.c_str());
        return false;
    }
    const std::streamsize n = f.tellg();
    if (n < 0) return false;
    f.seekg(0);
    out->resize(static_cast<size_t>(n));
    if (n > 0 && !f.read(reinterpret_cast<char*>(out->data()), n)) {
        std::fprintf(stderr, "[decomp] short read of %s\n", path.c_str());
        return false;
    }
    return true;
}

bool read_u16_file(const std::string& path, std::vector<uint16_t>* out) {
    std::vector<uint8_t> raw;
    if (!read_file(path, &raw)) return false;
    if (raw.size() % 2 != 0) {
        std::fprintf(stderr, "[decomp] %s has an odd byte count (%zu)\n",
                     path.c_str(), raw.size());
        return false;
    }
    out->resize(raw.size() / 2);
    // Explicit little-endian rather than a memcpy: the file's byte order is a
    // property of the GBA, not of whatever host happens to read it.
    for (size_t i = 0; i < out->size(); ++i)
        (*out)[i] = static_cast<uint16_t>(raw[i * 2] | (raw[i * 2 + 1] << 8));
    return true;
}

bool read_jasc_pal(const std::string& path, uint16_t* out16) {
    std::string text;
    if (!read_text(path, &text)) return false;

    std::istringstream in(text);
    std::string        line;

    // Header: "JASC-PAL", a version, then the entry count. We require 16 —
    // every GBA palette is 16 colours and a file that says otherwise is not
    // something to interpolate over.
    if (!std::getline(in, line) || trim(line) != "JASC-PAL") {
        std::fprintf(stderr, "[decomp] %s is not a JASC-PAL file\n", path.c_str());
        return false;
    }
    std::getline(in, line);   // version, "0100"
    if (!std::getline(in, line) || std::atoi(trim(line).c_str()) != 16) {
        std::fprintf(stderr, "[decomp] %s does not declare 16 colours\n", path.c_str());
        return false;
    }

    for (int i = 0; i < 16; ++i) {
        if (!std::getline(in, line)) {
            std::fprintf(stderr, "[decomp] %s ends after %d colours\n",
                         path.c_str(), i);
            return false;
        }
        int r = 0, g = 0, b = 0;
        if (std::sscanf(trim(line).c_str(), "%d %d %d", &r, &g, &b) != 3) {
            std::fprintf(stderr, "[decomp] %s line %d is not 'R G B'\n",
                         path.c_str(), i + 4);
            return false;
        }
        // >> 3 is the exact inverse of gbagfx's (v << 3) | (v >> 2) expansion.
        // See the header comment; this round trips every one of the 32 values.
        const uint16_t r5 = static_cast<uint16_t>((r & 0xFF) >> 3);
        const uint16_t g5 = static_cast<uint16_t>((g & 0xFF) >> 3);
        const uint16_t b5 = static_cast<uint16_t>((b & 0xFF) >> 3);
        out16[i] = static_cast<uint16_t>(r5 | (g5 << 5) | (b5 << 10));
    }
    return true;
}

// ── Indexing ────────────────────────────────────────────────────────────────

bool Decomp::index_layouts() {
    const std::string path = join(root_, "data/layouts/layouts.json");
    vr::json::Value doc;
    if (!vr::json::parse_file(path.c_str(), &doc)) return false;

    const vr::json::Value* arr = doc.find("layouts");
    if (!arr || !arr->is_array()) {
        std::fprintf(stderr, "[decomp] %s has no 'layouts' array\n", path.c_str());
        return false;
    }

    for (const vr::json::Value& e : arr->items) {
        const vr::json::Value* id = e.find("id");
        if (!id) continue;   // layouts.json carries a null entry or two

        Layout l;
        l.id     = id->as_str();
        l.name   = e.find("name")   ? e.find("name")->as_str()   : "";
        l.width  = e.find("width")  ? e.find("width")->as_int()  : 0;
        l.height = e.find("height") ? e.find("height")->as_int() : 0;
        l.primary_tileset   = e.find("primary_tileset")
                            ? e.find("primary_tileset")->as_str() : "";
        l.secondary_tileset = e.find("secondary_tileset")
                            ? e.find("secondary_tileset")->as_str() : "";
        l.blockdata_path = e.find("blockdata_filepath")
                         ? e.find("blockdata_filepath")->as_str() : "";
        l.border_path    = e.find("border_filepath")
                         ? e.find("border_filepath")->as_str() : "";
        if (!l.id.empty()) layouts_[l.id] = std::move(l);
    }
    std::fprintf(stderr, "[decomp] %zu layouts\n", layouts_.size());
    return !layouts_.empty();
}

bool Decomp::index_tilesets() {
    // Resolve actual pointer targets, not similarly named symbols. In
    // particular six SecretBase headers share gMetatiles_SecretBaseSecondary
    // while their graphics/palettes live in six different directories.
    std::map<std::string, std::vector<std::string>> assets;
    for (const char* source : {"data/tilesets/metatiles.inc", "data/tilesets/graphics.inc"}) {
        std::string text;
        if (!read_text(join(root_, source), &text)) return false;
        std::istringstream in(text);
        std::string line, symbol;
        while (std::getline(in, line)) {
            const std::string t = trim(line);
            const size_t colons = t.find("::");
            if (colons != std::string::npos) { symbol = t.substr(0, colons); continue; }
            if (!symbol.empty() && t.rfind(".incbin", 0) == 0) {
                const std::string path = incbin_path(t);
                if (!path.empty()) assets[symbol].push_back(path);
            }
        }
    }
    std::string text;
    // General's graphics were converted to C while the other tilesets remain
    // assembly. Resolve the named INCBIN declarations from the same source of
    // truth, including multi-line palette arrays.
    if (!read_text(join(root_, "src/data/graphics.c"), &text)) return false;
    {
        std::map<std::string, std::vector<std::string>> c_assets;
        std::istringstream graphics(text);
        std::string line, symbol;
        while (std::getline(graphics, line)) {
            const std::string t = trim(line);
            if (t.rfind("const ", 0) == 0) {
                symbol.clear();
                const size_t begin = t.find("gTileset");
                const size_t end = t.find('[', begin);
                if (begin != std::string::npos && end != std::string::npos)
                    symbol = trim(t.substr(begin, end - begin));
            }
            if (!symbol.empty() && (t.find("INCBIN_U16(") != std::string::npos ||
                                    t.find("INCBIN_U8(") != std::string::npos)) {
                const std::string path = incbin_path(t);
                if (!path.empty()) c_assets[symbol].push_back(path);
            }
            if (t.find(';') != std::string::npos) symbol.clear();
        }
        // Shop appears in assembly for English and C for German, with the
        // same operands. Do not append the duplicate palette declaration.
        for (auto& [name, paths] : c_assets) {
            const auto existing = assets.find(name);
            if (existing != assets.end() && existing->second != paths) {
                std::fprintf(stderr, "[decomp] conflicting graphics declarations for %s\n", name.c_str());
                return false;
            }
            if (existing == assets.end()) assets.emplace(name, std::move(paths));
        }
    }
    if (!read_text(join(root_, "data/tilesets/headers.inc"), &text)) return false;
    std::istringstream in(text);
    std::map<std::string, Tileset> indexed;
    std::string line;
    Tileset current;
    std::vector<std::string> pointers;
    int bytes = 0;
    auto finish = [&]() {
        if (current.name.empty()) return true;
        if (bytes != 2 || pointers.size() != 5) {
            std::fprintf(stderr, "[decomp] incomplete tileset header %s\n", current.name.c_str());
            return false;
        }
        for (int i = 0; i < 4; ++i) if (!assets.count(pointers[i]) || assets[pointers[i]].empty()) {
            std::fprintf(stderr, "[decomp] %s: unresolved asset %s\n", current.name.c_str(), pointers[i].c_str());
            return false;
        }
        fs::path pixels(assets[pointers[0]].front());
        if (pixels.extension() == ".lz") pixels.replace_extension();
        if (pixels.extension() != ".4bpp") return false;
        pixels.replace_extension(".png");
        current.tiles_path = join(root_, pixels.string());
        current.dir = fs::path(current.tiles_path).parent_path().string();
        current.metatiles_path = join(root_, assets[pointers[2]].front());
        current.attributes_path = join(root_, assets[pointers[3]].front());
        if (assets[pointers[1]].size() < 16) {
            std::fprintf(stderr, "[decomp] %s: palette pointer must name sixteen entries\n", current.name.c_str());
            return false;
        }
        // Some arrays are followed by unlabelled unused tile data. The
        // palette pointer addresses exactly sixteen palettes, not every
        // incbin up to the next assembly label.
        for (size_t i = 0; i < 16; ++i) {
            const auto& source = assets[pointers[1]][i];
            fs::path palette(source);
            if (palette.extension() != ".gbapal") return false;
            palette.replace_extension(".pal");
            current.palette_paths.push_back(join(root_, palette.string()));
        }
        const std::string name = current.name;
        return indexed.emplace(name, std::move(current)).second;
    };
    while (std::getline(in, line)) {
        const std::string t = trim(line.substr(0, line.find('@')));
        const size_t colons = t.find("::");
        if (t.rfind("gTileset_", 0) == 0 && colons != std::string::npos) {
            if (!finish()) return false;
            current = Tileset{}; current.name = t.substr(0, colons);
            pointers.clear(); bytes = 0;
        } else if (!current.name.empty() && t.rfind(".byte", 0) == 0) {
            const std::string value = trim(t.substr(5));
            if (value != "TRUE" && value != "FALSE" && value != "0" && value != "1") return false;
            const bool flag = value == "TRUE" || value == "1";
            if (bytes == 0) current.is_compressed = flag;
            if (bytes == 1) current.is_secondary = flag;
            ++bytes;
        } else if (!current.name.empty() && t.rfind(".4byte", 0) == 0) {
            pointers.push_back(trim(t.substr(6)));
        }
    }
    if (!finish()) return false;
    tilesets_ = std::move(indexed);

    std::fprintf(stderr, "[decomp] %zu tilesets\n", tilesets_.size());
    return !tilesets_.empty();
}

bool Decomp::index_maps() {
    const std::string maps = join(root_, "data/maps");
    std::error_code   ec;
    decltype(maps_) indexed;
    decltype(map_dir_by_name_) directories;

    for (fs::directory_iterator it(maps, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& e = *it;
        const bool is_directory = e.is_directory(ec);
        if (ec) break;
        if (!is_directory) continue;
        const std::string dir  = e.path().string();
        const std::string file = join(dir, "map.json");
        const bool has_map = fs::exists(file, ec);
        if (ec) break;
        if (!has_map) continue;

        // Only the id and the directory here — connections and the layout are
        // read on demand, because indexing exists to answer "which folder is
        // MAP_OLDALE_TOWN" and nothing else.
        vr::json::Value doc;
        if (!vr::json::parse_file(file.c_str(), &doc)) {
            std::fprintf(stderr, "[decomp] cannot index map metadata %s\n", file.c_str());
            return false;
        }
        const vr::json::Value* id = doc.find("id");
        if (!id || id->type != vr::json::Value::Type::String || id->string.empty()) {
            std::fprintf(stderr, "[decomp] %s: missing or invalid map id\n", file.c_str());
            return false;
        }

        MapDef m;
        m.id   = id->as_str();
        m.name = e.path().filename().string();
        m.dir  = dir;
        if (const auto duplicate = indexed.find(m.id); duplicate != indexed.end()) {
            std::fprintf(stderr, "[decomp] duplicate map id %s in %s and %s\n",
                         m.id.c_str(), duplicate->second.dir.c_str(), dir.c_str());
            return false;
        }
        directories.emplace(m.name, m.id);
        const std::string map_id = m.id;
        indexed.emplace(map_id, std::move(m));
    }
    if (ec) {
        std::fprintf(stderr, "[decomp] cannot list %s: %s\n",
                     maps.c_str(), ec.message().c_str());
        return false;
    }
    vr::json::Value groups;
    if(!vr::json::parse_file(join(maps,"map_groups.json").c_str(),&groups)) return false;
    const auto* order=groups.find("group_order");
    if(!order || !order->is_array() || order->items.size()>256) return false;
    std::set<std::string> seen_groups,seen_maps;
    for(size_t gi=0;gi<order->items.size();++gi) {
        const auto& group=order->items[gi];
        if(group.type!=vr::json::Value::Type::String || !seen_groups.insert(group.string).second) return false;
        const auto* names=groups.find(group.string.c_str());
        if(!names || !names->is_array() || names->items.size()>256) return false;
        for(size_t ni=0;ni<names->items.size();++ni) {
            const auto& name=names->items[ni];
            if(name.type!=vr::json::Value::Type::String || !seen_maps.insert(name.string).second) return false;
            const auto dir=directories.find(name.string);if(dir==directories.end()) return false;
            auto& m=indexed.at(dir->second);m.group=int(gi);m.number=int(ni);
        }
    }
    maps_ = std::move(indexed);
    map_dir_by_name_ = std::move(directories);
    std::fprintf(stderr, "[decomp] %zu maps\n", maps_.size());
    return !maps_.empty();
}

bool Decomp::open(const std::string& root) {
    root_ = root;
    if (!fs::exists(join(root_, "data/layouts/layouts.json"))) {
        std::fprintf(stderr,
                     "[decomp] %s does not look like a pokeruby checkout "
                     "(no data/layouts/layouts.json)\n", root_.c_str());
        return false;
    }
    return index_layouts() && index_tilesets() && index_maps();
}

// ── On-demand loading ───────────────────────────────────────────────────────

bool Decomp::load_layout_blocks(Layout& l) {
    if (l.map_loaded) return true;
    if (l.blockdata_path.empty() || l.width <= 0 || l.height <= 0) {
        std::fprintf(stderr, "[decomp] layout %s has no usable blockdata\n",
                     l.id.c_str());
        return false;
    }
    if (!read_u16_file(join(root_, l.blockdata_path), &l.map)) return false;

    const size_t want = static_cast<size_t>(l.width) * l.height;
    if (l.map.size() != want) {
        // A length mismatch means layouts.json and map.bin disagree, which is
        // exactly the kind of thing that would otherwise show up much later as
        // a map that is subtly sheared.
        std::fprintf(stderr,
                     "[decomp] layout %s: map.bin has %zu cells, "
                     "layouts.json says %dx%d = %zu\n",
                     l.id.c_str(), l.map.size(), l.width, l.height, want);
        return false;
    }
    l.map_loaded = true;
    return true;
}

const Layout* Decomp::layout(const std::string& id) {
    auto it = layouts_.find(id);
    if (it == layouts_.end()) {
        std::fprintf(stderr, "[decomp] no layout %s\n", id.c_str());
        return nullptr;
    }
    if (!load_layout_blocks(it->second)) return nullptr;
    return &it->second;
}

bool Decomp::load_map(MapDef& m) {
    if (m.loaded) return true;

    vr::json::Value doc;
    if (!vr::json::parse_file(join(m.dir, "map.json").c_str(), &doc)) return false;

    if (const vr::json::Value* v = doc.find("layout")) m.layout_id = v->as_str();

    if (const vr::json::Value* conns = doc.find("connections")) {
        if (conns->is_array()) {
            for (const vr::json::Value& c : conns->items) {
                Connection k;
                k.direction = c.find("direction") ? c.find("direction")->as_str() : "";
                k.offset    = c.find("offset")    ? c.find("offset")->as_int()    : 0;
                k.map_id    = c.find("map")       ? c.find("map")->as_str()       : "";
                if (!k.direction.empty() && !k.map_id.empty())
                    m.connections.push_back(std::move(k));
            }
        }
        // A map with no connections has "connections": null, which is not an
        // array and correctly leaves the list empty.
    }
    m.loaded = true;
    return true;
}

const MapDef* Decomp::map(const std::string& id) {
    auto it = maps_.find(id);
    if (it == maps_.end()) {
        std::fprintf(stderr, "[decomp] no map %s\n", id.c_str());
        return nullptr;
    }
    if (!load_map(it->second)) return nullptr;
    return &it->second;
}

const MapDef* Decomp::map_by_name(const std::string& name) {
    auto it = map_dir_by_name_.find(name);
    if (it == map_dir_by_name_.end()) {
        std::fprintf(stderr, "[decomp] no map directory named %s\n", name.c_str());
        return nullptr;
    }
    return map(it->second);
}

bool Decomp::load_tileset(Tileset& t) {
    if (t.metatile_count > 0) return true;   // already loaded
    // Publish only after every asset is valid. A failed PNG/palette load must
    // not leave a positive metatile_count that makes a retry appear successful.
    Tileset loaded = t;
    if (!read_u16_file(t.metatiles_path, &loaded.metatiles)) return false;
    if (!read_u16_file(t.attributes_path, &loaded.attributes))
        return false;

    // Eight tile entries per metatile, and one attribute word per metatile. If
    // those two disagree the tileset is malformed and every id above the
    // shorter one would silently read the wrong thing.
    if (loaded.metatiles.empty() || loaded.metatiles.size() % 8 != 0) {
        std::fprintf(stderr, "[decomp] %s: metatiles.bin is not a multiple of 8 entries\n",
                     t.name.c_str());
        return false;
    }
    loaded.metatile_count = static_cast<int>(loaded.metatiles.size() / 8);
    if (static_cast<int>(loaded.attributes.size()) != loaded.metatile_count) {
        std::fprintf(stderr,
                     "[decomp] %s: %d metatiles but %zu attributes\n",
                     t.name.c_str(), loaded.metatile_count, loaded.attributes.size());
        return false;
    }

    png::Indexed img;
    if (!png::decode(t.tiles_path.c_str(), &img)) return false;
    if (!png::pack_gba_tiles(img, &loaded.tiles, &loaded.tile_count)) return false;

    if (t.palette_paths.size() != 16) return false;
    loaded.palettes.assign(16 * 16, 0);
    for (int i = 0; i < 16; ++i) {
        if (!read_jasc_pal(t.palette_paths[size_t(i)], &loaded.palettes[static_cast<size_t>(i) * 16]))
            return false;
    }
    t = std::move(loaded);

    std::fprintf(stderr,
                 "[decomp] %s: %d metatiles, %d tiles, %s\n",
                 t.name.c_str(), t.metatile_count, t.tile_count,
                 t.is_secondary ? "secondary" : "primary");
    return true;
}

const Tileset* Decomp::tileset(const std::string& name) {
    auto it = tilesets_.find(name);
    if (it == tilesets_.end()) {
        std::fprintf(stderr, "[decomp] no tileset %s\n", name.c_str());
        return nullptr;
    }
    if (!load_tileset(it->second)) return nullptr;
    return &it->second;
}

}  // namespace studio
