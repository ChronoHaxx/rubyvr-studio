#include "cutout.h"
// overrides.cpp â€” see overrides.h for the format and why it is shaped this way.

#include "overrides.h"
#include "terrain.h"

#include "json_scan.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <set>
#include <initializer_list>

namespace vr {
namespace overrides {
namespace {

bool integer(const json::Value* v, int lo, int hi) {
    return v && v->type == json::Value::Type::Number && std::isfinite(v->number) &&
           v->number >= lo && v->number <= hi && std::floor(v->number) == v->number;
}

bool fields(const json::Value& v, std::initializer_list<const char*> allowed) {
    if (!v.is_object()) return false;
    for (const auto& key : v.keys) {
        if (key == "_comment") continue;
        bool known = false;
        for (const auto name : allowed) known = known || key == name;
        if (!known) {
            std::fprintf(stderr, "[override] unsupported field: %s\n", key.c_str());
            return false;
        }
    }
    return true;
}

bool unique_keys(const json::Value& value) {
    if (value.is_object()) {
        std::set<std::string> keys;
        for (size_t i = 0; i < value.keys.size(); ++i)
            if (!keys.insert(value.keys[i]).second || !unique_keys(value.values[i])) return false;
    }
    if (value.is_array()) for (const auto& item : value.items) if (!unique_keys(item)) return false;
    return true;
}

bool read_mask(const json::Value* v, Cutout* out) {
    if(!v || !fields(*v,{"w","h","first","runs"}) || !integer(v->find("w"),1,1024) ||
       !integer(v->find("h"),1,1024) || !integer(v->find("first"),0,1)) return false;
    const auto runs=v->find("runs");
    if(!runs || !runs->is_array() || runs->items.size()>1048576) return false;
    std::vector<uint32_t> lengths;
    for(const auto& n:runs->items) { if(!integer(&n,1,1048576)) return false; lengths.push_back(n.as_int()); }
    return cutout::decode(v->find("w")->as_int(),v->find("h")->as_int(),v->find("first")->as_int(),lengths,out);
}
bool read_region(const json::Value* v, ArtRegion* out) {
    if(!v || !v->is_array() || v->items.size()!=4) return false;
    for(int i=0;i<4;++i) { if(!integer(&v->items[i],0,1024)) return false; (*out)[i]=v->items[i].as_int(); }
    return true;
}

// Read a JSON array of numbers into `out`, requiring exactly `want` of them.
bool read_u16_array(const json::Value* v, size_t want, const char* what,
                    const char* name, std::vector<uint16_t>* out) {
    if (!v || !v->is_array()) {
        std::fprintf(stderr, "[override] %s: %s is missing or not an array\n",
                     name, what);
        return false;
    }
    if (v->items.size() != want) {
        std::fprintf(stderr, "[override] %s: %s has %zu entries, expected %zu\n",
                     name, what, v->items.size(), want);
        return false;
    }
    out->clear();
    out->reserve(want);
    for (const json::Value& e : v->items) {
        if (!integer(&e, 0, 65535)) return false;
        out->push_back(static_cast<uint16_t>(e.as_int()));
    }
    return true;
}

bool parse_apply(const json::Value* v, const char* name, Apply* out) {
    if (!v) return true;   // authoring nothing is legal, if pointless
    if (!v->is_object()) {
        std::fprintf(stderr, "[override] %s: apply is not an object\n", name);
        return false;
    }
    if (!fields(*v, {"class", "roof_rows", "height", "rise"})) return false;
    if (const json::Value* c = v->find("class")) {
        const std::string s = c->as_str();
        if      (s == "prop")      out->cls = ApplyClass::kProp;
        else if (s == "structure") out->cls = ApplyClass::kStructure;
        else if (s == "mass")      out->cls = ApplyClass::kMass;
        else {
            std::fprintf(stderr,
                         "[override] %s: apply.class is \"%s\"; expected prop, "
                         "structure or mass\n", name, s.c_str());
            return false;
        }
    }
    if (const auto r = v->find("roof_rows")) {
        if (!integer(r, -1, 64)) return false;
        out->roof_rows = r->as_int();
    }
    if (const auto h = v->find("height")) {
        if (!integer(h, -1, 256)) return false;
        out->height = h->as_int();
    }
    if (const auto r = v->find("rise")) {
        if (r->type != json::Value::Type::Number || !std::isfinite(r->number) ||
            r->number < -1 || r->number > 64) return false;
        out->rise = static_cast<float>(r->number);
    }
    return true;
}

bool parse_tiles(const json::Value* v, const char* name, Pattern* p) {
    if (!v || !v->is_object()) {
        std::fprintf(stderr,
                     "[override] %s: tiles is missing or not an object. It is "
                     "the tileset check, not an optional extra\n", name);
        return false;
    }
    for (size_t i = 0; i < v->keys.size(); ++i) {
        const std::string& key = v->keys[i];
        if (key.empty() || key.size() > 4 ||
            key.find_first_not_of("0123456789") != std::string::npos) return false;
        const int id = std::atoi(key.c_str());
        if (key != std::to_string(id)) return false;
        if (id < 0 || id >= world::kMetatilesTotal) {
            std::fprintf(stderr, "[override] %s: tiles key \"%s\" is not a "
                         "metatile id\n", name, v->keys[i].c_str());
            return false;
        }
        const json::Value& def = v->values[i];
        if (!fields(def, {"entries", "attr"})) return false;
        const json::Value* entries = def.find("entries");
        const json::Value* attr    = def.find("attr");
        if (!entries || !entries->is_array() ||
            entries->items.size() != world::kTilesPerMetatile || !attr) {
            std::fprintf(stderr,
                         "[override] %s: tiles[%d] needs %d entries and an attr\n",
                         name, id, world::kTilesPerMetatile);
            return false;
        }
        TileDef d;
        for (int k = 0; k < world::kTilesPerMetatile; ++k) {
            if (!integer(&entries->items[k], 0, 65535)) return false;
            d.entries[k] = static_cast<uint16_t>(entries->items[k].as_int());
        }
        if (!integer(attr, 0, 65535)) return false;
        d.attr = static_cast<uint16_t>(attr->as_int());
        p->tiles[static_cast<uint16_t>(id)] = d;
    }
    return true;
}

// Does this snapshot's tileset actually contain the metatiles the pattern was
// authored against? Checked ONCE per pattern: if the answer is no, the pattern
// cannot match anywhere, and testing it at every cell would be wasted work.
bool tileset_matches(const world::Snapshot& s, const Pattern& p) {
    for (const auto& kv : p.tiles) {
        const uint16_t id = kv.first;
        const size_t base = static_cast<size_t>(id) * world::kTilesPerMetatile;
        if (base + world::kTilesPerMetatile > s.metatiles.size()) return false;
        if (id >= s.attributes.size()) return false;
        if (s.attributes[id] != kv.second.attr) return false;
        for (int k = 0; k < world::kTilesPerMetatile; ++k)
            if (s.metatiles[base + k] != kv.second.entries[k]) return false;
    }
    return true;
}

// Do the member cells at this origin carry the pattern's ids?
bool cells_match(const world::Snapshot& s, const Pattern& p, int ox, int oy) {
    for (int k = 0; k < p.cells(); ++k) {
        if (!p.mask[k]) continue;
        const int c = k % p.w, r = k / p.w;
        const uint16_t raw = s.cell(ox + c, oy + r);
        if (raw == world::kGridUndefined) return false;
        if ((raw & world::kMetatileIdMask) != p.ids[k]) return false;
    }
    return true;
}

}  // namespace

static bool load_data(const char* path, OverrideSet* out) {
    if (!out) return false;
    *out = OverrideSet{};

    json::Value doc;
    if (!json::parse_file(path, &doc) || !fields(doc, {"version", "patterns", "terrain"}) ||
        !unique_keys(doc)) return false;

    const json::Value* ver = doc.find("version");
    if (!integer(ver, 0, 65535)) {
        std::fprintf(stderr, "[override] %s has no version field\n", path);
        return false;
    }
    out->version = static_cast<uint32_t>(ver->as_int());
    if (!supported_version(out->version)) {
        // REFUSED, not migrated. See the header.
        std::fprintf(stderr,
                     "[override] %s is version %u, this build reads version %u\n",
                     path, out->version, kVersion);
        return false;
    }

    if(const auto* t=doc.find("terrain")) {
        if(out->version!=kTerrainVersion || !terrain::read(*t,&out->terrain)) return false;
    } else if(out->version==kTerrainVersion) return false;
    const json::Value* arr = doc.find("patterns");
    if (!arr || !arr->is_array()) {
        std::fprintf(stderr, "[override] %s has no patterns array\n", path);
        return false;
    }

    if (arr->items.size() > 4096) return false;
    std::set<std::string> identities;
    for (const json::Value& e : arr->items) {
        if (!fields(e, {"name", "id", "source", "w", "extent", "ids", "mask", "tiles", "apply", "cutout", "parts", "model_seeded", "voxel", "follow_ground"}))
            return false;
        Pattern p;
        if (const auto name = e.find("name")) {
            if (name->type != json::Value::Type::String || name->string.size() > 256) return false;
            p.name = name->string;
        } else p.name = "(unnamed)";
        if (const auto id = e.find("id")) {
            if (id->type != json::Value::Type::String || id->string.empty() ||
                id->string.size() > 128 || !identities.insert(id->string).second) return false;
            p.id = id->string;
        }
        if (const auto source = e.find("source")) {
            if (!fields(*source, {"room", "x", "y", "coordinates"})) return false;
            const auto room = source->find("room"), coords = source->find("coordinates");
            const auto x = source->find("x"), y = source->find("y");
            if (!room || room->type != json::Value::Type::String ||
                room->string.rfind("MAP_", 0) != 0 || room->string.size() > 128 ||
                !coords || coords->type != json::Value::Type::String ||
                coords->string != "backup-map" || !integer(x, 0, 1023) || !integer(y, 0, 1023))
                return false;
            p.source = {room->string, x->as_int(), y->as_int()};
        }
        if (!integer(e.find("w"), 1, 64) || !integer(e.find("extent"), 1, 64)) return false;
        p.w      = e.find("w")      ? e.find("w")->as_int()      : 0;
        p.extent = e.find("extent") ? e.find("extent")->as_int() : 0;

        if (p.w <= 0 || p.extent <= 0 || p.w > 64 || p.extent > 64) {
            std::fprintf(stderr, "[override] %s: implausible size %dx%d\n",
                         p.name.c_str(), p.w, p.extent);
            return false;
        }
        const size_t n = static_cast<size_t>(p.w) * p.extent;

        if (!read_u16_array(e.find("ids"), n, "ids", p.name.c_str(), &p.ids))
            return false;

        std::vector<uint16_t> mask16;
        if (!read_u16_array(e.find("mask"), n, "mask", p.name.c_str(), &mask16))
            return false;
        for (size_t k = 0; k < n; ++k) {
            if (mask16[k] > 1 || p.ids[k] >= world::kMetatilesTotal) return false;
            if (!mask16[k]) p.ids[k] = 0;
        }
        p.mask.assign(mask16.begin(), mask16.end());

        if (!parse_tiles(e.find("tiles"), p.name.c_str(), &p)) return false;
        if (!parse_apply(e.find("apply"), p.name.c_str(), &p.apply)) return false;
        if (const auto image = e.find("cutout")) {
            if (!fields(*image, {"w", "h", "first", "runs"}) ||
                !integer(image->find("w"), 1, 1024) || !integer(image->find("h"), 1, 1024) ||
                !integer(image->find("first"), 0, 1)) return false;
            const auto runs = image->find("runs");
            if (!runs || !runs->is_array() || runs->items.size() > 1048576) return false;
            std::vector<uint32_t> lengths;
            for (const auto& run : runs->items) {
                if (!integer(&run, 1, 1048576)) return false;
                lengths.push_back(uint32_t(run.as_int()));
            }
            Cutout decoded;
            if (!cutout::decode(image->find("w")->as_int(), image->find("h")->as_int(),
                                image->find("first")->as_int(), lengths, &decoded)) return false;
            p.cutout = std::move(decoded);
            if (!cutout::valid(p)) return false;
        }


        if(const auto v=e.find("voxel")) {
            if(out->version<kVoxelVersion || !fields(*v,{"pixels_per_cell","ground","shadow"}) ||
               !integer(v->find("pixels_per_cell"),16,16)) return false;
            Voxel voxel;
            if(!read_mask(v->find("ground"),&voxel.ground) || !read_mask(v->find("shadow"),&voxel.shadow)) return false;
            p.voxel=std::move(voxel);
        }
        if (const auto seeded=e.find("model_seeded")) {
            if (seeded->type!=json::Value::Type::Bool) return false;
            p.model_seeded=seeded->boolean;
        }
        if(const auto follow=e.find("follow_ground")) {
            if(follow->type!=json::Value::Type::Bool || !p.voxel) return false;
            p.follow_ground=follow->boolean;
        }
        if (const auto parts=e.find("parts")) {
            if (!parts->is_array() || parts->items.size()>64) return false;
            std::set<std::string> ids;
            auto vector=[](const json::Value* v, part_geometry::Vec* out) {
                if (!v || !v->is_array() || v->items.size()!=3) return false;
                for (const auto& n:v->items) if (n.type!=json::Value::Type::Number ||
                    !std::isfinite(n.number) || std::abs(n.number)>360) return false;
                *out={float(v->items[0].number),float(v->items[1].number),float(v->items[2].number)};
                return true;
            };
            for (const auto& item:parts->items) {
                if (!fields(item,{"id","name","kind","position","size","angles","axis","direction","art_region","surfaces","side_art","local_mask"})) return false;
                Part part;
                const auto id=item.find("id"),name=item.find("name"),kind=item.find("kind");
                if (!id || id->type!=json::Value::Type::String || id->string.empty() ||
                    id->string.size()>128 || !ids.insert(id->string).second ||
                    !name || name->type!=json::Value::Type::String || name->string.size()>256 ||
                    !kind || kind->type!=json::Value::Type::String) return false;
                part.id=id->string; part.name=name->string;
                if (kind->string=="box") part.kind=PartKind::Box;
                else if (kind->string=="billboard" && p.cutout) part.kind=PartKind::Billboard;
                else if (kind->string=="wedge") part.kind=PartKind::Wedge;
                else return false;
                if(part.kind==PartKind::Wedge) {
                    const auto axis=item.find("axis"),direction=item.find("direction");
                    if(!axis || axis->type!=json::Value::Type::String || (axis->string!="x" && axis->string!="z") ||
                       !integer(direction,-1,1) || direction->as_int()==0) return false;
                    part.wedge_axis=axis->string=="x"?0:2;part.wedge_direction=direction->as_int();
                } else if(item.find("axis") || item.find("direction")) return false;
                if(const auto region=item.find("art_region")) {
                    if((part.kind==PartKind::Billboard && !p.voxel) || !region->is_array() || region->items.size()!=4) return false;
                    for(size_t i=0;i<4;++i) {
                        if(!integer(&region->items[i],0,1024)) return false;
                        part.art_region[i]=region->items[i].as_int();
                    }
                }
                if(const auto surfaces=item.find("surfaces")) {
                    if(!p.voxel || !surfaces->is_array() || surfaces->items.size()!=6) return false;
                    for(const auto& value:surfaces->items) {
                        Surface surface;
                        if(!fields(value,{"region","flip_u","flip_v","offset"}) || !read_region(value.find("region"),&surface.region)) return false;
                        if(const auto offset=value.find("offset")) {
                            if(!offset->is_array() || offset->items.size()!=2) return false;
                            for(int k=0;k<2;++k) {if(!integer(&offset->items[k],-1024,1024)) return false;surface.offset[k]=offset->items[k].as_int();}
                        }
                        for(const auto key:{"flip_u","flip_v"}) if(const auto b=value.find(key)) {
                            if(b->type!=json::Value::Type::Bool) return false;
                            (std::string(key)=="flip_u"?surface.flip_u:surface.flip_v)=b->boolean;
                        }
                        part.surfaces.push_back(surface);
                    }
                }
                if(const auto mask=item.find("local_mask")) {
                    Cutout value;if(!p.voxel || !read_mask(mask,&value)) return false;part.local_mask=std::move(value);
                }
                if(const auto value=item.find("side_art")) {
                    Surface surface;
                    if(!p.voxel || !fields(*value,{"region","flip_u","flip_v","offset"}) || !read_region(value->find("region"),&surface.region)) return false;
                    if(const auto offset=value->find("offset")) {
                        if(!offset->is_array() || offset->items.size()!=2) return false;
                        for(int k=0;k<2;++k) {if(!integer(&offset->items[k],-1024,1024)) return false;surface.offset[k]=offset->items[k].as_int();}
                    }
                    for(const auto key:{"flip_u","flip_v"}) if(const auto b=value->find(key)) {
                        if(b->type!=json::Value::Type::Bool) return false;
                        (std::string(key)=="flip_u"?surface.flip_u:surface.flip_v)=b->boolean;
                    }
                    part.side_art=surface;
                }
                if (!vector(item.find("position"),&part.transform.position) ||
                    !vector(item.find("size"),&part.transform.size) ||
                    !vector(item.find("angles"),&part.transform.angles) || !part_geometry::valid(part.transform)) return false;
                p.parts.push_back(std::move(part));
            }
        }

        // The anchor is the first member cell; matching keys on it.
        for (int k = 0; k < p.cells(); ++k)
            if (p.mask[k]) { p.anchor = k; break; }
        if (p.anchor < 0) {
            std::fprintf(stderr, "[override] %s: mask has no member cells\n",
                         p.name.c_str());
            return false;
        }

        // Every id the mask actually references must carry a definition, or the
        // tileset check has a hole in exactly the place it matters.
        for (int k = 0; k < p.cells(); ++k) {
            if (!p.mask[k]) continue;
            if (p.tiles.find(p.ids[k]) == p.tiles.end()) {
                std::fprintf(stderr,
                             "[override] %s: member id %u has no entry in tiles\n",
                             p.name.c_str(), p.ids[k]);
                return false;
            }
        }
        if (!valid_parts(p)) return false;
        out->patterns.push_back(std::move(p));
    }

    std::fprintf(stderr, "[override] loaded %s: %zu pattern(s), version %u\n",
                 path, out->patterns.size(), out->version);
    return true;
}

bool valid_parts(const Pattern& p) {
    if(p.follow_ground && !p.voxel) return false;
    auto valid_mask=[](const Cutout& c,int w,int h) {
        return c.w==w && c.h==h && c.opacity.size()==size_t(w)*h &&
            std::all_of(c.opacity.begin(),c.opacity.end(),[](uint8_t a){return a<=1;});
    };
    if(p.voxel) {
        if(p.voxel->pixels_per_cell!=16 || !p.cutout || !cutout::valid(p) ||
           !valid_mask(p.voxel->ground,p.w*16,p.extent*16) || !valid_mask(p.voxel->shadow,p.w*16,p.extent*16)) return false;
        for(size_t i=0;i<p.cutout->opacity.size();++i) {
            const int roles=p.cutout->opacity[i]+p.voxel->ground.opacity[i]+p.voxel->shadow.opacity[i];
            const int cell=int(i/(p.w*16))/16*p.w+int(i%(p.w*16))/16;
            if(roles!=(p.mask[cell]?1:0)) return false;
        }
    }
    if(p.parts.empty()) return true;
    if(p.parts.size()>64 || p.w<1 || p.w>64 || p.extent<1 || p.extent>64) return false;
    if(!p.voxel && size_t(p.w)*p.extent*256*6*p.parts.size()>262144) return false;
    auto region_valid=[&](const ArtRegion& r) {
        return r[0]>=0 && r[1]>=0 && r[2]>0 && r[3]>0 && r[0]+r[2]<=p.w*16 && r[1]+r[3]<=p.extent*16;
    };
    part_geometry::Vec lower{1000,1000,1000},upper{-1000,-1000,-1000};
    std::set<std::string> ids;
    for(const auto& part:p.parts) {
        if(part.id.empty() || part.id.size()>128 || !ids.insert(part.id).second || part.name.size()>256 ||
           !part_geometry::valid(part.transform)) return false;
        if(part.kind==PartKind::Billboard) { if(!p.cutout) return false; }
        else if(part.kind!=PartKind::Box && part.kind!=PartKind::Wedge) return false;
        if(part.kind==PartKind::Wedge) {
            if((part.wedge_axis!=0 && part.wedge_axis!=2) || (part.wedge_direction!=1 && part.wedge_direction!=-1)) return false;
        } else if(part.wedge_axis!=0 || part.wedge_direction!=1) return false;
        if(part.art_region!=std::array<int,4>{}) {
            if(part.kind==PartKind::Billboard && !p.voxel) return false;
            const auto& r=part.art_region;
            for(int v:r) if(v<0 || v>1024) return false;
            if(r[2]<1 || r[3]<1 || r[0]+r[2]>p.w*16 || r[1]+r[3]>p.extent*16) return false;
        }
        if(!p.voxel) { if(!part.surfaces.empty() || part.local_mask || part.side_art) return false; }
        else {
            if(part.kind==PartKind::Billboard) {
                const auto r=part.art_region==ArtRegion{}?ArtRegion{0,0,p.w*16,p.extent*16}:part.art_region;
                if(!part.surfaces.empty() || (part.local_mask && !valid_mask(*part.local_mask,r[2],r[3])) ||
                   std::abs(part.transform.size.x*16-r[2])>1e-4f || std::abs(part.transform.size.y*16-r[3])>1e-4f) return false;
            } else {
                if(part.local_mask || part.side_art || part.art_region!=ArtRegion{} || part.surfaces.size()!=6) return false;
                for(const auto& surface:part.surfaces) {
                    for(int n:surface.offset) if(n < -1024 || n > 1024) return false;
                    const auto& r=surface.region;if(!region_valid(r)) return false;
                    for(int y=r[1];y<r[1]+r[3];++y) for(int x=r[0];x<r[0]+r[2];++x)
                        if(!p.cutout->opacity[size_t(y)*p.w*16+x]) return false;
                }
            }
            if(part.side_art) {
                for(int n:part.side_art->offset) if(n < -1024 || n > 1024) return false;
                const auto& r=part.side_art->region;if(!region_valid(r)) return false;
                for(int y=r[1];y<r[1]+r[3];++y) for(int x=r[0];x<r[0]+r[2];++x)
                    if(!p.cutout->opacity[size_t(y)*p.w*16+x]) return false;
            }
            const auto& t=part.transform;
            for(int k=0;k<8;++k) {
                const auto q=part_geometry::to_group({(k&1)?t.size.x:0,(k&2)?t.size.y:0,(k&4)?t.size.z/2:-t.size.z/2},t);
                lower={std::min(lower.x,q.x),std::min(lower.y,q.y),std::min(lower.z,q.z)};
                upper={std::max(upper.x,q.x),std::max(upper.y,q.y),std::max(upper.z,q.z)};
            }
        }
    }
    if(p.voxel) {
        const double cells=(std::ceil(upper.x*16)-std::floor(lower.x*16))*(std::ceil(upper.y*16)-std::floor(lower.y*16))*(std::ceil(upper.z*16)-std::floor(lower.z*16));
        if(cells>1048576 || cells*p.parts.size()>16777216) return false;
    }
    return true;
}

bool load(const char* path, OverrideSet* out) {
    if (!out) return false;
    OverrideSet candidate;
    if (!load_data(path, &candidate)) return false;
    *out = std::move(candidate);
    return true;
}

std::vector<Match> find(const world::Snapshot& s, const OverrideSet& set) {
    std::vector<Match> out;
    if (!s.valid || set.empty()) return out;

    for (size_t pi = 0; pi < set.patterns.size(); ++pi) {
        const Pattern& p = set.patterns[pi];

        if (!tileset_matches(s, p)) {
            // Not an error. A file authored for another tileset pair simply
            // does not apply here, and saying so beats matching silently.
            std::fprintf(stderr,
                         "[override] %s: tileset does not match this map, "
                         "pattern skipped\n", p.name.c_str());
            continue;
        }

        const int ax = p.anchor % p.w, ay = p.anchor / p.w;
        const uint16_t aid = p.ids[p.anchor];
        int found = 0;

        for (int cy = 0; cy < s.height; ++cy)
            for (int cx = 0; cx < s.width; ++cx) {
                if (s.metatile_id(cx, cy) != aid) continue;
                const int ox = cx - ax, oy = cy - ay;
                if (ox < 0 || oy < 0 ||
                    ox + p.w > s.width || oy + p.extent > s.height) continue;
                if (!cells_match(s, p, ox, oy)) continue;
                Match m;
                m.pattern = static_cast<int>(pi);
                m.x = ox;
                m.y = oy;
                out.push_back(m);
                ++found;
            }

        std::fprintf(stderr, "[override] %s: %d match(es)\n", p.name.c_str(), found);
    }

    // Sorted by ORIGIN so that overlap resolution downstream is deterministic
    // rather than a function of which pattern happened to be listed first.
    std::sort(out.begin(), out.end(), [](const Match& a, const Match& b) {
        if (a.y != b.y) return a.y < b.y;
        if (a.x != b.x) return a.x < b.x;
        return a.pattern < b.pattern;
    });
    return out;
}

Claims resolve(const world::Snapshot& s, const OverrideSet& set) {
    Claims claims;
    if (!s.valid || s.width <= 0 || s.height <= 0) return claims;
    claims.owner.assign(size_t(s.width) * s.height, -1);
    for (const auto& match : find(s, set)) {
        const auto& pattern = set.patterns[size_t(match.pattern)];
        bool clash = false;
        for (int k = 0; k < pattern.cells(); ++k)
            if (pattern.mask[k] && claims.owner[size_t(match.y + k / pattern.w) * s.width +
                                                match.x + k % pattern.w] >= 0) { clash = true; break; }
        if (clash) { claims.rejected.push_back(match); continue; }
        for (int k = 0; k < pattern.cells(); ++k)
            if (pattern.mask[k]) claims.owner[size_t(match.y + k / pattern.w) * s.width +
                                              match.x + k % pattern.w] = match.pattern;
        claims.accepted.push_back(match);
    }
    return claims;
}

}  // namespace overrides
}  // namespace vr
