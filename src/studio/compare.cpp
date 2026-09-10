// compare.cpp — Milestone 1's gate: is the disk-built map the same map?
//
// WHAT THIS ANSWERS, and why it is worth an executable of its own:
//
//   rubyvr_studio will let a person author roof shapes against geometry it
//   renders itself. If the Snapshot it builds from pokeruby's files differs
//   from the one capture() reads out of the running game, then every authored
//   decision is made against geometry the game will not reproduce — silently,
//   and for as long as nobody checks. So this checks, before anything is built
//   on top of it.
//
// TWO QUESTIONS, ASKED SEPARATELY, because they fail differently:
//
//   1. IS THE SOURCE DATA THE SAME? Scoped to what the mesher can actually
//      reach — see the reachability argument below. Everything outside that
//      scope is enumerated and classified by CAUSE rather than waved at.
//
//   2. DOES THE MESHER AGREE? Every scene in tools/uat-scenes.json, meshed
//      twice, once from each Snapshot, comparing the [stats] line's geom hash
//      and the [object]/[mass] lines that follow it.
//
//   The second is the one that matters and the first is what makes a failure
//   in it diagnosable.
//
// WHY THIS NEEDS NO CHANGES TO diorama.cpp:
//
//   build_isolate() is already public and already resets g_meshed_layout, so
//   consecutive calls always rebuild. update() rebuilds whenever layout_ptr
//   differs from the mesh it holds. And the mesher ALREADY prints everything
//   the milestone asks for — object count, per-object footprints, and an
//   FNV-1a hash over every vertex — on the same [stats]/[object] surface
//   tools/uat.ps1 has been parsing all along.
//
//   That is deliberate. Exposing a structured object-model API comes AFTER
//   this gate, never to enable it: validating the loader against the
//   UNMODIFIED mesher removes any possibility that the API change perturbed
//   the thing being measured.
//
// WHY IT NEEDS A GL CONTEXT AT ALL:
//
//   build_mesh() is not a pure function — it ends by uploading to a VBO, and
//   diorama::init() must have run first. So this opens a hidden SDL window and
//   a 3.3 core context exactly as vr_layer.cpp does for the offline inspector
//   modes. Nothing is ever drawn.

// SDL's Windows entry point normally macro-renames main() to SDL_main and
// pulls in SDL2main. This is a console tool with its own main, so tell SDL to
// keep its hands off — and call SDL_SetMainReady() before SDL_Init, which is
// the other half of that contract.
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "capture.h"
#include "asset_catalog.h"
#include "decomp_source.h"
#include "json_scan.h"
#include "pattern_io.h"
#include "snapshot_build.h"
#include "coverage_view.h"

#include "diorama.h"
#include "gl_loader.h"
#include "ruby_world.h"
#include "world_io.h"

namespace {

using vr::world::Snapshot;

// ── Capturing the mesher's own output ─────────────────────────────
//
// build_mesh reports through stderr, which is the contract tools/uat.ps1 reads
// and therefore the one to reuse rather than replace. The mechanics of holding
// on to that output live in studio/capture.h, because the GUI needs the same
// trick to put the current geom= hash on screen.
using studio::capture_stderr;

// What the mesher said about one build: the LAST [stats] line, and the
// [object]/[mass] lines that follow it.
//
// The "last" matters and is not defensive coding — an isolate build meshes the
// live map first and then replaces it, so an earlier [stats] describes a mesh
// that no longer exists. diorama.cpp says so at its own print site.
struct MeshReport {
    std::string              stats;
    std::vector<std::string> objects;

    bool operator==(const MeshReport& o) const {
        return stats == o.stats && objects == o.objects;
    }
};

MeshReport parse_report(const std::string& log) {
    std::vector<std::string> lines;
    size_t                   start = 0;
    while (start <= log.size()) {
        size_t nl = log.find('\n', start);
        if (nl == std::string::npos) nl = log.size();
        std::string line = log.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (nl == log.size()) break;
        start = nl + 1;
    }

    int last_stats = -1;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i)
        if (lines[i].rfind("[stats]", 0) == 0) last_stats = i;

    MeshReport r;
    if (last_stats < 0) return r;
    r.stats = lines[last_stats];
    for (size_t i = last_stats + 1; i < lines.size(); ++i)
        if (lines[i].rfind("[object]", 0) == 0 || lines[i].rfind("[mass]", 0) == 0)
            r.objects.push_back(lines[i]);
    return r;
}

// ── The reachable set ───────────────────────────────────────────────────────
//
// WHAT THE MESHER CAN ACTUALLY SEE, derived rather than assumed.
//
//   Every geometry decision starts from a grid cell's metatile id. That id
//   selects eight tile entries and an attribute word; each tile entry selects
//   one tile's pixels and one palette. Nothing else in the four big tables is
//   reachable from any code path in diorama.cpp — an id the grid never names
//   cannot be read, and neither can a tile no reachable metatile references.
//
//   So a difference INSIDE this closure is a real defect and a difference
//   outside it provably cannot change a vertex. That is the whole argument for
//   scoping the comparison, and it is why the residue can be reported rather
//   than fixed.
struct Reach {
    std::set<uint16_t> metatile_ids;
    std::set<uint16_t> tiles;
    std::set<uint8_t>  palettes;
};

Reach reachable(const Snapshot& s) {
    Reach r;
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x) {
            const uint16_t raw = s.cell(x, y);
            if (raw == vr::world::kGridUndefined) continue;
            r.metatile_ids.insert(static_cast<uint16_t>(raw & vr::world::kMetatileIdMask));
        }

    // build_isolate substitutes the map's commonest flat metatile for every
    // cell of its synthetic plot, and compute_ground_context unions in
    // metatile 1 (the primary tileset's plain grass) for the silhouette
    // flood's background. Both are reachable without appearing in the grid of
    // an isolate, so both join the closure.
    r.metatile_ids.insert(1);

    for (uint16_t id : r.metatile_ids) {
        const size_t base = static_cast<size_t>(id) * vr::world::kTilesPerMetatile;
        if (base + vr::world::kTilesPerMetatile > s.metatiles.size()) continue;
        for (int i = 0; i < vr::world::kTilesPerMetatile; ++i) {
            const vr::world::TileEntry e =
                vr::world::unpack_tile_entry(s.metatiles[base + i]);
            r.tiles.insert(e.index);
            r.palettes.insert(e.palette);
        }
    }
    return r;
}

