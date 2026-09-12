// diorama.h — Ruby's field map as 3D geometry, pop-up-book style.
//
// THE SHAPE OF THE IDEA:
//
//   A metatile's LOWER pair lies flat on the board. Its UPPER pair — the half
//   the game draws in front of the player — stands UP as a vertical billboard.
//   That one rule is what turns a map into a diorama: trees, signs, house
//   fronts, tall grass and cliff faces rise off the board, while paths, water
//   and floor stay flat, because the game already told us which is which.
//
//   world::draws_above_player() is the trigger, verified against pokeruby's
//   DrawMetatile rather than guessed. See ruby_world.h.
//
// WHY THE TILE SHEET IS A TEXTURE AND NOT AN ATLAS:
//
//   The obvious approach is to composite all 1024 metatiles into one RGBA atlas
//   on the CPU and texture from that. It works, and it means rebuilding a
//   512x512 image whenever anything changes — which is constantly, because the
//   game animates tiles in place (water, flowers, waterfalls) via DMA.
//
//   Instead the GPU does the indexing: the raw 4bpp tile sheet goes up as a
//   256x256 integer texture of palette indices, the 16 palettes go up as a
//   16x16 RGBA texture, and the fragment shader does index -> colour. Then
//   animation is free — re-upload 64 KB a frame and every animated tile just
//   moves. No dirty tracking, no atlas rebuild, no cache invalidation.
//
//   It also keeps the mesh static: geometry only changes when the MAP changes.

#pragma once
#include <atomic>
#include <memory>

#include "overrides.h"
#include "ruby_world.h"
#include "vr_math.h"

namespace vr {
namespace diorama {

// Build the shader, textures and buffers. Needs a current GL context and
// vr::gl::load() to have succeeded.
bool init();
void shutdown();
bool ready();
enum class BuildMode { Inferred, Diorama };
void set_build_mode(BuildMode);
BuildMode build_mode();
struct DioramaStats {
    size_t terrain_cells=0, terrain_rejected=0, terrain_vertices=0, unresolved_placements=0;
    size_t raised_instances=0, flat_vertices=0, authored_vertices=0;
    size_t accepted_instances=0, part_instances=0;
    uint16_t ground_tile=0;
    uint64_t geometry_hash=0;
};
const DioramaStats& diorama_stats();

// Hand over the newest snapshot. Rebuilds the mesh only when the map layout
// changes; re-uploads the tile sheet and palette every call so animated tiles
// keep animating. Call once per VR frame, before draw().
void update(const world::Snapshot& s);

// Draw the board. `view_proj` is the eye's combined matrix; the model matrix
// (board placement and scale) is owned here.
void draw(const math::Mat4& view_proj);

// ── Inspection ───────────────────────────────────────────────────────────────
//
// Draw with a caller-supplied model matrix and debug mode, bypassing board
// placement entirely so the offline inspector can put cameras in raw MAP CELL
// coordinates. `debug` 0 is the normal render; 1 false-colours every column by
// the height the repeat detector gave it.
// Presentation-only multiplier. Every call supplies its own value; the game
// and batch callers remain neutral. Debug views ignore tint.
struct Tint { float r=1, g=1, b=1; };
void draw_raw(const math::Mat4& view_proj, const math::Mat4& model, int debug, Tint tint = {});
// Monotonic diagnostic counter of mesh VBO uploads, not texture uploads.
uint64_t mesh_upload_count();

// Map size in cells, and where the player is in fractional cells — the
// inspector frames its shots around both.
void map_size(float* w, float* h);

// ── The object model ─────────────────────────────────────────────────────────
//
// WHAT SEGMENTATION FOUND, promoted from an internal detail to a real
// interface — deliberately, and no further.
//
// The mesher already answers "which cells belong to the same drawing", with a
// union-find over standing cells bounded into a PROP, a STRUCTURE or a MASS.
// Until now that lived entirely inside diorama.cpp and the only thing that
// escaped was [object]/[mass] text on stderr.
//
// rubyvr_studio needs to read it: select an object, show its pattern, find
// every other instance of that pattern, author one property, apply to all
// matches. That needs the footprint, the classification, the tile-id signature
// and the cell -> object mapping. It does not need anything else the mesher
// knows, so nothing else is here.
//
// EXPOSING THE NAMESPACE WHOLESALE WOULD HAVE BEEN THE MISTAKE. It freezes
// today's implementation as public API and blocks the refactors the geometry
// still needs — the silhouette machinery, the carve/box thresholds, the fold,
// the roof split and every GL object stay private. If the studio ever needs to
// explain WHY something classified as it did, the answer is a reason field
// here, not a published constant.

// Which of the three things a segmented component turned out to be.
//
// A MASS is not a failure: cliffs, hedges and cave walls are masses, and so is
// a building whose door fell outside the rectangle it was isolated to. It
// renders through the per-column path, and it is named rather than absorbed so
// a component nothing claims stays visible in the numbers.
enum class ObjectClass : uint8_t { kProp, kStructure, kMass };

struct Object {
    // Bounding box, in BACKUP-MAP cells — the space the grid, the acceptance
    // scenes and object coordinates all already use.
    int x = 0, y = 0;
    int w = 0;        // columns
    int extent = 0;   // rows of drawing: footprint rows plus overhang rows

