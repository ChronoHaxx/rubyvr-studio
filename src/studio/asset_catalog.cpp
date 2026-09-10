#include "asset_catalog.h"
#include "cutout.h"
#include "diorama.h"
#include "json_scan.h"
#include "mesh_audit.h"
#include "pattern_io.h"
#include "png_write.h"
#include "snapshot_build.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace studio {
namespace {
namespace fs = std::filesystem;
using vr::overrides::Pattern;
std::string quote(const std::string& s) {
    std::ostringstream out; out << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else out << c;
    }
    return out.str() + '"';
}
struct Occurrence {
    std::string map; int x, y, count; bool boundary;
    std::vector<pattern_io::Cell> positions; // exact flat placements, not only a representative
};
struct Asset {
    std::string id, matcher_group, kind, primary, secondary;
    Pattern pattern;
    std::vector<uint32_t> pixels;
    std::vector<Occurrence> occurrences;
};
struct MapRow {
    std::string id, type, primary, secondary, error;
    int w=0, h=0, objects=0, unexportable=0, ground_cells=0, events=0;
    std::set<std::string> graphics;
};
struct ExportFailure {
    std::string map, kind, reason, art;
    int x=0,y=0,w=0,h=0,cells=0;
    std::vector<std::string> fragments;
    bool fragments_complete=false;
};
std::string fingerprint(const Pattern& p, const std::vector<uint32_t>& pixels, const std::string& kind) {
    uint64_t hash=14695981039346656037ull;
    auto add=[&](uint32_t n) { for(int k=0;k<4;++k) { hash^=(n>>(k*8))&255; hash*=1099511628211ull; } };
    add(p.w); add(p.extent);
    for(size_t i=0;i<p.ids.size();++i) { add(p.mask[i]); add(p.mask[i]?p.ids[i]:0); }
    for(const auto& [id,tile]:p.tiles) { add(id); add(tile.attr); for(auto e:tile.entries) add(e); }
    for(auto color:pixels) add(color);
    for(unsigned char c:kind) add(c);
    std::ostringstream out; out << std::hex << std::setw(16) << std::setfill('0') << hash; return out.str();
}
const char* kind(vr::diorama::ObjectClass c) {
    switch(c) { case vr::diorama::ObjectClass::kProp:return "prop"; case vr::diorama::ObjectClass::kStructure:return "structure"; default:return "mass"; }
}
}