// The tiles General's tileset callback DMAs over the primary sheet every few
// frames — flowers, water, the tops of waterfalls.
//
// BG_TILE_ADDR IS NOT A TILE INDEX, and reading it as one is a mistake this
// harness made first and then caught itself making. include/gba/defines.h:40:
//
//     #define BG_TILE_ADDR(n)  (void *)(BG_VRAM + (0x80 * (n)))
//
// 0x80 bytes is FOUR 4bpp tiles, so the argument counts groups of four, not
// tiles. Read as a tile index it puts every animation in 108..137, which is
// both wrong and plausible — the harness duly reported 58 genuine animation
// frames as "a tileset defines these, nothing uses them, and they differ
// anyway: suspect the tile decode". A confidently wrong cause is worse than
// no cause, which is exactly why that bucket exists.
//
// From TilesetCB_General's own QueueTilesetAnimDma calls (src/tileset_anim.c:
// 570-604 and the sub_807361C group), destination tile = n * 4 and the count
// is size / 0x20:
//
//     BG_TILE_ADDR(108) 0x3c0 -> 30 tiles at 432    432..461
//     BG_TILE_ADDR(116) 0x140 -> 10 tiles at 464    464..473
//     BG_TILE_ADDR(120) 0x140 -> 10 tiles at 480    480..489
//     BG_TILE_ADDR(124) 0x0c0 ->  6 tiles at 496    496..501
//     BG_TILE_ADDR(127) 0x080 ->  4 tiles at 508    508..511
//
// Verified against the data rather than only read off the source: four live
// captures 30 frames apart differ from each other at exactly these tiles and
// nowhere else.
bool is_animated_tile(uint16_t t) {
    return (t >= 432 && t <= 461) || (t >= 464 && t <= 473)
        || (t >= 480 && t <= 489) || (t >= 496 && t <= 501)
        || (t >= 508 && t <= 511);
}

struct Diff {
    int  checked = 0;
    int  differing_reachable = 0;
    int  differing_residue = 0;
    int  differing_animated = 0;
    bool fatal = false;
};

// ── Scenes ──────────────────────────────────────────────────────────────────

struct Scene {
    std::string name;
    bool        isolate = false;
    int         x = 0, y = 0, w = 0, h = 0;
};

bool load_scenes(const char* path, std::vector<Scene>* out) {
    vr::json::Value doc;
    if (!vr::json::parse_file(path, &doc)) return false;
    const vr::json::Value* arr = doc.find("scenes");
    if (!arr || !arr->is_array()) {
        std::fprintf(stderr, "[cmp] %s has no 'scenes' array\n", path);
        return false;
    }
    for (const vr::json::Value& e : arr->items) {
        Scene sc;
        sc.name = e.find("name") ? e.find("name")->as_str() : "";
        if (sc.name.empty()) continue;
        if (const vr::json::Value* iso = e.find("isolate")) {
            if (std::sscanf(iso->as_str(), "%d,%d,%d,%d",
                            &sc.x, &sc.y, &sc.w, &sc.h) >= 2) {
                if (sc.w <= 0) sc.w = 4;
                if (sc.h <= 0) sc.h = 4;
                sc.isolate = true;
            }
        }
        out->push_back(std::move(sc));
    }
    return !out->empty();
}

// Meshing one scene, wrapped so capture_stderr can call it as a plain function.
struct MeshJob {
    const Snapshot* snap = nullptr;
    const Scene*    scene = nullptr;
};

void run_scene(void* ctx) {
    MeshJob* j = static_cast<MeshJob*>(ctx);
    if (j->scene->isolate)
        vr::diorama::build_isolate(*j->snap, j->scene->x, j->scene->y,
                                   j->scene->w, j->scene->h);
    else
        vr::diorama::update(*j->snap);
}

// ── Reporting ───────────────────────────────────────────────────────────────

void heading(const char* text) {
    std::printf("\n== %s ==\n", text);
}

// The grid as metatile ids, in build_isolate's own [cells] layout.
//
// WHY THIS EXISTS SEPARATELY FROM THE COMPARISON: it can be run with no live
// capture and no emulator at all, which makes it the first thing that can
// falsify the stitching. _docs/voxel-geometry.md and tools/uat-scenes.json
// already record what several of these rectangles contain — the Oldale house
// is 620/628/636/644 with door 647, the Pokemon Centre 72/80/88/96 with door
// 97, a tree is 468 469 over 476 477 — and every one of those was read off the
// LIVE game's own [cells] dump. If the disk-built backup map reproduces them,
// the connection port is right, and it is right before any .snap exists.
void dump_cells(const Snapshot& s, int x, int y, int w, int h) {
    std::printf("  cells (%d,%d) %dx%d of a %dx%d backup map\n",
                x, y, w, h, s.width, s.height);
    for (int j = 0; j < h; ++j) {
        std::printf("  row %2d |", y + j);
        for (int i = 0; i < w; ++i) {
            const uint16_t raw = s.cell(x + i, y + j);
            if (raw == vr::world::kGridUndefined) std::printf("    -");
            else std::printf(" %4u", static_cast<unsigned>(raw & vr::world::kMetatileIdMask));
        }
        // Collision and elevation travel in the same word and are inputs to
        // every standing rule, so they are worth seeing beside the ids.
        std::printf("  | coll");
        for (int i = 0; i < w; ++i)
            std::printf(" %u", s.collision(x + i, y + j));
        std::printf("  elev");
        for (int i = 0; i < w; ++i)
            std::printf(" %2u", s.elevation(x + i, y + j));
        std::printf("\n");
    }
}

// ── The object model ────────────────────────────────────────────────────────
//
// diorama::segment() RECOMPUTES the segmentation from a Snapshot rather than
// handing back whatever build_mesh last left in a global. That is the right
// design — the result is a value the caller owns, it survives the next mesh,
// and it works on snapshots nobody ever meshes — but it turns "the public API
// agrees with the mesher" from a tautology into a claim. These checks are what
// make it a fact again.

using vr::diorama::Object;
using vr::diorama::ObjectClass;
using vr::diorama::ObjectModel;

const char* class_name(ObjectClass c) {
    switch (c) {
        case ObjectClass::kProp:      return "prop";
        case ObjectClass::kStructure: return "structure";
        case ObjectClass::kMass:      return "mass";
    }
    return "?";
}

