// world_io.cpp — see world_io.h for what a .snap is and why it is not a memcpy.

#include "world_io.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace vr {
namespace world_io {
namespace {

constexpr char kMagic[8] = {'R', 'V', 'R', 'S', 'N', 'A', 'P', '\0'};

// The same bound capture() gates on (ruby_world.cpp's kMaxMapDataSize, which is
// pokeruby's MAX_MAP_DATA_SIZE). Repeated rather than shared because it is a
// VALIDATION limit here and a sanity gate there; if the game's constant ever
// grows, a stale reader refusing a large file is the safe failure.
constexpr int64_t kMaxMapCells = 0x2800;   // 10240

// ── Writing ──────────────────────────────────────────────────────────────────
//
// Assemble into memory and write once. The file is ~100 KB, so this costs
// nothing, and it means a failed write cannot leave a half-valid file behind
// that reads as a truncated Snapshot on the next run.
struct Writer {
    std::vector<uint8_t> b;

    void u8v(uint8_t v) { b.push_back(v); }
    void u16v(uint16_t v) {
        b.push_back(static_cast<uint8_t>(v));
        b.push_back(static_cast<uint8_t>(v >> 8));
    }
    void u32v(uint32_t v) {
        for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (i * 8)));
    }
    // Signed values go out as two's-complement of the same width, which is what
    // the cast to the unsigned type gives on every implementation C++20 allows.
    void i16v(int16_t v) { u16v(static_cast<uint16_t>(v)); }
    void i32v(int32_t v) { u32v(static_cast<uint32_t>(v)); }

    void vec_u16(const std::vector<uint16_t>& v) {
        u32v(static_cast<uint32_t>(v.size()));
        for (uint16_t x : v) u16v(x);
    }
    void vec_u8(const std::vector<uint8_t>& v) {
        u32v(static_cast<uint32_t>(v.size()));
        b.insert(b.end(), v.begin(), v.end());
    }
};

// ── Reading ──────────────────────────────────────────────────────────────────
//
// One cursor, one `ok` flag. Every accessor checks the remaining length and
// latches failure rather than throwing, so the parse below reads as straight
// line code and a truncated file fails once at the end instead of at whichever
// field happened to run off the edge.
struct Reader {
    const uint8_t* p = nullptr;
    size_t         n = 0;
    size_t         i = 0;
    bool           ok = true;

    bool need(size_t k) {
        if (!ok || i + k > n) { ok = false; return false; }
        return true;
    }
    uint8_t u8v() {
        if (!need(1)) return 0;
        return p[i++];
    }
    uint16_t u16v() {
        if (!need(2)) return 0;
        const uint16_t v = static_cast<uint16_t>(p[i] | (p[i + 1] << 8));
        i += 2;
        return v;
    }
    uint32_t u32v() {
        if (!need(4)) return 0;
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) v |= static_cast<uint32_t>(p[i + k]) << (k * 8);
        i += 4;
        return v;
    }
    int16_t i16v() { return static_cast<int16_t>(u16v()); }
    int32_t i32v() { return static_cast<int32_t>(u32v()); }

    // `expect` is the whole safety story for the length fields: every count in
    // this format is fixed by one of the game's own constants or by the grid
    // dimensions we just read, so a count that disagrees is a broken file, not
    // a variation to accommodate. Without this a corrupt u32 becomes a
    // multi-gigabyte resize.
    bool vec_u16(std::vector<uint16_t>& out, size_t expect, const char* what) {
        const uint32_t count = u32v();
        if (!ok) return false;
        if (count != expect) {
            std::fprintf(stderr, "[snap] %s: expected %zu entries, file says %u\n",
                         what, expect, count);
            ok = false;
            return false;
        }
        if (!need(static_cast<size_t>(count) * 2)) return false;
        out.resize(count);
        for (uint32_t k = 0; k < count; ++k) out[k] = u16v();
        return ok;
    }
    bool vec_u8(std::vector<uint8_t>& out, size_t expect, const char* what) {
        const uint32_t count = u32v();
        if (!ok) return false;
        if (count != expect) {
            std::fprintf(stderr, "[snap] %s: expected %zu bytes, file says %u\n",
                         what, expect, count);
            ok = false;
            return false;
        }
        if (!need(count)) return false;
        out.assign(p + i, p + i + count);
        i += count;
        return ok;
    }
};

}  // namespace