bool write_asset_catalog(Decomp& decomp, const char* directory) {
    std::error_code ec;
    const fs::path root=fs::absolute(directory,ec);
    if(ec) return false;
    fs::create_directories(root/"art",ec); if(ec) return false;
    fs::create_directories(root/"proposals",ec); if(ec) return false;
    fs::create_directories(root/"unexportable",ec); if(ec) return false;
    std::vector<Asset> assets;
    std::map<std::string,std::vector<size_t>> buckets;
    std::map<std::string,std::vector<size_t>> matcher_buckets;
    std::vector<MapRow> maps;
    std::vector<ExportFailure> failures;
    bool ok=true;
    auto record_failure=[&](const vr::world::Snapshot& snapshot,const MapRow& row,
                            const std::vector<pattern_io::Cell>& cells,const std::string& cls,const std::string& reason) {
        ExportFailure f;f.map=row.id;f.kind=cls;f.reason=reason;f.cells=int(cells.size());
        if(cells.empty()) { failures.push_back(f);return; }
        f.x=cells.front().x;f.y=cells.front().y;int xmax=f.x,ymax=f.y;
        for(auto c:cells) { f.x=std::min(f.x,c.x);f.y=std::min(f.y,c.y);xmax=std::max(xmax,c.x);ymax=std::max(ymax,c.y); }
        f.w=xmax-f.x+1;f.h=ymax-f.y+1;
        const std::string stem="unexportable/"+row.id+"-"+std::to_string(f.x)+"-"+std::to_string(f.y);
        const int width=f.w*16,height=f.h*16;
        std::vector<uint8_t> rgb(size_t(width)*height*3);
        for(int y=0;y<height;++y) for(int x=0;x<width;++x)
            std::fill_n(rgb.begin()+(size_t(y)*width+x)*3,3,((x/8+y/8)&1)?42:56);
        int exported=0;bool complete=true;
        // Preserve oversized terrain as explicit source fragments, each within
        // the existing file-format limit. They remain unreviewed fragments of
        // ONE failed proposal, never silently promoted to independent assets.
        for(int y=f.y;y<=ymax;y+=64) for(int x=f.x;x<=xmax;x+=64) {
            std::vector<pattern_io::Cell> chunk;
            for(auto c:cells) if(c.x>=x && c.x<x+64 && c.y>=y && c.y<y+64) chunk.push_back(c);
            if(chunk.empty()) continue;
            Pattern p;vr::cutout::Art art;
            if(!pattern_io::from_cells(snapshot,chunk,"Unreviewed source fragment",&p) || !vr::cutout::compose(snapshot,p,&art)) { complete=false;continue; }
            p.source.room=row.id;p.id=row.id+"-fragment-"+std::to_string(x)+"-"+std::to_string(y);
            const std::string path=stem+"-fragment-"+std::to_string(x)+"-"+std::to_string(y)+".json";
            vr::overrides::OverrideSet fragment;fragment.patterns.push_back(p);
            if(!pattern_io::write((root/path).string().c_str(),fragment)) { complete=false;continue; }
            f.fragments.push_back(path);exported+=int(chunk.size());
            for(int py=0;py<art.h;++py) for(int px=0;px<art.w;++px) {
                const auto color=art.pixels[size_t(py)*art.w+px].rgba;
                if(!(color>>24)) continue;
                const size_t dest=(size_t((p.source.y-f.y)*16+py)*width+(p.source.x-f.x)*16+px)*3;
                for(int c=0;c<3;++c) rgb[dest+c]=uint8_t(color>>(c*8));
            }
        }
        f.fragments_complete=complete && exported==f.cells;
        if(f.fragments_complete && png::write_rgb((root/(stem+".png")).string().c_str(),width,height,rgb.data())) f.art=stem+".png";
        failures.push_back(std::move(f));
    };
    auto collect=[&](const vr::world::Snapshot& snapshot, Pattern p, const std::string& cls,
                     const MapRow& row, int count, bool boundary, const std::vector<pattern_io::Cell>& positions) {
        vr::cutout::Art art;
        if(!vr::cutout::compose(snapshot,p,&art)) return false;
        std::vector<uint32_t> pixels; pixels.reserve(art.pixels.size());
        for(const auto& px:art.pixels) pixels.push_back(px.rgba);
        const auto key=fingerprint(p,pixels,cls);
        auto& candidates=buckets[key];
        for(size_t index:candidates) {
            auto& asset=assets[index];
            // Hashes index only: exact structure AND rendered source pixels
            // decide identity. Palette/graphics variants never disappear.
            if(asset.kind==cls && pattern_io::same_key(asset.pattern,p) && asset.pixels==pixels) {
                asset.occurrences.push_back({row.id,p.source.x,p.source.y,count,boundary,positions}); return true;
            }
        }
        Asset asset;
        asset.id="asset-"+key+(candidates.empty()?"":"-"+std::to_string(candidates.size()));
        const auto matcher_key=fingerprint(p,{},"");
        auto& matcher_candidates=matcher_buckets[matcher_key];
        for(size_t index:matcher_candidates) if(pattern_io::same_key(assets[index].pattern,p)) {
            asset.matcher_group=assets[index].matcher_group; break;
        }
        if(asset.matcher_group.empty()) asset.matcher_group="match-"+matcher_key+"-"+std::to_string(matcher_candidates.size());
        asset.kind=cls; asset.primary=row.primary; asset.secondary=row.secondary;
        p.id=asset.id; p.name=cls+" "+asset.id; p.source.room=row.id;
        asset.pattern=p; asset.pixels=std::move(pixels);
        asset.occurrences.push_back({row.id,p.source.x,p.source.y,count,boundary,positions});
        vr::overrides::OverrideSet proposal; proposal.patterns.push_back(p);
        if(!pattern_io::write((root/"proposals"/(asset.id+".json")).string().c_str(),proposal)) return false;
        std::vector<uint8_t> rgb(asset.pixels.size()*3);
        for(size_t i=0;i<asset.pixels.size();++i) {
            uint32_t rgba=asset.pixels[i];
            const uint8_t background=((i%art.w/8+i/art.w/8)&1)?42:56;
            for(int c=0;c<3;++c) rgb[i*3+c]=(rgba>>24)?uint8_t(rgba>>(8*c)):background;
        }
        if(!png::write_rgb((root/"art"/(asset.id+".png")).string().c_str(),art.w,art.h,rgb.data())) return false;
        candidates.push_back(assets.size()); matcher_candidates.push_back(assets.size()); assets.push_back(std::move(asset)); return true;
    };
    const auto map_ids=decomp.map_ids();
    for(const auto& id:map_ids) {
        MapRow row; row.id=id;
        const auto* map=decomp.map(id);
        const auto* layout=map?decomp.layout(map->layout_id):nullptr;
        if(map) {
            vr::json::Value json;
            if(vr::json::parse_file((fs::path(map->dir)/"map.json").string().c_str(),&json)) {
                if(auto* type=json.find("map_type")) row.type=type->as_str();
                if(auto* events=json.find("object_events")) for(const auto& event:events->items) {
                    ++row.events; if(auto* gfx=event.find("graphics_id")) row.graphics.insert(gfx->as_str());
                }
            } else { row.error="Map metadata could not be read"; ok=false; }
        }
        if(layout) { row.w=layout->width; row.h=layout->height; row.primary=layout->primary_tileset; row.secondary=layout->secondary_tileset; }
        vr::world::Snapshot snapshot; BuildInfo info; vr::diorama::ObjectModel objects;
        if(!map || !layout || !build_snapshot(decomp,id,&snapshot,&info) || !vr::diorama::segment(snapshot,&objects)) {
            row.error="Map, tileset, snapshot or segmentation failed; see catalog log"; ok=false; maps.push_back(row); continue;
        }
        // Count only the current layout, not duplicated connection padding.
        // Objects crossing the boundary are retained and explicitly flagged.
        const int x0=7,y0=7,x1=x0+row.w,y1=y0+row.h;
        std::set<int> selected;
        std::map<uint16_t,std::vector<pattern_io::Cell>> ground;
        for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) {
            int object=objects.object_at(x,y);
            if(object>=0) selected.insert(object);
            else if(snapshot.cell(x,y)!=vr::world::kGridUndefined) {
                ++row.ground_cells; auto& item=ground[snapshot.metatile_id(x,y)];
                item.push_back({x,y});
            }
        }
        row.objects=int(selected.size());
        for(int index:selected) {
            const auto& object=objects.objects[index];
            int first_x=-1,first_y=-1;
            for(int y=object.y;y<object.y+object.extent && first_x<0;++y)
                for(int x=object.x;x<object.x+object.w;++x) if(objects.object_at(x,y)==index) { first_x=x; first_y=y; break; }
            Pattern p;
            const bool derived=first_x>=0 && pattern_io::from_object(snapshot,objects,first_x,first_y,"proposal",&p);
            if(!derived || !collect(snapshot,p,kind(object.cls),row,1,object.x<x0 || object.y<y0 || object.x+object.w>x1 || object.y+object.extent>y1,{})) {
                ++row.unexportable; ok=false;
                std::vector<pattern_io::Cell> cells;
                for(int y=object.y;y<object.y+object.extent;++y) for(int x=object.x;x<object.x+object.w;++x)
                    if(objects.object_at(x,y)==index) cells.push_back({x,y});
                record_failure(snapshot,row,cells,kind(object.cls),object.w>64 || object.extent>64?
                    "Source object exceeds the 64-cell pattern dimension limit":derived?"Source art or proposal write failed":"Pattern derivation failed");
            }
        }
        for(const auto& [tile,item]:ground) {
            Pattern p;
            if(!pattern_io::from_cells(snapshot,{item.front()},"ground proposal",&p) ||
               !collect(snapshot,p,"flat",row,int(item.size()),false,item)) {
                ++row.unexportable;ok=false;record_failure(snapshot,row,{item.front()},"flat","Source art or proposal export failed");
            }
        }
        maps.push_back(row);
        std::fprintf(stderr,"[catalog] %zu/%zu %s: %d objects, %zu flat drawings, %zu families\n",maps.size(),map_ids.size(),id.c_str(),row.objects,ground.size(),assets.size());
    }
    std::ofstream out(root/"catalog.json",std::ios::binary);
    if(!out) return false;
    int failed_maps=0,unexportable=0,failed_export_maps=0,events=0;
    for(const auto& row:maps) { failed_maps+=!row.error.empty(); unexportable+=row.unexportable; failed_export_maps+=row.unexportable>0; events+=row.events; }
    out << "{\n\"version\":2,\"scope\":\"all decomp maps: static drawings and exact static placements; event references only\","
        << "\"visual_review\":\"UNREVIEWED\",\"release_ready\":false,"
        << "\"source_load_complete\":" << (failed_maps==0?"true":"false")
        << ",\"export_complete\":" << (ok?"true":"false") << ",\"failed_export_maps\":" << failed_export_maps
        << ",\"map_count\":" << maps.size() << ",\"failed_maps\":" << failed_maps
        << ",\"unexportable_proposals\":" << unexportable << ",\"asset_count\":" << assets.size()
        << ",\"object_events\":" << events << ",\"maps\":[\n";
    bool comma=false;
    for(const auto& row:maps) {
        if(comma) out << ",\n"; comma=true;
        out << "{\"id\":" << quote(row.id) << ",\"type\":" << quote(row.type)
            << ",\"primary\":" << quote(row.primary) << ",\"secondary\":" << quote(row.secondary)
            << ",\"width\":" << row.w << ",\"height\":" << row.h << ",\"objects\":" << row.objects
            << ",\"flat_cells\":" << row.ground_cells << ",\"object_events\":" << row.events
            << ",\"unexportable\":" << row.unexportable << ",\"error\":" << quote(row.error) << ",\"graphics\":[";
        bool gc=false; for(const auto& gfx:row.graphics) { if(gc) out << ','; gc=true; out << quote(gfx); } out << "]}";
    }
    out << "],\n\"unexportable_details\":[";comma=false;
    for(const auto& f:failures) {
        if(comma) out << ',';comma=true;
        out << "{\"map\":" << quote(f.map) << ",\"kind\":" << quote(f.kind) << ",\"reason\":" << quote(f.reason)
            << ",\"x\":" << f.x << ",\"y\":" << f.y << ",\"width\":" << f.w << ",\"height\":" << f.h
            << ",\"member_cells\":" << f.cells << ",\"art\":" << quote(f.art)
            << ",\"fragments_complete\":" << (f.fragments_complete?"true":"false") << ",\"fragments\":[";
        bool comma=false;for(const auto& path:f.fragments) { if(comma) out << ',';comma=true;out << quote(path); }out << "]}";
    }
    out << "],\n\"assets\":[\n"; comma=false;
    for(const auto& asset:assets) {
        if(comma) out << ",\n"; comma=true;
        out << "{\"id\":" << quote(asset.id) << ",\"kind\":" << quote(asset.kind)
            << ",\"matcher_group\":" << quote(asset.matcher_group)
            << ",\"status\":\"unreviewed\",\"width\":" << asset.pattern.w << ",\"height\":" << asset.pattern.extent
            << ",\"primary\":" << quote(asset.primary) << ",\"secondary\":" << quote(asset.secondary)
            << ",\"art\":" << quote("art/"+asset.id+".png") << ",\"proposal\":" << quote("proposals/"+asset.id+".json")
            << ",\"occurrences\":[";
        bool oc=false; for(const auto& place:asset.occurrences) {
            if(oc) out << ','; oc=true;
            out << "{\"map\":" << quote(place.map) << ",\"x\":" << place.x << ",\"y\":" << place.y
                << ",\"count\":" << place.count << ",\"crosses_layout_boundary\":" << (place.boundary?"true":"false");
            if(!place.positions.empty()) {
                out << ",\"positions\":[";
                for(size_t i=0;i<place.positions.size();++i) {
                    if(i) out << ',';
                    out << '[' << place.positions[i].x << ',' << place.positions[i].y << ']';
                }
                out << ']';
            }
            out << '}';
        }
        out << "]}";
    }
    out << "]\n}\n"; out.close();
    std::fprintf(stderr,"[catalog] %zu maps, %d failed; %zu source-art families, %d unexportable. Visual review remains pending.\n",maps.size(),failed_maps,assets.size(),unexportable);
    return ok && bool(out);
}