// One Object in the mesher's own line format, MINUS the trailing " solid NN%".
// solid_pct is deliberately not public (see the Component comment in
// diorama.cpp), so the comparison covers every field the API does expose and
// nothing it cannot see.
std::string object_line(int k, const Object& o) {
    // THE AUTHORED SUFFIX, byte-for-byte as diorama.cpp prints it — same order,
    // same formats, same "only when active" rule. Empty when nothing was
    // authored, so a run with no override file compares exactly as it always
    // did. If the two ever drift, this comparison is what says so.
    char auth[96] = {};
    if (o.authored.active)
        std::snprintf(auth, sizeof(auth),
                      " authored roof_rows=%d rise=%.3f height=%d",
                      o.authored.roof_rows,
                      static_cast<double>(o.authored.rise),
                      o.authored.height);

    char buf[384];
    if (o.cls == ObjectClass::kMass)
        std::snprintf(buf, sizeof(buf),
                      "[mass] #%d %d,%d %dx%d extent %d door %d%s",
                      k, o.x, o.y, o.w, o.extent, o.extent,
                      o.has_door ? 1 : 0, auth);
    else
        std::snprintf(buf, sizeof(buf),
                      "[object] #%d %d,%d %dx%d extent %d class %s door %d%s",
                      k, o.x, o.y, o.w, o.footprint_rows, o.extent,
                      class_name(o.cls), o.has_door ? 1 : 0, auth);
    return buf;
}

std::string strip_solid(const std::string& line) {
    const size_t at = line.find(" solid ");
    return at == std::string::npos ? line : line.substr(0, at);
}

bool models_equal(const ObjectModel& a, const ObjectModel& b) {
    if (a.width != b.width || a.height != b.height) return false;
    if (a.cell_object != b.cell_object) return false;
    if (a.objects.size() != b.objects.size()) return false;
    for (size_t i = 0; i < a.objects.size(); ++i) {
        const Object& x = a.objects[i];
        const Object& y = b.objects[i];
        if (x.x != y.x || x.y != y.y || x.w != y.w || x.extent != y.extent
            || x.footprint_rows != y.footprint_rows || x.cls != y.cls
            || x.has_door != y.has_door || x.ids != y.ids)
            return false;
        // THE AUTHORED BLOCK IS PART OF THE MODEL. It is what the studio reads
        // to show which properties a person decided and which the data still
        // answers, so a comparison that skipped it would call two models equal
        // while they disagreed about the only thing an author edits.
        //
        // rise is compared exactly on purpose: both sides parse the same file
        // with the same reader, so any difference at all is a real defect
        // rather than arithmetic drift.
        if (x.authored.active    != y.authored.active
            || x.authored.roof_rows != y.authored.roof_rows
            || x.authored.rise      != y.authored.rise
            || x.authored.height    != y.authored.height)
            return false;
    }
    return true;
}

// Invariants that must hold for ANY model of ANY map. Cheap, and each one
// catches a class of defect that would otherwise surface much later as an
// override matching geometry it was never authored against.
bool check_model(const Snapshot& s, const ObjectModel& m, const char* label) {
    int bad = 0;
    auto fail = [&](const char* what, int a = -1, int b = -1) {
        if (bad < 8) {
            if (a < 0) std::printf("  %-5s FAIL  %s\n", label, what);
            else       std::printf("  %-5s FAIL  %s (%d, %d)\n", label, what, a, b);
        }
        ++bad;
    };

    if (m.width != s.width || m.height != s.height)
        fail("model dimensions do not match the Snapshot");
    if (m.cell_object.size() != static_cast<size_t>(m.width) * m.height)
        fail("cell_object is not width * height");

    const int n = static_cast<int>(m.objects.size());

    // THE SIGNATURE. ids[r*w + c] must be the metatile at (x+c, y+r). A row and
    // column transposition here is invisible in every count and fatal to an
    // override key — the same shape of bug the PNG nibble order already cost
    // this project once.
    for (int k = 0; k < n; ++k) {
        const Object& o = m.objects[k];
        if (o.w <= 0 || o.extent <= 0) { fail("object has an empty bbox", k, 0); continue; }
        if (o.ids.size() != static_cast<size_t>(o.w) * o.extent) {
            fail("ids is not w * extent", k, static_cast<int>(o.ids.size()));
            continue;
        }
        bool sig_ok = true;
        for (int r = 0; r < o.extent && sig_ok; ++r)
            for (int c = 0; c < o.w; ++c)
                if (o.ids[static_cast<size_t>(r) * o.w + c] !=
                    s.metatile_id(o.x + c, o.y + r)) {
                    fail("ids does not match the grid", k, r * o.w + c);
                    sig_ok = false;
                    break;
                }
        if (o.footprint_rows < 0 || o.footprint_rows > o.extent)
            fail("footprint_rows outside the extent", k, o.footprint_rows);
    }

    // MEMBERSHIP, and the published ORDER. Every index in range, every member
    // inside its own bounding box, every object claimed by at least one cell,
    // and objects numbered in the order their first member cell is reached by
    // a row-major scan — which is the contract diorama.h states.
    std::vector<int> members(static_cast<size_t>(n > 0 ? n : 1), 0);
    int next_expected = 0;
    for (int y = 0; y < m.height; ++y)
        for (int x = 0; x < m.width; ++x) {
            const int k = m.object_at(x, y);
            if (k < 0) continue;
            if (k >= n) { fail("cell_object index out of range", x, y); continue; }
            ++members[k];
            const Object& o = m.objects[k];
            if (x < o.x || x >= o.x + o.w || y < o.y || y >= o.y + o.extent)
                fail("member cell outside its object's bbox", x, y);
            if (members[k] == 1) {
                if (k != next_expected)
                    fail("objects are not in first-seen row-major order",
                         k, next_expected);
                ++next_expected;
            }
        }
    for (int k = 0; k < n; ++k)
        if (members[k] == 0) fail("object has no member cells", k, 0);

    if (bad > 8) std::printf("  %-5s ... and %d more failures\n", label, bad - 8);
    return bad == 0;
}

