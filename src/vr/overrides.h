// overrides.h — authored answers to the questions the map data does not ask.
//
// WHY THIS EXISTS, and it is the whole argument for the authoring tool:
//
//   _docs/voxel-geometry.md states the gap outright. For a PROP, "how tall and
//   what shape" is answered by the art's own silhouette. For a BUILDING,
//   *nothing states it* — and every failure this project has had came from
//   inventing a rule where the data was silent. The hipped roof has had two
//   attempts and two reverts.
//
//   So that one row gets AUTHORED. Infer what the data states, author what it
//   does not, never invent.
//
// AUTHORED PARAMETERS AND PRIMITIVES, NEVER BAKED VERTICES. Parts and transforms
// are now allowed by studio-workflow-rev2 and studio-parts-contract. The game and
// studio retain ONE rendering path; the shared mesher builds every vertex.
// A file carrying baked quads would be a
// second geometry source, and diorama.h's own turntable comment already names
// that failure: a second geometry path "would drift and start answering about
// itself."
//
// MATCHED AGAINST THE GRID, BEFORE SEGMENTATION.
//
//   The alternative was to key an override on segmentation's OUTPUT — on an
//   Object's bbox and ids. That is cheaper, and it is wrong twice over. It
//   cannot fix what segmentation gets wrong, because when two trees fuse there
//   is no object carrying the right key. And it would make every authored file
//   only as stable as kSeamJoin and kCarveMaxSolid — coupling authored data to
//   the exact constants diorama.h deliberately keeps private, so tuning a
//   threshold would silently invalidate every override ever written.
//
//   Matching against the raw grid keeps the two independent. A pattern is a
//   rectangle of metatile ids; where it matches, it CLAIMS those cells and
//   defines the object itself.
//
// STRUCTURAL VERIFICATION, NOT A HASH.
//
//   A hash may index; it may never decide. A match is accepted only after the
//   full id sequence, the member mask AND the definition of every referenced
//   metatile compare equal.
//
//   That last part is not belt-and-braces, it is the whole tileset check.
//   Metatile ids only mean something within a tileset PAIR: id 468 is a tree
//   under General+Petalburg and something else entirely under a different
//   secondary tileset. Pinning each referenced id's eight tile entries and its
//   attribute word makes a pattern self-verifying, with no separate
//   tileset-identity field to keep in sync.
//
//   And it is scoped to REFERENCED ids on purpose. A digest over the whole
//   metatile table would be useless here: Milestone 1 established that a live
//   snapshot's tables carry ROM over-read garbage above the last defined id, so
//   a whole-table digest differs between a live capture and a disk build even
//   when every id the map uses is identical.

#pragma once

#include <cstdint>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ruby_world.h"
#include "part_geometry.h"

namespace vr {
namespace overrides {

// Bumped whenever the file's meaning changes. load() REFUSES anything else
// rather than guessing — reading v1 bytes under v2 rules is how a format
// silently starts meaning something different.
inline constexpr uint32_t kVersion = 5;
inline constexpr uint32_t kVoxelVersion = 6;
inline constexpr uint32_t kTerrainVersion = 7;
inline bool supported_version(uint32_t v) { return v == kVersion || v == kVoxelVersion || v == kTerrainVersion; }

// One referenced metatile's definition, as the pattern expects to find it.
struct TileDef {
    uint16_t entries[world::kTilesPerMetatile] = {};
    uint16_t attr = 0;
    bool operator==(const TileDef&) const = default;
};

// V7 terrain is map-local, in backup cells and native source pixels. Physical
// height is authored independently of the game's collision/elevation bits.
enum class TerrainKind { Ground, Water, Deck };
struct TerrainSurface {
    int layer = 0, height = 0, thickness = 0;
    TerrainKind kind = TerrainKind::Ground;
    int top = -1, side = -1; // -1 uses the cell's original/recovered floor
    // Ground grade: NW height, NE/SW rises and an extra SE corner correction.
    // Zero preserves flat v7 surfaces; the top uses the NW-SE diagonal.
    int rise_x = 0, rise_z = 0, corner_delta = 0, side_offset = 0;
    bool operator==(const TerrainSurface&) const = default;
};
struct TerrainCell {
    int x = 0, y = 0;
    uint16_t expected = 0; // the complete packed source cell, including layer
    int underlay = -1; // -1 retains automatic floor recovery and Ground pixels
    std::vector<TerrainSurface> surfaces;
    bool operator==(const TerrainCell&) const = default;
};
struct TerrainMap {
    int group = -1, number = -1, width = 0, height = 0;
    std::vector<TerrainCell> cells;
    std::map<uint16_t, TileDef> tiles; // structural guards for source + materials
    bool operator==(const TerrainMap&) const = default;
};

// What an override says about the object it claims. Every field is ABSOLUTE and
// never a delta: "roof_rows 2", not "+1". A delta silently re-means itself the
// moment the inference rule it modifies changes, which is the one thing an
// authored file must never do.
//
// -1 (or a negative rise) means "leave this to inference", so a pattern can
// author one property and let the rest stand.
enum class ApplyClass : int8_t { kInfer = -1, kProp = 0, kStructure = 1, kMass = 2 };

struct Apply {
    ApplyClass cls = ApplyClass::kInfer;
    int        roof_rows = -1;   // rows of the drawing that are roof
    float      rise = -1.0f;     // cells of ridge rise PER ROOF ROW, the same
                                 // units as RUBYVR_ROOF_RISE
    int        height = -1;      // cells the object stands
    bool operator==(const Apply&) const = default;
};

// Editor provenance, never a runtime matching filter. Coordinates are in the
// connection-inclusive backup map; a matcher anchor is a different concept.
struct Source {
    std::string room;
    int x = 0, y = 0;
    bool operator==(const Source&) const = default;
};

// Pixel opacity is independent of metatile membership. A present empty image
// deliberately emits no model; only absence requests ordinary inference.
struct Cutout {
    int w = 0, h = 0;
    std::vector<uint8_t> opacity;
    bool operator==(const Cutout&) const = default;
};

enum class PartKind { Box, Billboard, Wedge };
using ArtRegion = std::array<int,4>;
struct Surface {
    ArtRegion region{};
    bool flip_u=false, flip_v=false;
    std::array<int,2> offset{}; // native pixel phase, before repetition and flips
    bool operator==(const Surface&) const = default;
};
struct Voxel {
    int pixels_per_cell=16;
    Cutout ground, shadow; // disjoint source ownership; object is Pattern::cutout
    bool operator==(const Voxel&) const = default;
};
struct Part {
    std::string id, name;
    PartKind kind = PartKind::Box;
    part_geometry::Transform transform;
    int wedge_axis=0, wedge_direction=1; // X=0 or Z=2; local high edge +/-1
    std::array<int,4> art_region{}; // source-pixel x,y,w,h; all zero = full drawing
    // V6 only: front, back, right, left, top, bottom; native-scale repetition.
    std::vector<Surface> surfaces;
    // Optional v6 relief edge material. Front/back retain the masked drawing;
    // side/top/bottom repeat this object-owned patch at native pixel scale.
    std::optional<Surface> side_art;
    std::optional<Cutout> local_mask; // cropped billboard relief, top-left pixels
    bool operator==(const Part&) const = default;
};

struct Pattern {
    std::string id;              // stable editor identity; optional for batch definitions
    Source source;
    std::string name;            // a human label. NOT an identity.
    int         w = 0;
    int         extent = 0;