bool audit_asset_pack(Decomp& decomp, const vr::overrides::OverrideSet& pack, const char* report_path) {
    struct Result {
        MeshAudit mesh;
        bool source_matches=false, emitted=false;
        std::vector<Occurrence> accepted, rejected;
    };
    std::vector<Result> results(pack.patterns.size());
    std::vector<std::string> failed_maps;
    const auto maps=decomp.map_ids();
    bool ok=!pack.empty();
    for(const auto& id:maps) {
        vr::world::Snapshot snapshot; BuildInfo info;
        const auto* map=decomp.map(id);
        const auto* layout=map?decomp.layout(map->layout_id):nullptr;
        if(!layout || !build_snapshot(decomp,id,&snapshot,&info)) {
            failed_maps.push_back(id); ok=false; continue;
        }
        const auto claims=vr::overrides::resolve(snapshot,pack);
        auto collect=[&](const auto& matches, bool accepted) {
            for(const auto& m:matches) {
                auto& result=results.at(m.pattern);
                const auto& p=pack.patterns[m.pattern];
                // Keep padding matches as evidence, with a separate flag so
                // stitched neighbours are never counted as new game assets.
                bool boundary=m.x<7 || m.y<7 || m.x+p.w>7+layout->width || m.y+p.extent>7+layout->height;
                (accepted?result.accepted:result.rejected).push_back({id,m.x,m.y,1,boundary});
                if(accepted && id==p.source.room && m.x==p.source.x && m.y==p.source.y) result.source_matches=true;
            }
        };
        collect(claims.accepted,true); collect(claims.rejected,false);
        for(size_t i=0;i<pack.patterns.size();++i) {
            const auto& p=pack.patterns[i];
            if(p.source.room!=id) continue;
            auto& result=results[i];
            std::vector<vr::diorama::AuthoredVertex> mesh;
            result.emitted=vr::diorama::inspect_authored_mesh(snapshot,p,&mesh);
            if(result.emitted) result.mesh=audit_mesh(mesh);
        }
    }
    for(size_t i=0;i<results.size();++i) {
        const auto& r=results[i];
        const bool geometry_ok=pack.patterns[i].ground_only()?r.mesh.triangles==0:
            r.mesh.closed_connected() && std::isfinite(r.mesh.volume) && r.mesh.volume>0;
        bool pass=r.emitted && r.source_matches && r.rejected.empty() && geometry_ok;
        ok &= pass;
        std::fprintf(stderr,"[asset-audit] %s %s: %zu triangles, %zu boundary, %zu nonmanifold, %zu components, volume %.9g; %zu accepted, %zu rejected\n",
            pass?"PASS":"FAIL",pack.patterns[i].id.c_str(),r.mesh.triangles,r.mesh.boundary,r.mesh.nonmanifold,r.mesh.components,r.mesh.volume,r.accepted.size(),r.rejected.size());
    }
    std::ofstream out(report_path,std::ios::binary);
    if(!out) return false;
    out << "{\"status\":" << quote(ok?"PASS":"FAIL") << ",\"scope\":\"closed solid topology at authored source; matching on every decomp map\","
        << "\"visual_review\":\"pending\",\"release_ready\":false,\"maps_checked\":" << maps.size() << ",\"failed_maps\":[";
    bool comma=false;
    for(const auto& id:failed_maps) { if(comma) out << ','; comma=true; out << quote(id); }
    out << "],\"assets\":["; comma=false;
    for(size_t i=0;i<results.size();++i) {
        if(comma) out << ','; comma=true;
        const auto& r=results[i]; const auto& m=r.mesh;
        out << "{\"id\":" << quote(pack.patterns[i].id) << ",\"source_matches\":" << (r.source_matches?"true":"false")
            << ",\"emitted\":" << (r.emitted?"true":"false") << ",\"closed_connected\":" << (m.closed_connected()?"true":"false")
            << ",\"ground_only\":" << (pack.patterns[i].ground_only()?"true":"false")
            << ",\"triangles\":" << m.triangles << ",\"degenerate\":" << m.degenerate << ",\"duplicate\":" << m.duplicate
            << ",\"boundary\":" << m.boundary << ",\"nonmanifold\":" << m.nonmanifold << ",\"winding\":" << m.winding
            << ",\"components\":" << m.components << ",\"volume\":" << std::setprecision(12) << m.volume;
        auto write_places=[&](const char* key, const auto& places) {
            out << ',' << quote(key) << ":[";
            bool comma=false;
            for(const auto& p:places) {
                if(comma) out << ','; comma=true;
                out << "{\"map\":" << quote(p.map) << ",\"x\":" << p.x << ",\"y\":" << p.y
                    << ",\"crosses_layout_boundary\":" << (p.boundary?"true":"false") << '}';
            }
            out << ']';
        };
        write_places("accepted",r.accepted); write_places("rejected",r.rejected); out << '}';
    }
    out << "]}\n"; out.close(); return ok && bool(out);
}
}