    // Rows of SOLID member cells. Zero is legal, and means the component is
    // overhang art touching nothing solid at all — which is classed kMass.
    int footprint_rows = 0;

    ObjectClass cls = ObjectClass::kMass;
    bool        has_door = false;

    // WHAT WAS AUTHORED RATHER THAN INFERRED, if anything.
    //
    // `active` says an override pattern claimed these cells; the rest are the
    // values it supplied, with -1 (or a negative rise) meaning "this one was
    // left to inference". So the studio can show, per object, exactly which
    // properties a person decided and which the data still answers — which is
    // the distinction the whole project turns on.
    struct Authored {
        bool  active = false;
        int   roof_rows = -1;
        float rise = -1.0f;
        int   height = -1;
    };
    Authored authored;

    // THE ORDERED TILE-ID SIGNATURE. Row-major over the bounding box, exactly
    // w * extent entries, ids[0] the north-west corner:
    //
    //     ids[r * w + c] == snapshot.metatile_id(x + c, y + r)
    //
    // This is the same rectangle the mesher itself meshes — build_silhouette is
    // handed these very ids — which is what makes it a legitimate key for an
    // override. A key derived from anything else could match a pattern the
    // geometry was never actually built from, and that failure would be silent.
    //
    // The bounding box may contain cells belonging to a DIFFERENT component;
    // the signature covers the rectangle regardless, because the rectangle is
    // what the hull is built from. Ask cell_object about membership.
    std::vector<uint16_t> ids;
};

struct ObjectModel {
    int width = 0, height = 0;          // the Snapshot's backup-map dimensions

    // ORDERED, and the order is part of the contract: objects appear in the
    // order their FIRST MEMBER CELL is reached by a row-major scan — north to
    // south, then west to east. Unique by construction, because every standing
    // cell belongs to exactly one component.
    std::vector<Object> objects;

    // width * height entries: an index into `objects`, or -1 for a cell that
    // never stood at all. This is the membership answer.
    std::vector<int32_t> cell_object;