// Write an override pattern for the object covering (cx, cy).
//
// THE DERIVATION AND THE SERIALISATION BOTH LIVE IN studio::pattern_io, so this
// and the GUI produce the same bytes from the same object. That is the point of
// the split: the fixture this mode generates and anything a person authors by
// clicking cannot drift apart, because there is only one writer.
//
// What stays here is the REPORTING. --emit-pattern is a console mode, and its
// output is what an author reads to check the file describes what they meant.
bool emit_pattern(const Snapshot& s, const ObjectModel& m, int cx, int cy,
                  const char* name, int roof_rows, const char* path) {
    const int k = m.object_at(cx, cy);
    if (k < 0) {
        std::fprintf(stderr, "[emit] no object at (%d,%d)\n", cx, cy);
        return false;
    }

    vr::overrides::Pattern p;
    if (!studio::pattern_io::from_object(s, m, cx, cy, name, &p)) return false;
    if (roof_rows >= 0) p.apply.roof_rows = roof_rows;

    vr::overrides::OverrideSet set;
    set.version = vr::overrides::kVersion;
    set.patterns.push_back(std::move(p));
    if (!studio::pattern_io::write(path, set)) return false;

    const vr::overrides::Pattern& w = set.patterns.front();
    std::printf("  wrote %s: %s, %dx%d at (%d,%d), %zu member cells, "
                "%zu metatile definitions\n",
                path, name, w.w, w.extent, m.objects[k].x, m.objects[k].y,
                static_cast<size_t>(std::count(w.mask.begin(), w.mask.end(), 1)),
                w.tiles.size());
    return true;
}

// Everything the object-model API has to prove, for one Snapshot.
//
// The load-bearing one is the CROSS-CHECK. build_mesh and diorama::segment()
// reach the segmentation by different routes — one from the cov/ground it
// already had in hand, one recomputed from scratch — and this is what says the
// two routes land in the same place rather than merely looking as though they
// ought to.
bool run_object_checks(const Snapshot& s, const vr::overrides::OverrideSet& ov,
                       const char* label, ObjectModel* out) {
    ObjectModel model;
    // THE SAME SET THE MESHER HAS. segment() is a pure function of its
    // arguments rather than a reader of mesher state, so agreeing with
    // build_mesh means being handed what build_mesh was handed.
    if (!vr::diorama::segment(s, ov, &model)) {
        std::printf("  %-5s FAIL  segment() refused the snapshot\n", label);
        return false;
    }

    bool ok = check_model(s, model, label);

    // ── What the file said, as the model reports it ─────────────────────────
    //
    // Object::Authored is public API and rubyvr_gui reads it to show which
    // properties a person decided. Nothing checked it until now: object_line
    // and models_equal both predate the field, so a model whose entire authored
    // block was wrong passed line-for-line green. That is the shape of failure
    // _docs/voxel-geometry.md already records — 45 of 45 through four visibly
    // broken states — arriving in the verification surface rather than the
    // geometry.
    //
    // CONTAINMENT, NOT AN EXACT ORIGIN. An object's bbox bounds its CLAIMED
    // MEMBER cells, so a mask whose first row is all zeros leaves the object
    // inset within the rectangle the pattern matched at.
    if (!ov.empty()) {
        const std::vector<vr::overrides::Match> matches =
            vr::overrides::find(s, ov);
        int authored = 0;
        for (size_t i = 0; i < model.objects.size(); ++i) {
            const Object& o = model.objects[i];
            if (!o.authored.active) continue;
            ++authored;

            bool backed = false;
            for (const vr::overrides::Match& m : matches) {
                const vr::overrides::Pattern& p = ov.patterns[
                    static_cast<size_t>(m.pattern)];
                if (o.x < m.x || o.y < m.y ||
                    o.x + o.w      > m.x + p.w ||
                    o.y + o.extent > m.y + p.extent) continue;
                if (o.authored.roof_rows != p.apply.roof_rows ||
                    o.authored.height    != p.apply.height ||
                    o.authored.rise      != p.apply.rise) continue;
                backed = true;
                break;
            }

            std::printf("  %-5s   #%zu at %d,%d authored roof_rows=%d "
                        "rise=%.3f height=%d%s\n",
                        label, i, o.x, o.y, o.authored.roof_rows,
                        static_cast<double>(o.authored.rise),
                        o.authored.height,
                        backed ? "" : "   <-- NO PATTERN SUPPLIED THIS");
            if (!backed) ok = false;
        }

        // A file that matched and claimed nothing is the same defect as one
        // that matched nothing: it changed the world without saying so.
        if (!matches.empty() && authored == 0) {
            std::printf("  %-5s FAIL  %zu pattern match(es), but no object "
                        "reports authored values\n", label, matches.size());
            ok = false;
        }
        std::printf("  %-5s %d authored object(s) from %zu match(es)\n",
                    label, authored, matches.size());
    }

    // DETERMINISM. A second call on the same Snapshot must produce the same
    // model exactly; the ordering contract is worth nothing otherwise.
    ObjectModel again;
    if (!vr::diorama::segment(s, ov, &again) || !models_equal(model, again)) {
        std::printf("  %-5s FAIL  segment() is not deterministic\n", label);
        ok = false;
    }

    Scene whole;
    whole.name = label;
    MeshJob job{&s, &whole};
    const auto saved_mode=vr::diorama::build_mode();
    vr::diorama::set_build_mode(vr::diorama::BuildMode::Inferred); // segmentation diagnostic, not raised-instance counters
    const MeshReport rep = parse_report(capture_stderr(run_scene, &job));
    vr::diorama::set_build_mode(saved_mode);

    if (rep.stats.empty()) {
        std::printf("  %-5s FAIL  the mesher printed no [stats] line\n", label);
        ok = false;
    } else {
        std::vector<std::string> mine;
        for (size_t k = 0; k < model.objects.size(); ++k)
            mine.push_back(object_line(static_cast<int>(k), model.objects[k]));
        std::vector<std::string> theirs;
        for (const std::string& l : rep.objects) theirs.push_back(strip_solid(l));

        if (mine == theirs) {
            std::printf("  %-5s %zu objects, line-for-line identical to the "
                        "mesher's own\n", label, mine.size());
        } else {
            ok = false;
            std::printf("  %-5s FAIL  public model and mesher lines differ "
                        "(%zu vs %zu)\n", label, mine.size(), theirs.size());
            const size_t n = mine.size() > theirs.size() ? mine.size() : theirs.size();
            for (size_t i = 0, shown = 0; i < n && shown < 6; ++i) {
                const std::string a = i < mine.size()   ? mine[i]   : "(none)";
                const std::string b = i < theirs.size() ? theirs[i] : "(none)";
                if (a != b) {
                    std::printf("           api %s\n        mesher %s\n",
                                a.c_str(), b.c_str());
                    ++shown;
                }
            }
        }
    }

    if (out) *out = std::move(model);
    return ok;
}

}  // namespace