bool write(const world::Snapshot& s, const char* path) {
    const bool unknown=s.identity_source==world::Snapshot::IdentitySource::Unknown;
    if((unknown && (s.map_group!=-1 || s.map_number!=-1)) ||
       (!unknown && (!s.has_map_identity() || int(s.identity_source)>2)) || !s.valid_connections()) return false;
    Writer w;
    w.b.reserve(1 << 17);

    w.b.insert(w.b.end(), kMagic, kMagic + sizeof(kMagic));
    w.u32v(kVersion);
    w.u32v(0);   // reserved: a flags word, so adding one later is not a version bump

    w.u8v(s.valid ? 1u : 0u);
    w.u32v(s.layout_ptr);
    w.i32v(s.width);
    w.i32v(s.height);
    w.vec_u16(s.grid);

    w.i32v(s.cam_px);
    w.i32v(s.cam_py);
    w.i16v(s.view_x);
    w.i16v(s.view_y);
    w.i16v(s.view_base_x);
    w.i16v(s.view_base_y);

    w.i32v(s.player_index);
    w.u32v(static_cast<uint32_t>(world::kObjectEventCount));
    for (int k = 0; k < world::kObjectEventCount; ++k) {
        const world::ObjectSnapshot& o = s.objects[k];
        w.u8v(o.active ? 1u : 0u);
        w.u8v(o.invisible ? 1u : 0u);
        w.u8v(o.is_player ? 1u : 0u);
        w.u8v(o.graphics_id);
        w.u8v(o.sprite_id);
        w.u8v(o.elevation);
        w.u8v(o.facing);
        w.i16v(o.x);
        w.i16v(o.y);
    }

    w.vec_u16(s.metatiles);
    w.vec_u16(s.attributes);
    w.vec_u8(s.vram_tiles);
    w.vec_u16(s.bg_palette);
    w.u8v(uint8_t(s.identity_source));w.i16v(int16_t(s.map_group));w.i16v(int16_t(s.map_number));
    w.u8v(uint8_t(s.connections.size()));
    for(const auto& c:s.connections) {
        for(int v:{c.group,c.number,c.width,c.height,c.x,c.y,c.source_x,c.source_y,c.w,c.h}) w.i16v(int16_t(v));
        w.u8v(c.compatible_art?1:0);
    }

    std::FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "[snap] cannot open %s for writing\n", path);
        return false;
    }
    const size_t got = std::fwrite(w.b.data(), 1, w.b.size(), f);
    std::fclose(f);

    if (got != w.b.size()) {
        std::fprintf(stderr, "[snap] short write to %s (%zu of %zu bytes)\n",
                     path, got, w.b.size());
        return false;
    }
    std::fprintf(stderr, "[snap] wrote %s: %dx%d layout %08X, %zu bytes\n",
                 path, s.width, s.height, s.layout_ptr, w.b.size());
    return true;
}