    int object_at(int cell_x, int cell_y) const {
        if (cell_x < 0 || cell_y < 0 || cell_x >= width || cell_y >= height)
            return -1;
        return cell_object[static_cast<size_t>(cell_y) * width + cell_x];
    }
};

// Segment `s` into objects, and hand the caller a result it owns outright.
//
// PURE, and each part of that is load-bearing. No GL and no init(), so the
// studio can build a model before any window exists. No globals and no
// environment dials — RUBYVR_MIN_UNIT and RUBYVR_ROOF_RISE move GEOMETRY but
// not segmentation, so objects do not shift while you sweep min-unit in the
// viewer. Therefore also safe from any thread, unlike capture() (emulation
// thread only) and update() (GL thread only).
//
// The result is a VALUE. Nothing in it points back at mesher state, so it stays
// valid across later update(), build_isolate(), build_turntable() and
// shutdown() calls, and holding one cannot change what the next draw() shows.
//
// It describes the Snapshot it is HANDED: give it an isolate snapshot and you
// get that isolate's segmentation, exactly as build_mesh would see it.
//
// Returns false for a null `out` or an invalid Snapshot.
//
// This two-argument form means EXACTLY "no overrides", which is what keeps the
// purity above literally true. When overrides are in play, pass them.
bool segment(const world::Snapshot& s, ObjectModel* out);

// The same, with an override set applied — patterns matched against the grid,
// verified structurally, and their cells claimed before segmentation runs.
//
// PASS THE SAME SET THE MESHER HAS, or the two will describe different worlds.
// That is not a trap so much as the honest consequence of segment() being a
// pure function of its arguments rather than a reader of mesher state.
bool segment(const world::Snapshot& s, const overrides::OverrideSet& ov,
             ObjectModel* out);

// Give the mesher an override set and force a rebuild, exactly as
// set_min_unit does.
//
// THIS IS CONFIGURATION, NOT A RESULT, and the distinction is the whole reason
// it is allowed to be global when the object model is not. The objection to
// mutable mesher globals was about handing a CALLER a result whose lifetime is
// tied to the next update(); a dial the caller sets is the opposite direction,
// and diorama already establishes the pattern for min_unit.
void set_overrides(overrides::OverrideSet ov);

// What the mesher is currently using, so a tool can hand the same set to
// segment() and be sure the two agree.
const overrides::OverrideSet& current_overrides();

// ── Tileset turntable ────────────────────────────────────────────────────────
//
// Every distinct metatile on the current map, alone on its own plot of plain
// ground, meshed by the ORDINARY mesher. That last part is the point: the
// question a turntable answers is "what do the rules do with this metatile",
// and a second geometry path built to answer it would drift from the one that
// actually runs and start answering about itself.
//
// It audits the TILESET rather than the assembled map. On the real map a
// metatile's geometry depends on the run it lands in, on its neighbours' heights
// and on which faces are buried — so a wrong rule shows up as one odd shape
// among six hundred. Alone on a plot there is nothing else to blame.
struct Turntable {
    std::vector<uint16_t> ids;        // metatile per plot, row-major
    std::vector<uint8_t>  height;     // cells the plot's unit stands, 0 = flat
    std::vector<uint8_t>  collision;  // the representative collision it was given
    int cols = 0, rows = 0;
    int spacing = 0;                  // cells from one plot's origin to the next
};

// Replace the mesh with the turntable's. The real map's mesh is gone until the
// next map change, so this is for the offline inspector, not for a live session.
bool build_turntable(const world::Snapshot& s, Turntable* out);

// Replace the mesh with ONE RECTANGLE of the map, lifted onto empty ground.
//
// The isolation is the whole value: on the real map a structure's geometry
// depends on every neighbour it touches, so a defect in one is
// indistinguishable from an interaction between two. Alone on a plot there is
// nothing else it could be. Also moves the inspector's focus to the subject.
//
// Destructive in the same way build_turntable is — offline only.
bool build_isolate(const world::Snapshot& s, int x, int y, int w, int h);

// Direct definition preview in local coordinates; no fabricated snapshot or
// unrelated pattern matches. Uses the same cutout emitter as the live map.
bool build_cutout_preview(const world::Snapshot&, const overrides::Pattern&,
                          uint64_t* geometry_hash = nullptr, int* vertices = nullptr);
// CPU inspection of the actual authored emitter, for material and topology checks.
// Triangle order is production clockwise winding; UVs identify winning source texels.
struct AuthoredVertex {
    part_geometry::Vec position;
    float u, v;
    uint16_t tile, palette;
};
// Optional part index per triangle. Picking splits otherwise mergeable voxel
// runs at part boundaries; positions, source texels and visible coverage agree
// with the rendered mesh. The normal rendering/cache path is unchanged.
bool inspect_authored_mesh(const world::Snapshot&, const overrides::Pattern&, std::vector<AuthoredVertex>*,
                           std::vector<size_t>* triangle_parts = nullptr);
// `closed_base` adds the connected explorer's -16 source-pixel preview base to
// the same shared mesher output. The editor path leaves it false; the region
// path and its equivalence checks opt in.
bool inspect_diorama_mesh(const world::Snapshot&, const overrides::OverrideSet&,
                          std::vector<AuthoredVertex>*, DioramaStats*, bool closed_base = false);

// Bounded desktop region. Each source keeps its own indexed tiles/palette.
// Offsets translate backup-map coordinates; input snapshots are borrowed only
// during build/inspection. Ordinary map/model buffers are never replaced.
struct RegionMap { const world::Snapshot* source=nullptr; int x=0,z=0; };
struct RegionStats {
    size_t maps=0, vertices=0, gpu_bytes=0, primary_cells=0, padding_cells=0;
    size_t hidden_cells=0, raised_instances=0, terrain_rejected=0, unresolved_placements=0;
    // vertices is the expanded, ordered scene; stored_vertices counts VBO
    // storage. Reuse reduces memory, not the triangles submitted to the GPU.
    size_t stored_vertices=0,models=0,model_instances=0,deformed_instances=0,batches=0;
    uint64_t geometry_hash=0;
    size_t draw_calls=0,drawn_vertices=0;
    size_t uploaded_maps=0,reused_maps=0,released_maps=0;
};
struct RegionMesh { std::vector<AuthoredVertex> vertices; DioramaStats stats; };
struct PreparedRegion;
// Preparation owns all CPU/source data and touches no GL/global renderer state.
// Publish is main-thread-only and retains the old complete region on failure.
std::shared_ptr<PreparedRegion> prepare_region(const std::vector<RegionMap>&,
    const overrides::OverrideSet&,std::string* error,const std::atomic<bool>* cancel=nullptr);
bool publish_region(const std::shared_ptr<PreparedRegion>&,std::string* error);
bool inspect_region_mesh(const std::vector<RegionMap>&, const overrides::OverrideSet&,
                         std::vector<RegionMesh>*, RegionStats*, std::string* error);
bool build_region(const std::vector<RegionMap>&, const overrides::OverrideSet&, std::string* error);
void clear_region();
// Live regional consumer: update current-map materials and actors without
// building a duplicate single-map mesh. Actor coordinates remain map-local.
void update_region_live(const world::Snapshot&);
const RegionStats& region_stats();
bool region_bounds(part_geometry::Vec* lo,part_geometry::Vec* hi);
void draw_region_raw(const math::Mat4& view_proj, int debug=0, Tint tint={});

// Change the minimum standing-unit height and force a rebuild. Exists so the
// live viewer can sweep the height rule and watch the world change, which beats
// diffing two static renders.
void set_min_unit(int cells);
void player_cell(float* x, float* y, float* z);

// Board placement, driven from the comfort-key poller.
//   scale_by(+/-)  bigger / smaller
//   raise(+/-)     up / down
//   place(pos,yaw) put the board in front of the viewer
void scale_by(float factor);
void raise(float metres);
void place(float x, float y, float z, float yaw);

// ── First person ─────────────────────────────────────────────────────────────
//
// WHO MOVES, because it is the only real choice here:
//
//   The camera is the headset. We cannot move it — the runtime owns the head
//   pose and moving it would fight tracking and make people ill. So when the
//   player walks in-game, THE WORLD SLIDES around a stationary viewer. Your own
//   physical steps still move you within it, because those come through the
//   head pose as usual.
//
//   The world's YAW is never driven by the game. Turning in-game rotates your
//   facing, and rotating the world to match would spin the room around you —
//   the single most reliably sickening thing in VR. You turn by turning; the
//   snap-turn below exists for chairs that do not swivel.
void set_first_person(bool on);
bool first_person();

// Pin the world so the player's tile sits at (x, y, z) with the given yaw.
// Called on entering first person and on snap turns, not every frame.
void anchor_first_person(float x, float y, float z, float yaw);

// Rotate the world about the viewer, in whole steps. Snap, never smooth:
// smooth rotation you did not initiate with your neck is the classic trigger.
void snap_turn(float radians);

// True once a map has actually been meshed — the frame loop skips drawing
// before then rather than submitting an empty buffer.
bool has_geometry();

}  // namespace diorama
}  // namespace vr