namespace studio {
int terrain_selftest(const char* output);
int terrain_source_selftest(const char* source,const char* pack);
}
int foundation_selftest(const char* output);
int connected_selftest();
int connected_source_test(const char* root,const char* pack,const char* output);
int portability_selftest(const char* workdir);
int main(int argc, char** argv) {
    if(argc==2 && !std::strcmp(argv[1],"--test-connected")) return connected_selftest();
    if(argc==5 && !std::strcmp(argv[1],"--test-connected-source")) return connected_source_test(argv[2],argv[3],argv[4]);
    if(argc==3 && !std::strcmp(argv[1],"--test-foundation")) return foundation_selftest(argv[2]);
    if(argc==3 && !std::strcmp(argv[1],"--test-terrain")) return studio::terrain_selftest(argv[2]);
    if(argc==4 && !std::strcmp(argv[1],"--test-terrain-source")) return studio::terrain_source_selftest(argv[2],argv[3]);
    // Native file/capture portability fixtures; no source art, window or GL.
    if(argc==3 && !std::strcmp(argv[1],"--test-portability")) return portability_selftest(argv[2]);
    // Validate review snapshots without source art, a ROM, a window or GL.
    if(argc==3 && !std::strcmp(argv[1],"--check-review-index")) {
        studio::coverage::Index index;std::string error;
        if(!studio::coverage::load_index(argv[2],&index,&error)) {std::fprintf(stderr,"%s\n",error.c_str());return 1;}
        size_t counts[5]={},defaults=0,rows=0;
        for(const auto& map:index.maps) {
            studio::coverage::Room room;
            if(!studio::coverage::load_room(index,map.id,&room,&error)) {std::fprintf(stderr,"%s: %s\n",map.id.c_str(),error.c_str());return 1;}
            rows+=room.rows.size();
            for(const auto& row:room.rows) {
                defaults+=studio::coverage::matches(row,0,false,false,"");
                for(int filter=0;filter<5;++filter) counts[filter]+=studio::coverage::matches(row,filter,true,true,"");
            }
        }
        std::printf("{\"maps\":%zu,\"rows\":%zu,\"default_visible\":%zu,\"filters\":[%zu,%zu,%zu,%zu,%zu]}\n",
                    index.maps.size(),rows,defaults,counts[0],counts[1],counts[2],counts[3],counts[4]);
        return 0;
    }
    const char* live_path   = nullptr;
    const char* decomp_root = "third_party/pokeruby";
    const char* map_id      = "MAP_ROUTE101";
    const char* scenes_path = nullptr;
    const char* cells_arg   = nullptr;
    const char* overrides_path = nullptr;
    const char* mode=nullptr;
    const char* catalog_directory=nullptr;
    const char* asset_report=nullptr;
    const char* emit_cell   = nullptr;
    const char* emit_out    = "pattern.json";
    const char* emit_name   = "pattern";
    int         emit_roof_rows = -1;
    bool        bisect      = false;
    bool        objects_mode = false;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&](const char** dst) {
            if (i + 1 < argc) *dst = argv[++i];
            else std::fprintf(stderr, "[cmp] %s needs a value\n", a);
        };
        if      (!std::strcmp(a, "--compare")) next(&live_path);
        else if (!std::strcmp(a, "--decomp"))  next(&decomp_root);
        else if (!std::strcmp(a, "--map"))     next(&map_id);
        else if (!std::strcmp(a, "--catalog")) { next(&catalog_directory); if(!catalog_directory) return 2; }
        else if (!std::strcmp(a, "--audit-assets")) { next(&asset_report); if(!asset_report) return 2; }
        else if (!std::strcmp(a, "--scenes"))  next(&scenes_path);
        else if (!std::strcmp(a, "--cells"))   next(&cells_arg);
        else if (!std::strcmp(a, "--overrides")) next(&overrides_path);
        else if (!std::strcmp(a, "--mode")) { next(&mode);if(!mode) return 2; }
        else if (!std::strcmp(a, "--emit-pattern")) next(&emit_cell);
        else if (!std::strcmp(a, "--emit-out"))     next(&emit_out);
        else if (!std::strcmp(a, "--emit-name"))    next(&emit_name);
        else if (!std::strcmp(a, "--apply-roof-rows")) {
            const char* v = nullptr;
            next(&v);
            if (v) emit_roof_rows = std::atoi(v);
        }
        else if (!std::strcmp(a, "--bisect"))  bisect = true;
        else if (!std::strcmp(a, "--objects")) objects_mode = true;
        else {
            std::fprintf(stderr,
                "usage: rubyvr_studio [--decomp <pokeruby>] [--map MAP_ROUTE101]\n"
                "                    [--mode inferred|diorama]\n"
                "                     --compare <live.snap> [--scenes uat-scenes.json]\n"
                "                                           [--objects] [--bisect]\n"
                "                     --cells x,y,w,h\n"
                "                     --catalog <directory> (all-map source inventory; no GL)\n"
                "                     --audit-assets <report.json> --overrides <closed-solid-pack.json> (no GL)\n"
                "                     --test-portability <dir> (native file/capture fixtures; no GL)\n"
                "\n"
                "  --cells   print that rectangle of the disk-built backup map and\n"
                "            stop. Needs no capture and no emulator, so it is the\n"
                "            cheapest way to falsify the connection stitching.\n"
                "  --objects check diorama::segment()'s public object model: its\n"
                "            invariants, its determinism, and that it agrees\n"
                "            line-for-line with the mesher's own [object] lines.\n"
                "  --bisect  take vram_tiles and bg_palette from the reference, so\n"
                "            a mesh difference can only be the MAP half.\n");
            return 2;
        }
    }
    if (!live_path && !cells_arg && !scenes_path && !objects_mode && !emit_cell && !catalog_directory && !asset_report) {
        std::fprintf(stderr,
                     "[cmp] one of --compare <live.snap>, --scenes <file>, "
                     "--objects or --cells x,y,w,h\n");
        return 2;
    }
    if(mode && std::strcmp(mode,"inferred") && std::strcmp(mode,"diorama")) {
        std::fprintf(stderr,"[cmp] --mode must be inferred or diorama\n");return 2;
    }

    // Authored overrides, if any, BEFORE anything meshes — so the scene
    // comparison and the object model both see the same world the game would.
    // A failed load is fatal here rather than a silent fallback: a run that was
    // asked to test an override and quietly tested the inferred world instead
    // is worse than no run.
    if (overrides_path) {
        vr::overrides::OverrideSet set;
        if (!vr::overrides::load(overrides_path, &set)) {
            std::fprintf(stderr, "[cmp] --overrides %s failed to load\n",
                         overrides_path);
            return 1;
        }
        vr::diorama::set_overrides(std::move(set));
    }

    // ── The disk Snapshot ───────────────────────────────────────────────────
    // Built first, and without the reference, so --cells works standalone.
    studio::Decomp decomp;
    if (!decomp.open(decomp_root)) return 1;
    if(catalog_directory) {
        if(live_path || scenes_path || cells_arg || objects_mode || emit_cell || overrides_path || mode || bisect || asset_report) {
            std::fprintf(stderr,"[catalog] use --catalog with only --decomp; the inventory records unmodified source data\n"); return 2;
        }
        return studio::write_asset_catalog(decomp,catalog_directory)?0:1;
    }
    if(asset_report) {
        if(!overrides_path || live_path || scenes_path || cells_arg || objects_mode || emit_cell || mode || bisect) {
            std::fprintf(stderr,"[asset-audit] use --audit-assets with --overrides and optionally --decomp only\n"); return 2;
        }
        return studio::audit_asset_pack(decomp,vr::diorama::current_overrides(),asset_report)?0:1;
    }

    Snapshot live;
    if (live_path && !vr::world_io::read(live, live_path)) return 1;

    studio::BuildOptions opt;
    if (bisect) {
        if (!live_path) {
            std::fprintf(stderr, "[cmp] --bisect needs --compare to borrow from\n");
            return 2;
        }
        opt.borrow_tiles_from = &live;
    }

    Snapshot          disk;
    studio::BuildInfo info;
    if (!studio::build_snapshot(decomp, map_id, &disk, &info, opt)) return 1;

    // WHAT THE OVERRIDE FILE ACTUALLY DID, reported where a person reads it.
    //
    // The mesher's own [override] lines go to stderr, and the scene loop
    // redirects stderr while it meshes — so without this, a file that matched
    // nothing would produce a completely clean, completely green run. An
    // override that changes nothing is as broken as one that changes too much;
    // the difference has to be visible.
    bool overrides_ok = true;
    if (overrides_path) {
        heading("overrides");
        const vr::overrides::OverrideSet& ov = vr::diorama::current_overrides();
        const std::vector<vr::overrides::Match> matches =
            vr::overrides::find(disk, ov);
        std::printf("  %zu pattern(s) loaded, %zu match(es)\n",
                    ov.patterns.size(), matches.size());
        for (const vr::overrides::Match& m : matches)
            std::printf("    %-24s at (%d,%d)\n",
                        ov.patterns[m.pattern].name.c_str(), m.x, m.y);
        if (matches.empty() && !ov.empty()) {
            std::printf("  NOTHING MATCHED. The file changed nothing, so a green\n"
                        "  run below would be measuring the inferred world.\n");
            overrides_ok = false;
        }
    }

    if (cells_arg) {
        int cx = 0, cy = 0, cw = 0, ch = 0;
        if (std::sscanf(cells_arg, "%d,%d,%d,%d", &cx, &cy, &cw, &ch) != 4 ||
            cw <= 0 || ch <= 0) {
            std::fprintf(stderr, "[cmp] --cells wants x,y,w,h\n");
            return 2;
        }
        heading("grid");
        dump_cells(disk, cx, cy, cw, ch);
        return 0;
    }

    // Emitting a pattern needs no GL: segment() is pure CPU, so an author can
    // produce an override file without a window ever opening.
    if (emit_cell) {
        int ex = 0, ey = 0;
        if (std::sscanf(emit_cell, "%d,%d", &ex, &ey) != 2) {
            std::fprintf(stderr, "[cmp] --emit-pattern wants x,y\n");
            return 2;
        }
        heading("emit pattern");
        ObjectModel model;
        if (!vr::diorama::segment(disk, vr::diorama::current_overrides(), &model))
            return 1;
        return emit_pattern(disk, model, ex, ey, emit_name, emit_roof_rows,
                            emit_out) ? 0 : 1;
    }

    // Seeded from the override report above: a file that matched nothing has
    // already failed, whatever the rest of the run says.
    bool ok = overrides_ok;

    // Question 1 is skipped when there is no reference. --scenes on its own
    // meshes the disk map alone, which is how the geometry gets checked
    // against the RECORDED baseline in _uat/report.md before anyone has
    // captured a fresh .snap.
    if (live_path) {
        // ── Question 1: is the source data the same? ────────────────────────────
        heading("source data");


        if (live.width != disk.width || live.height != disk.height) {
            std::printf("  dimensions  live %dx%d  disk %dx%d   MISMATCH\n",
                        live.width, live.height, disk.width, disk.height);
            // Everything below indexes by cell, so there is nothing further to say.
            std::printf("\nFAIL: the backup map is not even the same shape.\n");
            return 1;
        }
        std::printf("  dimensions  %dx%d  identical\n", live.width, live.height);

        {
            size_t bad = 0, first = 0;
            for (size_t i = 0; i < live.grid.size(); ++i)
                if (live.grid[i] != disk.grid[i]) {
                    if (!bad) first = i;
                    ++bad;
                }
            if (bad) {
                ok = false;
                std::printf("  grid        %zu of %zu cells DIFFER, first at (%d,%d): "
                            "live %04X disk %04X\n",
                            bad, live.grid.size(),
                            static_cast<int>(first % live.width),
                            static_cast<int>(first / live.width),
                            live.grid[first], disk.grid[first]);
                // Show a handful, because the pattern names the bug: a whole band
                // wrong is a connection, a scattered few are runtime edits.
                int shown = 0;
                for (size_t i = 0; i < live.grid.size() && shown < 8; ++i)
                    if (live.grid[i] != disk.grid[i]) {
                        std::printf("                (%2d,%2d) live %04X  disk %04X\n",
                                    static_cast<int>(i % live.width),
                                    static_cast<int>(i / live.width),
                                    live.grid[i], disk.grid[i]);
                        ++shown;
                    }
            } else {
                std::printf("  grid        %zu cells identical\n", live.grid.size());
            }
        }

        const Reach reach = reachable(disk);
        std::printf("  reachable   %zu metatile ids, %zu tiles, %zu palettes\n",
                    reach.metatile_ids.size(), reach.tiles.size(),
                    reach.palettes.size());

        // Metatiles and attributes, split into reachable and residue.
        {
            Diff d;
            for (uint16_t id = 0; id < vr::world::kMetatilesTotal; ++id) {
                const bool   reachable_id = reach.metatile_ids.count(id) != 0;
                const size_t base = static_cast<size_t>(id) * vr::world::kTilesPerMetatile;
                bool differs = live.attributes[id] != disk.attributes[id];
                for (int i = 0; i < vr::world::kTilesPerMetatile && !differs; ++i)
                    differs = live.metatiles[base + i] != disk.metatiles[base + i];
                if (!differs) continue;
                if (reachable_id) { ++d.differing_reachable; }
                else              { ++d.differing_residue; }
            }
            if (d.differing_reachable) {
                ok = false;
                std::printf("  metatiles   %d REACHABLE ids differ  <-- defect\n",
                            d.differing_reachable);
                int shown = 0;
                for (uint16_t id : reach.metatile_ids) {
                    if (shown >= 8) break;
                    const size_t base = static_cast<size_t>(id) * vr::world::kTilesPerMetatile;
                    bool differs = live.attributes[id] != disk.attributes[id];
                    for (int i = 0; i < vr::world::kTilesPerMetatile && !differs; ++i)
                        differs = live.metatiles[base + i] != disk.metatiles[base + i];
                    if (differs) {
                        std::printf("                id %4u attr live %04X disk %04X\n",
                                    id, live.attributes[id], disk.attributes[id]);
                        ++shown;
                    }
                }
            } else {
                std::printf("  metatiles   all %zu reachable ids identical\n",
                            reach.metatile_ids.size());
            }
            std::printf("  residue     %d unreachable ids differ "
                        "(expected: %s defines %d, %s defines %d, so ids >= %d are "
                        "ROM over-read live and zero here)\n",
                        d.differing_residue,
                        info.primary_name.c_str(), info.primary_metatiles,
                        info.secondary_name.c_str(), info.secondary_metatiles,
                        info.first_undefined_metatile);
        }

        // Tile pixels.
        {
            // FIVE BUCKETS, and the order they are tested in is the whole
            // point. An earlier version asked "is it reachable" first, so an
            // ANIMATED tile that no metatile on this map happens to use was
            // reported as stale VRAM — a plausible sentence naming the wrong
            // cause, which is worse than no sentence at all. Animation is
            // therefore tested before reachability, and "defined but unused
            // and yet different" gets a bucket of its own because it is the
            // one residue class that would indicate a real decode bug.
            int defect = 0, anim_used = 0, anim_unused = 0;
            int stale = 0, defined_unused = 0;
            int defined_live_blank = 0, defined_disk_blank = 0;
            std::vector<uint16_t> bad_reachable, bad_defined;

            for (uint16_t t = 0; t < vr::world::kTileCount; ++t) {
                const size_t base = static_cast<size_t>(t) * vr::world::kTileBytes;
                if (base + vr::world::kTileBytes > live.vram_tiles.size()) break;
                if (std::memcmp(&live.vram_tiles[base], &disk.vram_tiles[base],
                                vr::world::kTileBytes) == 0)
                    continue;

                const bool used = reach.tiles.count(t) != 0;
                if (is_animated_tile(t)) {
                    if (used) ++anim_used; else ++anim_unused;
                } else if (used) {
                    ++defect;
                    if (bad_reachable.size() < 12) bad_reachable.push_back(t);
                } else if (t >= info.first_undefined_tile) {
                    ++stale;
                } else {
                    ++defined_unused;
                    if (bad_defined.size() < 64) bad_defined.push_back(t);
                    // Which SIDE is blank tells the two candidate causes apart:
                    // an all-zero live tile is VRAM the game never wrote, an
                    // all-zero disk tile is a decode that produced nothing.
                    bool live_blank = true, disk_blank = true;
                    for (int k = 0; k < vr::world::kTileBytes; ++k) {
                        if (live.vram_tiles[base + k]) live_blank = false;
                        if (disk.vram_tiles[base + k]) disk_blank = false;
                    }
                    if (live_blank) ++defined_live_blank;
                    if (disk_blank) ++defined_disk_blank;
                }
            }

            if (defect) {
                ok = false;
                std::printf("  tiles       %d REACHABLE, non-animated tiles differ  <-- defect\n",
                            defect);
                std::printf("                ");
                for (uint16_t t : bad_reachable) std::printf("%u ", t);
                std::printf("\n");
            } else {
                std::printf("  tiles       all reachable non-animated tiles identical\n");
            }
            std::printf("  residue     %d animated tiles differ (%d used by this map, "
                        "%d not) — TilesetCB_General DMA, tiles 432..511\n",
                        anim_used + anim_unused, anim_used, anim_unused);
            std::printf("  residue     %d unreferenced tiles >= %d differ (expected: "
                        "%s defines only %d tiles, so these are stale VRAM live)\n",
                        stale, info.first_undefined_tile,
                        info.secondary_name.c_str(), info.secondary_tiles);
            if (defined_unused) {
                // Not fatal — nothing reads them — but nothing SHOULD have made
                // them differ either. A PNG decode or nibble-order bug in a
                // tile this map does not happen to use would land exactly here.
                std::printf("  WARNING     %d tiles are defined by a tileset, unused by "
                            "this map, and still differ\n"
                            "              (%d blank live, %d blank on disk):\n              ",
                            defined_unused, defined_live_blank, defined_disk_blank);
                for (size_t i = 0; i < bad_defined.size(); ++i)
                    std::printf("%u%s", bad_defined[i],
                                ((i + 1) % 16 == 0) ? "\n              " : " ");
                std::printf("\n");
            }
        }

        // Palettes. Only 0..191 are tileset data; NUM_PALS_TOTAL is 12.
        {
            int bad = 0;
            for (int i = 0; i < 12 * 16; ++i)
                if (live.bg_palette[i] != disk.bg_palette[i]) {
                    if (bad < 8)
                        std::printf("                entry %3d (pal %d idx %2d) "
                                    "live %04X disk %04X\n",
                                    i, i / 16, i % 16, live.bg_palette[i], disk.bg_palette[i]);
                    ++bad;
                }
            if (bad) {
                ok = false;
                std::printf("  palette     %d of 192 tileset entries DIFFER  <-- defect\n", bad);
            } else {
                std::printf("  palette     entries 0..191 identical\n");
            }
            std::printf("  residue     entries 192..255 out of scope "
                        "(NUM_PALS_TOTAL is 12; not tileset data)\n");
        }

    }

    // ── Question 2: does the mesher agree? ──────────────────────────────────
    if (!scenes_path && !objects_mode) {
        std::printf("\n(no --scenes and no --objects; nothing further to check)\n");
        return ok ? 0 : 1;
    }

    std::vector<Scene> scenes;
    if (scenes_path && !load_scenes(scenes_path, &scenes)) return 1;

    // A hidden window and a 3.3 core context, exactly as vr_layer.cpp asks for
    // them. Nothing is drawn; build_mesh simply needs somewhere to put a VBO.
    SDL_SetMainReady();   // the other half of SDL_MAIN_HANDLED, above
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[cmp] SDL video init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* win = SDL_CreateWindow("rubyvr_studio-compare",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       64, 64,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!win) {
        std::fprintf(stderr, "[cmp] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        std::fprintf(stderr, "[cmp] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return 1;
    }
    if (!vr::gl::load() || !vr::diorama::init()) {
        std::fprintf(stderr, "[cmp] could not bring up the mesher's GL objects\n");
        return 1;
    }
    if(mode) vr::diorama::set_build_mode(!std::strcmp(mode,"diorama")?vr::diorama::BuildMode::Diorama:vr::diorama::BuildMode::Inferred);

    // ── The object model ────────────────────────────────────────────────────
    if (objects_mode) {
        heading("object model");
        const vr::overrides::OverrideSet& ov = vr::diorama::current_overrides();
        ObjectModel live_model, disk_model;
        ok = run_object_checks(disk, ov, "disk", &disk_model) && ok;
        if (live_path) {
            ok = run_object_checks(live, ov, "live", &live_model) && ok;
            // Milestone 1's guarantee restated on the surface the studio will
            // actually author against: the two sources must produce not merely
            // the same geometry but the same OBJECTS, because an override is
            // keyed on an object's signature rather than on a vertex.
            if (models_equal(live_model, disk_model)) {
                std::printf("  both  live and disk object models identical\n");
            } else {
                std::printf("  both  FAIL  live and disk object models differ\n");
                ok = false;
            }
        }
    }

    int pass = 0, fail = 0;
    if (scenes_path) {
    heading("geometry");
    std::printf("  %-20s %-18s %s\n", "scene", "result", "detail");

    for (const Scene& sc : scenes) {
        MeshJob jd{&disk, &sc};
        const MeshReport rd = parse_report(capture_stderr(run_scene, &jd));

        // SINGLE-SOURCE MODE. With no reference to compare against, print what
        // the disk-built map meshes to and let the reader check it against
        // _uat/report.md — which was generated from the LIVE game and records
        // the vertex count and every object figure. Not as strong as the geom
        // hash, and strong enough to catch a loader that is wrong.
        if (!live_path) {
            if (rd.stats.empty()) {
                ++fail;
                std::printf("  %-20s %-18s the mesher printed no [stats] line\n",
                            sc.name.c_str(), "NO OUTPUT");
            } else {
                ++pass;
                std::printf("  %-20s %s\n", sc.name.c_str(),
                            rd.stats.c_str() + std::strlen("[stats] "));
            }
            continue;
        }

        MeshJob jl{&live, &sc};
        const MeshReport rl = parse_report(capture_stderr(run_scene, &jl));

        if (rl.stats.empty() || rd.stats.empty()) {
            ++fail;
            std::printf("  %-20s %-18s the mesher printed no [stats] line\n",
                        sc.name.c_str(), "NO OUTPUT");
            continue;
        }
        if (rl == rd) {
            ++pass;
            // The geom hash is the informative half of the line; show it.
            const size_t g = rl.stats.find("geom=");
            std::printf("  %-20s %-18s %s, %zu objects\n", sc.name.c_str(), "identical",
                        g == std::string::npos ? "" : rl.stats.substr(g).c_str(),
                        rl.objects.size());
        } else {
            ++fail;
            ok = false;
            std::printf("  %-20s %-18s\n", sc.name.c_str(), "DIFFERS");
            std::printf("      live  %s\n", rl.stats.c_str());
            std::printf("      disk  %s\n", rd.stats.c_str());
            const size_t n = rl.objects.size() > rd.objects.size()
                           ? rl.objects.size() : rd.objects.size();
            for (size_t i = 0, shown = 0; i < n && shown < 6; ++i) {
                const std::string a = i < rl.objects.size() ? rl.objects[i] : "(none)";
                const std::string b = i < rd.objects.size() ? rd.objects[i] : "(none)";
                if (a != b) {
                    std::printf("      live  %s\n      disk  %s\n", a.c_str(), b.c_str());
                    ++shown;
                }
            }
        }
    }
    }   // if (scenes_path)

    vr::diorama::shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();

    // SAY WHAT WAS ACTUALLY CHECKED. Single-source mode compares NOTHING
    // across sources — it meshes the disk map and prints the result — so it
    // must never report a pass for work it did not do. An earlier version of
    // this function did, and a run that had verified nothing at all ended with
    // the word PASS on it. That is the failure mode _docs/voxel-geometry.md
    // already warns about: 45 of 45 checks went green through four successive
    // visibly-broken states.
    if (scenes_path && !live_path)
        std::printf("\n  %d of %zu scenes meshed. NOTHING WAS COMPARED across\n"
                    "  sources — there was no --compare reference. Check these\n"
                    "  against _uat/report.md, generated from the live game.\n",
                    pass, scenes.size());
    else if (scenes_path)
        std::printf("\n  %d of %zu scenes identical\n", pass, scenes.size());

    if (!live_path) {
        // The object-model self-checks ARE real results — invariants,
        // determinism, and segment() agreeing with the mesher. The
        // cross-source comparison simply did not run. Claim the first and
        // not the second.
        const bool good = ok && fail == 0;
        std::printf("\n%s\n", good
            ? "OK: self-checks passed. No --compare reference, so nothing was "
              "compared across sources."
            : "FAIL: see the lines marked FAIL above.");
        return good ? 0 : 1;
    }

    std::printf("\n%s\n", ok
        ? "PASS: the disk-built map is equivalent everywhere the mesher can reach it."
        : "FAIL: see the lines marked <-- defect, DIFFERS and FAIL above.");
    return ok ? 0 : 1;
}
