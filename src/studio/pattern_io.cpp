#include "cutout.h"
// pattern_io.cpp â€” see pattern_io.h.

#include "pattern_io.h"
#include "platform_io.h"
#include "terrain.h"

#include <cstdio>
#include <algorithm>
#include <set>

namespace studio {
namespace pattern_io {
namespace {

using vr::diorama::ObjectClass;
using vr::overrides::ApplyClass;

const char* class_name(ApplyClass c) {
    switch (c) {
        case ApplyClass::kProp:      return "prop";
        case ApplyClass::kStructure: return "structure";
        case ApplyClass::kMass:      return "mass";
        case ApplyClass::kInfer:     break;
    }
    return nullptr;   // kInfer is written by OMITTING the key
}

ApplyClass apply_class_of(ObjectClass c) {
    switch (c) {
        case ObjectClass::kProp:      return ApplyClass::kProp;
        case ObjectClass::kStructure: return ApplyClass::kStructure;
        case ObjectClass::kMass:      return ApplyClass::kMass;
    }
    return ApplyClass::kInfer;
}

}  // namespace

bool from_object(const vr::world::Snapshot& s,
                 const vr::diorama::ObjectModel& model,
                 int cell_x, int cell_y,
                 const char* name,
                 vr::overrides::Pattern* out) {
    if (!out) return false;
    const int k = model.object_at(cell_x, cell_y);
    if (k < 0) {
        std::fprintf(stderr, "[pattern] no object at (%d,%d)\n", cell_x, cell_y);
        return false;
    }
    const vr::diorama::Object& o = model.objects[static_cast<size_t>(k)];
    std::vector<Cell> cells;
    for (int y = o.y; y < o.y + o.extent; ++y)
        for (int x = o.x; x < o.x + o.w; ++x)
            if (model.object_at(x, y) == k) cells.push_back({x, y});
    if (!from_cells(s, cells, name, out)) return false;
    out->apply.cls = apply_class_of(o.cls);
    return true;
}

bool from_cells(const vr::world::Snapshot& s, const std::vector<Cell>& cells,
                const char* name, vr::overrides::Pattern* out) {
    if (!out || !s.valid || cells.empty()) return false;
    int x0 = s.width, y0 = s.height, x1 = -1, y1 = -1;
    for (const Cell& cell : cells) {
        if (cell.x < 0 || cell.y < 0 || cell.x >= s.width || cell.y >= s.height ||
            s.cell(cell.x, cell.y) == vr::world::kGridUndefined) return false;
        x0 = std::min(x0, cell.x); y0 = std::min(y0, cell.y);
        x1 = std::max(x1, cell.x); y1 = std::max(y1, cell.y);
    }
    vr::overrides::Pattern p;
    p.name = name ? name : "group";
    p.w = x1 - x0 + 1; p.extent = y1 - y0 + 1;
    if (p.w > 64 || p.extent > 64) return false;
    p.source.x = x0; p.source.y = y0;
    p.ids.assign(size_t(p.cells()), 0);
    p.mask.assign(size_t(p.cells()), 0);
    for (const Cell& cell : cells) {
        const size_t k = size_t(cell.y - y0) * p.w + cell.x - x0;
        const uint16_t id = s.metatile_id(cell.x, cell.y);
        const size_t base = size_t(id) * vr::world::kTilesPerMetatile;
        if (id >= s.attributes.size() || base + vr::world::kTilesPerMetatile > s.metatiles.size())
            return false;
        p.mask[k] = 1; p.ids[k] = id;
        vr::overrides::TileDef tile;
        std::copy_n(s.metatiles.begin() + base, vr::world::kTilesPerMetatile, tile.entries);
        tile.attr = s.attributes[id];
        p.tiles[id] = tile;
    }
    for (int i = 0; i < p.cells(); ++i) if (p.mask[i]) { p.anchor = i; break; }
    *out = std::move(p);
    return true;
}

bool same_key(const vr::overrides::Pattern& a, const vr::overrides::Pattern& b) {
    if (a.w != b.w || a.extent != b.extent || a.mask != b.mask || a.tiles != b.tiles ||
        a.ids.size() != b.ids.size()) return false;
    for (size_t i = 0; i < a.mask.size(); ++i)
        if (a.mask[i] && a.ids[i] != b.ids[i]) return false;
    return true;
}

void quoted(std::FILE* f, const std::string& value) {
    std::fputc('"', f);
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { std::fputc('\\', f); std::fputc(c, f); }
        else if (c < 0x20) std::fprintf(f, "\\u%04x", unsigned(c));
        else std::fputc(c, f);
    }
    std::fputc('"', f);
}

bool write(const char* path, const vr::overrides::OverrideSet& set) {
    if(!vr::overrides::supported_version(set.version)) return false;
    if(!vr::terrain::valid(set.terrain) || (!set.terrain.empty() && set.version!=vr::overrides::kTerrainVersion)) return false;
    for (const auto& pattern : set.patterns)
        if (!vr::cutout::valid(pattern) || !vr::overrides::valid_parts(pattern) ||
            (pattern.voxel && set.version<vr::overrides::kVoxelVersion)) return false;
    if (!path || !*path) return false;
    // The temporary is a sibling of `path`, created exclusively. That is what
    // lets the publication below be a single atomic rename and keeps a failed
    // write from touching the previous file at all. See platform_io.h.
    std::string temporary;
    std::FILE* f = platform_io::create_sibling_temporary(path, &temporary);
    if (!f) {
        std::fprintf(stderr, "[pattern] cannot create temporary output for %s\n", path);
        return false;
    }

    // No BOM, ever. A UTF-8 BOM made the JSON reader report "expected a value
    // at byte 0", which reads like a corrupt file rather than an encoding â€” and
    // these files are meant to be hand-editable, so it cost an hour once
    // already. Writing plain bytes is how it stays fixed.
    std::fprintf(f, "{\n  \"version\": %u,\n  \"patterns\": [\n",
                 set.version);

    auto mask=[&](const vr::overrides::Cutout& c) {
        std::fprintf(f,"{ \"w\": %d, \"h\": %d, \"first\": %d, \"runs\": [",c.w,c.h,int(c.opacity.front()));
        const auto runs=vr::cutout::encode(c);
        for(size_t i=0;i<runs.size();++i) std::fprintf(f,"%s%u",i?", ":"",runs[i]);
        std::fputs("] }",f);
    };

    for (size_t pi = 0; pi < set.patterns.size(); ++pi) {
        const vr::overrides::Pattern& p = set.patterns[pi];
        std::fprintf(f, "    {\n");
        std::fputs("      \"name\": ", f); quoted(f, p.name); std::fputs(",\n", f);
        if (!p.id.empty()) {
            std::fputs("      \"id\": ", f); quoted(f, p.id); std::fputs(",\n", f);
        }
        if (!p.source.room.empty()) {
            std::fputs("      \"source\": { \"room\": ", f); quoted(f, p.source.room);
            std::fprintf(f, ", \"x\": %d, \"y\": %d, \"coordinates\": \"backup-map\" },\n",
                         p.source.x, p.source.y);
        }
        std::fprintf(f, "      \"w\": %d,\n      \"extent\": %d,\n", p.w, p.extent);

        std::fprintf(f, "      \"ids\": [");
        for (size_t i = 0; i < p.ids.size(); ++i)
            std::fprintf(f, "%s%u", i ? ", " : "", p.ids[i]);
        std::fprintf(f, "],\n");

        std::fprintf(f, "      \"mask\": [");
        for (size_t i = 0; i < p.mask.size(); ++i)
            std::fprintf(f, "%s%d", i ? ", " : "", p.mask[i]);
        std::fprintf(f, "],\n");

        std::fprintf(f, "      \"tiles\": {\n");
        size_t n = 0;
        for (const auto& kv : p.tiles) {
            std::fprintf(f, "        \"%u\": { \"entries\": [", kv.first);
            for (int i = 0; i < vr::world::kTilesPerMetatile; ++i)
                std::fprintf(f, "%s%u", i ? ", " : "", kv.second.entries[i]);
            std::fprintf(f, "], \"attr\": %u }%s\n", kv.second.attr,
                         (++n < p.tiles.size()) ? "," : "");
        }
        std::fprintf(f, "      },\n");

        if (p.cutout) {
            const auto& c = *p.cutout;
            std::fprintf(f, "      \"cutout\": { \"w\": %d, \"h\": %d, \"first\": %d, \"runs\": [",
                         c.w, c.h, c.opacity.empty() ? 0 : c.opacity.front());
            const auto runs = vr::cutout::encode(c);
            for (size_t i = 0; i < runs.size(); ++i) std::fprintf(f, "%s%u", i ? ", " : "", runs[i]);
            std::fputs("] },\n", f);
        }

        if(p.voxel) {
            std::fprintf(f,"      \"voxel\": { \"pixels_per_cell\": %d, \"ground\": ",p.voxel->pixels_per_cell);
            mask(p.voxel->ground);std::fputs(", \"shadow\": ",f);mask(p.voxel->shadow);std::fputs(" },\n",f);
        }
        if (p.model_seeded) std::fputs("      \"model_seeded\": true,\n", f);
        if (p.follow_ground) std::fputs("      \"follow_ground\": true,\n", f);
        if (!p.parts.empty()) {
            std::fputs("      \"parts\": [\n", f);
            for (size_t i=0;i<p.parts.size();++i) {
                const auto& part=p.parts[i];
                std::fputs("        { \"id\": ",f); quoted(f,part.id);
                std::fputs(", \"name\": ",f); quoted(f,part.name);
                const char* kind=part.kind==vr::overrides::PartKind::Box?"box":
                    part.kind==vr::overrides::PartKind::Billboard?"billboard":
                    part.kind==vr::overrides::PartKind::Wedge?"wedge":"invalid";
                std::fprintf(f,", \"kind\": \"%s\"",kind);
                if(part.kind==vr::overrides::PartKind::Wedge)
                    std::fprintf(f,", \"axis\": \"%s\", \"direction\": %d",part.wedge_axis==0?"x":"z",part.wedge_direction);
                if(part.art_region!=std::array<int,4>{}) {
                    const auto& r=part.art_region;
                    std::fprintf(f,", \"art_region\": [%d, %d, %d, %d]",r[0],r[1],r[2],r[3]);
                }
                if(!part.surfaces.empty()) {
                    std::fputs(", \"surfaces\": [",f);
                    for(size_t j=0;j<part.surfaces.size();++j) {
                        const auto& s=part.surfaces[j];const auto& r=s.region;
                        std::fprintf(f,"%s{ \"region\": [%d, %d, %d, %d]",j?", ":"",r[0],r[1],r[2],r[3]);
                        if(s.flip_u) std::fputs(", \"flip_u\": true",f);
                        if(s.flip_v) std::fputs(", \"flip_v\": true",f);
                        if(s.offset!=std::array<int,2>{}) std::fprintf(f,", \"offset\": [%d, %d]",s.offset[0],s.offset[1]);
                        std::fputs(" }",f);
                    }
                    std::fputc(']',f);
                }
                if(part.local_mask) {std::fputs(", \"local_mask\": ",f);mask(*part.local_mask);}
                if(part.side_art) {
                    const auto& s=*part.side_art;const auto& r=s.region;
                    std::fprintf(f,", \"side_art\": { \"region\": [%d, %d, %d, %d]",r[0],r[1],r[2],r[3]);
                    if(s.flip_u) std::fputs(", \"flip_u\": true",f);
                    if(s.flip_v) std::fputs(", \"flip_v\": true",f);
                    if(s.offset!=std::array<int,2>{}) std::fprintf(f,", \"offset\": [%d, %d]",s.offset[0],s.offset[1]);
                    std::fputs(" }",f);
                }
                auto vector=[&](const char* name,vr::part_geometry::Vec v) {
                    std::fprintf(f,", \"%s\": [%.9g, %.9g, %.9g]",name,double(v.x),double(v.y),double(v.z));
                };
                vector("position",part.transform.position); vector("size",part.transform.size);
                vector("angles",part.transform.angles);
                std::fprintf(f," }%s\n",i+1<p.parts.size()?",":"");
            }
            std::fputs("      ],\n",f);
        }

        // Only what was actually authored. An omitted key means "leave this to
        // inference", which is a different statement from any value.
        std::fprintf(f, "      \"apply\": {");
        const char* cls = class_name(p.apply.cls);
        int written = 0;
        if (cls) std::fprintf(f, "%s \"class\": \"%s\"", written++ ? "," : "", cls);
        if (p.apply.roof_rows >= 0)
            std::fprintf(f, "%s \"roof_rows\": %d", written++ ? "," : "",
                         p.apply.roof_rows);
        if (p.apply.height >= 0)
            std::fprintf(f, "%s \"height\": %d", written++ ? "," : "",
                         p.apply.height);
        if (p.apply.rise >= 0.0f)
            std::fprintf(f, "%s \"rise\": %.9g", written++ ? "," : "",
                         static_cast<double>(p.apply.rise));
        std::fprintf(f, " }\n");

        std::fprintf(f, "    }%s\n", pi + 1 < set.patterns.size() ? "," : "");
    }

    std::fputs("  ]",f);
    if(set.version==vr::overrides::kTerrainVersion) vr::terrain::write(f,set.terrain);
    std::fputs("\n}\n",f);
    // Flush, then synchronise the bytes to the device BEFORE the rename. A
    // crash between the two must not leave a published file whose contents
    // were still sitting in a page cache.
    const bool wrote = !std::ferror(f) && platform_io::sync_file(f);
    const bool closed = std::fclose(f) == 0;
    vr::overrides::OverrideSet verified;
    const bool valid = wrote && closed && vr::overrides::load(temporary.c_str(), &verified);
    const bool replaced = valid && platform_io::replace_file(temporary.c_str(), path);
    if (replaced) {
        // Best effort: makes the rename itself durable on filesystems that
        // support directory fsync. Its limitation is documented in
        // platform_io.h and docs/native-wsl.md; it never invalidates a
        // replacement that already happened.
        platform_io::sync_parent_directory(path);
    } else {
        // The destination was never touched, so the previous output is intact.
        platform_io::remove_file(temporary.c_str());
        std::fprintf(stderr, "[pattern] write to %s failed; previous output preserved\n", path);
    }
    return replaced;
}

}  // namespace pattern_io
}  // namespace studio