    // Row-major over the bounding box, north-west corner first, w * extent
    // entries each.
    std::vector<uint16_t> ids;
    std::vector<uint8_t>  mask;   // 1 = a member cell, 0 = don't care

    // WHY THE MASK EXISTS. A component's bounding box can contain cells
    // belonging to a DIFFERENT component. Requiring those to match would
    // over-specify the pattern — the same tree standing beside a different
    // neighbour would stop matching, which defeats the entire point of "find
    // every instance". Only member cells are compared.
    std::map<uint16_t, TileDef> tiles;   // every id the mask references

    Apply apply;
    std::optional<Cutout> cutout;
    std::optional<Voxel> voxel; // explicit opt-in; v5 primitives keep their meaning
    std::vector<Part> parts;
    bool model_seeded = false;
    bool follow_ground = false; // flexible ground cover; rigid models keep one anchor

    // Index into ids/mask of the first member cell in row-major order. Matching
    // anchors here, so only cells carrying that metatile are ever tested.
    int anchor = -1;

    int cells() const { return w * extent; }
    // Existing v6 roles can deliberately restore floor without a raised model.
    // This classification does not relax mask/source validation.
    bool ground_only() const {
        if(!voxel || !cutout || !parts.empty()) return false;
        for(auto value:cutout->opacity) if(value) return false;
        for(auto value:voxel->shadow.opacity) if(value) return true;
        return false;
    }
    bool operator==(const Pattern&) const = default;
};

// A loaded file. A value: no globals, no lifetime tied to anything.
struct OverrideSet {
    uint32_t             version = kVersion;
    std::vector<Pattern> patterns;
    std::vector<TerrainMap> terrain;

    bool empty() const { return patterns.empty() && terrain.empty(); }
    bool operator==(const OverrideSet&) const = default;
};

// Read `path`. Returns false, having said why, on a missing file, an unknown
// version, or a pattern that does not describe itself consistently (ids and
// mask the wrong length, no member cells, a member id with no definition).
//
// A pattern that fails validation fails the whole load. Half-loading an
// authored file would apply some of an author's intent and not the rest.
bool load(const char* path, OverrideSet* out);
// Conservative pre-composition face budget; rejects oversized models before allocation.
bool valid_parts(const Pattern&);

// One place a pattern matched, given by the bounding box origin in backup-map
// cells.
struct Match {
    int pattern = -1;
    int x = 0, y = 0;
};

// Every match of every pattern against `s`, sorted by origin (north to south,
// then west to east) so that overlap resolution is deterministic.
//
// Verification is structural and complete: a Match is only produced where the
// ids, the mask and every referenced metatile definition agree. A pattern whose
// tileset does not match this snapshot is rejected once, up front, rather than
// tested at every cell.
std::vector<Match> find(const world::Snapshot& s, const OverrideSet& set);

// The same deterministic ownership decision used by the mesher and editor.
struct Claims {
    std::vector<Match> accepted, rejected;
    std::vector<int> owner; // backup cell -> pattern index, -1 unclaimed
};
Claims resolve(const world::Snapshot& s, const OverrideSet& set);

}  // namespace overrides
}  // namespace vr