bool read(world::Snapshot& out, const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "[snap] cannot open %s\n", path);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > (1 << 20)) {
        std::fprintf(stderr, "[snap] %s is empty\n", path);
        std::fclose(f);
        return false;
    }
    std::vector<uint8_t> raw(static_cast<size_t>(size));
    const size_t got = std::fread(raw.data(), 1, raw.size(), f);
    std::fclose(f);
    if (got != raw.size()) {
        std::fprintf(stderr, "[snap] short read of %s\n", path);
        return false;
    }

    Reader r;
    r.p = raw.data();
    r.n = raw.size();

    if (!r.need(sizeof(kMagic)) ||
        std::memcmp(raw.data(), kMagic, sizeof(kMagic)) != 0) {
        std::fprintf(stderr, "[snap] %s is not a .snap file\n", path);
        return false;
    }
    r.i += sizeof(kMagic);

    const uint32_t version = r.u32v();
    const uint32_t flags=r.u32v();
    if (!r.ok || flags!=0) return false;
    if (version != 1 && version != kVersion) {
        // REFUSED, not guessed at. Reading v1 bytes under v2 rules is how a
        // format silently starts meaning something else.
        std::fprintf(stderr,
                     "[snap] %s is version %u, this build reads version %u\n",
                     path, version, kVersion);
        return false;
    }

    // Parse into a local and only publish on success, so a rejected file never
    // leaves the caller holding half a Snapshot.
    world::Snapshot s;

    s.valid      = r.u8v() != 0;
    s.layout_ptr = r.u32v();
    s.width      = r.i32v();
    s.height     = r.i32v();
    if (!r.ok) return false;

    if (s.width <= 0 || s.height <= 0 || s.width > 1024 || s.height > 1024 ||
        static_cast<int64_t>(s.width) * s.height > kMaxMapCells) {
        std::fprintf(stderr, "[snap] %s: implausible grid %dx%d\n",
                     path, s.width, s.height);
        return false;
    }
    const size_t cells = static_cast<size_t>(s.width) * s.height;
    if (!r.vec_u16(s.grid, cells, "grid")) return false;

    s.cam_px      = r.i32v();
    s.cam_py      = r.i32v();
    s.view_x      = r.i16v();
    s.view_y      = r.i16v();
    s.view_base_x = r.i16v();
    s.view_base_y = r.i16v();

    s.player_index = r.i32v();
    const uint32_t objects = r.u32v();
    if (!r.ok) return false;
    if (objects != static_cast<uint32_t>(world::kObjectEventCount)) {
        std::fprintf(stderr, "[snap] %s: %u object slots, expected %d\n",
                     path, objects, world::kObjectEventCount);
        return false;
    }
    for (int k = 0; k < world::kObjectEventCount; ++k) {
        world::ObjectSnapshot& o = s.objects[k];
        o.active      = r.u8v() != 0;
        o.invisible   = r.u8v() != 0;
        o.is_player   = r.u8v() != 0;
        o.graphics_id = r.u8v();
        o.sprite_id   = r.u8v();
        o.elevation   = r.u8v();
        o.facing      = r.u8v();
        o.x           = r.i16v();
        o.y           = r.i16v();
    }
    if (!r.ok) return false;

    constexpr size_t kMetatileEntries =
        static_cast<size_t>(world::kMetatilesTotal) * world::kTilesPerMetatile;
    if (!r.vec_u16(s.metatiles,  kMetatileEntries,            "metatiles"))  return false;
    if (!r.vec_u16(s.attributes, world::kMetatilesTotal,      "attributes")) return false;
    if (!r.vec_u8 (s.vram_tiles, world::kTileSheetSize,       "vram_tiles")) return false;
    if (!r.vec_u16(s.bg_palette, world::kPaletteEntries,      "bg_palette")) return false;
    if(version==2) {
        const auto source=r.u8v();s.map_group=r.i16v();s.map_number=r.i16v();
        if(!r.ok || source>2) return false;
        s.identity_source=world::Snapshot::IdentitySource(source);
        if(source==0 ? (s.map_group!=-1 || s.map_number!=-1) : !s.has_map_identity()) return false;
        const int count=r.u8v();if(count>64) return false;
        for(int i=0;i<count;++i) {
            world::ConnectionSlice c;
            for(int* v:{&c.group,&c.number,&c.width,&c.height,&c.x,&c.y,&c.source_x,&c.source_y,&c.w,&c.h}) *v=r.i16v();
            const auto compatible=r.u8v();if(compatible>1) return false;c.compatible_art=compatible!=0;
            s.connections.push_back(c);
        }
        if(!r.ok || !s.valid_connections()) return false;
    }

    // Trailing bytes mean the writer and reader disagree about the format even
    // though every field parsed — worth saying out loud rather than ignoring.
    if (r.i != r.n) {
        std::fprintf(stderr, "[snap] %s: %zu trailing bytes %s\n",
                     path, r.n - r.i,version==2?"rejected":"ignored (legacy)");
        if(version==2) return false;
    }

    out = std::move(s);
    std::fprintf(stderr, "[snap] read %s: %dx%d layout %08X\n",
                 path, out.width, out.height, out.layout_ptr);
    return true;
}

}  // namespace world_io
}  // namespace vr
