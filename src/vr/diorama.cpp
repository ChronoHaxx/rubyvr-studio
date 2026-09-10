#include "cutout.h"
#include "terrain.h"
// diorama.cpp — see diorama.h for the pop-up-book rule and why the GPU indexes.

#include "diorama.h"
#include "gl_loader.h"
#include "tileset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace vr {
namespace diorama {
namespace {

// ── Geometry conventions ─────────────────────────────────────────────────────
//
// One metatile is one unit. Map X grows east (+X), map Y grows south (+Z), and
// the board lies in the XZ plane with +Y up. Map Y to world Z is the whole
// trick of turning a top-down game into a tabletop: what was "down the screen"
// becomes "away from you across the table".
//
// The mesh is built in these raw cell units with the map's (0,0) at the origin.
// Placement, scale and centring all live in the model matrix, so moving or
// resizing the board never rebuilds a vertex.
constexpr float kSubTile = 0.5f;   // a metatile is 2x2 sub-tiles

// Height of a cell's surface, from its 4-bit elevation.
//
// TODO(human): this is a FEEL decision, not a correctness one, and it is worth
// tuning with the headset on.
//
// Gen 3 elevation is not a height in any unit. 3 is ordinary ground, 0 means
// "transition / matches anything" (doorways, stairs, the seam at a cliff edge),
// and the rest are relative levels. So a linear elev * k buries the whole world
// three units underground and makes every doorway a pit. The table below pins 3
// to zero and treats 0 as ground, which is the least surprising reading.
//
// Worth trying once you can see it: a smaller step (0.25) reads as a gentle
// relief map, a larger one (0.6) as a proper stacked plateau. 15 is reserved
// for special cases and is clamped rather than sent to the moon.
float height_for(uint8_t elevation) {
    constexpr float kStep = 0.35f;
    if (elevation == 0) return 0.0f;          // transition: sit at ground
    if (elevation >= 15) return 0.0f;         // special marker, not a height
    return (static_cast<float>(elevation) - 3.0f) * kStep;
}

// ── Vertex format ────────────────────────────────────────────────────────────
//
// Position, tile-local UV, and the tile/palette indices the shader needs to
// look up a colour. Flips are baked into the UVs at build time, so the shader
// never has to know about them.
struct Vertex {
    float    x, y, z;
    float    u, v;
    uint16_t tile;
    uint16_t pal;
    uint8_t  shade;   // 0..255, multiplied into the colour
    uint8_t  unit_h;  // this column's unit height in cells; 0 = flat ground.
                      // Carried purely so the inspector can false-colour by
                      // classification and show WHY the mesh looks how it does.
};

// Per-face brightness. Voxel geometry with one texture on every face reads as
// flat no matter how correct the shape is — the eye needs the faces to differ
// to see a box at all. A fixed "sun" in the north-west is enough; real lighting
// would be worse, because the source art already contains its own baked
// shading and doubling that up looks muddy.
//
// THE SOUTH FACE IS NOT SHADED, and that is the point of the whole scheme.
// Once all four sides fold the same front stack (see the fold rule in pass 2),
// the south face is showing the artwork as the artist drew it, viewed the way
// the game views it. Darkening it would darken the drawing. The other three
// sides show that same drawing turned to face a direction it was never drawn
// for, so they take the shading — that is what makes them read as "the flank of
// this thing" rather than "another copy of its front".
//
// The top drops slightly below full for the same reason: a standing drawing
// with an equally bright plateau behind it reads as one flat field of art. The
// ratios are DRAMALESS_SHAPE's (Voxel3D.FACE_SHADE and VOLUME_TOP_SHADE),
// scaled to 0..255.
constexpr uint8_t kShadeTop   = 217;   // 0.85
constexpr uint8_t kShadeSouth = 255;   // 1.00 — the drawing itself
constexpr uint8_t kShadeEast  = 214;   // 0.84
constexpr uint8_t kShadeWest  = 184;   // 0.72
constexpr uint8_t kShadeNorth = 173;   // 0.68

// Flat ground stays at full brightness: seen from above, it is exactly what the
// GBA draws, so anything else is a deviation from the source frame.
constexpr uint8_t kShadeFlat  = 255;

// ── Shaders ──────────────────────────────────────────────────────────────────

const char* kVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in uint aTile;
layout(location = 3) in uint aPal;
layout(location = 4) in float aShade;
layout(location = 5) in uint aUnitH;
layout(location = 6) in vec3 aOffset;

uniform mat4 uMVP;
uniform int uPlaced;
uniform vec2 uRegion;

out vec2 vUV;
out vec3 vPosition;
out float vShade;
flat out uint vTile;
flat out uint vPal;
flat out uint vUnitH;

void main() {
    vec3 position = aPos;
    if (uPlaced != 0) position += aOffset;
    position.x += uRegion.x;
    position.z += uRegion.y;
    vUV    = aUV;
    vPosition = position;
    vShade = aShade;
    vTile  = aTile;
    vPal   = aPal;
    vUnitH = aUnitH;
    gl_Position = uMVP * vec4(position, 1.0);
}
)";

// The indexed-colour lookup the GBA's PPU does in hardware, done per fragment.
//
// texelFetch, not texture(): these are exact integer lookups into an index
// table, and any filtering would blend INDICES — sampling halfway between
// palette entry 3 and 12 would give 7, an unrelated colour. That is the classic
// way palette-indexed rendering goes wrong, and it looks like random confetti
// along every tile edge.
const char* kFrag = R"(#version 330 core
in vec2 vUV;
in vec3 vPosition;
in float vShade;
flat in uint vTile;
flat in uint vPal;
flat in uint vUnitH;

uniform usampler2D uTiles;   // 256x256 R8UI: 32x32 tiles of 8x8 palette indices
uniform sampler2D  uPal;     // 16x16 RGBA8:  x = colour index, y = palette
uniform int        uDebug;   // 0 normal, 1 false-colour by unit height
uniform vec4       uTint;    // presentation only; never changes palette indices

out vec4 FragColor;

void main() {
    ivec2 origin = ivec2(int(vTile % 32u) * 8, int(vTile / 32u) * 8);
    ivec2 inTile = ivec2(clamp(vUV * 8.0, vec2(0.0), vec2(7.999)));
    uint idx = texelFetch(uTiles, origin + inTile, 0).r;

    // Colour 0 is transparent, not black. Discarding rather than blending keeps
    // the depth buffer honest, which matters because the billboards are drawn
    // in the same pass as the ground.
    if (idx == 0u) discard;

    vec3 c = texelFetch(uPal, ivec2(int(idx), int(vPal)), 0).rgb;

    // Classification view: colour by how tall the repeat detector made this
    // column. Grey ground, green 1-cell, yellow 2-cell, red 3+. A forest that
    // has wrongly fused into a building shows up as a slab of red; a house that
    // failed to merge shows as green where it should be red.
    if (uDebug == 1) {
        if      (vUnitH == 0u) c = vec3(0.30, 0.30, 0.32);
        else if (vUnitH == 1u) c = vec3(0.25, 0.80, 0.35);
        else if (vUnitH == 2u) c = vec3(0.90, 0.82, 0.20);
        else                   c = vec3(0.95, 0.25, 0.25);
    }

    if(uDebug == 2) {
        vec3 n = abs(normalize(cross(dFdx(vPosition), dFdy(vPosition))));
        FragColor = vec4(vec3(0.76, 0.78, 0.81) * (0.4 + 0.4*n.y + 0.2*n.z), 1.0);
        return;
    }
    FragColor = vec4(c * vShade * (uDebug == 0 ? uTint.rgb : vec3(1.0)), 1.0);
}
)";

// ── State ────────────────────────────────────────────────────────────────────

bool     g_ready = false;
GLuint   g_prog = 0, g_vao = 0, g_vbo = 0;
GLuint   g_tex_tiles = 0, g_tex_pal = 0;
GLint    g_u_mvp = -1, g_u_tiles = -1, g_u_pal = -1, g_u_debug = -1;
GLint    g_u_tint = -1;
GLint    g_u_placed = -1, g_u_region = -1;
uint64_t g_mesh_upload_count = 0;
GLsizei  g_vertex_count = 0;

uint32_t g_meshed_layout = 0;    // which map the current mesh is of
float    g_map_w = 0, g_map_h = 0;

// Board placement. Metres, in the LOCAL reference space.
float g_board_scale = 0.045f;    // ~1.5 m across for a 35-cell map
float g_board_x = 0.0f, g_board_y = -0.45f, g_board_z = -0.9f;
float g_board_yaw = 0.0f;

// First person. A metatile is 16 GBA pixels and reads as roughly a metre of
// world, so scale 1.0 puts you at human size among the trees — which is the
// entire point, and also why every billboard flaw becomes visible at once.
bool  g_fpv = false;
float g_fpv_scale = 1.0f;
float g_fpv_ax = 0.0f, g_fpv_ay = -1.6f, g_fpv_az = 0.0f;   // where you stand
float g_fpv_yaw = 0.0f;

// The player's position in fractional cells, refreshed every update() so
// walking is smooth rather than a metre-per-step teleport.
float g_player_x = 0.0f, g_player_z = 0.0f, g_player_y = 0.0f;

std::vector<uint8_t> g_tile_scratch;   // 4bpp expanded to one index per byte

// The authored answers currently in force. CONFIGURATION, not a result — see
// set_overrides in diorama.h for why this one is allowed to be a global when
// the object model deliberately is not. Empty by default, so a build that is
// never handed a file behaves exactly as it did before overrides existed.
overrides::OverrideSet g_overrides;
BuildMode g_build_mode=BuildMode::Inferred;
DioramaStats g_diorama_stats;
int g_terrain_group=-2,g_terrain_number=-2;
bool g_terrain_identity=false;
int g_terrain_width=0,g_terrain_height=0;
std::vector<uint16_t> g_terrain_grid,g_terrain_metatiles,g_terrain_attributes;
std::vector<world::ConnectionSlice> g_terrain_connections;

GLuint compile(GLenum stage, const char* src, const char* label) {
    const GLuint s = gl::glCreateShader(stage);
    gl::glShaderSource(s, 1, &src, nullptr);
    gl::glCompileShader(s);
    GLint ok = 0;
    gl::glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        gl::glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
        std::fprintf(stderr, "[diorama] %s shader failed:\n%s\n", label, log);
        gl::glDeleteShader(s);
        return 0;
    }
    return s;
}

// Append one textured quad. Corners are given in winding order; `e` supplies
// the tile, palette and the flips, which are folded into the UVs here so the
// shader stays ignorant of them.
void push_quad(std::vector<Vertex>& out, world::TileEntry e, uint8_t shade,
               uint8_t unit_h,
               float ax, float ay, float az, float bx, float by, float bz,
               float cx, float cy, float cz, float dx, float dy, float dz,
               bool include_tile_zero = false) {
    // Skip tile 0 entirely: in this data it is the blank tile, and emitting six
    // vertices per empty sub-tile roughly doubles the mesh for nothing.
    if (e.index == 0 && !include_tile_zero) return;

    float u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;
    if (e.hflip) { u0 = 1.0f; u1 = 0.0f; }
    if (e.vflip) { v0 = 1.0f; v1 = 0.0f; }

    const Vertex a{ax, ay, az, u0, v0, e.index, e.palette, shade, unit_h};
    const Vertex b{bx, by, bz, u1, v0, e.index, e.palette, shade, unit_h};
    const Vertex c{cx, cy, cz, u1, v1, e.index, e.palette, shade, unit_h};
    const Vertex d{dx, dy, dz, u0, v1, e.index, e.palette, shade, unit_h};

    out.push_back(a); out.push_back(b); out.push_back(c);
    out.push_back(a); out.push_back(c); out.push_back(d);
}

// Expand the snapshot's raw 4bpp VRAM bytes to one palette index per byte,
// which is what the R8UI texture wants. Two pixels per byte, LOW nibble first.
void expand_tiles(const world::Snapshot& s) {
    g_tile_scratch.assign(static_cast<size_t>(256) * 256, 0);
    const size_t n = s.vram_tiles.size();

    for (int tile = 0; tile < world::kTileCount; ++tile) {
        const size_t src = static_cast<size_t>(tile) * world::kTileBytes;
        if (src + world::kTileBytes > n) break;

        const int ox = (tile % 32) * 8;
        const int oy = (tile / 32) * 8;

        for (int y = 0; y < 8; ++y) {
            const uint8_t* row = s.vram_tiles.data() + src + y * 4;
            for (int x = 0; x < 8; ++x) {
                const uint8_t byte = row[x >> 1];
                const uint8_t idx  = (x & 1) ? static_cast<uint8_t>(byte >> 4)
                                             : static_cast<uint8_t>(byte & 0x0F);
                g_tile_scratch[static_cast<size_t>(oy + y) * 256 + (ox + x)] = idx;
            }
        }
    }
}

// ── Reading metatile pixels ──────────────────────────────────────────────────
constexpr int kPropPx = 16;   // a metatile is 16x16 pixels, and so is the hull

// One pixel of a metatile PAIR, with the tile entry it came from.
//
// (px, py) are 0..15 across the whole 16x16 metatile; the pair's four 8x8 tiles
// are laid out TL, TR, BL, BR, matching face_metatile's indexing. Flips are
// applied to the lookup here rather than to a UV, because a per-pixel face
// samples one texel and has no UV span to flip.
//
// Returns the palette index; 0 means transparent.
uint8_t pair_pixel(const world::Snapshot& s, uint16_t id, int pair,
                   int px, int py, world::TileEntry* out_e,
                   int* out_tx, int* out_ty) {
    const size_t base = static_cast<size_t>(id) * world::kTilesPerMetatile
                      + static_cast<size_t>(pair) * 4;
    if (base + 4 > s.metatiles.size()) return 0;

    const int sub = (py / 8) * 2 + (px / 8);
    const world::TileEntry e = world::unpack_tile_entry(s.metatiles[base + sub]);

    int tx = px % 8, ty = py % 8;
    if (e.hflip) tx = 7 - tx;
    if (e.vflip) ty = 7 - ty;

    const size_t off = static_cast<size_t>(e.index) * world::kTileBytes
                     + static_cast<size_t>(ty) * 4 + static_cast<size_t>(tx >> 1);
    if (off >= s.vram_tiles.size()) return 0;

    const uint8_t byte = s.vram_tiles[off];
    const uint8_t idx  = (tx & 1) ? static_cast<uint8_t>(byte >> 4)
                                  : static_cast<uint8_t>(byte & 0x0F);
    if (out_e)  *out_e  = e;
    if (out_tx) *out_tx = tx;
    if (out_ty) *out_ty = ty;
    return idx;
}

// One pixel of the metatile as the GAME composites it: the upper pair if it has
// anything there, the lower pair otherwise. This is the whole drawing — the
// thing a person looking at the screen sees in that cell.
uint8_t composite_pixel(const world::Snapshot& s, uint16_t id, int px, int py,
                        world::TileEntry* out_e, int* out_tx, int* out_ty) {
    const uint8_t up = pair_pixel(s, id, 1, px, py, out_e, out_tx, out_ty);
    if (up != 0) return up;
    return pair_pixel(s, id, 0, px, py, out_e, out_tx, out_ty);
}

// ── Silhouettes ──────────────────────────────────────────────────────────────
//
// CUT THE TREE OUT OF ITS BACKGROUND. A tree in a box is a box; what the art
// depicts is a shape, and the shape is recoverable.
//
// _docs/voxel-geometry.md says this comes free, because GBA colour index 0 is
// transparent and the shader already discards it. That is true of the UPPER
// pair and useless here: a census of the live tileset says the LOWER pair is
// 256/256 opaque for EVERY metatile on the map, and a tree lives almost
// entirely in the lower pair (the upper pair of a canopy metatile is empty).
// The tree is drawn over grass, exactly as unmasked as a Game Boy sprite, so
// DRAMALESS_SHAPE's flood fill is needed after all.
//
// What we do have that Gen 1 does not is the MAP. We know which metatile is
// this map's ordinary ground, so we know what the background looks like instead
// of having to guess it from brightness. Flood inward from the cell border
// through pixels drawn in a ground colour; what the flood cannot reach is the
// object.
//
// FOUR THINGS THAT ARE LOAD-BEARING, each of which failed first:
//
//   PLAIN ground only. Seeding the background set from the flowery and
//   tall-grass variants as well leaks catastrophically — 260 of 1024 pixels
//   survived and the tree came out as confetti — because those variants contain
//   the canopy's own yellow-green as their flower speckle. Metatile 1's four
//   colours are enough and are the honest description of "the ground here".
//
//   COLOUR, not brightness. The reference floods by luminance, because Game Boy
//   art has four shades and the dark ones are the outline. Ruby's canopy
//   (132,198,99) and its grass (115,198,165) differ by two units of luminance
//   and by a lot of hue; a brightness threshold keeps the dark half of the tree
//   and eats the bright half.
//
//   EIGHT-connected. Grass is dithered, so a 4-connected flood cannot thread a
//   diagonal checker and every other dither pixel survives as a speck of
//   floating debris. The reference hits this and names it.
//
//   PER METATILE, no apron. A tree spans four metatiles, and the worry was that
//   flooding one of them alone would let the background in through the seam
//   where the tree continues. It does not: those edge pixels are tree, not
//   grass, so the flood stops there on its own. Which means a mask depends only
//   on the metatile id and can be cached — and a tree wall repeating four tiles
//   across hundreds of cells costs four floods.
// A drawing whose flood leaves at least this much of its cell as background
// has a shape worth carving. Measured on the FRONT CELL of every standing
// object on this map, not tuned: trees top out at 193 of 256 (75%), a
// building's ground-floor corner (the mesher's own defect 2 — a rounded
// eroded pillar) sits at 224-233, and every wall or cliff face still comes
// back the full 256. 204 (80%, integer truncation of 256*0.8) sits in the
// 193..224 gap. Reused as-is for segmentation's own "these two cells are both
// solid enough to join unconditionally" test — see kSeamJoin's neighbourhood.
//
// This bar used to be 92% (235), which the corner clears BUT SO DOES ROOM TO
// SPARE: the Pokemon Centre's own two rounded corners (ids 96 and 99) measure
// exactly 235 — once defect 1 stood their row up (see the standing-rule
// commit) they landed right on the old bar and got carved into notches on
// their own building, the same failure as the house's corner one bar over.
// 80% was checked against a round bush (236, boxes) and a second tree species
// (dark canopy, bases 243, boxes) elsewhere on the tileset, so it does not
// eat scenery that should box; see the generalisation gate in the design
// notes before trusting it on a tileset this map does not use.
constexpr int kCarveMaxSolid = (kPropPx * kPropPx * 80) / 100;

// A mask over the WHOLE drawing of one object — 16 wide by 16 * cells tall.
//
// Not one metatile at a time, and that is the difference between a tree and a
// snowman. The hull turns each mask ROW into a disc whose depth is that row's
// own drawn width, so if the mask stops at a metatile boundary the widths
// restart there: a two-metatile tree came out as a fat ball sitting on another
// fat ball, because the canopy's bottom row and the trunk's top row each
// measured their own full span instead of one continuous taper.
//
// Stacked, the spans run from a wide crown down to a narrow trunk and the shape
// tapers the way the drawing does. DRAMALESS_SHAPE says the same thing in
// roundTemplate: "NY = 2 * NX is a drawing STACKED two cells high on ONE cell
// of plot".
//
// Row 0 is the TOP of the drawing (the unit's northmost art row) and the last
// row is the bottom (its front row).
//
// `cols` defaults to one metatile (16px) and stays there for every EXISTING
// caller — a column's own vertical stack. Step 3a of the object-mesher work
// widens it for a multi-COLUMN prop (a 2-wide tree): the same disc-chord hull
// still tapers correctly, because a row's disc is computed from THAT ROW'S
// OWN found span regardless of how many columns wide the canvas is — see
// build_hull. What must NOT scale with `cols` is DEPTH, which stays capped at
// one cell (kPropPx) everywhere it is used: depth is "how far this pixel row
// bulges within its own single cell of plot", a per-CELL concept, independent
// of how many cells of PLOT the object's WIDTH spans.
struct Silhouette {
    int                           cols = kPropPx;   // 16 * width_cells
    int                           rows = 0;         // 16 * height_cells
    std::vector<uint8_t>          solid;            // cols * rows
    std::vector<world::TileEntry> e;
    std::vector<uint8_t>          tx, ty;
    int                           count = 0;

    void resize2d(int width_cells, int height_cells) {
        cols = kPropPx * width_cells;
        rows = kPropPx * height_cells;
        const size_t n = static_cast<size_t>(cols) * rows;
        solid.assign(n, 0);
        e.assign(n, world::TileEntry{});
        tx.assign(n, 0);
        ty.assign(n, 0);
        count = 0;
    }
    void resize(int cells) { resize2d(1, cells); }
};

// The colours of this map's ordinary ground, resolved through the palette.
// Comparing resolved colour rather than (palette, index) is deliberate: the
// same green reached through two palettes is the same green to the eye, and it
// is the eye we are modelling.
struct Background {
    uint32_t rgb[64];
    int      n = 0;
    bool     has(uint32_t c) const {
        for (int i = 0; i < n; ++i) if (rgb[i] == c) return true;
        return false;
    }
    void add(uint32_t c) {
        if (n < 64 && !has(c)) rgb[n++] = c;
    }
};

Background ground_colours(const world::Snapshot& s, uint16_t ground_id,
                          const uint32_t* pal) {
    Background bg;
    for (int py = 0; py < kPropPx; ++py)
        for (int px = 0; px < kPropPx; ++px) {
            world::TileEntry e{};
            int tx = 0, ty = 0;
            const uint8_t idx = composite_pixel(s, ground_id, px, py, &e, &tx, &ty);
            const int slot = (static_cast<int>(e.palette) & 15) * 16 + (idx & 15);
            bg.add(pal[slot]);
        }
    return bg;
}

// WIDTH x HEIGHT version. `ids` is a row-major WIDTH*HEIGHT grid of metatile
// ids — ids[0] is the drawing's TOP-LEFT (northmost, westmost), so it lands
// at image row 0 col 0 and the front-east corner lands at the bottom-right.
// The single-column build_silhouette below is the width=1 case of this.
bool build_silhouette(const world::Snapshot& s,
                      const uint16_t* ids, int width, int height,
                      const Background& bg, const uint32_t* pal,
                      Silhouette* out) {
    if (width <= 0 || height <= 0) return false;
    out->resize2d(width, height);
    const int W = out->cols, H = out->rows;

    std::vector<uint32_t> colour(static_cast<size_t>(W) * H, 0);
    for (int py = 0; py < H; ++py)
        for (int px = 0; px < W; ++px) {
            const int i = py * W + px;
            const uint16_t id = ids[(py / kPropPx) * width + (px / kPropPx)];
            world::TileEntry e{};
            int tx = 0, ty = 0;
            const uint8_t idx = composite_pixel(s, id,
                                                px % kPropPx, py % kPropPx,
                                                &e, &tx, &ty);
            out->e[i]  = e;
            out->tx[i] = static_cast<uint8_t>(tx);
            out->ty[i] = static_cast<uint8_t>(ty);
            colour[i]  = idx == 0
                       ? 0u   // genuinely transparent: always background
                       : pal[(static_cast<int>(e.palette) & 15) * 16 + (idx & 15)];
        }

    std::vector<uint8_t> reached(static_cast<size_t>(W) * H, 0);
    std::vector<int> stack;
    stack.reserve(static_cast<size_t>(W) * H);

    auto seed = [&](int x, int y) {
        const int i = y * W + x;
        if (reached[i]) return;
        if (colour[i] != 0 && !bg.has(colour[i])) return;
        reached[i] = 1;
        stack.push_back(i);
    };
    for (int x = 0; x < W; ++x) { seed(x, 0); seed(x, H - 1); }
    for (int y = 0; y < H; ++y) { seed(0, y); seed(W - 1, y); }

    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        const int x = i % W, y = i / W;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                seed(nx, ny);
            }
    }

    out->count = 0;
    for (size_t i = 0; i < reached.size(); ++i) {
        out->solid[i] = static_cast<uint8_t>(!reached[i]);
        if (out->solid[i]) ++out->count;
    }
    return out->count > 0;
}

// Single-COLUMN version — every caller before step 3a of the object-mesher
// work, and most callers after it: a unit's own vertical stack, one metatile
// wide.
bool build_silhouette(const world::Snapshot& s,
                      const uint16_t* ids, int cells,
                      const Background& bg, const uint32_t* pal,
                      Silhouette* out) {
    return build_silhouette(s, ids, 1, cells, bg, pal, out);
}

// The upper pair's own alpha, as a mask. No flood needed: this is the half of
// the data where index 0 really is transparent, and it is what tall grass is.
bool build_alpha_mask(const world::Snapshot& s, uint16_t id, Silhouette* out) {
    out->resize(1);
    for (int py = 0; py < kPropPx; ++py)
        for (int px = 0; px < kPropPx; ++px) {
            const int i = py * kPropPx + px;
            world::TileEntry e{};
            int tx = 0, ty = 0;
            const uint8_t idx = pair_pixel(s, id, 1, px, py, &e, &tx, &ty);
            out->solid[i] = static_cast<uint8_t>(idx != 0);
            out->e[i]  = e;
            out->tx[i] = static_cast<uint8_t>(tx);
            out->ty[i] = static_cast<uint8_t>(ty);
            if (idx != 0) ++out->count;
        }
    return out->count > 0;
}

// How many of the upper pair's 256 pixels are opaque.
int upper_coverage(const world::Snapshot& s, uint16_t id) {
    int n = 0;
    for (int py = 0; py < kPropPx; ++py)
        for (int px = 0; px < kPropPx; ++px)
            if (pair_pixel(s, id, 1, px, py, nullptr, nullptr, nullptr) != 0) ++n;
    return n;
}

// ── Standing objects, and the projection mistake they exist to fix ───────────
//
// THE BUG THIS SOLVES, because it is the whole reason v1 looked like cardboard:
//
//   In a top-down tile map, screen-Y conflates DEPTH and HEIGHT. A tree's
//   canopy is drawn in the cell ABOVE its trunk — but physically that canopy is
//   above the trunk, not behind it. Standing each cell's upper pair at its own
//   north edge therefore turns one 2 m tree into two 1 m panels a metre apart
//   in depth. Do that to a house and you get three panels receding away from
//   you, which is exactly the slab look.
//
//   So: find each vertical RUN of cells that stand, and rebuild it as one
//   object at a single depth, stacked upward. The southernmost cell of the run
//   keeps its ground position; everything north of it goes UP instead of BACK.
//   That is the correct reading of the 2.5D projection the art was drawn in.
//
// A run of length 1 is small scenery — a tree, a sign, tall grass. Those get a
// CROSS billboard (two perpendicular quads) so they read as solid from any
// angle instead of vanishing edge-on. Longer runs are structures and get a
// single plane, because crossing a house wall would look absurd.

// WHAT STANDS, corrected again 2026-09-01 (later the same day) against the
// decomp's own map grids after RUBYVR_ISOLATE showed a house rendering as
// three towers of unequal height with a notch over its door.
//
// TWO KINDS OF CELL DRAW ABOVE THE PLAYER, and they used to be judged by one
// rule. A metatile's ATTRIBUTE layer type says which BG layer the upper pair
// goes on, and kLayerCovered was read as "this cell is flat" — which is wrong.
// Covered means "the player is drawn IN FRONT of this", and that is true of a
// ledge you hop down AND of the ground floor of every building in the game: a
// house's four rows (traced from the Oldale house, ids 620/628/636/644) are
// Covered on row 3 alone, because that is the row you stand in front of when
// you walk up to the door. The old rule gated on layer type FIRST, so it
// rejected that whole row and every building in the game lost its ground
// floor — three towers of unequal height, each with its own roof, is exactly
// what a heightfield looks like when one column is a row short.
//
// So there are two questions, not one, and they use different signals:
//
//   FOOTPRINT ("is this cell substance, floor to ceiling") is COLLISION, not
//   layer type. The game already computes it: an impassable cell is
//   something, whatever BG layer its art happens to sit on. The only reason
//   collision alone does not work is that it is also true of things that are
//   genuinely flat — a LEDGE (`row 20  id 135  coll 1  elev 0  layer 1`, the
//   kind you hop down), water, a berry-tree patch, a mountain plateau, the
//   directional one-way walls. Those are named by their BEHAVIOUR byte, not
//   inferred from shape, and excluded explicitly: see is_flat_behaviour.
//   A DOOR cell is the opposite case — the game marks it PASSABLE (you walk
//   onto it to warp) but it is unambiguously part of a building's front face,
//   so it counts as footprint regardless of collision: see is_door_behaviour.
//
//   OVERHANG ("art with no footprint of its own — you can walk behind it") is
//   what the OLD rule actually measured: layer type draws the upper pair in
//   front of the player, and that pair carries enough of the drawing to be a
//   thing rather than a stray tuft. A building's roof-top row is exactly
//   this — collision 0 (you walk behind the house under the eave) but its art
//   belongs to the object above it. TALL GRASS is the case that must NOT
//   pass: it draws above the player too, but its upper pair is a 17-pixel
//   tuft, well under the coverage floor. The observed coverage distribution
//   on this map is strongly bimodal — 0, 17, then nothing until 56, and
//   mostly 100+ — so 32 sits in an empty band rather than on a tuned edge.
//   (Grass gets a per-pixel hull instead; see build_prop_hull.)
//
// A cell counts as standing if EITHER is true. Cliff seams and water edges
// (ids 96-110, 142, 213, 214 here) sort themselves correctly once behaviour
// does the work layer type used to: 96-99 are the Pokemon Centre's own ground
// floor (97 is its door), 110/135/142/213/214 are ledge behaviours and stay
// flat, and nothing here needed a special case.
constexpr int kMinUpperStanding = 32;   // of 256

// Impassable-but-genuinely-flat: excluded from FOOTPRINT even though the game
// will not let you walk through them. Named by behaviour, not guessed from
// art, because the shape of a ledge and the shape of a wall can be identical
// in silhouette — the game already knows which is which.
//
//   MB_JUMP_*                 0x38-0x3F  a ledge you hop down
//   water, seaweed            0x10-0x1A, 0x22, 0x2A
//   MB_MOUNTAIN_TOP            0x0C       a plateau's flat top
//   MB_BERRY_TREE_SOIL         0xA0       the tilled patch; the tree itself is
//                                         an object event, not this metatile
//   MB_IMPASSABLE_* directional 0x30-0x37, 0xC0, 0xC1  a one-way wall
//
// Checked against Route 102/103/104/110, Petalburg, Rustboro, Fortree and
// Rusturf Tunnel (via the decomp's own map grids, not yet in a headset) —
// see the generalisation gate in the design notes before trusting this list
// on a tileset none of those maps use.
bool is_flat_behaviour(uint8_t behaviour) {
    if (behaviour >= 0x38 && behaviour <= 0x3F) return true;
    if (behaviour >= 0x10 && behaviour <= 0x1A) return true;
    if (behaviour == 0x22 || behaviour == 0x2A) return true;
    if (behaviour == 0x0C) return true;
    if (behaviour == 0xA0) return true;
    if (behaviour >= 0x30 && behaviour <= 0x37) return true;
    if (behaviour == 0xC0 || behaviour == 0xC1) return true;
    return false;
}

// A door: passable (you walk onto it to warp) but part of a building's front
// face, so it counts as FOOTPRINT regardless of collision. 0x69 and 0x8B are
// also DOOR_ANCHORs once object segmentation exists — a door is what tells the
// mesher "this drawing is a building" without a per-tileset table.
//
//   MB_NON_ANIMATED_DOOR  0x60   collision 0 everywhere observed (Fortree,
//                                Devon Corp, Mr Briney's house)
//   MB_ANIMATED_DOOR      0x69   collision 1 everywhere observed on this map
//   MB_WATER_DOOR         0x6C
//   MB_CLOSED_SOOTOPOLIS_DOOR 0x8B
bool is_door_behaviour(uint8_t behaviour) {
    return behaviour == 0x60 || behaviour == 0x69 || behaviour == 0x6C
        || behaviour == 0x8B;
}

// FOOTPRINT: floor-to-ceiling substance, independent of which BG layer the
// art happens to sit on. Factored out of cell_stands because the box
// branch's roof/facade split (below) needs the same question answered per
// ROW of a unit's drawing, not just per cell of the whole grid — a roof-top
// row is OVERHANG (art, no footprint) even though the rows below it, and the
// unit as a whole, plainly stand.
bool cell_is_footprint(const world::Snapshot& s, int mx, int my) {
    const uint8_t behaviour = s.behaviour(mx, my);
    if (s.collision(mx, my) != 0 && !is_flat_behaviour(behaviour)) return true;
    return is_door_behaviour(behaviour);
}

// Opaque pixels in the upper pair, indexed by metatile id. cell_stands is
// called once per cell per run-walk, and a 256-pixel count each time is not
// free, so the table is built once per mesh.
std::vector<uint16_t> upper_coverage_table(const world::Snapshot& s) {
    const size_t n = s.metatiles.size() / world::kTilesPerMetatile;
    std::vector<uint16_t> cov(n, 0);
    for (size_t id = 0; id < n; ++id)
        cov[id] = static_cast<uint16_t>(
            upper_coverage(s, static_cast<uint16_t>(id)));
    return cov;
}

bool cell_stands(const world::Snapshot& s, const std::vector<uint16_t>& cov,
                 int mx, int my) {
    const uint16_t raw = s.cell(mx, my);
    if (raw == world::kGridUndefined) return false;
    if (cell_is_footprint(s, mx, my)) return true;

    // OVERHANG. The old rule, kept for exactly the case it was right about.
    if (!world::draws_above_player(s.layer_type(mx, my))) return false;
    const uint16_t id = static_cast<uint16_t>(raw & world::kMetatileIdMask);
    return id < cov.size() && cov[id] >= kMinUpperStanding;
}

// ── Repeat-aware height: the discriminator that makes this work ──────────────
//
// The problem: a vertical run of cells that all "stand" is either ONE tall
// object (a house's facade) or MANY short ones (a column of trees). Both look
// identical per-cell — same collision, same layer type — so per-cell data
// cannot separate them, and merging blindly turns Route 101's forests into
// walls while not merging leaves houses as receding staircases.
//
// The discriminator is REPETITION. A border forest repeats a two-row canopy
// for forty rows; a house does not repeat at all. So: find the shortest period
// at which the column's metatile IDs repeat, and treat THAT as the height of
// one drawn unit. No repeat means the whole run is one object.
//
// Credit where due: this is the approach used by DRAMALESS_SHAPE (MIT,
// Copyright (c) 2026 Stahltier and others) in its Structures.buildVolume, found
// after a naive merge here had already been written and reverted for exactly
// the forest-into-monolith failure its comments describe. The implementation
// below is our own, against Gen 3 metatile IDs.
constexpr int kMaxUnitCells = 6;   // matches the reference's MAX_ROWS (48px of
                                   // drawing); nothing in Hoenn is a tower

// MINIMUM height of a standing unit. DEFAULT 1 — that is, off.
//
// It existed because the repeat detector measured Route 101's trees as one
// metatile, and taking that literally made every tree knee-high. A floor of 2
// was the cheapest stand-in for the authored per-class heights the reference
// uses (tree 16, fence 10, sign 12, cliff 32, roof 28 px).
//
// The MEASUREMENT was wrong, not the drawing. A tree IS two metatiles — a
// canopy over a base — and the classifier could only see one of them, because
// the canopy's upper pair is empty (see cell_stands). With that fixed, trees
// measure 2 on their own and the floor has nothing left to correct.
//
// What it still did was DUPLICATE. Padding a genuinely one-row drawing to two
// cells makes the fold repeat its top band, which for a tree reads as a taller
// tree and for a ROUTE SIGN reads as two signs stacked on one post. There is no
// reading of the art that supports that.
//
// Kept as a dial rather than deleted, because it is a LOOK decision and the
// viewer can sweep it live with N / M.
int g_min_unit = [] {
    if (const char* e = std::getenv("RUBYVR_MIN_UNIT")) {
        const int n = std::atoi(e);
        if (n >= 1 && n <= 6) return n;
    }
    return 1;
}();

int min_unit_cells() { return g_min_unit; }

// How far a roof's ridge stands above the eave, in cells. 0 = flat lid.
//
// WHICH UNITS GET A ROOF: three cells or more of drawing. Trees read as two,
// so the split falls exactly where the classification view already draws it —
// red is roofed, yellow is not — which makes it checkable at a glance rather
// than by reading the mesh log.
//
// Takes ROOF ROWS, not the unit's whole extent — a change from the first
// version of this function, which took `extent` and rose a fixed proportion
// of the WHOLE drawing regardless of how much of it was actually roof. That
// is also what caused defect 4 (the duplicated roof art): with the roof's
// rise keyed on the whole unit, the wall's own top (`ty`) was ALSO keyed on
// the whole unit, so the fold walked the wall all the way up through the
// roof's own art rows before the roof ever started. roof_rows is now read
// from the data (see the call site: the unit's own leading OVERHANG rows,
// capped at half the drawing) rather than guessed as a ratio, so a building
// whose roof is proportionally deeper or shallower than this map's two still
// gets its own roof's actual depth rather than a fixed split.
//
// The PITCH is still a LOOK decision — the reference takes it from an
// authored table (data/voxel_heights.lua) we do not have — but the default
// is now 1.0, not 0.35: a rise of `roof_rows * 1.0` puts the ridge exactly
// `roof_rows` cells above the eave, so a roofed unit's TOTAL height still
// equals its drawn extent (facade_rows + roof_rows), matching what the art
// actually states instead of flattening the building to roughly three
// quarters of its drawn height.
float roof_rise(int roof_rows) {
    static const float k = [] {
        if (const char* e = std::getenv("RUBYVR_ROOF_RISE")) {
            const float f = static_cast<float>(std::atof(e));
            if (f >= 0.0f && f <= 2.0f) return f;
        }
        return 1.0f;
    }();
    return static_cast<float>(roof_rows) * k;
}

// How many cells of the run make up ONE drawn unit. ids[0] is the run's FRONT
// (southmost) cell and ids[k] is k cells north of it.
//
// The rule is the distance to the FIRST RECURRENCE OF THE FRONT TILE, which is
// not the same thing as the run repeating with that period — and the difference
// is the whole robustness of it.
//
// This replaces a strict whole-run period test ("shortest p with ids[i] ==
// ids[i+p] everywhere"). That test is correct and useless: it demands the run
// be perfectly periodic end to end, so a single odd cell anywhere collapses the
// answer to "no repeat, one tall object". It survived only while runs were one
// cell long. The moment collision-aware standing made forests into forty-cell
// runs, Route 101's mixed tree art — 468/469/470/471 canopies over
// 476/477/484-487 bases, not one pair repeated — failed it everywhere and the
// classification view went solid red, which is precisely the forest-fused-into-
// a-wall failure this rule exists to prevent.
//
// Straight from DRAMALESS_SHAPE's Structures.buildVolume (MIT), including both
// of its refinements:
//
//   - the floor of 2. A unit read from a repeat is never one cell: art that
//     repeats every row is a texture, not a stack of one-metre objects.
//   - the TRIM FOOT. A run whose front cell is a one-off — a rounded corner
//     tile finishing a cliff's south edge — hides the recurrence from a scan
//     anchored on it, and the column reads its whole extent. Their note records
//     the symptom: "a 48px fin (or a whole tent of them) sticking out of a 16px
//     mesa".
//
// The trim foot is GENERALISED here, and it had to be. Their test is that the
// two rows above the front match EACH OTHER, which finds a trim sitting on a
// uniform body — a cliff. Ruby's forests are not uniform: a traced column reads
//
//     484 468 476 468 476 468 476 468
//     ^trim  ^canopy/base alternating with period 2
//
// so the body repeats but no two adjacent rows match, their test fails, and the
// whole eight-cell run became one six-cell wall — the classification view went
// solid red across every forest. Retrying the scan anchored one row up finds
// 468 recurring two rows later and reads the unit as 2. Their case is the
// special one where that period happens to be 1.
//
// Not ported: their REGION CONSENSUS, which votes a dominant height across all
// the columns of one structure so a doorway column adopts its building's
// height. That needs their region segmentation, which we do not have.
int unit_cells(const uint16_t* ids, int n) {
    // Anchor on the front; if the front never recurs it is a trim, so try again
    // from the row above it. Two anchors is enough — a trim is one row.
    for (int anchor = 0; anchor < 2 && anchor + 1 < n; ++anchor)
        for (int k = anchor + 1; k < n; ++k)
            if (ids[k] == ids[anchor]) {
                const int u = k - anchor;
                return u < 2 ? 2 : (u > kMaxUnitCells ? kMaxUnitCells : u);
            }

    return n > kMaxUnitCells ? kMaxUnitCells : n;
}

// ── Per-pixel props ──────────────────────────────────────────────────────────
//
// WHERE THE FREE SILHOUETTE IS, AND WHERE IT IS NOT. Read this before trying to
// point this path at trees; that was the plan, and the plan was wrong.
//
//   To carve an object out of a drawing you must know which pixels are the
//   object and which are the background behind it. DRAMALESS_SHAPE spends its
//   single hardest subsystem on that question — classifying every pixel
//   solid/candidate/air/barrier and flood-filling inward from a one-pixel
//   "ground apron" along the SOUTH edge only, because a flood let in from the
//   north "would pour in from the north and shred the roof into misdetected
//   sprite clusters (it did)".
//
//   _docs/voxel-geometry.md says we can skip all of that, because GBA 4bpp has
//   real transparency: colour index 0 IS transparent and the shader already
//   discards it. That is TRUE, and it is true of the wrong half of the data.
//
//   A census of Ruby's live tileset (RUBYVR_CENSUS=1) says the LOWER pair is
//   256/256 opaque for every single metatile on the map — it is the background
//   layer and it always covers its cell. All the transparency is in the UPPER
//   pair, and the upper pair holds only what must draw in front of the player.
//   For a tree that is a 17-pixel tuft; the tree itself is in the opaque lower
//   pair, drawn over grass, exactly as unmasked as a Game Boy sprite.
//
//   Diffing a tree metatile against the map's plain-grass metatile does not
//   rescue it either — 226 of 256 pixels differ, because the grass drawn behind
//   a tree is not the same grass, only the same green.
//
//   So: a tree needs the flood fill after all. It is not built here.
//
// WHAT THIS PATH IS FOR, then: the upper pair's own overlays — the things that
// really are alpha-masked, really are small, and really are separate from the
// ground they sit on. Tall grass is the case that exists on Route 101. Those
// are PASSABLE (see cell_stands), so they are not standing units at all; they
// are decoration on a flat cell, and they stand at the height their art
// actually states rather than a padded one, because for ankle-high grass the
// literal reading is the correct one.
//
// The machinery below is general and the geometry is the reference's. When a
// silhouette extractor for the lower pair exists, trees come here unchanged.
//
// WHAT WE BUILD FROM THE MASK — a hull, not a billboard and not a lathe.
//
//   Each mask ROW is a disc. The row's opaque span gives a centre and a
//   half-width, and every opaque pixel's column runs that circle's CHORD in
//   depth. So the front elevation is exactly the drawing, and the plan view is
//   the drawing's own width profile turned through 90 degrees. Both step pixel
//   by pixel; nothing is invented that the art does not state.
//
//   Not a cross billboard: this diorama's whole framing is a tabletop seen from
//   above, and two crossed planes seen from directly above are an X.
//
//   Not a lathe either. DRAMALESS_SHAPE shipped one first and replaced it —
//   "it read exactly like what it was: the sprite pasted on a cylinder, with
//   the wrap smearing the pixels into vertical stripes."
//
//   Their caveat is the one to watch when this path grows: a revolve only means
//   something when the drawing STATES a width to turn. A tuft's outline is
//   drawn and so is a canopy's. Art that runs off all four sides of its cell
//   states no profile, and revolving it can only produce a cylinder — the
//   "hedge column a plant must never become". Anything like that must stay a
//   volume, which is what the standing test already does with it.
//
// Shades are DRAMALESS_SHAPE's ROUND_SHADE. The front is the drawing, so it is
// unshaded, exactly as the south face of a volume is.
constexpr uint8_t kPropFront  = 255;   // 1.00
constexpr uint8_t kPropBack   = 173;   // 0.68
constexpr uint8_t kPropSide   = 199;   // 0.78
constexpr uint8_t kPropTop    = 255;   // 1.00
constexpr uint8_t kPropBottom = 140;   // 0.55

// Append one quad showing a SINGLE texel. A per-pixel voxel face is one pixel
// of the drawing, so all four corners sample the same place: the shader's
// clamp(vUV * 8) lands every fragment on the same texel and the face comes out
// flat-coloured, which is what a voxel face is.
//
// (r) and (u) are the face's edge vectors, and the winding convention is the
// same one face_metatile uses: r CROSS u must point OUT of the solid. The
// emitted triangles then wind clockwise seen from outside, which is what
// glFrontFace(GL_CW) is set for.
void push_texel(std::vector<Vertex>& out, const world::TileEntry& e,
                int tx, int ty, uint8_t shade, uint8_t unit_h,
                float ox, float oy, float oz,
                float rx, float ry, float rz,
                float ux, float uy, float uz) {
    const float u = (static_cast<float>(tx) + 0.5f) / 8.0f;
    const float v = (static_cast<float>(ty) + 0.5f) / 8.0f;

    const Vertex a{ox + ux,      oy + uy,      oz + uz,      u, v, e.index, e.palette, shade, unit_h};
    const Vertex b{ox + rx + ux, oy + ry + uy, oz + rz + uz, u, v, e.index, e.palette, shade, unit_h};
    const Vertex c{ox + rx,      oy + ry,      oz + rz,      u, v, e.index, e.palette, shade, unit_h};
    const Vertex d{ox,           oy,           oz,           u, v, e.index, e.palette, shade, unit_h};

    out.push_back(a); out.push_back(b); out.push_back(c);
    out.push_back(a); out.push_back(c); out.push_back(d);
}

// Shared upright cutout emitter. One source pixel is 1/16 cell; +X goes
// right, +Y up, +Z toward the viewer. The supplied origin is the bottom-left
// of the image at its depth midplane. Two source pixels of thickness retain
// exposed hole/island edges; adjacent opaque pixels do not emit buried faces.
void emit_cutout(std::vector<Vertex>& out, const cutout::Art& art,
                 const overrides::Cutout& mask, float ox, float oy, float oz) {
    constexpr float px = 1.0f / 16, depth = 2.0f / 16;
    auto solid = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= art.w || y >= art.h) return false;
        const size_t i = size_t(y) * art.w + x;
        return mask.opacity[i] && (art.pixels[i].rgba >> 24);
    };
    for (int py = 0; py < art.h; ++py) for (int ix = 0; ix < art.w; ++ix) {
        if (!solid(ix, py)) continue;
        const auto& pixel = art.pixels[size_t(py) * art.w + ix];
        const auto& e = pixel.tile; const int tx = pixel.tx, ty = pixel.ty;
        const float x = ox + ix * px, y = oy + (art.h - 1 - py) * px;
        const float za = oz - depth * .5f, zb = oz + depth * .5f;
        push_texel(out, e, tx, ty, kPropFront, 1, x, y, zb, px,0,0, 0,px,0);
        push_texel(out, e, tx, ty, kPropBack, 1, x+px, y, za, -px,0,0, 0,px,0);
        if (!solid(ix+1,py)) push_texel(out,e,tx,ty,kPropSide,1, x+px,y,zb, 0,0,-depth, 0,px,0);
        if (!solid(ix-1,py)) push_texel(out,e,tx,ty,kPropSide,1, x,y,za, 0,0,depth, 0,px,0);
        if (!solid(ix,py-1)) push_texel(out,e,tx,ty,kPropTop,1, x,y+px,zb, px,0,0, 0,0,-depth);
        if (!solid(ix,py+1)) push_texel(out,e,tx,ty,kPropBottom,1, x,y,za, px,0,0, 0,0,depth);
    }
}

// Convex subtraction can join differently sampled faces at a T junction: one
// triangle owns a long edge while its neighbour owns two shorter ones. Split
// only those unmatched edges at existing surface vertices. This neither fills
// holes nor changes materials, and conforming meshes retain their exact order.
void conform_part_edges(std::vector<Vertex>& mesh, std::vector<size_t>* triangle_parts = nullptr) {
    using Point=std::array<int64_t,3>;
    using Edge=std::array<size_t,2>;
    std::map<Point,size_t> index;
    std::vector<std::array<double,3>> points;
    std::vector<size_t> ids; ids.reserve(mesh.size());
    for(const auto& v:mesh) {
        const Point key={int64_t(std::llround(v.x*1000000.0)),int64_t(std::llround(v.y*1000000.0)),int64_t(std::llround(v.z*1000000.0))};
        auto [it,inserted]=index.emplace(key,points.size());
        if(inserted) points.push_back({v.x,v.y,v.z});
        ids.push_back(it->second);
    }
    struct Use { size_t count=0, offset=0; int side=0; };
    std::map<Edge,Use> edges;
    for(size_t i=0;i<mesh.size();i+=3) for(int k=0;k<3;++k) {
        const size_t a=ids[i+k],b=ids[i+(k+1)%3];
        auto& use=edges[{std::min(a,b),std::max(a,b)}]; ++use.count;use.offset=i;use.side=k;
    }
    std::vector<size_t> boundary_points;
    for(const auto& [edge,use]:edges) if(use.count==1) {
        boundary_points.push_back(edge[0]);boundary_points.push_back(edge[1]);
    }
    if(boundary_points.empty()) return;
    std::sort(boundary_points.begin(),boundary_points.end());
    boundary_points.erase(std::unique(boundary_points.begin(),boundary_points.end()),boundary_points.end());
    // A balanced point tree keeps long diagonal edges from scanning every
    // vertex on an entire facade. Queries use the edge's narrow bounding box.
    struct Node { size_t point; int left=-1,right=-1,axis=0; };
    std::vector<Node> tree;
    auto build=[&](auto&& self,size_t begin,size_t end,int axis)->int {
        if(begin==end) return -1;
        const size_t middle=begin+(end-begin)/2;
        std::nth_element(boundary_points.begin()+begin,boundary_points.begin()+middle,boundary_points.begin()+end,
            [&](size_t a,size_t b) { return points[a][axis]<points[b][axis] || (points[a][axis]==points[b][axis] && a<b); });
        const int node=int(tree.size());tree.push_back({boundary_points[middle],-1,-1,axis});
        const int left=self(self,begin,middle,(axis+1)%3),right=self(self,middle+1,end,(axis+1)%3);
        tree[node].left=left;tree[node].right=right;return node;
    };
    const int root=build(build,0,boundary_points.size(),0);
    std::map<size_t,std::array<std::vector<size_t>,3>> cuts;
    constexpr double epsilon=1e-6;
    for(const auto& [edge,use]:edges) if(use.count==1) {
        const size_t ia=ids[use.offset+use.side],ib=ids[use.offset+(use.side+1)%3];
        const auto a=points[ia],b=points[ib];
        std::array<double,3> delta,lo,hi; double length2=0;
        for(int k=0;k<3;++k) { delta[k]=b[k]-a[k];length2+=delta[k]*delta[k];lo[k]=std::min(a[k],b[k])-epsilon;hi[k]=std::max(a[k],b[k])+epsilon; }
        if(length2<epsilon*epsilon) continue;
        std::vector<std::pair<double,size_t>> found;
        auto query=[&](auto&& self,int n)->void {
            if(n<0) return;
            const auto& node=tree[n];const auto& p=points[node.point];
            if(node.point!=ia && node.point!=ib && p[0]>=lo[0] && p[0]<=hi[0] && p[1]>=lo[1] && p[1]<=hi[1] && p[2]>=lo[2] && p[2]<=hi[2]) {
                double dot=0;for(int k=0;k<3;++k) dot+=(p[k]-a[k])*delta[k];
                const double t=dot/length2;
                if(t>0 && t<1) {
                    double distance2=0;for(int k=0;k<3;++k) { const double d=p[k]-a[k]-t*delta[k];distance2+=d*d; }
                    if(distance2<epsilon*epsilon) found.push_back({t,node.point});
                }
            }
            if(lo[node.axis]<=p[node.axis]) self(self,node.left);
            if(hi[node.axis]>=p[node.axis]) self(self,node.right);
        };
        query(query,root);
        if(found.empty()) continue;
        std::sort(found.begin(),found.end());
        auto& target=cuts[use.offset][use.side];for(auto [t,id]:found) target.push_back(id);
    }
    if(cuts.empty()) return;
    std::vector<Vertex> joined;joined.reserve(mesh.size()+cuts.size()*9);
    std::vector<size_t> joined_parts;
    for(size_t i=0;i<mesh.size();i+=3) {
        const auto cut=cuts.find(i);
        if(cut==cuts.end()) {
            joined.insert(joined.end(),mesh.begin()+i,mesh.begin()+i+3);
            if(triangle_parts) joined_parts.push_back((*triangle_parts)[i/3]);
            continue;
        }
        // V5 has constant texels. V6 may merge affine source-texel strips;
        // interpolate UVs when conforming them, retaining the tile/palette.
        std::vector<Vertex> ring;
        for(int k=0;k<3;++k) {
            ring.push_back(mesh[i+k]);
            for(size_t id:cut->second[k]) {
                auto v=mesh[i+k];const auto& b=mesh[i+(k+1)%3];
                const double dx=b.x-v.x,dy=b.y-v.y,dz=b.z-v.z;
                const double t=((points[id][0]-v.x)*dx+(points[id][1]-v.y)*dy+(points[id][2]-v.z)*dz)/(dx*dx+dy*dy+dz*dz);
                v.u+=float(t)*(b.u-v.u);v.v+=float(t)*(b.v-v.v);
                v.x=float(points[id][0]);v.y=float(points[id][1]);v.z=float(points[id][2]);ring.push_back(v);
            }
        }
        auto center=mesh[i];
        center.x=(mesh[i].x+mesh[i+1].x+mesh[i+2].x)/3;
        center.y=(mesh[i].y+mesh[i+1].y+mesh[i+2].y)/3;
        center.z=(mesh[i].z+mesh[i+1].z+mesh[i+2].z)/3;
        if(mesh[i].u!=mesh[i+1].u || mesh[i].u!=mesh[i+2].u) center.u=(mesh[i].u+mesh[i+1].u+mesh[i+2].u)/3;
        if(mesh[i].v!=mesh[i+1].v || mesh[i].v!=mesh[i+2].v) center.v=(mesh[i].v+mesh[i+1].v+mesh[i+2].v)/3;
        for(size_t k=0;k<ring.size();++k) {
            joined.push_back(center);joined.push_back(ring[k]);joined.push_back(ring[(k+1)%ring.size()]);
            if(triangle_parts) joined_parts.push_back((*triangle_parts)[i/3]);
        }
    }
    mesh=std::move(joined);
    if(triangle_parts) *triangle_parts=std::move(joined_parts);
}

// Authored solids use the cutout emitter and one shared convex subtraction path.
// Faces are split before upload, never hidden by depth-buffer draw ordering.
#include "voxel_parts.inl"

bool emit_parts(std::vector<Vertex>& destination, const cutout::Art& art,
                const overrides::Pattern& pattern, float ox, float oy, float oz,
                std::vector<size_t>* triangle_parts = nullptr) {
    namespace pg = part_geometry;
    if(!overrides::valid_parts(pattern)) return false;
    std::vector<Vertex> out;
    if(pattern.voxel) {
        if(!emit_voxel_parts(out,art,pattern,triangle_parts)) {std::fprintf(stderr,"[voxel] refused invalid source material: %s\n",pattern.id.c_str());return false;}
        for(auto v:out) {v.x+=ox;v.y+=oy;v.z+=oz;destination.push_back(v);}return true;
    }
    const cutout::Pixel* fallback=nullptr;
    for (const auto& p:art.pixels) if (p.rgba>>24) { fallback=&p; break; }
    auto opaque=[&](int x,int y) {
        const size_t i=size_t(y)*art.w+x;
        return pattern.cutout && pattern.cutout->opacity[i] && (art.pixels[i].rgba>>24);
    };
    for (size_t owner=0;owner<pattern.parts.size();++owner) {
        const size_t first_triangle=out.size()/3;
        const auto& part=pattern.parts[owner]; const auto& t=part.transform;
        if (!pg::valid(t)) continue;
        std::vector<Vertex> base;
        if (part.kind==overrides::PartKind::Billboard) {
            if (!pattern.cutout) continue;
            emit_cutout(base,art,*pattern.cutout,0,0,0);
            for (auto& v:base) {
                v.x*=t.size.x/float(pattern.w); v.y*=t.size.y/float(pattern.extent);
                v.z*=t.size.z/.125f;
            }
        } else if ((part.kind==overrides::PartKind::Box || part.kind==overrides::PartKind::Wedge) && fallback) {
            struct Face { pg::Vec origin,right,up; uint8_t shade; };
            const float w=t.size.x,h=t.size.y,d=t.size.z;
            Face faces[]={
                {{0,0,d/2},{w,0,0},{0,h,0},kPropFront},
                {{w,0,-d/2},{-w,0,0},{0,h,0},kPropBack},
                {{w,0,d/2},{0,0,-d},{0,h,0},kPropSide},
                {{0,0,-d/2},{0,0,d},{0,h,0},kPropSide},
                {{0,h,d/2},{w,0,0},{0,0,-d},kPropTop},
                {{0,0,-d/2},{w,0,0},{0,0,d},kPropBottom}};
            if(part.kind==overrides::PartKind::Wedge) {
                const float sign=float(part.wedge_direction);
                if(part.wedge_axis==0) faces[4]={{0,sign>0?0:h,d/2},{w,sign*h,0},{0,0,-d},kPropTop};
                else faces[4]={{0,sign>0?h:0,d/2},{w,0,0},{0,-sign*h,-d},kPropTop};
            }
            for (const auto& f:faces) for(int y=0;y<art.h;++y) for(int x=0;x<art.w;++x) {
                const auto& region=part.art_region;
                const int sx=region[2]?region[0]+x*region[2]/art.w:x;
                const int sy=region[3]?region[1]+y*region[3]/art.h:y;
                const auto& source=art.pixels[size_t(sy)*art.w+sx];
                const auto& pixel=(source.rgba>>24)?source:*fallback;
                const auto r=f.right*(1.f/art.w),u=f.up*(1.f/art.h);
                const auto o=f.origin+r*float(x)+u*float(art.h-1-y);
                push_texel(base,pixel.tile,pixel.tx,pixel.ty,f.shade,1,o.x,o.y,o.z,r.x,r.y,r.z,u.x,u.y,u.z);
            }
        }
        for (auto& v:base) { const auto p=pg::to_group({v.x,v.y,v.z},t); v.x=p.x;v.y=p.y;v.z=p.z; }
        for(size_t face=0;face<base.size();face+=6) {
            auto position=[&](int i) { const auto& v=base[face+i]; return pg::Vec{v.x,v.y,v.z}; };
            // Outward orientation for clipping; production triangles use clockwise winding.
            const pg::Polygon original={position(5),position(2),position(1),position(0)};
            std::vector<pg::Polygon> fragments{original};
            if(part.kind==overrides::PartKind::Wedge) {
                auto clipped=pg::intersect(original,{pg::wedge_roof(t,part.wedge_axis,part.wedge_direction)});
                fragments.clear();if(clipped.size()>=3) fragments.push_back(std::move(clipped));
            }
            for(size_t other=0;other<pattern.parts.size() && !fragments.empty();++other) {
                if(other==owner) continue;
                const auto& cutter=pattern.parts[other]; const auto& ct=cutter.transform;
                auto subtract=[&](pg::Vec lo,pg::Vec hi) {
                    auto volume=pg::prism(lo,hi,ct);
                    if(cutter.kind==overrides::PartKind::Wedge)
                        volume.push_back(pg::wedge_roof(ct,cutter.wedge_axis,cutter.wedge_direction));
                    std::vector<pg::Polygon> next;
                    for(const auto& fragment:fragments) {
                        auto pieces=pg::subtract(fragment,volume,owner<other);
                        next.insert(next.end(),std::make_move_iterator(pieces.begin()),std::make_move_iterator(pieces.end()));
                    }
                    fragments=std::move(next);
                };
                if(cutter.kind==overrides::PartKind::Box || cutter.kind==overrides::PartKind::Wedge) {
                    if(fallback) subtract({0,0,-ct.size.z/2},{ct.size.x,ct.size.y,ct.size.z/2});
                } else if(pattern.cutout) {
                    // Restrict pixel prisms to this face's bounds in the cutter's local space.
                    pg::Vec lo=pg::to_local(original[0],ct),hi=lo;
                    for(auto v:original) { const auto p=pg::to_local(v,ct);
                        lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);
                        hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z); }
                    if(hi.z < -ct.size.z/2-1e-6f || lo.z > ct.size.z/2+1e-6f) continue;
                    const float dx=ct.size.x/art.w,dy=ct.size.y/art.h;
                    const int x0=std::max(0,int(std::floor(lo.x/dx))-1),x1=std::min(art.w-1,int(std::floor(hi.x/dx))+1);
                    const int y0=std::max(0,int(std::floor(lo.y/dy))-1),y1=std::min(art.h-1,int(std::floor(hi.y/dy))+1);
                    for(int y=y0;y<=y1 && !fragments.empty();++y) for(int x=x0;x<=x1 && !fragments.empty();++x)
                        if(opaque(x,art.h-1-y)) subtract({x*dx,y*dy,-ct.size.z/2},{(x+1)*dx,(y+1)*dy,ct.size.z/2});
                }
            }
            if(fragments.size()==1 && fragments[0]==original) {
                for(int k=0;k<6;++k) out.push_back(base[face+k]);
            } else for(const auto& polygon:fragments) {
                auto append=[&](pg::Vec p) { auto v=base[face];v.x=p.x;v.y=p.y;v.z=p.z;out.push_back(v); };
                for(size_t k=1;k+1<polygon.size();++k) {
                    const auto normal=pg::cross(polygon[k]-polygon[0],polygon[k+1]-polygon[0]);
                    if(pg::dot(normal,normal)<1e-16f) continue;
                    append(polygon[0]);append(polygon[k+1]);append(polygon[k]);
                }
            }
        }
        if(triangle_parts) triangle_parts->insert(triangle_parts->end(),out.size()/3-first_triangle,owner);
    }
    conform_part_edges(out,triangle_parts);
    for(auto v:out) { v.x+=ox;v.y+=oy;v.z+=oz;destination.push_back(v); }
    return true;
}

// Build one prop's hull in LOCAL space: the plot's north-west-bottom corner
// is the origin. The footprint is one cell deep in Z (see the depth cap
// below) and `art.cols / kPropPx` cells wide in X — one, for every caller
// before step 3a of the object-mesher work; more, for a multi-column PROP
// object whose drawing spans several columns (a 2-wide tree).
//
// `height_cells` <= 0 means LITERAL: voxels stay cubic and the prop is as tall
// as its art says, which for tall grass is about a third of a cell. That is the
// honest reading for something you walk through. A positive value stretches the
// drawing to stand exactly that tall — the min_unit_cells() treatment, for art
// that under-reports its own height — and is measured to the top of the DRAWING
// rather than the top of the empty cell it was drawn in, or every prop comes
// out as much too short as its art has headroom.
//
// Built once per metatile id and stamped per cell: a tuft repeats across
// hundreds of cells, and re-deriving the same hull for each is the one part of
// this that would actually cost something.
void build_hull(const Silhouette& art, float height_cells, bool ground_it,
                std::vector<Vertex>& out) {
    const int H  = art.rows;                // 16 per cell of drawing, vertically
    const int Wc = art.cols;                // 16 per cell of drawing, across —
                                             // step 3a's addition: a multi-column
                                             // prop's canvas is wider than one
                                             // metatile. DEPTH stays capped at
                                             // kPropPx wherever it appears below;
                                             // only WIDTH grows with Wc.
    auto opaque = [&](int x, int y) {
        return art.solid[y * Wc + x] != 0;
    };

    // Which art row each hull row samples. Normally itself — but see the
    // grounding step below, where the rows under the drawing borrow.
    std::vector<int> src_row(H);
    for (int i = 0; i < H; ++i) src_row[i] = i;

    // The drawing's vertical extent. Art rarely fills its cell: a bush is drawn
    // in the middle of its 16 rows with air above and its own cast shadow
    // below.
    int top_row = H, bot_row = -1;
    for (int py = 0; py < H; ++py)
        for (int px = 0; px < Wc; ++px)
            if (opaque(px, py)) {
                if (py < top_row) top_row = py;
                bot_row = py;
                break;
            }
    if (bot_row < 0) return;                        // nothing drawn at all

    // GROUNDING. If the drawing stops short of its bottom edge, the rows below
    // it repeat the lowest drawn row all the way down, so the prop stands on a
    // short foot instead of hovering over its own shadow. Straight from
    // DRAMALESS_SHAPE, which does the same for the same reason.
    //
    // Only for something that ends at the ground. A tree's CANOPY metatile sits
    // on the trunk metatile below it, and repeating its lowest row down would
    // grow a skirt of leaves through the trunk.
    if (ground_it)
        for (int py = bot_row + 1; py < H; ++py) src_row[py] = bot_row;

    // How tall a voxel is. A GROUNDED prop is scaled so the top of its drawing
    // lands at height_cells: it is one object and its own art states its whole
    // height. A stacked one keeps the cell's own scale, because it is one slice
    // of a taller drawing and the slices have to line up.
    const int   rows = ground_it ? (H - top_row) : H;
    const float vy   = height_cells > 0.0f
                     ? height_cells / static_cast<float>(rows)
                     : 1.0f / kPropPx;              // literal: cubic voxels

    // z0[y*Wc+x] .. z1[...] is the pixel's chord in DEPTH, in 1/16-cell voxels
    // — always capped at ONE cell (kPropPx) below, regardless of Wc. Depth is
    // "how far this pixel row bulges within its own single cell of plot", a
    // per-CELL concept independent of how many cells of PLOT the object's
    // WIDTH spans. z1 == z0 means the pixel is transparent.
    std::vector<int8_t> z0(static_cast<size_t>(Wc) * H, 0);
    std::vector<int8_t> z1(static_cast<size_t>(Wc) * H, 0);

    for (int py = 0; py < H; ++py) {
        const int ay = src_row[py];
        int lo = Wc, hi = -1;
        for (int px = 0; px < Wc; ++px)
            if (opaque(px, ay)) {
                if (px < lo) lo = px;
                hi = px;
            }
        if (hi < 0) continue;                       // an empty row of the drawing

        // The row's disc: centre and radius of its OWN drawn span — up to Wc
        // wide for a multi-column row, which is what lets a 2-wide crown's
        // equator bulge to the full depth a 1-wide crown already reaches
        // (see the depth clamp just below) instead of a proportionally
        // shallower one.
        const float cx = (static_cast<float>(lo) + static_cast<float>(hi) + 1.0f) * 0.5f;
        const float r  = (static_cast<float>(hi) - static_cast<float>(lo) + 1.0f) * 0.5f;

        for (int px = lo; px <= hi; ++px) {
            if (!opaque(px, ay)) continue;
            const float dx = static_cast<float>(px) + 0.5f - cx;
            const float t  = r * r - dx * dx;
            const float half = t > 0.0f ? std::sqrt(t) : 0.0f;

            // DEPTH, capped at kPropPx (one cell) — NOT Wc. A row wider than
            // one cell still only bulges as deep as a single cell allows; the
            // extra width goes across (X), never further into depth (Z) than
            // one plot cell already permits.
            int a = static_cast<int>(std::lround(kPropPx * 0.5f - half));
            int b = static_cast<int>(std::lround(kPropPx * 0.5f + half));
            if (a < 0) a = 0;
            if (b > kPropPx) b = kPropPx;
            if (b <= a) { a = kPropPx / 2 - 1; b = a + 1; }   // always at least one voxel

            const int i = py * Wc + px;
            z0[i] = static_cast<int8_t>(a);
            z1[i] = static_cast<int8_t>(b);
        }
    }

    const float vx = 1.0f / kPropPx;                     // voxel size across —
                                                           // one metatile's own
                                                           // 16, not Wc: MORE
                                                           // voxel columns make
                                                           // a wider object, each
                                                           // one still 1/16 cell.
    auto solid = [&](int x, int y, int* a, int* b) {
        if (x < 0 || y < 0 || x >= Wc || y >= H) return false;
        const int i = y * Wc + x;
        if (z1[i] <= z0[i]) return false;
        *a = z0[i]; *b = z1[i];
        return true;
    };

    for (int py = 0; py < H; ++py) {
        for (int px = 0; px < Wc; ++px) {
            int a, b;
            if (!solid(px, py, &a, &b)) continue;

            const int ai = src_row[py] * Wc + px;
            const world::TileEntry e = art.e[ai];
            const int tx = art.tx[ai], ty = art.ty[ai];

            // Mask row 0 is the TOP of the drawing, world +Y is up. Rows above
            // top_row are empty, so measuring from the bottom edge puts the
            // foot on the ground and the crown at height_cells.
            const int   wy = H - 1 - py;
            const float x  = static_cast<float>(px) * vx;
            const float y  = static_cast<float>(wy) * vy;
            const float za = static_cast<float>(a) * vx;
            const float zb = static_cast<float>(b) * vx;

            // Front (+Z, the face you look at) and back (-Z). Both are always
            // exposed: the chord is the pixel's full extent in depth.
            push_texel(out, e, tx, ty, kPropFront, 1,
                       x, y, zb,   vx, 0, 0,   0, vy, 0);
            push_texel(out, e, tx, ty, kPropBack, 1,
                       x + vx, y, za,   -vx, 0, 0,   0, vy, 0);

            // The other four faces show only the part of this pixel's chord
            // that the neighbouring pixel's chord does NOT already cover.
            // Both are single intervals, so what is left is at most two
            // pieces — the near end and the far end. Emitting those rather
            // than the whole face is what keeps the hull hollow: two
            // neighbouring pixels of equal depth share their whole boundary
            // and emit nothing at all between them.
            int pc[2][2];
            auto pieces = [&](int dx, int dy) -> int {
                int na = 0, nb = 0;
                if (!solid(px + dx, py + dy, &na, &nb)) {
                    pc[0][0] = a; pc[0][1] = b;
                    return 1;
                }
                int n = 0;
                if (a < na) { pc[n][0] = a;              pc[n][1] = (b < na ? b : na); ++n; }
                if (b > nb) { pc[n][0] = (a > nb ? a : nb); pc[n][1] = b;              ++n; }
                return n;
            };

            // r CROSS u = the outward normal, in every one of these.
            for (int p = 0, n = pieces(1, 0); p < n; ++p) {     // east (+X)
                const float pa = pc[p][0] * vx, pb = pc[p][1] * vx;
                push_texel(out, e, tx, ty, kPropSide, 1,
                           x + vx, y, pb,   0, 0, pa - pb,   0, vy, 0);
            }
            for (int p = 0, n = pieces(-1, 0); p < n; ++p) {    // west (-X)
                const float pa = pc[p][0] * vx, pb = pc[p][1] * vx;
                push_texel(out, e, tx, ty, kPropSide, 1,
                           x, y, pa,   0, 0, pb - pa,   0, vy, 0);
            }
            for (int p = 0, n = pieces(0, -1); p < n; ++p) {    // top (+Y)
                const float pa = pc[p][0] * vx, pb = pc[p][1] * vx;
                push_texel(out, e, tx, ty, kPropTop, 1,
                           x, y + vy, pb,   vx, 0, 0,   0, 0, pa - pb);
            }
            for (int p = 0, n = pieces(0, 1); p < n; ++p) {     // bottom (-Y)
                const float pa = pc[p][0] * vx, pb = pc[p][1] * vx;
                push_texel(out, e, tx, ty, kPropBottom, 1,
                           x, y, pa,   vx, 0, 0,   0, 0, pb - pa);
            }
        }
    }

}

// ── Tileset census ───────────────────────────────────────────────────────────
//
// One line per DISTINCT metatile on the map: what the game says it is
// (behaviour, layer type, collision) next to what its art actually contains
// (opaque pixels in each pair). RUBYVR_CENSUS=1.
//
// This exists because the classification rules here are the whole ball game and
// they had been reasoned about from renders rather than read off the data. The
// first run of it overturned an assumption that had been load-bearing since
// Phase 6 — see the "what stands" comment on cell_stands().
void census(const world::Snapshot& s) {
    struct Row { int count = 0; uint8_t coll_mask = 0; };
    std::unordered_map<uint16_t, Row> seen;

    for (int my = 0; my < s.height; ++my)
        for (int mx = 0; mx < s.width; ++mx) {
            if (s.cell(mx, my) == world::kGridUndefined) continue;
            Row& r = seen[s.metatile_id(mx, my)];
            ++r.count;
            r.coll_mask = static_cast<uint8_t>(r.coll_mask | (1u << (s.collision(mx, my) & 3)));
        }

    std::vector<uint16_t> ids;
    ids.reserve(seen.size());
    for (const auto& kv : seen) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());

    std::fprintf(stderr,
                 "[census] %d distinct metatiles on this map\n"
                 "[census]   id  cells  layer  behav  coll  lower  upper  stands\n",
                 static_cast<int>(ids.size()));
    for (uint16_t id : ids) {
        int lower = 0, upper = 0;
        for (int py = 0; py < kPropPx; ++py)
            for (int px = 0; px < kPropPx; ++px) {
                if (pair_pixel(s, id, 0, px, py, nullptr, nullptr, nullptr)) ++lower;
                if (pair_pixel(s, id, 1, px, py, nullptr, nullptr, nullptr)) ++upper;
            }
        const uint8_t layer = static_cast<uint8_t>(
            id < s.attributes.size()
                ? (s.attributes[id] & world::kAttrLayerMask) >> world::kAttrLayerShift
                : 0);
        const uint8_t behav = static_cast<uint8_t>(
            id < s.attributes.size() ? (s.attributes[id] & world::kAttrBehaviorMask) : 0);

        char coll[8] = {};
        int n = 0;
        for (int c = 0; c < 4; ++c)
            if (seen[id].coll_mask & (1u << c)) coll[n++] = static_cast<char>('0' + c);

        std::fprintf(stderr,
                     "[census] %4u %6d %6u   0x%02X %5s %6d %6d  %s\n",
                     id, seen[id].count, layer, behav, coll, lower, upper,
                     (world::draws_above_player(layer) && upper > 0) ? "yes" : "");
    }
}

// ── Ground context: what this map's floor looks like ────────────────────────
//
// The palette, the commonest flat metatile, and the silhouette flood's
// background colour set — everything the carve/box decision and the
// silhouette flood both need. Factored out of the emitter (Pass 2, below) so
// segmentation (next section) can use the identical values: they must AGREE,
// not just resemble each other, or a cell judged "solid" by one pass and
// "carveable" by the other would disagree about the same drawing.
struct GroundContext {
    uint32_t   pal[world::kPaletteEntries] = {};
    uint16_t   ground_id = 0;
    Background bg;
};

GroundContext compute_ground_context(const world::Snapshot& s,
                                     const std::vector<uint16_t>& cov) {
    GroundContext gc;
    tileset::expand_palette(s, gc.pal);

    const int W = s.width, H = s.height;
    {
        std::unordered_map<uint16_t, int> flat;
        for (int my = 0; my < H; ++my)
            for (int mx = 0; mx < W; ++mx) {
                if (s.cell(mx, my) == world::kGridUndefined) continue;
                if (s.collision(mx, my) != 0) continue;
                const uint16_t g = s.metatile_id(mx, my);
                if (g < cov.size() && cov[g] >= kMinUpperStanding) continue;
                if (g < cov.size() && cov[g] > 0) continue;   // no tufts either
                ++flat[g];
            }
        int best = -1;
        for (const auto& kv : flat)
            if (kv.second > best) { best = kv.second; gc.ground_id = kv.first; }
    }

    gc.bg = ground_colours(s, gc.ground_id, gc.pal);
    // UNION IN THE PRIMARY TILESET'S PLAIN GRASS (metatile 1) — see the note
    // on cell_stands()'s standing-rule commit for why: a tree is drawn over
    // grass in its own art regardless of what THIS map's commonest passable
    // tile happens to be.
    if (gc.ground_id != 1) {
        const Background g2 = ground_colours(s, 1, gc.pal);
        for (int i = 0; i < g2.n; ++i) gc.bg.add(g2.rgb[i]);
    }
    return gc;
}

// ── Objects: segmenting standing cells into things worth naming ─────────────
//
// Everything above this line decides ONE CELL at a time: does it stand, is
// its front solid enough to box. Four of the five defects this project set
// out to fix are really one defect seen from different columns — a tree
// split down the middle, a house's corner carved because ITS front cell
// alone reads too open, two stacked trees whose canopy variants happen not
// to repeat. The column was never the right unit of decision; the DRAWING
// is, and a drawing can span several columns and several rows at once.
//
// This section answers "which cells belong to the same drawing" with a
// UNION-FIND over every standing cell (cell_stands — SOLID footprint or
// OVERHANG art, exactly Pass 1's own test), joining a cell to its 4-neighbour
// when either is true:
//
//   BOTH cells clear the carve bar (kCarveMaxSolid, the same 80% line the
//   carve/box decision already uses) — two wall cells join unconditionally,
//   because a wall's edge pixels do not always overlap as cleanly as its
//   interior is solid (a framed window, a cave wall's texture), and pixel
//   seam overlap alone would fragment a mostly-solid mass into a field of
//   one-cell props. Checked against Rusturf Tunnel's cave wall (206/256
//   solid, seams 8-12) in the design review — seam alone gives 1,677
//   one-cell hulls; this rule keeps it one mass.
//
//   OR the SEAM between their masks overlaps by at least kSeamJoin pixels —
//   the tree rule: the two halves of one tree's canopy overlap 16 of 16 at
//   their shared edge, two DIFFERENT trees standing side by side overlap 0,
//   and 9-13 is empty on this tileset. This is what joins a tree's left and
//   right metatiles into one object (defect 3), and stops two stacked trees
//   whose canopy variants differ from being read as one four-cell tower
//   (defect 5) — their bases share a variant (overlap 14-15, join) while a
//   base sitting under the WRONG tree's canopy does not (overlap 4-6, no
//   join).
//
// THE BOUND, because segmentation alone answers "what touches what", not
// "is this a real object" — a same-id seam of 12-16 is exactly as measured
// for repeating jungle canopy, so seam-joining alone would fuse an entire
// forest into one component on a denser tileset than this map's. A
// component only becomes a named OBJECT if it looks like one of the two
// things this project actually needs to build:
//
//   PROP    no door cell anywhere in it, at most kMaxPropRows (2) rows of
//           FOOTPRINT, and its southmost footprint row reads below the carve
//           bar. Trees, the ledge's rounded cap, a tree base clipped by the
//           map edge.
//
//   STRUCTURE  contains a door cell (see is_door_behaviour), and its
//              bounding box is no bigger than a building gets on this map's
//              evidence (kMaxStructureW x kMaxStructureH). A door is what
//              tells the mesher "this drawing is a building" without a
//              per-tileset table — see the standing-rule commit.
//
// Anything else is a MASS: cliffs, hedges, cave walls, a building with no
// door in the rectangle it was isolated to (this map's clipped Littleroot
// fragments). A mass is not yet built any differently from today — it
// renders through the same per-column path Pass 2 already has — logged as
// [mass] rather than silently absorbed, so a component nothing claims is
// visible in the numbers instead of vanishing into whichever bucket ran
// last.
constexpr int kSeamJoin        = 10;   // of 16, at a shared metatile edge
constexpr int kMaxPropRows     = 2;    // footprint rows; a deeper thing is a mass
constexpr int kMaxStructureW   = 12;   // cells; Devon Corp in Rustboro is 11 wide
constexpr int kMaxStructureH   = 10;   // cells; Devon Corp is 9 rows including its roof

int uf_find(std::vector<int>& parent, int i) {
    while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
    return i;
}
void uf_unite(std::vector<int>& parent, std::vector<int>& size, int a, int b) {
    a = uf_find(parent, a);
    b = uf_find(parent, b);
    if (a == b) return;
    if (size[a] < size[b]) { const int t = a; a = b; b = t; }
    parent[b] = a;
    size[a] += size[b];
}

// ── Pass 0: authored claims ─────────────────────────────────────────────────
//
// An override pattern that matches the grid CLAIMS its member cells. Claimed
// cells are withheld from the union-find below and become an object defined by
// the override instead, which is what lets an authored answer correct a
// segmentation the rules got wrong — a fused pair of trees, a building split at
// a doorway — rather than merely decorate one they got right.
//
// See overrides.h for why matching happens here, against the raw grid, instead
// of against segmentation's own output.
struct Claim {
    int pattern = -1;
    int x = 0, y = 0, w = 0, extent = 0;
    std::vector<uint8_t> mask;          // w * extent, 1 = member
    Object::Authored     authored;
    ObjectClass          cls = ObjectClass::kMass;
    bool                 has_cls = false;
};

struct ClaimMap {
    std::vector<int>   cell;    // W*H -> index into claims, or -1
    std::vector<Claim> claims;
    bool empty() const { return claims.empty(); }
};

ClaimMap build_claims(const world::Snapshot& s,
                      const overrides::OverrideSet& ov) {
    ClaimMap cm;
    if (ov.empty() || !s.valid) return cm;

    const int W = s.width, H = s.height;
    cm.cell.assign(static_cast<size_t>(W) * H, -1);

    const auto resolved = overrides::resolve(s, ov);
    for (const auto& m : resolved.rejected)
        std::fprintf(stderr, "[override] %s at (%d,%d) overlaps an earlier claim, skipped\n",
                     ov.patterns[m.pattern].name.c_str(), m.x, m.y);
    for (const auto& m : resolved.accepted) {
        const auto& p = ov.patterns[m.pattern];

        Claim c; c.pattern = m.pattern;
        c.x = m.x; c.y = m.y; c.w = p.w; c.extent = p.extent;
        c.mask = p.mask;
        c.authored.active    = true;
        c.authored.roof_rows = p.apply.roof_rows;
        c.authored.rise      = p.apply.rise;
        c.authored.height    = p.apply.height;
        switch (p.apply.cls) {
            case overrides::ApplyClass::kProp:
                c.cls = ObjectClass::kProp;      c.has_cls = true; break;
            case overrides::ApplyClass::kStructure:
                c.cls = ObjectClass::kStructure; c.has_cls = true; break;
            case overrides::ApplyClass::kMass:
                c.cls = ObjectClass::kMass;      c.has_cls = true; break;
            case overrides::ApplyClass::kInfer:
                break;   // leave the class to the ordinary rules
        }

        const int ci = static_cast<int>(cm.claims.size());
        for (int k = 0; k < p.cells(); ++k) {
            if (!p.mask[k]) continue;
            const int cx = m.x + k % p.w, cy = m.y + k / p.w;
            cm.cell[static_cast<size_t>(cy) * W + cx] = ci;
        }
        cm.claims.push_back(std::move(c));
    }

    if (!cm.claims.empty())
        std::fprintf(stderr, "[override] %zu object(s) claimed\n", cm.claims.size());
    return cm;
}

// The PUBLIC Object (diorama.h) plus the one field that is deliberately not
// public.
//
// solid_pct is the southmost footprint row's average single-metatile solidity,
// ROUNDED, and it exists for the [object] line alone — the carve/box decision
// below compares raw pixel sums so the two can never round to different answers
// for a borderline row. A number that decides nothing is exactly what an author
// would otherwise reason from, so diorama.h omits it.
//
// WRAPPING RATHER THAN DUPLICATING keeps ONE segmentation implementation. What
// the studio reads and what the [object] line prints are the same values by
// construction, not by two computations agreeing.
struct Component {
    Object obj;
    int    solid_pct = 0;
};

// `cell_object_out`, when given, is filled to map[cell index] = index into
// the RETURNED vector, for every cell that belongs to a component — so a
// caller can ask "which object owns this cell" in O(1) without repeating the
// union-find. -1 for a cell that never stood at all.
std::vector<Component> segment_objects(const world::Snapshot& s,
                                       const std::vector<uint16_t>& cov,
                                       const GroundContext& ground,
                                       const ClaimMap& claims,
                                       std::vector<int32_t>* cell_object_out = nullptr) {
    const int W = s.width, H = s.height;
    auto idx = [&](int mx, int my) { return my * W + mx; };

    // Per-metatile-id masks, shared with nothing else — this runs once per
    // mesh build (a map change or an isolate rebuild), not per frame, and
    // every mask is used at most a handful of times in a run this small.
    std::unordered_map<uint16_t, Silhouette> mask_cache;
    auto mask_for = [&](uint16_t id) -> const Silhouette& {
        auto it = mask_cache.find(id);
        if (it == mask_cache.end()) {
            Silhouette sil;
            build_silhouette(s, &id, 1, ground.bg, ground.pal, &sil);
            it = mask_cache.emplace(id, std::move(sil)).first;
        }
        return it->second;
    };

    // A claimed cell is withheld from the union-find entirely, which is what
    // makes an override able to CORRECT segmentation rather than only decorate
    // it. Because a claimed cell keeps parent -1, the uniting loop below skips
    // it without needing to know claims exist.
    auto claimed_at = [&](int i) {
        return !claims.cell.empty() && claims.cell[i] >= 0;
    };

    std::vector<int> parent(static_cast<size_t>(W) * H, -1);
    for (int my = 0; my < H; ++my)
        for (int mx = 0; mx < W; ++mx) {
            const int i = idx(mx, my);
            if (!claimed_at(i) && cell_stands(s, cov, mx, my)) parent[i] = i;
        }
    std::vector<int> size(parent.size(), 1);

    // The owner of a cell: a claim (encoded as -2 - claim index, so it cannot
    // collide with a union-find root) or a component root, or -1 for a cell
    // that neither stands nor was claimed. One key space means the first-seen
    // ordering below covers authored and inferred objects alike.
    auto owner_of = [&](int i) -> int {
        if (claimed_at(i)) return -2 - claims.cell[i];
        if (parent[i] >= 0) return uf_find(parent, i);
        return -1;
    };

    auto both_solid = [&](uint16_t a, uint16_t b) {
        return mask_for(a).count >= kCarveMaxSolid
            && mask_for(b).count >= kCarveMaxSolid;
    };
    // Seam overlap: how many pixels of the two masks' SHARED EDGE are solid
    // on both sides. `west`/`north` contribute their east/south border;
    // `east`/`south` contribute their west/north border.
    auto seam_h = [&](uint16_t west, uint16_t east) {
        const Silhouette& w = mask_for(west);
        const Silhouette& e = mask_for(east);
        int n = 0;
        for (int py = 0; py < kPropPx; ++py)
            if (w.solid[py * kPropPx + kPropPx - 1] && e.solid[py * kPropPx])
                ++n;
        return n;
    };
    auto seam_v = [&](uint16_t north, uint16_t south) {
        const Silhouette& a = mask_for(north);
        const Silhouette& b = mask_for(south);
        int n = 0;
        for (int px = 0; px < kPropPx; ++px)
            if (a.solid[(kPropPx - 1) * kPropPx + px] && b.solid[px]) ++n;
        return n;
    };

    for (int my = 0; my < H; ++my)
        for (int mx = 0; mx < W; ++mx) {
            if (parent[idx(mx, my)] < 0) continue;
            const uint16_t id = s.metatile_id(mx, my);
            if (mx + 1 < W && parent[idx(mx + 1, my)] >= 0) {
                const uint16_t id2 = s.metatile_id(mx + 1, my);
                if (both_solid(id, id2) || seam_h(id, id2) >= kSeamJoin)
                    uf_unite(parent, size, idx(mx, my), idx(mx + 1, my));
            }
            if (my + 1 < H && parent[idx(mx, my + 1)] >= 0) {
                const uint16_t id2 = s.metatile_id(mx, my + 1);
                if (both_solid(id, id2) || seam_v(id, id2) >= kSeamJoin)
                    uf_unite(parent, size, idx(mx, my), idx(mx, my + 1));
            }
        }

    struct Acc {
        int  minX = 1 << 30, maxX = -1, minY = 1 << 30, maxY = -1;
        int  fminY = 1 << 30, fmaxY = -1;   // footprint (SOLID-only) row span
        bool has_door = false;
    };
    std::unordered_map<int, Acc> comps;

    // FIRST-SEEN ORDER, and it is now part of a published contract.
    //
    // The emission loop below used to iterate `comps` itself. That is an
    // unordered_map, so the order of the returned vector — and therefore every
    // index in cell_object — was whatever the hash table happened to yield.
    // Stable enough in practice that nothing ever caught it, and not a thing to
    // put in a public API whose entire purpose is stable object identity.
    //
    // This scan is already row-major, so recording each root the first time it
    // is reached gives north-to-south, west-to-east ordering for free, and it
    // is unique by construction: every standing cell belongs to exactly one
    // component, so exactly one cell is any component's first.
    std::vector<int> order;
    for (int my = 0; my < H; ++my)
        for (int mx = 0; mx < W; ++mx) {
            const int i = idx(mx, my);
            const int root = owner_of(i);
            if (root == -1) continue;
            const auto ins = comps.emplace(root, Acc{});
            if (ins.second) order.push_back(root);
            Acc& a = ins.first->second;
            if (mx < a.minX) a.minX = mx;
            if (mx > a.maxX) a.maxX = mx;
            if (my < a.minY) a.minY = my;
            if (my > a.maxY) a.maxY = my;
            if (cell_is_footprint(s, mx, my)) {
                if (my < a.fminY) a.fminY = my;
                if (my > a.fmaxY) a.fmaxY = my;
            }
            if (is_door_behaviour(s.behaviour(mx, my))) a.has_door = true;
        }

    std::vector<Component> out;
    out.reserve(order.size());
    std::unordered_map<int, int> root_to_index;   // union-find root -> out[]
    for (const int root : order) {
        root_to_index[root] = static_cast<int>(out.size());
        const Acc& a = comps[root];
        const bool   is_claim = root <= -2;
        const Claim* cl = is_claim ? &claims.claims[-2 - root] : nullptr;

        Component comp;
        Object&   o = comp.obj;
        if (cl) {
            // AN AUTHORED OBJECT TAKES ITS BOUNDS FROM THE PATTERN, not from
            // the member cells that happen to stand. The rectangle a person
            // selected is the rectangle they authored against, and the hull is
            // built from it — so shrinking it to the cells that stood would
            // silently author something else.
            o.x = cl->x; o.y = cl->y; o.w = cl->w; o.extent = cl->extent;
            o.authored = cl->authored;
        } else {
            o.x = a.minX; o.y = a.minY;
            o.w = a.maxX - a.minX + 1;
            o.extent = a.maxY - a.minY + 1;
        }
        o.has_door = a.has_door;

        // THE SIGNATURE, and it is filled for every component rather than only
        // for the PROP objects the carve path used to build it for.
        //
        // Row-major over the bounding box, north-west corner first — which is
        // build_silhouette's own convention, so the carve path below can hand
        // this very vector straight to it instead of rebuilding the same
        // rectangle from the same rule. One computation, so the pattern an
        // override is keyed on cannot drift from the rectangle the hull was
        // actually meshed from.
        o.ids.reserve(static_cast<size_t>(o.w) * o.extent);
        for (int r = o.y; r < o.y + o.extent; ++r)
            for (int c = o.x; c < o.x + o.w; ++c)
                o.ids.push_back(s.metatile_id(c, r));

        if (a.fmaxY < 0) {
            // No footprint cell at all — a stray OVERHANG patch touching
            // nothing solid. Not observed on this map; treated as a mass
            // rather than crashing on a division the data never states. An
            // author who said otherwise is still obeyed.
            o.cls = (cl && cl->has_cls) ? cl->cls : ObjectClass::kMass;
            out.push_back(std::move(comp));
            continue;
        }
        o.footprint_rows = a.fmaxY - a.fminY + 1;

        // The carve/box test itself: RAW pixel sums against the same
        // kCarveMaxSolid the per-column path already uses, not a rounded
        // percentage — solid_pct below is for the [object] line, not this
        // decision, so the two can never round to different answers for a
        // borderline row.
        long long solid_sum = 0;
        int front_cells = 0;
        for (int mx = a.minX; mx <= a.maxX; ++mx) {
            const int i = idx(mx, a.fmaxY);
            if (owner_of(i) != root) continue;
            solid_sum += mask_for(s.metatile_id(mx, a.fmaxY)).count;
            ++front_cells;
        }
        comp.solid_pct = front_cells
            ? static_cast<int>((solid_sum * 100)
                              / (static_cast<long long>(front_cells) * kPropPx * kPropPx))
            : 0;
        const bool front_below_carve_bar =
            front_cells > 0
            && solid_sum < static_cast<long long>(front_cells) * kCarveMaxSolid;

        if (cl && cl->has_cls) {
            // An author naming the class outranks the rules, which is the
            // point: the rules are what could not tell a clipped building from
            // a cliff in the first place.
            o.cls = cl->cls;
        } else if (o.has_door && o.w <= kMaxStructureW && o.extent <= kMaxStructureH) {
            o.cls = ObjectClass::kStructure;
        } else if (!o.has_door && o.footprint_rows <= kMaxPropRows
                   && front_below_carve_bar) {
            o.cls = ObjectClass::kProp;
        } else {
            o.cls = ObjectClass::kMass;
        }
        out.push_back(std::move(comp));
    }

    if (cell_object_out) {
        cell_object_out->assign(static_cast<size_t>(W) * H, -1);
        for (int my = 0; my < H; ++my)
            for (int mx = 0; mx < W; ++mx) {
                const int i = idx(mx, my);
                const int owner = owner_of(i);
                if (owner == -1) continue;
                (*cell_object_out)[i] = root_to_index[owner];
            }
    }
    return out;
}

// FNV-1a over every vertex's own fields (position, UV, tile, palette, shade —
// not unit_h, which is a classification label rather than geometry). A
// vertex COUNT can grow by one and shrink by one elsewhere and still match;
// this cannot. What it is FOR: step 2 of the object-mesher work adds a whole
// new segmentation pass that must change nothing about what gets drawn, only
// what gets counted — this is the number that proves it.
uint64_t fnv1a_mix(uint64_t h, uint32_t x) {
    for (int b = 0; b < 4; ++b) {
        h ^= static_cast<uint8_t>(x >> (b * 8));
        h *= 1099511628211ULL;
    }
    return h;
}
uint64_t hash_vertex(uint64_t h, const Vertex& vert) {
    uint32_t bits;
    std::memcpy(&bits, &vert.x, sizeof(bits)); h = fnv1a_mix(h, bits);
    std::memcpy(&bits, &vert.y, sizeof(bits)); h = fnv1a_mix(h, bits);
    std::memcpy(&bits, &vert.z, sizeof(bits)); h = fnv1a_mix(h, bits);
    std::memcpy(&bits, &vert.u, sizeof(bits)); h = fnv1a_mix(h, bits);
    std::memcpy(&bits, &vert.v, sizeof(bits)); h = fnv1a_mix(h, bits);
    h = fnv1a_mix(h, vert.tile);
    h = fnv1a_mix(h, vert.pal);
    h = fnv1a_mix(h, vert.shade);
    return h;
}
uint64_t hash_geometry(const std::vector<Vertex>& v) {
    uint64_t h = 1469598103934665603ULL;   // FNV-1a 64-bit offset basis
    for (const Vertex& vert : v) h = hash_vertex(h,vert);
    return h;
}

// ── Mesh ─────────────────────────────────────────────────────────────────────

// V6 removes explicitly owned object/shadow pixels from the floor. Recover
// their material locally: a map-wide majority can be water beside a grass sign.
// Retained Ground pixels supply palette/index evidence, never RGB brightness.
// This changes no v5 or inferred geometry. Missing local evidence retains the
// established fallback; authored terrain/underlay overrides are a separate task.
class LocalGround {
    struct Material {std::array<bool,256> present{};bool opaque=true;};
    const world::Snapshot& s;
    const overrides::Claims& claims;
    const std::vector<uint16_t>& coverage;
    std::map<uint16_t,Material> materials;
    const Material& material(uint16_t id) {
        const auto found=materials.find(id);if(found!=materials.end()) return found->second;
        Material m;
        for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
            world::TileEntry tile{};int tx=0,ty=0;
            const auto index=composite_pixel(s,id,x,y,&tile,&tx,&ty);
            m.opaque &= index!=0;if(index) m.present[tile.palette*16+index]=true;
        }
        return materials.emplace(id,m).first->second;
    }
public:
    using Evidence=std::array<int,256>;
    LocalGround(const world::Snapshot& snapshot,const overrides::Claims& accepted,const std::vector<uint16_t>& cov)
        :s(snapshot),claims(accepted),coverage(cov) {}
    Evidence evidence(const overrides::Pattern& p,const cutout::Art& art,int cell=-1) const {
        Evidence result{};
        for(int y=0;y<art.h;++y) for(int x=0;x<art.w;++x) {
            if(cell>=0 && y/16*p.w+x/16!=cell) continue;
            const size_t i=size_t(y)*art.w+x;const auto& pixel=art.pixels[i];
            if(!p.voxel->ground.opacity[i] || !(pixel.rgba>>24)) continue;
            const size_t address=size_t(pixel.tile.index)*world::kTileBytes+pixel.ty*4+pixel.tx/2;
            const int index=(s.vram_tiles[address]>>((pixel.tx&1)*4))&15;
            ++result[pixel.tile.palette*16+index];
        }
        return result;
    }
    uint16_t find(int x,int y,const Evidence& known,uint16_t fallback) {
        uint16_t selected=fallback;int best_score=-1,best_distance=1000;
        constexpr int radius=6;
        for(int ny=std::max(0,y-radius);ny<std::min(s.height,y+radius+1);++ny)
            for(int nx=std::max(0,x-radius);nx<std::min(s.width,x+radius+1);++nx) {
                if(s.cell(nx,ny)==world::kGridUndefined || s.collision(nx,ny)!=0 ||
                   claims.owner[size_t(ny)*s.width+nx]>=0) continue;
                const auto id=s.metatile_id(nx,ny);
                if(id>=coverage.size() || coverage[id]!=0) continue;
                const auto& candidate=material(id);if(!candidate.opaque) continue;
                int score=0;for(int i=0;i<256;++i) if(candidate.present[i]) score+=known[i];
                const int distance=std::abs(nx-x)+std::abs(ny-y);
                if(score>best_score || (score==best_score && distance<best_distance)) {
                    selected=id;best_score=score;best_distance=distance;
                }
            }
        return selected;
    }
};

#include "terrain_mesh.inl"
#include "placed_mesh.inl"

bool build_authored_diorama(const world::Snapshot& s,const overrides::OverrideSet& set,
                            std::vector<Vertex>* out,DioramaStats* stats,
                            const std::vector<uint8_t>* visible=nullptr,
                            const terrain::Resolved* shared_surfaces=nullptr,
                            PlacedMesh* placed=nullptr) {
    if(!s.valid || !out || !stats || !overrides::supported_version(set.version)) return false;
    if(!terrain::valid(set.terrain) || (!set.terrain.empty() && set.version!=overrides::kTerrainVersion)) return false;
    for(const auto& p:set.patterns) if(!cutout::valid(p) || !overrides::valid_parts(p) ||
        (p.voxel && set.version<overrides::kVoxelVersion) ||
        p.w<1 || p.extent<1 || p.anchor<0 || p.anchor>=p.cells() ||
        p.mask.size()!=size_t(p.cells()) || p.ids.size()!=p.mask.size()) return false;
    const auto claims=overrides::resolve(s,set);
    const auto coverage=upper_coverage_table(s);
    const auto ground=compute_ground_context(s,coverage);
    DioramaStats result;result.ground_tile=ground.ground_id;
    const auto surfaces=shared_surfaces?*shared_surfaces:terrain::resolve(s,set.terrain);
    auto shown=[&](int x,int y){return !visible || (x>=0 && y>=0 && x<s.width && y<s.height && (*visible)[size_t(y)*s.width+x]);};
    auto owned=[&](const auto& c){const auto& p=set.patterns[size_t(c.pattern)];return shown(c.x+p.anchor%p.w,c.y+p.anchor/p.w);};
    result.terrain_cells=surfaces.matched;result.terrain_rejected=surfaces.rejected;
    result.accepted_instances=claims.accepted.size();
    if(visible) {
        result.terrain_cells=result.terrain_rejected=result.accepted_instances=0;
        for(int y=0;y<s.height;++y) for(int x=0;x<s.width;++x) if(shown(x,y)) {
            result.terrain_cells+=surfaces.cell(x,y)!=nullptr;
            result.terrain_rejected+=surfaces.mismatched[size_t(y)*s.width+x];
        }
        for(const auto& c:claims.accepted) result.accepted_instances+=owned(c);
    }
    std::vector<uint8_t> recovered(size_t(s.width)*s.height,0);
    std::vector<uint16_t> floor(size_t(s.width)*s.height,ground.ground_id);
    LocalGround local_ground(s,claims,coverage);
    std::map<int,std::vector<Vertex>> authored;
    for(const auto& claim:claims.accepted) {
        const auto& p=set.patterns[size_t(claim.pattern)];
        if(!p.cutout && p.parts.empty()) continue;
        for(int i=0;i<p.cells();++i) if(p.mask[size_t(i)])
            recovered[size_t(claim.y+i/p.w)*s.width+claim.x+i%p.w]=1;
        if(p.voxel) {
            cutout::Art art;if(!cutout::compose(s,p,&art)) return false;
            const auto whole=local_ground.evidence(p,art);
            for(int i=0;i<p.cells();++i) if(p.mask[size_t(i)]) {
                const int x=claim.x+i%p.w,y=claim.y+i/p.w;
                auto known=local_ground.evidence(p,art,i);
                if(std::all_of(known.begin(),known.end(),[](int n){return n==0;})) known=whole;
                floor[size_t(y)*s.width+x]=local_ground.find(x,y,known,ground.ground_id);
            }
        }
        if(authored.find(claim.pattern)==authored.end()) {
            cutout::Art art;if(!cutout::compose(s,p,&art)) return false;
            auto& mesh=authored[claim.pattern];
            if(p.parts.empty()) emit_cutout(mesh,art,*p.cutout,0,0,0);
            else if(!emit_parts(mesh,art,p,0,0,0)) return false;
        }
    }
    std::vector<Vertex> mesh;mesh.reserve(size_t(s.width)*s.height*48);
    // Original layer quads keep animated palette/texel and transparency semantics.
    for(int y=0;y<s.height;++y) for(int x=0;x<s.width;++x) {
        if(s.cell(x,y)==world::kGridUndefined || !shown(x,y)) continue;
        uint16_t id=recovered[size_t(y)*s.width+x]?floor[size_t(y)*s.width+x]:s.metatile_id(x,y);
        if(const auto* cell=surfaces.cell(x,y)) {
            if(cell->underlay>=0) id=uint16_t(cell->underlay);
            const size_t before=mesh.size();terrain_cell_mesh(s,mesh,surfaces,*cell,x,y,id);
            if(mesh.size()>2000000) return false;
            result.terrain_vertices+=mesh.size()-before;continue;
        }
        const size_t base=size_t(id)*world::kTilesPerMetatile;
        if(base+8>s.metatiles.size()) return false;
        for(int pair=0;pair<2;++pair) for(int k=0;k<4;++k) {
            const auto e=world::unpack_tile_entry(s.metatiles[base+pair*4+k]);
            const float px=x+(k&1)*.5f,pz=y+(k>>1)*.5f,py=pair*.002f;
            push_quad(mesh,e,kShadeFlat,0,px,py,pz,px+.5f,py,pz,px+.5f,py,pz+.5f,px,py,pz+.5f,true);
        }
    }
    // V6 preserves explicitly owned background pixels at their original map
    // positions. Object and source cast-shadow pixels reveal recovered ground.
    for(const auto& claim:claims.accepted) {
        const auto& p=set.patterns[size_t(claim.pattern)];if(!p.voxel) continue;
        cutout::Art art;if(!cutout::compose(s,p,&art)) return false;
        for(int y=0;y<art.h;++y) for(int x=0;x<art.w;++x) {
            const size_t i=size_t(y)*art.w+x;const auto& pixel=art.pixels[i];
            if(!p.voxel->ground.opacity[i] || !(pixel.rgba>>24)) continue;
            const int cx=claim.x+x/16,cy=claim.y+y/16;
            if(!shown(cx,cy)) continue;
            const auto height=surfaces.query(cx,cy,s.elevation(cx,cy));
            const auto* cell=surfaces.cell(cx,cy);
            if(cell && (cell->underlay>=0 || !height.resolved() || (height.surface && height.surface->top>=0))) continue;
            const size_t before=mesh.size();
            push_texel(mesh,pixel.tile,pixel.tx,pixel.ty,kShadeFlat,0,claim.x+x/16.f,height.pixels/16.f+.003f,claim.y+(y+1)/16.f,
                1/16.f,0,0,0,0,-1/16.f);
            if(height.surface) for(size_t k=before;k<mesh.size();++k)
                mesh[k].y+=(terrain::surface_height(*height.surface,mesh[k].x-cx,mesh[k].z-cy)-height.pixels)/16.f;
        }
    }
    result.flat_vertices=mesh.size();
    std::map<int,MeshDraw> stored_models;
    if(placed) placed->direct(0,mesh.size());
    for(const auto& claim:claims.accepted) {
        if(!owned(claim)) continue;
        const auto found=authored.find(claim.pattern);
        if(found==authored.end() || found->second.empty()) continue;
        const auto& p=set.patterns[size_t(claim.pattern)];
        const int cx=claim.x+p.w/2,cy=claim.y+p.extent-1;
        const auto height=surfaces.query(cx,cy,s.elevation(cx,cy),p.w%2?.5f:0.f,.5f);
        if(!height.resolved()) ++result.unresolved_placements;
        result.authored_vertices+=found->second.size();
        if(placed && !p.follow_ground) {
            auto stored=stored_models.find(claim.pattern);
            if(stored==stored_models.end()) {
                stored=stored_models.emplace(claim.pattern,MeshDraw{mesh.size(),found->second.size()}).first;
                mesh.insert(mesh.end(),found->second.begin(),found->second.end());
                ++placed->models;
            }
            placed->instance(stored->second,{float(claim.x),height.pixels/16.f+.002f,claim.y+p.extent-.5f});
        } else {
            const size_t first=mesh.size();
            for(auto vertex:found->second) {
            vertex.x+=claim.x;vertex.z+=claim.y+p.extent-.5f;
            float floor_height=height.pixels;
            if(p.follow_ground) {
                // Use the containing source cell even at a part's outer edge,
                // so a clump cannot sample the lower side of a ledge by rounding.
                const int gx=std::clamp(int(std::floor(vertex.x)),claim.x,claim.x+p.w-1);
                const int gz=std::clamp(int(std::floor(vertex.z)),claim.y,claim.y+p.extent-1);
                const auto sampled=surfaces.query(gx,gz,s.elevation(gx,gz),
                    std::clamp(vertex.x-gx,0.f,1.f),std::clamp(vertex.z-gz,0.f,1.f));
                if(sampled.resolved()) floor_height=sampled.pixels;
            }
            vertex.y+=floor_height/16.f+.002f;
            mesh.push_back(vertex);
            }
            if(placed) {placed->direct(first,mesh.size()-first);++placed->deformed_instances;}
        }
        if(placed && (mesh.size()>placed->vertex_limit ||
           result.flat_vertices+result.authored_vertices>placed->expanded_limit))return false;
        ++result.raised_instances;
        result.part_instances+=p.parts.size();
    }
    uint64_t hash=1469598103934665603ULL;
    if(placed) visit_placed(mesh,*placed,0,0,[&](const Vertex& v){hash=hash_vertex(hash,v);});
    else hash=hash_geometry(mesh);
    result.geometry_hash=fnv1a_mix(hash,0x44494F52); // DIOR, distinct from inference
    *stats=result;*out=std::move(mesh);return true;
}

void build_mesh(const world::Snapshot& s,
                std::vector<int16_t>* col_h_out = nullptr) {
    if(g_build_mode==BuildMode::Diorama) {
        std::vector<Vertex> mesh;
        if(!build_authored_diorama(s,g_overrides,&mesh,&g_diorama_stats)) {
            g_vertex_count=0;g_diorama_stats={};std::fprintf(stderr,"[diorama] invalid authored state\n");return;
        }
        if(col_h_out) col_h_out->assign(size_t(s.width)*s.height,0);
        g_vertex_count=GLsizei(mesh.size());g_map_w=float(s.width);g_map_h=float(s.height);
        gl::glBindVertexArray(g_vao);gl::glBindBuffer(GL_ARRAY_BUFFER,g_vbo);
        gl::glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(mesh.size()*sizeof(Vertex)),mesh.data(),GL_STATIC_DRAW);
        ++g_mesh_upload_count;
        gl::glBindVertexArray(0);
        std::fprintf(stderr,"[stats] vertices=%d objects=%zu masses=0 carved=0 boxed=0 tufts=0 dupart=0 heights=none geom=%016llx mode=diorama raised=%zu flat_vertices=%zu\n",
            int(g_vertex_count),g_diorama_stats.raised_instances,(unsigned long long)g_diorama_stats.geometry_hash,
            g_diorama_stats.raised_instances,g_diorama_stats.flat_vertices);
        return;
    }
    const bool trace_runs =
        std::getenv("RUBYVR_CENSUS") && std::getenv("RUBYVR_CENSUS")[0] == '1';
    if (trace_runs) {
        census(s);
        // The cells the first-person eye looks straight down. When something
        // stands here that should not, this is the fastest way to name it.
        const int px = static_cast<int>(s.camera_cell_x());
        const int pz = static_cast<int>(s.camera_cell_y());
        std::fprintf(stderr, "[ahead] from player (%d,%d), north:\n", px, pz);
        for (int k = 0; k <= 8; ++k) {
            const int my = pz - k;
            if (my < 0) break;
            std::fprintf(stderr,
                         "[ahead]   row %2d  id %4u  coll %u  elev %2u  layer %u\n",
                         my, s.metatile_id(px, my), s.collision(px, my),
                         s.elevation(px, my), s.layer_type(px, my));
        }
    }

    std::vector<Vertex> v;
    v.reserve(static_cast<size_t>(s.width) * s.height * 48);

    const int W = s.width, H = s.height;
    const size_t cells = static_cast<size_t>(W) * H;
    const std::vector<uint16_t> cov = upper_coverage_table(s);

    // Computed once, up front, because segmentation (right after Pass 1) and
    // Pass 2's carve branch both need the SAME palette, ground metatile and
    // silhouette background — see the GroundContext comment above.
    const GroundContext ground = compute_ground_context(s, cov);

    // ── Pass 1: how tall is each cell's column? ──────────────────────────────
    //
    // Split every column's standing run into repeating UNITS (see
    // unit_cells), and give every cell of a unit that unit's full height.
    // The result is a HEIGHTFIELD: each cell is either flat ground or a solid
    // column of some whole number of cells. That is what makes this real voxel
    // geometry rather than billboards — a house is a block of columns all six
    // tall, a forest is a field of columns two tall.
    // Per cell: how tall its unit STANDS, and the two map rows that bound the
    // unit's ART. Those are different numbers and conflating them was the bug.
    //
    //   col_h        stand height in cells, after the min_unit floor. 0 = ground
    //   col_front    the unit's SOUTHMOST row — the row whose art faces you
    //   col_art_top  the unit's NORTHMOST row that actually has art
    //
    // col_h can exceed (front - art_top + 1) whenever the min_unit floor padded
    // a short unit. Reading art at `front - col_h + 1` then lands on a row
    // OUTSIDE the unit — for a one-row tree padded to two cells, on whatever
    // grass happens to sit behind it. That is where the blank walls came from.
    std::vector<int16_t> col_h(cells, 0);
    std::vector<int16_t> col_front(cells, 0);
    std::vector<int16_t> col_art_top(cells, 0);

    std::vector<uint16_t> ids;
    int stat_units = 0, stat_tall = 0, stat_props = 0, stat_carved = 0, stat_boxed = 0;
    int stat_dup_art = 0;

    // A histogram of unit heights, and the carve/box split. These exist so a
    // check can be a NUMBER rather than a look at a picture: "this structure's
    // columns all agree on one height" and "this structure is boxed, not
    // carved" are exactly the two rules the remaining defects break, and both
    // are assertable from here.
    int stat_height_hist[8] = {};

    for (int mx = 0; mx < W; ++mx) {
        int my = H - 1;
        while (my >= 0) {
            if (!cell_stands(s, cov, mx, my)) { --my; continue; }

            const int bottom = my;
            int top = my;
            while (top - 1 >= 0 && cell_stands(s, cov, mx, top - 1)) --top;
            const int len = bottom - top + 1;

            ids.clear();
            for (int k = 0; k < len; ++k)
                ids.push_back(s.metatile_id(mx, bottom - k));

            const int period = unit_cells(ids.data(), len);

            if (trace_runs && period >= 3) {
                std::fprintf(stderr, "[runs] col %2d  rows %2d..%-2d len %2d "
                                     "unit %d  ids:", mx, top, bottom, len, period);
                for (int k = 0; k < len && k < 10; ++k)
                    std::fprintf(stderr, " %u", ids[k]);
                std::fprintf(stderr, "\n");
            }

            for (int base = bottom; base >= top; base -= period) {
                const int drawn = (base - top + 1 < period) ? (base - top + 1) : period;
                // `drawn` is how many rows of ART this unit spans; `h` is how
                // tall it actually stands. They differ because the drawing
                // under-reports height — see min_unit_cells().
                const int h = drawn < min_unit_cells() ? min_unit_cells() : drawn;
                for (int k = 0; k < drawn; ++k) {
                    const size_t i = static_cast<size_t>(base - k) * W + mx;
                    col_h[i]       = static_cast<int16_t>(h);
                    col_front[i]   = static_cast<int16_t>(base);
                    col_art_top[i] = static_cast<int16_t>(base - drawn + 1);
                }
                ++stat_units;
                if (h >= 3) ++stat_tall;
                stat_height_hist[h < 8 ? h : 7] += 1;
            }
            my = top - 1;
        }
    }

    // ── Pass 1.5: segment standing cells into objects and masses ─────────────
    //
    // A DIAGNOSTIC PASS ONLY, for now — see segment_objects' own comment.
    // Nothing below this line reads `objects`; Pass 2 still emits exactly
    // what it emitted before this existed, one column at a time. What changes
    // is what the [stats]/[object]/[mass] lines REPORT, so a check can ask
    // "is this a single object" instead of "does the per-column stat agree
    // with itself", which is the question every remaining defect actually is.
    // ── Pass 0: authored claims ──────────────────────────────────────────────
    // Matched against the raw grid before anything is inferred, so an override
    // can correct segmentation rather than only decorate it.
    const ClaimMap claims = build_claims(s, g_overrides);

    std::vector<int32_t> cell_object;   // cell index -> objects[] index, or -1
    const std::vector<Component> objects =
        segment_objects(s, cov, ground, claims, &cell_object);

    // AN AUTHORED HEIGHT REWRITES col_h, and it has to happen here rather than
    // in Pass 2. top_of() reads col_h directly to decide which side faces are
    // buried, so overriding a local copy of the height would leave a column
    // standing taller than its neighbours believed and open seams along every
    // shared edge. Rewriting the heightfield keeps every consumer agreeing.
    for (size_t i = 0; i < cell_object.size(); ++i) {
        const int k = cell_object[i];
        if (k < 0) continue;
        const Object& o = objects[k].obj;
        if (o.authored.active && o.authored.height > 0)
            col_h[i] = static_cast<int16_t>(o.authored.height);
    }

    // Custom cutouts replace inference for every member, including cells
    // that inference classified as flat or as part of a structure.
    auto cutout_claim = [&](size_t cell) -> const Claim* {
        if (claims.cell.empty() || claims.cell[cell] < 0) return nullptr;
        const auto& claim = claims.claims[size_t(claims.cell[cell])];
        const auto& p=g_overrides.patterns[size_t(claim.pattern)];
        return p.cutout || !p.parts.empty() ? &claim : nullptr;
    };
    for (size_t i = 0; i < cells; ++i) if (cutout_claim(i)) col_h[i] = 0;

    // Height of a cell's solid top, in world units. Out of range reads as
    // ground, so map edges emit their outward faces instead of a hole.
    auto top_of = [&](int mx, int my) -> float {
        if (mx < 0 || my < 0 || mx >= W || my >= H) return -1e9f;
        const size_t i = static_cast<size_t>(my) * W + mx;
        return height_for(s.elevation(mx, my)) + static_cast<float>(col_h[i]);
    };
    auto ground_of = [&](int mx, int my) -> float {
        if (mx < 0 || my < 0 || mx >= W || my >= H) return -1e9f;
        return height_for(s.elevation(mx, my));
    };

    // Draw one 16x16 metatile's worth of art onto a face.
    //
    // `pair` picks the lower or upper half of the metatile. Both are drawn on
    // an exposed face, upper very slightly proud of lower, because the 2D art
    // composites them and dropping either loses detail — a house wall lives in
    // the lower pair for NORMAL layer types, a tree canopy in the upper.
    //
    // (au,av) is the face's own 2D basis: `ax` steps one sub-tile "right"
    // across the face and `ay` steps one sub-tile "up" it. Expressing faces
    // this way means one routine handles all five orientations, and the
    // metatile's TL/TR/BL/BR order maps the same way on every one of them.
    auto face_metatile = [&](uint16_t id, int pair, uint8_t shade, uint8_t unit_h,
                             float ox, float oy, float oz,        // face origin
                             float rx, float ry, float rz,        // +1 sub-tile right
                             float ux, float uy, float uz) {      // +1 sub-tile up
        const size_t base = static_cast<size_t>(id) * world::kTilesPerMetatile
                          + static_cast<size_t>(pair) * 4;
        if (base + 4 > s.metatiles.size()) return;

        for (int i = 0; i < 4; ++i) {
            const world::TileEntry e = world::unpack_tile_entry(s.metatiles[base + i]);
            const float cx = static_cast<float>(i & 1);        // column, 0..1
            const float cy = static_cast<float>(1 - (i >> 1)); // row: 0 is TOP

            const float px = ox + rx * cx + ux * cy;
            const float py = oy + ry * cx + uy * cy;
            const float pz = oz + rz * cx + uz * cy;

            push_quad(v, e, shade, unit_h,
                      px + ux,      py + uy,      pz + uz,        // top-left
                      px + rx + ux, py + ry + uy, pz + rz + uz,   // top-right
                      px + rx,      py + ry,      pz + rz,        // bottom-right
                      px,           py,           pz);            // bottom-left
        }
    };

    // ── Pass 2: emit ─────────────────────────────────────────────────────────
    constexpr float kS = kSubTile;   // one sub-tile in world units (0.5)

    // Hulls in cell-local space, keyed by metatile id. Local to this build
    // because the vertical scale comes from min_unit_cells(), which the viewer
    // can change at runtime — and every change forces a rebuild anyway.
    std::unordered_map<uint16_t, std::vector<Vertex>> prop_cache;    // tufts
    // Keyed by the UNIT's whole id sequence, because that is what a hull is
    // built from now. std::map rather than unordered_map so a vector key needs
    // no hand-written hash.
    std::map<std::vector<uint16_t>, std::vector<Vertex>> carve_cache;
    std::map<std::vector<uint16_t>, Silhouette>          sil_cache;
    // Same idea, for a PROP object's WxH drawing (step 3a) rather than one
    // column's own vertical stack — see the object-aware carve path below.
    std::map<std::vector<uint16_t>, std::vector<Vertex>> object_carve_cache;

    // Solid-pixel count for a SINGLE metatile, which is what the carve/box
    // decision is judged on. Separate from the hulls, which are built from the
    // whole unit — see the decision comment below for why merging them broke.
    std::unordered_map<uint16_t, int> front_solid;

    // The palette, the map's ordinary ground and the silhouette flood's
    // background — computed once, up front, and shared with segmentation
    // (Pass 1.5, above). See the GroundContext comment for why they must be
    // the SAME values rather than independently recomputed ones.
    const uint32_t* pal      = ground.pal;
    const uint16_t  ground_id = ground.ground_id;
    const Background& bg     = ground.bg;

    for (int my = 0; my < H; ++my) {
        for (int mx = 0; mx < W; ++mx) {
            const uint16_t raw = s.cell(mx, my);
            if (raw == world::kGridUndefined) continue;

            const uint16_t id = static_cast<uint16_t>(raw & world::kMetatileIdMask);
            if (static_cast<size_t>(id) * world::kTilesPerMetatile
                    + world::kTilesPerMetatile > s.metatiles.size())
                continue;

            const size_t ci = static_cast<size_t>(my) * W + mx;
            const int    h  = col_h[ci];
            const float  gy = height_for(s.elevation(mx, my));
            const float  x0 = static_cast<float>(mx);
            const float  z0 = static_cast<float>(my);
            const uint8_t uh = static_cast<uint8_t>(h < 255 ? h : 255);

            if (cutout_claim(ci)) {
                // The source art stands once below. Use the same recovered
                // ordinary ground as inferred carved props beneath its cells.
                face_metatile(ground_id, 0, kShadeFlat, 0, x0, gy, z0 + 1,
                              kS,0,0, 0,0,-kS);
                face_metatile(ground_id, 1, kShadeFlat, 0, x0, gy + .002f, z0 + 1,
                              kS,0,0, 0,0,-kS);
                continue;
            }
            if (h == 0) {
                // Flat ground: one horizontal quad, both pairs. Seen from above,
                // so its 2D art needs no reinterpretation at all.
                //
                // An OVERLAY — tall grass, the one case where the upper pair
                // holds a whole small thing rather than the visible corner of a
                // big one. It is too small to stand as a unit and passable, so
                // the cell is flat ground; its upper pair becomes a per-pixel
                // hull standing on that ground instead of a decal lying in it.
                const bool overlay = cov[id] > 0 && cov[id] < kMinUpperStanding;

                face_metatile(id, 0, kShadeFlat, 0, x0, gy,          z0 + 1.0f,
                              kS, 0, 0,   0, 0, -kS);
                if (!overlay)
                    face_metatile(id, 1, kShadeFlat, 0, x0, gy + 0.002f, z0 + 1.0f,
                                  kS, 0, 0,   0, 0, -kS);

                if (overlay) {
                    auto it = prop_cache.find(id);
                    if (it == prop_cache.end()) {
                        std::vector<Vertex> hull;
                        Silhouette art;
                        if (build_alpha_mask(s, id, &art))
                            build_hull(art, 0.0f, true, hull);   // literal height
                        it = prop_cache.emplace(id, std::move(hull)).first;
                    }
                    for (Vertex vert : it->second) {
                        vert.x += x0;
                        vert.y += gy;
                        vert.z += z0;
                        v.push_back(vert);
                    }
                    ++stat_props;
                }
                continue;
            }

            // ── Scenery: carve it, do not box it ─────────────────────────────
            //
            // A volume is right for a house and wrong for a tree. What the art
            // draws is a shape; the box is only there because the shape was
            // thought to be unrecoverable, and it is not — see build_silhouette
            // for how the tree is cut out of the grass it is drawn over.
            //
            // The carve uses THE SAME FOLD as the box's faces: band k shows the
            // unit's front row stepped k north, clamped to its art. So the
            // footprint, the height and the art rows are all exactly what the
            // volume path would have used, and only the shape changes — a
            // stack of carved metatiles instead of a cube wearing them.
            //
            // WHICH THINGS GET CARVED, and the test is the art's own answer
            // rather than a proxy for it.
            //
            // The first cut asked whether the unit was two cells of drawing or
            // fewer, on the theory that anything taller is a building. It is a
            // proxy, and it broke on the six runs the unit rule still reads as
            // four cells (487 469 477 471 — two trees whose canopy variants
            // differ). Those stayed boxes, and a slab of tree-textured wall
            // standing in a carved forest is the most obvious artefact on the
            // map.
            //
            // Ask the silhouette instead: a tree leaves a third of its cell as
            // background, a house wall fills the cell edge to edge and encloses
            // nothing. If the flood cannot get in, there is no shape to carve
            // and the box was already the right answer — so this degrades to
            // the box exactly where the box is correct, instead of guessing
            // from a row count.
            const int extent = col_front[ci] - col_art_top[ci] + 1;

            // The unit's drawing, top row first, which is the order the hull
            // wants: ids[0] is the northmost art row and lands at the top.
            std::vector<uint16_t> unit_ids;
            unit_ids.reserve(static_cast<size_t>(extent));
            for (int r = col_art_top[ci]; r <= col_front[ci]; ++r)
                unit_ids.push_back(s.metatile_id(mx, r));

            // THE DECISION IS JUDGED ON THE FRONT METATILE ALONE, even though
            // the geometry is built from the whole unit. They are different
            // questions and merging them broke the buildings.
            //
            // Scaling the threshold by the unit's height (count <= max * extent)
            // sounds equivalent and is not: a three-cell structure's drawing has
            // background at its corners and along its eaves, which is a small
            // fraction of one cell but adds up over three, so the building
            // cleared the scaled bar and got carved — and carving a wall erodes
            // its edges into black notches you can see through.
            //
            // One cell of facade is the measurement that actually separates the
            // two cases, and it is sharply bimodal: trees 175-182 of 256, every
            // wall and cliff face 256.
            // unit_ids runs top to bottom, so the front row is the last one.
            const uint16_t front_id = unit_ids.back();

            auto fs = front_solid.find(front_id);
            if (fs == front_solid.end()) {
                Silhouette one;
                build_silhouette(s, &front_id, 1, bg, pal, &one);
                fs = front_solid.emplace(front_id, one.count).first;
            }
            const bool carve = fs->second > 0 && fs->second <= kCarveMaxSolid;

            if (carve) {
                // PLAIN GROUND under it, not this cell's own lower pair.
                //
                // The cell's art IS the tree — for a tree metatile the lower
                // pair is the canopy drawn over grass, not the grass. Laying
                // that flat painted a copy of the tree on the floor under the
                // carved one, and every forest cell wore a dark mottled patch
                // instead of the ground it stands on. The map's dominant flat
                // metatile is what is actually underneath.
                face_metatile(ground_id, 0, kShadeFlat, 0, x0, gy, z0 + 1.0f,
                              kS, 0, 0,   0, 0, -kS);

                // OBJECT-AWARE PATH (step 3a of the object-mesher work): a
                // drawing may span SEVERAL COLUMNS, not just several rows — a
                // tree is two metatiles wide, and building each column's own
                // hull independently is defect 3: two touching round blobs
                // with a visible seam down the middle, a figure-8 from above,
                // instead of one crown that tapers smoothly across the whole
                // width. Where segmentation (Pass 1.5) has already named this
                // cell's DRAWING — a PROP object, never a mass or a structure,
                // which keep the per-column path below untouched — every
                // member cell of that object stamps the SAME shared hull
                // exactly once, at the object's own front-west corner. Every
                // other member cell leaves its ground in place (already
                // emitted, above) and moves on: this is the SAME "one object
                // per unit, at its front cell" rule the per-column path uses,
                // generalised from ONE column to however many the drawing
                // spans.
                const int obj_idx = (ci < cell_object.size()) ? cell_object[ci] : -1;
                const bool is_prop_object = obj_idx >= 0
                    && objects[obj_idx].obj.cls == ObjectClass::kProp;

                if (is_prop_object) {
                    const Object& o = objects[obj_idx].obj;
                    const int front_row = o.y + o.extent - 1;
                    if (mx != o.x || my != front_row) continue;

                    // The object's own WxH drawing, row-major, top-left first
                    // — build_silhouette's own convention, generalised from
                    // one column to o.w. Content-keyed, like the per-column
                    // cache below: two ordinary trees with the same canopy
                    // and base variant share one hull, not one each.
                    //
                    // o.ids IS that rectangle, built once by segmentation
                    // (see Object::ids) rather than rebuilt here. The pattern
                    // an override is keyed on and the rectangle this hull is
                    // meshed from are now the same vector, so they cannot
                    // drift into disagreeing — which is the whole basis for
                    // keying an override on a tile-id signature at all.
                    auto it = object_carve_cache.find(o.ids);
                    if (it == object_carve_cache.end()) {
                        Silhouette art;
                        build_silhouette(s, o.ids.data(), o.w, o.extent, bg, pal, &art);
                        std::vector<Vertex> hull;
                        build_hull(art, static_cast<float>(o.extent), true, hull);
                        it = object_carve_cache.emplace(o.ids, std::move(hull)).first;
                    }
                    for (Vertex vert : it->second) {
                        vert.x += x0;
                        vert.y += gy;
                        vert.z += z0;
                        vert.unit_h = uh;
                        v.push_back(vert);
                    }
                    ++stat_carved;
                    continue;
                }

                // PER-COLUMN FALLBACK, unchanged — anything segmentation did
                // not confidently claim as a PROP object still renders
                // exactly as it did before step 3a.
                //
                // ONE object per unit, at its FRONT cell — not one per cell of
                // the footprint.
                //
                // This is the pop-up-book rule applied to the carve, and it is
                // the same correction diorama.h opens with: in a top-down map
                // screen-Y conflates DEPTH and HEIGHT, and a tree's canopy is
                // drawn in the cell NORTH of its trunk because it sits ABOVE
                // it, not behind it. Stamping a hull in both cells builds the
                // tree twice, one metre apart, and a forest of those fuses into
                // a solid two-cell-deep hedge — which is exactly what the first
                // carve looked like, at two million vertices.
                if (my != col_front[ci]) continue;

                auto sil = sil_cache.find(unit_ids);
                if (sil == sil_cache.end()) {
                    Silhouette art;
                    build_silhouette(s, unit_ids.data(), extent, bg, pal, &art);
                    sil = sil_cache.emplace(unit_ids, std::move(art)).first;
                }

                auto it = carve_cache.find(unit_ids);
                if (it == carve_cache.end()) {
                    std::vector<Vertex> hull;
                    // ONE hull for the whole drawing, standing `extent` cells.
                    // Per-metatile hulls made a two-metatile tree into two
                    // stacked balls, because each 16-row slice measured its own
                    // widths and started a fresh taper. And a unit whose art is
                    // one row used to have that row stamped TWICE to reach the
                    // min_unit floor, which is where the tree-on-top-of-a-tree
                    // and the double signposts came from: the height rule is
                    // the drawing's own extent now, and the shape carries it.
                    build_hull(sil->second, static_cast<float>(extent),
                               true, hull);
                    it = carve_cache.emplace(unit_ids, std::move(hull)).first;
                }
                for (Vertex vert : it->second) {
                    vert.x += x0;
                    vert.y += gy;
                    vert.z += z0;
                    vert.unit_h = uh;
                    v.push_back(vert);
                }
                ++stat_carved;
                continue;
            }

            // ROOF ROWS vs FACADE ROWS. How many of the unit's ART rows are
            // ROOF (no footprint of their own, walked behind — see OVERHANG
            // in cell_stands) versus FACADE (footprint, folded upright on the
            // walls). Read from the data instead of a fixed ratio: count the
            // unit's own leading OVERHANG rows and add one, capped at half
            // the drawing so a unit with NO overhang row (extent >= 3 but
            // every row is footprint — a plain cliff face) still gets some
            // roof once it is tall enough to have one at all. A unit with two
            // overhang rows (a deeper eave) gets a two-row roof; one with
            // none still gets a one-row roof once roofed at all, because a
            // roof of zero rows is a flat lid, and that branch is handled
            // separately below.
            int overhang_rows = 0;
            while (overhang_rows < extent
                   && !cell_is_footprint(s, mx, col_art_top[ci] + overhang_rows))
                ++overhang_rows;
            int roof_rows_col = extent >= 3 ? overhang_rows + 1 : 0;
            const int roof_cap = (extent + 1) / 2;
            if (roof_rows_col > roof_cap) roof_rows_col = roof_cap;

            // AUTHORED ROOF ROWS replace the inference outright.
            //
            // One value applies across EVERY column of the object, which is
            // incidentally the "region consensus" fix _docs/voxel-geometry.md
            // has had open for structures: a doorway column can no longer
            // disagree with its neighbours about where the roof begins,
            // because the answer no longer comes from the column.
            //
            // Clamped to what the drawing can support. A file asking for more
            // roof rows than the unit is tall would index past the unit's own
            // art and leave a negative wall; refusing quietly here is better
            // than rendering something the author cannot have meant.
            const int aidx = (ci < cell_object.size()) ? cell_object[ci] : -1;
            const Object::Authored* auth =
                (aidx >= 0 && objects[aidx].obj.authored.active)
                    ? &objects[aidx].obj.authored : nullptr;
            if (auth && auth->roof_rows >= 0) {
                roof_rows_col = auth->roof_rows;
                if (roof_rows_col > extent) roof_rows_col = extent;
                if (roof_rows_col > h)      roof_rows_col = h;
            }

            // Bands padded on by min_unit_cells() beyond the drawing's own
            // extent belong to the WALL, not the roof — they repeat the
            // facade's own top row (the fold's clamp below), which is what
            // makes a padded tree's extra band still read as more tree
            // rather than more roof it was never drawn to have.
            const int wall_rows = h - roof_rows_col;

            if (my == col_front[ci]) {
                ++stat_boxed;

                // DOES THIS UNIT'S DRAWING APPEAR ON TWO SURFACES AT ONCE?
                // Kept as a LIVE CHECK rather than dead arithmetic: with the
                // fold now clamped at the first FACADE row (wall_rows above,
                // and the fold itself below) this must always read zero by
                // construction — tools/uat.ps1's dupart check is what
                // verifies that stays true.
                if (roof_rows_col > 0) {
                    const int fold_top = col_front[ci] - (wall_rows - 1);
                    const int roof_bot = col_art_top[ci] + roof_rows_col - 1;
                    const int lo = fold_top > col_art_top[ci] ? fold_top : col_art_top[ci];
                    if (roof_bot >= lo) stat_dup_art += roof_bot - lo + 1;
                }
            }

            // A solid column. Its TOP — now the top of the FACADE, not the
            // whole unit, which is the fix for defect 4 — wears the unit's
            // northmost FACADE row when there is no roof at all. A roofed
            // unit never reads this row directly; the roof surface below
            // picks its own art by proximity to the ridge instead.
            const float ty = gy + static_cast<float>(wall_rows);
            const uint16_t top_id = s.metatile_id(mx, col_art_top[ci]);

            // ── The roof ─────────────────────────────────────────────────────
            //
            // A flat lid on a building is a warehouse. A structure gets a
            // GABLE: the surface rises from the facade top at the south eave to
            // a ridge across the middle of the footprint, then falls back to
            // the facade at the north edge — so the far side sits LOW.
            //
            // The far side is the whole point. DRAMALESS_SHAPE records the
            // first attempt so it need not be repeated: "a shed plane rising
            // all the way north, which turns a building into a ramp."
            //
            // Reassembling Ruby's two buildings from their metatile ids (see
            // probe_buildings, and the trace ids 620/628/636/644) shows the
            // convention the reference assumes holds here too: the NORTHERN
            // rows of a building's drawing are its roof, seen from above, and
            // the southern rows are its facade, seen face on. The Pokemon
            // Centre's roof is even drawn already hipped.
            //
            // The RIDGE POSITION (mid, d0, gable_h below) stays keyed on the
            // unit's full extent — that is a question about DEPTH (where
            // along the footprint the ridge sits), separate from roof_rows_col
            // above, which is a question about HEIGHT (how many of the art's
            // rows are roof versus wall). Confusing the two was the earlier
            // (extent+1)/2 formula's real mistake: one ratio was made to
            // answer both questions, and it only matched this map's two
            // buildings by coincidence.
            // An authored rise is per ROOF ROW, the same units RUBYVR_ROOF_RISE
            // uses, so `rise` keeps meaning what roof_rise() means: the ridge
            // stands roof_rows * k cells above the eave.
            const float rise = (auth && auth->rise >= 0.0f)
                             ? static_cast<float>(roof_rows_col) * auth->rise
                             : roof_rise(roof_rows_col);

            if (rise > 0.0f) {
                const float mid = static_cast<float>(extent) * 0.5f;
                auto gable_h = [&](float d) {
                    const float t = (d <= mid)
                                  ? d / mid
                                  : (static_cast<float>(extent) - d) / (static_cast<float>(extent) - mid);
                    return ty + rise * (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
                };
                const float d0 = static_cast<float>(col_front[ci] - my);
                const float hS = gable_h(d0);
                const float hN = gable_h(d0 + 1.0f);

                // Roof art by proximity to the ridge, mirrored over the back:
                // ridge art at the ridge, eave art at the eave. The roof is
                // taken to be the drawing's northern half — which is what both
                // of this map's buildings actually are, and is the piece the
                // reference gets from an authored profile we do not have.
                const float half = mid > 0.5f ? mid : 0.5f;
                const float rel  = 1.0f - std::fabs(d0 + 0.5f - mid) / half;
                int idx = static_cast<int>((1.0f - rel) * static_cast<float>(roof_rows_col));
                if (idx < 0) idx = 0;
                if (idx > roof_rows_col - 1) idx = roof_rows_col - 1;
                const uint16_t roof_id = s.metatile_id(mx, col_art_top[ci] + idx);

                // HIPPED FLANKS — the roof falls to the eave at the east and
                // west ends too, instead of stopping dead and leaving a wedge.
                //
                // A GABLE stops the slope at the end wall, and the triangle of
                // daylight under it has to be filled with geometry THE ART
                // NEVER STATES. That filler was one quad wearing a single
                // corner tile of the roof metatile, and it failed twice over:
                // it sampled entry 0 — the LOWER pair, which for a roof
                // metatile is the plain grass drawn behind the building, so
                // both buildings wore a patch of lawn on each flank — and on
                // the Pokemon Centre the upper pair's corner tile is index 0,
                // the blank tile, which push_quad drops entirely, so the
                // Centre had no gable ends at all and you could see straight
                // into it. Neither was visible from any of the six
                // south-facing shots: a gable end faces east or west, so it is
                // edge-on or occluded in every one of them. The side views
                // found both within a minute of existing.
                //
                // Hipping deletes the problem rather than patching it. There
                // is no wedge, so there is nothing to invent art for.
                // DRAMALESS_SHAPE hips for the same reason, and
                // _docs/voxel-geometry.md already called it the nicer shape —
                // skipped only because it needs the footprint's EAST-WEST
                // extent, which a per-column heightfield cannot know. The
                // object table knows it, so the blocker named in the doc is
                // gone.
                //
                // The surface is min(gable profile, lateral ramp) at the same
                // slope: identical to the old gable across the middle of a
                // wide building, falling to the eave within one cell of each
                // end. A building deeper than it is wide hips to a point; the
                // ridge survives wherever the width exceeds the depth.
                const int  robj = (ci < cell_object.size()) ? cell_object[ci] : -1;
                const bool hip  = robj >= 0;
                const float wx0 = hip ? static_cast<float>(objects[robj].obj.x) : 0.0f;
                const float wx1 = hip ? static_cast<float>(objects[robj].obj.x
                                                           + objects[robj].obj.w) : 0.0f;
                const float slope = rise / (mid > 0.001f ? mid : 1.0f);

                // Roof height at an absolute (x, z). Sampling a FUNCTION OF
                // POSITION at cell corners is what keeps the surface
                // watertight: two cells sharing an edge evaluate the same two
                // corners and get the same answers, so a hip crease running
                // diagonally through a cell is rounded off inside it rather
                // than tearing it open. The ridge already gets rounded the
                // same way when it falls inside a cell on an odd extent.
                auto surf = [&](float wx, float wz) {
                    const float g =
                        gable_h(static_cast<float>(col_front[ci]) + 1.0f - wz);
                    if (!hip) return g;
                    const float dw = wx - wx0, de = wx1 - wx;
                    const float dl = dw < de ? dw : de;
                    const float l  = ty + slope * (dl < 0.0f ? 0.0f : dl);
                    return g < l ? g : l;
                };

                // Sub-tile by sub-tile, each corner at its own height.
                // face_metatile cannot draw this: it carries ONE linear "up"
                // step, so it can slope in depth or across but never both, and
                // inside an end cell a hip does both. Same TL/TR/BL/BR order
                // and the same winding it uses, so culling is unchanged — art
                // row 0 is the metatile's top, which is NORTH.
                // THE UPPER PAIR ONLY, and this is the third time the same
                // confusion has produced a visible bug. A roof row is an
                // OVERHANG row, which by construction has upper-pair art (see
                // cell_stands' coverage floor) — and whose LOWER pair is the
                // grass drawn behind the building. Laying both down, the way
                // flat ground correctly does, paints that grass onto the roof
                // wherever the upper pair is not opaque: the Pokemon Centre's
                // roof corners (72/643, 100 of 256 opaque up top) came out
                // wearing green blotches. Where the upper pair IS blank the
                // art is saying "no roof here" — its 2D roof is already drawn
                // hipped — and the hip has now cut most of that corner away
                // geometrically anyway.
                // WHERE THE DRAWN HIP AND THE BUILT HIP DISAGREE, prefer the
                // roof art that is actually solid.
                //
                // The Pokemon Centre's roof is drawn already hipped, so its
                // corner metatiles (72, 643) are only 100 of 256 opaque up
                // top: the art rounds those corners off. Our hip rounds them
                // off too, but GEOMETRICALLY and not to the same curve, so the
                // blank texels no longer line up with the removed geometry and
                // the roof came out with holes you could see sky through.
                // Taking the most opaque metatile of the SAME art row across
                // the object's width fills them with the roof's own material —
                // the ridge course rather than the corner course. What is lost
                // is the drawn corner, which the hip has already replaced.
                uint16_t roof_art = roof_id;
                if (hip && roof_id < cov.size() && cov[roof_id] < 200) {
                    const int rrow = col_art_top[ci] + idx;
                    int best = roof_id < cov.size() ? cov[roof_id] : 0;
                    for (int c = objects[robj].obj.x;
                         c < objects[robj].obj.x + objects[robj].obj.w; ++c) {
                        const uint16_t cand = s.metatile_id(c, rrow);
                        if (cand < cov.size() && cov[cand] > best) {
                            best = cov[cand];
                            roof_art = cand;
                        }
                    }
                }

                const size_t rbase =
                    static_cast<size_t>(roof_art) * world::kTilesPerMetatile;
                if (rbase + 8 <= s.metatiles.size()) {
                    constexpr int pair = 1;

                    // A ROOF MAY NOT HAVE HOLES IN IT, so no sub-tile of one
                    // is ever allowed to be the blank tile. push_quad drops
                    // index 0 (it is the empty tile, and emitting six vertices
                    // per empty sub-tile would roughly double the mesh), which
                    // is right everywhere except here: on a closed surface a
                    // dropped quad is a window into the building. Choosing the
                    // most opaque METATILE above is not enough on its own —
                    // 73 and 642 are 220 of 256, and the missing 36 pixels are
                    // one whole 8x8 sub-tile, which is the notch that was left
                    // beside the ridge. So a blank sub-tile borrows a solid
                    // one from the same roof metatile.
                    world::TileEntry solid{};
                    for (int i = 0; i < 4; ++i) {
                        const world::TileEntry c = world::unpack_tile_entry(
                            s.metatiles[rbase + pair * 4 + i]);
                        if (c.index != 0) { solid = c; break; }
                    }

                    for (int i = 0; i < 4; ++i) {
                        world::TileEntry e = world::unpack_tile_entry(
                            s.metatiles[rbase + pair * 4 + i]);
                        if (e.index == 0) e = solid;
                        const float xa = x0 + static_cast<float>(i & 1) * kS;
                        const float xb = xa + kS;
                        const float zn = z0 + static_cast<float>(i >> 1) * kS;
                        const float zs = zn + kS;
                        push_quad(v, e, kShadeTop, uh,
                                  xa, surf(xa, zn), zn,    // north-west
                                  xb, surf(xb, zn), zn,    // north-east
                                  xb, surf(xb, zs), zs,    // south-east
                                  xa, surf(xa, zs), zs);   // south-west
                    }
                }

                // Only a component with no bbox — which no standing cell
                // should have — still needs the old wedge filled. Kept as the
                // honest fallback rather than deleted, and now reading the
                // UPPER pair (entries 4-7) so at least it is roof material
                // rather than the grass behind the building.
                if (!hip && rbase + 5 <= s.metatiles.size()) {
                    const world::TileEntry re =
                        world::unpack_tile_entry(s.metatiles[rbase + 4]);
                    if (top_of(mx + 1, my) < ty - 0.001f)          // east flank
                        push_quad(v, re, kShadeEast, uh,
                                  x0 + 1.0f, hS, z0 + 1.0f,
                                  x0 + 1.0f, hN, z0,
                                  x0 + 1.0f, ty, z0,
                                  x0 + 1.0f, ty, z0 + 1.0f);
                    if (top_of(mx - 1, my) < ty - 0.001f)          // west flank
                        push_quad(v, re, kShadeWest, uh,
                                  x0, hN, z0,
                                  x0, hS, z0 + 1.0f,
                                  x0, ty, z0 + 1.0f,
                                  x0, ty, z0);
                }
            } else {
                face_metatile(top_id, 0, kShadeTop, uh, x0, ty,          z0 + 1.0f,
                              kS, 0, 0,   0, 0, -kS);
                face_metatile(top_id, 1, kShadeTop, uh, x0, ty + 0.002f, z0 + 1.0f,
                              kS, 0, 0,   0, 0, -kS);
            }

            // Side faces, emitted ONLY where something is actually exposed.
            //
            // This is classic voxel hidden-face removal and it is doing two
            // jobs: it removes the faces buried inside a building (most of
            // them), and because those faces never reach the rasteriser it cuts
            // overdraw, which is what the frame budget is actually spent on.
            //
            // THE FOLD RULE, which is the whole of what a side face shows:
            //
            //   Band k of EVERY side shows the unit's front row stepped k rows
            //   north, clamped to the unit's own FACADE (art_top + the roof's
            //   own row count, not art_top itself — see roof_rows_col above,
            //   and defect 4 in the design notes for what clamping at bare
            //   art_top used to duplicate):
            //
            //       row(k) = max(art_top + roof_rows_col, front - k)
            //
            // Three things follow from that, and each one was a visible bug:
            //
            //   FRONT, not per-cell. The earlier rule read `my - k` — k rows
            //   north of THIS cell. For a forest of identical tiles every cell
            //   is its own unit's front, so every band read the same canopy and
            //   every tree wore its canopy down all four sides. Anchoring to
            //   the unit's front means a house's flank shows the house's
            //   facade, which is the only art that exists for it.
            //
            //   THE SAME stack on all four sides. Not each side reading its own
            //   row: there is exactly one drawing of this object and turning it
            //   to face east does not conjure an east elevation. The shading is
            //   what distinguishes the sides; the art cannot.
            //
            //   CLAMPED, so bands above the art repeat the topmost row rather
            //   than reading past the unit. min_unit_cells() routinely pads a
            //   unit taller than it was drawn, and without the clamp those
            //   extra bands sampled whatever lay north — usually plain grass,
            //   which is why padded trees stood in front of blank green walls.
            //
            // Following DRAMALESS_SHAPE's ChunkMesher (MIT): "the south face is
            // the drawing itself (full brightness); flanks and back wear the
            // same front stack darkened". Its `run` path mirrors the NORTH face
            // instead (band k reads art_top + k, so the back reads back-to-
            // front). That is defensible — you are looking at the object's
            // back — but it is a look decision on art that has no back, and one
            // rule for four faces is the simpler thing to judge. Changing it is
            // one line, below.
            struct Side {
                int dx, dz; uint8_t shade;
                float ox, oz, rx, rz;   // origin corner and the "right" step
            };
            const Side sides[4] = {
                // south (+Z): right runs +X
                { 0,  1, kShadeSouth, 0.0f, 1.0f,  kS, 0.0f },
                // north (-Z): right runs -X, so the art is not mirrored
                { 0, -1, kShadeNorth, 1.0f, 0.0f, -kS, 0.0f },
                // east (+X): right runs -Z
                { 1,  0, kShadeEast,  1.0f, 1.0f, 0.0f, -kS },
                // west (-X): right runs +Z
                {-1,  0, kShadeWest,  0.0f, 0.0f, 0.0f,  kS },
            };

            for (const Side& sd : sides) {
                const float nb_top = top_of(mx + sd.dx, my + sd.dz);
                if (nb_top >= ty - 0.001f) continue;   // fully buried

                // Only the part standing proud of the neighbour needs a face.
                const float from = (nb_top > gy) ? nb_top : gy;
                const int   k0   = static_cast<int>((from - gy) + 0.001f);

                for (int k = k0; k < wall_rows; ++k) {
                    // The fold: this unit's front row, stepped k north,
                    // clamped to the FACADE — not the whole unit's art. That
                    // is defect 4's actual fix: clamping at art_top let the
                    // fold walk over the roof's own rows once a unit had a
                    // roof, so a roofed building's art (the Pokemon Centre's
                    // Pokeball, a house's window) appeared twice — once
                    // folded up the wall, once lying on the roof above it.
                    // (For the north-face mirror, this line becomes
                    // min(front, art_top + k) when sd.dz < 0.)
                    int row = col_front[ci] - k;
                    const int facade_top = col_art_top[ci] + roof_rows_col;
                    if (row < facade_top) row = facade_top;
                    const uint16_t band = s.metatile_id(mx, row);
                    const float by = gy + static_cast<float>(k);

                    // The upper pair sits slightly PROUD of the lower one, and
                    // it is not a nicety: for a building the facade is in the
                    // upper pair and the lower pair is the grass drawn behind
                    // it (census: ids 620-622 carry 251/256 opaque pixels up
                    // top, over a lower pair that is plain ground). Emitted at
                    // the same depth the two z-fight, and the walls of every
                    // house came out mottled grass-green instead of showing
                    // their own door. The flat-ground and roof faces already
                    // separated their pairs this way; the side faces never did,
                    // and it only became visible once the fold started showing
                    // a structure's real art on its flanks.
                    constexpr float kProud = 0.002f;
                    const float ox = static_cast<float>(sd.dx) * kProud;
                    const float oz = static_cast<float>(sd.dz) * kProud;

                    face_metatile(band, 0, sd.shade, uh,
                                  x0 + sd.ox, by, z0 + sd.oz,
                                  sd.rx, 0, sd.rz,   0, kS, 0);
                    face_metatile(band, 1, sd.shade, uh,
                                  x0 + sd.ox + ox, by, z0 + sd.oz + oz,
                                  sd.rx, 0, sd.rz,   0, kS, 0);
                }
            }

            // Skirt down to a lower neighbour's GROUND, so a column standing on
            // a ledge does not leave a gap you can see under.
            for (const Side& sd : sides) {
                const float nb_g = ground_of(mx + sd.dx, my + sd.dz);
                if (nb_g <= -1e8f || nb_g >= gy - 0.001f) continue;
                const int steps = static_cast<int>((gy - nb_g) / kS + 0.999f);
                for (int k = 0; k < steps; ++k) {
                    const float by = gy - static_cast<float>(k + 1) * kS;
                    if (by < nb_g - 0.001f) break;
                    face_metatile(id, 0, sd.shade, uh,
                                  x0 + sd.ox, by, z0 + sd.oz,
                                  sd.rx, 0, sd.rz,   0, kS, 0);
                }
            }
        }
    }

    for (const auto& claim : claims.claims) {
        const auto& pattern = g_overrides.patterns[size_t(claim.pattern)];
        if (!pattern.cutout && pattern.parts.empty()) continue;
        cutout::Art art;
        if (!cutout::valid(pattern) || !cutout::compose(s, pattern, &art)) continue;
        int foot = pattern.anchor;
        for (int k = pattern.cells()-1; k >= 0; --k) if (pattern.mask[size_t(k)]) { foot = k; break; }
        const float gy = height_for(s.elevation(claim.x + foot % pattern.w, claim.y + foot / pattern.w));
        if (!pattern.parts.empty()) emit_parts(v,art,pattern,float(claim.x),gy,float(claim.y+pattern.extent)-.5f);
        else emit_cutout(v, art, *pattern.cutout, float(claim.x), gy, float(claim.y + pattern.extent) - .5f);
    }

    if (col_h_out) *col_h_out = col_h;

    g_vertex_count = static_cast<GLsizei>(v.size());
    g_map_w = static_cast<float>(s.width);
    g_map_h = static_cast<float>(s.height);

    gl::glBindVertexArray(g_vao);
    gl::glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    gl::glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(v.size() * sizeof(Vertex)),
                     v.data(), GL_STATIC_DRAW);
    ++g_mesh_upload_count;
    gl::glBindVertexArray(0);

    // The unit stats are the diagnostic for repeat detection. A route that is
    // mostly forest should report many units, nearly all 1-2 cells tall, with a
    // small number of tall ones for the houses. If almost everything is tall,
    // the repeat check has failed and the forests have fused into walls.
    std::fprintf(stderr,
                 "[diorama] meshed %dx%d layout %08X: %d vertices, "
                 "%d standing units, %d of them 3+ cells tall, "
                 "%d carved from %d silhouettes, %d tufts from %d, "
                 "%d objects segmented\n",
                 s.width, s.height, s.layout_ptr, (int)g_vertex_count,
                 stat_units, stat_tall,
                 stat_carved, (int)carve_cache.size(),
                 stat_props, (int)prop_cache.size(),
                 (int)objects.size());

    // OBJECT-TABLE fields, not the per-column counters above (stat_carved
    // etc, kept only for the human-readable line and Pass 2's own
    // bookkeeping). A check can now ask "is this ONE object" instead of
    // "does every column individually agree with itself" — which is what
    // every remaining defect actually turns out to be. carved counts PROP
    // objects; boxed counts STRUCTURE objects PLUS masses, each once — a
    // mass still renders through Pass 2's per-column path unchanged, but it
    // is one THING as far as the count is concerned.
    int n_props = 0, n_structs = 0, n_masses = 0;
    std::map<int, int> extent_hist;   // sorted by key for free
    for (const Component& c : objects) {
        switch (c.obj.cls) {
            case ObjectClass::kMass:      ++n_masses;  break;
            case ObjectClass::kStructure: ++n_structs; break;
            case ObjectClass::kProp:      ++n_props;   break;
        }
        ++extent_hist[c.obj.extent];
    }
    const int n_objects = n_props + n_structs;
    const int n_carved  = n_props;
    const int n_boxed   = n_structs + n_masses;

    char hist[96] = {};
    int hp = 0;
    for (const auto& kv : extent_hist)
        if (hp < 90)
            hp += std::snprintf(hist + hp, sizeof(hist) - hp, "%s%dx%d",
                                hp ? " " : "", kv.first, kv.second);

    // Machine-readable, one line, stable field order. The acceptance checks in
    // tools/uat.ps1 read THIS rather than a screenshot or a claim in chat.
    // geom is an FNV-1a hash of every vertex's own fields (see
    // hash_geometry) — a count can drift by one and back and still match;
    // this cannot, which is what makes "this commit changed nothing about
    // what gets drawn" a checkable claim rather than an assertion.
    const uint64_t geom = hash_geometry(v);
    std::fprintf(stderr,
                 "[stats] vertices=%d objects=%d masses=%d carved=%d boxed=%d "
                 "tufts=%d dupart=%d heights=%s geom=%016llx\n",
                 (int)g_vertex_count, n_objects, n_masses, n_carved, n_boxed,
                 stat_props, stat_dup_art, hp ? hist : "none",
                 static_cast<unsigned long long>(geom));

    // [object]/[mass] lines, ONE PER COMPONENT, printed AFTER [stats] — see
    // tools/uat.ps1's Read-Stats, which reads only what follows the LAST
    // [stats] line, because an isolate build meshes the live map first and
    // these describe whichever mesh was built LAST.
    // WHAT WAS AUTHORED, WHERE ANYTHING WAS. Printed only when a claim actually
    // supplied values, so a run with no override file emits byte-identical
    // lines to every run before this suffix existed — which is what lets the
    // recorded baselines stay the baselines.
    //
    // BEFORE " solid NN%", deliberately. solid_pct is not public API, so
    // compare.cpp strips it at the first " solid " to compare the mesher's line
    // against one rebuilt from the public model; a suffix after it would be
    // stripped too and never checked.
    //
    // This format is duplicated in compare.cpp's object_line(). That is safe in
    // a way the pattern writer was not: the two are compared line-for-line by
    // run_object_checks, so drift here FAILS loudly instead of passing quietly.
    auto authored_suffix = [](const Object& o, char* buf, size_t n) {
        if (!o.authored.active) { buf[0] = 0; return; }
        std::snprintf(buf, n, " authored roof_rows=%d rise=%.3f height=%d",
                      o.authored.roof_rows,
                      static_cast<double>(o.authored.rise),
                      o.authored.height);
    };

    for (int k = 0; k < static_cast<int>(objects.size()); ++k) {
        const Object& o = objects[k].obj;
        char auth[96];
        authored_suffix(o, auth, sizeof(auth));
        if (o.cls == ObjectClass::kMass) {
            std::fprintf(stderr,
                         "[mass] #%d %d,%d %dx%d extent %d door %d%s solid %d%%\n",
                         k, o.x, o.y, o.w, o.extent, o.extent,
                         o.has_door ? 1 : 0, auth, objects[k].solid_pct);
        } else {
            std::fprintf(stderr,
                         "[object] #%d %d,%d %dx%d extent %d class %s "
                         "door %d%s solid %d%%\n",
                         k, o.x, o.y, o.w, o.footprint_rows, o.extent,
                         o.cls == ObjectClass::kStructure ? "structure" : "prop",
                         o.has_door ? 1 : 0, auth, objects[k].solid_pct);
        }
    }
}


#include "region_mesh.inl"

// The one GL submission path, shared by the headset render and the offline
// inspector. Keeping it single means the inspector cannot drift into testing a
// pipeline the headset never uses.
void submit(const math::Mat4& view_proj, const math::Mat4& model, int debug, Tint tint = {},const RegionGPU* chunk=nullptr) {
    const math::Mat4 mvp = math::multiply(view_proj, model);

    gl::glUseProgram(g_prog);
    gl::glUniformMatrix4fv(g_u_mvp, 1, GL_FALSE, mvp.m);
    gl::glUniform1i(g_u_debug, debug);
    gl::glUniform4f(g_u_tint, tint.r, tint.g, tint.b, 1.f);
    gl::glUniform2f(g_u_region,chunk?float(chunk->x):0.f,chunk?float(chunk->z):0.f);
    gl::glUniform1i(g_u_placed,0);

    gl::glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, chunk?chunk->tiles:g_tex_tiles);
    gl::glUniform1i(g_u_tiles, 0);
    gl::glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, chunk?chunk->palette:g_tex_pal);
    gl::glUniform1i(g_u_pal, 1);
    gl::glActiveTexture(GL_TEXTURE0);

    // Back-face culling, now that the world is closed boxes rather than
    // single-sided billboards. This throws away the far side of every box
    // before rasterisation, which matters because this scene is fill-bound.
    //
    // GL_CW, not the GL_CCW default. face_metatile builds every quad as
    // top-left -> top-right -> bottom-right using the face's own right/up
    // basis, and for all five orientations that traces CLOCKWISE when viewed
    // from OUTSIDE the box. Declaring that as front-facing is one line; the
    // alternative is reversing the winding at every emission site.
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);
    glCullFace(GL_BACK);

    gl::glBindVertexArray(chunk?chunk->vao:g_vao);
    if(chunk) {
        for(const auto& draw:chunk->draws) {
            gl::glUniform1i(g_u_placed,draw.instances?1:0);
            if(draw.instances) {
                gl::glEnableVertexAttribArray(6);
                gl::glBindBuffer(GL_ARRAY_BUFFER,chunk->offsets);
                gl::glVertexAttribPointer(6,3,GL_FLOAT,GL_FALSE,sizeof(MeshOffset),
                    reinterpret_cast<void*>(draw.instance_first*sizeof(MeshOffset)));
                gl::glDrawArraysInstanced(GL_TRIANGLES,GLint(draw.first),GLsizei(draw.count),GLsizei(draw.instances));
            } else {
                // A direct range can outnumber the instance buffer. Disable its
                // attribute even though the shader branch does not consume it.
                gl::glDisableVertexAttribArray(6);
                glDrawArrays(GL_TRIANGLES,GLint(draw.first),GLsizei(draw.count));
            }
        }
    } else glDrawArrays(GL_TRIANGLES, 0, g_vertex_count);
    gl::glBindVertexArray(0);
}

}  // namespace

// ── The object model, made public ───────────────────────────────────────────
//
// See diorama.h for what this exposes and, more importantly, what it does not.
//
// WHY RECOMPUTING IS SAFE, which is the whole question this design turns on.
// The alternative was to stash the model build_mesh already computes in a
// global and hand out a pointer. That would have made the result mutable
// mesher state with a lifetime tied to the next update() — and it would have
// been a lie in the other direction too, because the studio wants to segment
// snapshots it never meshes.
//
// Recomputing is identical rather than merely similar because every input is a
// pure function of `s`: upper_coverage_table reads only the metatile tables,
// compute_ground_context only the grid and the palette, and segment_objects
// only those two plus the grid. None of them reads g_min_unit, g_meshed_layout,
// any GL object, or any environment dial — RUBYVR_MIN_UNIT and RUBYVR_ROOF_RISE
// are read by Pass 1 and the roof rise, both downstream of here. So the same
// Snapshot yields the same model, and the harness checks exactly that against
// the mesher's own [object] lines rather than taking it on trust.
bool segment(const world::Snapshot& s, ObjectModel* out) {
    return segment(s, overrides::OverrideSet{}, out);
}

void set_overrides(overrides::OverrideSet ov) {
    g_overrides = std::move(ov);
    g_meshed_layout = 0;   // forces update() to rebuild, exactly as set_min_unit
}
void set_build_mode(BuildMode mode) { if(mode!=g_build_mode) { g_build_mode=mode;g_meshed_layout=0;g_diorama_stats={}; } }
BuildMode build_mode() { return g_build_mode; }
const DioramaStats& diorama_stats() { return g_diorama_stats; }

const overrides::OverrideSet& current_overrides() { return g_overrides; }

bool segment(const world::Snapshot& s, const overrides::OverrideSet& ov,
             ObjectModel* out) {
    if (!out) return false;
    *out = ObjectModel{};
    if (!s.valid || s.width <= 0 || s.height <= 0) return false;

    // The same two precomputations build_mesh does, in the same order, from the
    // same Snapshot. build_mesh keeps passing its own copies to the internal
    // overload, so nothing is computed twice on the game's path.
    const std::vector<uint16_t> cov    = upper_coverage_table(s);
    const GroundContext         ground = compute_ground_context(s, cov);

    const ClaimMap               claims = build_claims(s, ov);
    std::vector<int32_t>         cells;
    const std::vector<Component> comps =
        segment_objects(s, cov, ground, claims, &cells);

    out->width  = s.width;
    out->height = s.height;
    out->cell_object = std::move(cells);
    out->objects.reserve(comps.size());
    for (const Component& c : comps) out->objects.push_back(c.obj);
    return true;
}

bool ready() { return g_ready; }
uint64_t mesh_upload_count() { return g_mesh_upload_count; }
bool has_geometry() { return g_vertex_count > 0; }

bool init() {
    if (!gl::loaded()) return false;
    if(const char* mode=std::getenv("RUBYVR_BUILD_MODE")) {
        if(!std::strcmp(mode,"diorama")) set_build_mode(BuildMode::Diorama);
        else if(!std::strcmp(mode,"inferred")) set_build_mode(BuildMode::Inferred);
        else { std::fprintf(stderr,"[diorama] RUBYVR_BUILD_MODE must be inferred or diorama\n");return false; }
    }

    const GLuint vs = compile(GL_VERTEX_SHADER, kVert, "vertex");
    if (!vs) return false;
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kFrag, "fragment");
    if (!fs) { gl::glDeleteShader(vs); return false; }

    g_prog = gl::glCreateProgram();
    gl::glAttachShader(g_prog, vs);
    gl::glAttachShader(g_prog, fs);
    gl::glLinkProgram(g_prog);
    gl::glDeleteShader(vs);
    gl::glDeleteShader(fs);

    GLint ok = 0;
    gl::glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        gl::glGetProgramInfoLog(g_prog, sizeof(log) - 1, nullptr, log);
        std::fprintf(stderr, "[diorama] link failed:\n%s\n", log);
        return false;
    }

    g_u_mvp   = gl::glGetUniformLocation(g_prog, "uMVP");
    g_u_debug = gl::glGetUniformLocation(g_prog, "uDebug");
    g_u_tint = gl::glGetUniformLocation(g_prog, "uTint");
    g_u_placed = gl::glGetUniformLocation(g_prog, "uPlaced");
    g_u_region = gl::glGetUniformLocation(g_prog, "uRegion");
    g_u_tiles = gl::glGetUniformLocation(g_prog, "uTiles");
    g_u_pal   = gl::glGetUniformLocation(g_prog, "uPal");

    gl::glGenVertexArrays(1, &g_vao);
    gl::glBindVertexArray(g_vao);
    gl::glGenBuffers(1, &g_vbo);
    gl::glBindBuffer(GL_ARRAY_BUFFER, g_vbo);

    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, x)));
    gl::glEnableVertexAttribArray(1);
    gl::glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, u)));
    // glVertexAttribIPointer, not glVertexAttribPointer: these feed `uint`
    // inputs and must arrive as integers. The float version would silently
    // convert, and tile 517 would sample somewhere near tile 517.0 with the
    // fractional part quietly deciding which row of the sheet you land on.
    gl::glEnableVertexAttribArray(2);
    gl::glVertexAttribIPointer(2, 1, GL_UNSIGNED_SHORT, sizeof(Vertex),
                               reinterpret_cast<void*>(offsetof(Vertex, tile)));
    gl::glEnableVertexAttribArray(3);
    gl::glVertexAttribIPointer(3, 1, GL_UNSIGNED_SHORT, sizeof(Vertex),
                               reinterpret_cast<void*>(offsetof(Vertex, pal)));
    // Normalized: the shader wants 0..1, the vertex stores 0..255 in one byte.
    gl::glEnableVertexAttribArray(4);
    gl::glVertexAttribPointer(4, 1, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, shade)));
    // Integer, NOT normalized: this is a count of cells, not a brightness.
    gl::glEnableVertexAttribArray(5);
    gl::glVertexAttribIPointer(5, 1, GL_UNSIGNED_BYTE, sizeof(Vertex),
                               reinterpret_cast<void*>(offsetof(Vertex, unit_h)));
    gl::glBindVertexArray(0);

    // Tile sheet: one byte per pixel, read as an exact integer.
    glGenTextures(1, &g_tex_tiles);
    glBindTexture(GL_TEXTURE_2D, g_tex_tiles);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 256, 256, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &g_tex_pal);
    glBindTexture(GL_TEXTURE_2D, g_tex_pal);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    g_ready = true;
    std::fprintf(stderr, "[diorama] ready\n");
    return true;
}

void update(const world::Snapshot& s) {
    if (!g_ready || !s.valid) return;

    // Mesh only on a map change. The grid is 1200-odd cells and the vertex
    // buffer runs to megabytes; rebuilding it every frame would be pure waste,
    // and the layout pointer is an exact "this is a different map" signal.
    const bool terrain_active=g_build_mode==BuildMode::Diorama && !g_overrides.terrain.empty();
    const bool terrain_changed=terrain_active && (g_terrain_group!=s.map_group || g_terrain_number!=s.map_number ||
        g_terrain_identity!=s.has_map_identity() || g_terrain_width!=s.width || g_terrain_height!=s.height ||
        g_terrain_grid!=s.grid || g_terrain_metatiles!=s.metatiles || g_terrain_attributes!=s.attributes || g_terrain_connections!=s.connections);
    if (s.layout_ptr != g_meshed_layout || terrain_changed) {
        build_mesh(s);
        g_meshed_layout = s.layout_ptr;
        if(terrain_active) {
            g_terrain_group=s.map_group;g_terrain_number=s.map_number;g_terrain_identity=s.has_map_identity();
            g_terrain_width=s.width;g_terrain_height=s.height;
            g_terrain_grid=s.grid;g_terrain_metatiles=s.metatiles;g_terrain_attributes=s.attributes;
            g_terrain_connections=s.connections;
        }
    }

    // Where the player is, in fractional cells, so first person walks smoothly.
    // Y comes from the cell under them, so stepping onto a ledge raises the
    // whole world rather than leaving you hovering.
    g_player_x = s.camera_cell_x();
    g_player_z = s.camera_cell_y();
    g_player_y = height_for(s.elevation(static_cast<int>(g_player_x),
                                        static_cast<int>(g_player_z)));

    // Tiles and palette every frame is what makes water and flowers animate for
    // free — but only re-expand and re-upload when the bytes actually CHANGED.
    // A 32 KB memcmp is far cheaper than 65,536 nibble extractions plus a
    // texture upload, and outside of animation frames the VRAM tile block is
    // identical from one frame to the next.
    static std::vector<uint8_t> prev_tiles;
    const bool tiles_changed =
        prev_tiles.size() != s.vram_tiles.size() ||
        std::memcmp(prev_tiles.data(), s.vram_tiles.data(), s.vram_tiles.size()) != 0;

    if (tiles_changed) {
        prev_tiles = s.vram_tiles;
        expand_tiles(s);
        glBindTexture(GL_TEXTURE_2D, g_tex_tiles);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256,
                        GL_RED_INTEGER, GL_UNSIGNED_BYTE, g_tile_scratch.data());
    }

    uint32_t pal[world::kPaletteEntries];
    tileset::expand_palette(s, pal);
    glBindTexture(GL_TEXTURE_2D, g_tex_pal);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 16, 16,
                    GL_RGBA, GL_UNSIGNED_BYTE, pal);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void draw(const math::Mat4& view_proj) {
    if (!g_ready || g_vertex_count == 0) return;

    // Both modes are the same four transforms with different arguments. Read
    // them right to left: put some point of the world at the origin, scale,
    // rotate, then move the result to where it should sit in the room.
    //
    //   Diorama:      centre on the MAP's middle, shrink to tabletop.
    //   First person: centre on the PLAYER, life size, and the anchor is where
    //                 you are standing — so the world slides past you as the
    //                 player walks, rather than the camera moving.
    math::Mat4 model;
    if (g_fpv) {
        model = math::multiply(
            math::multiply(math::translation(g_fpv_ax, g_fpv_ay, g_fpv_az),
                           math::rotation_y(g_fpv_yaw)),
            math::multiply(math::scale(g_fpv_scale),
                           math::translation(-g_player_x, -g_player_y, -g_player_z)));
    } else {
        model = math::multiply(
            math::multiply(math::translation(g_board_x, g_board_y, g_board_z),
                           math::rotation_y(g_board_yaw)),
            math::multiply(math::scale(g_board_scale),
                           math::translation(-g_map_w * 0.5f, 0.0f, -g_map_h * 0.5f)));
    }

    submit(view_proj, model, 0);
}

void draw_raw(const math::Mat4& view_proj, const math::Mat4& model, int debug, Tint tint) {
    if (!g_ready || g_vertex_count == 0) return;
    submit(view_proj, model, debug, tint);
}

bool build_region(const std::vector<RegionMap>& maps,const overrides::OverrideSet& set,std::string* error) {
    if(!g_ready) {if(error)*error="The renderer is not ready.";return false;}
    std::vector<RegionCPU> cpu;RegionStats stats;
    if(!region_cpu(maps,set,&cpu,&stats,error)) return false;
    std::vector<RegionGPU> candidate(maps.size());
    for(size_t i=0;i<maps.size();++i) if(!upload_region_chunk(cpu[i],*maps[i].source,&candidate[i])) {
        release_region(candidate);if(error)*error="GPU allocation failed. The previous view is retained.";return false;
    }
    release_region(g_region);g_region=std::move(candidate);g_region_stats=stats;return true;
}
void clear_region() {release_region(g_region);g_region_stats={};}
const RegionStats& region_stats() {return g_region_stats;}
bool region_bounds(part_geometry::Vec* lo,part_geometry::Vec* hi) {
    if(!lo || !hi || g_region.empty())return false;
    float low[]{1e9f,1e9f,1e9f},high[]{-1e9f,-1e9f,-1e9f};bool any=false;
    for(const auto& c:g_region)if(c.count) {any=true;for(int k=0;k<3;++k){low[k]=std::min(low[k],c.lo[k]);high[k]=std::max(high[k],c.hi[k]);}}
    *lo={low[0],low[1],low[2]};*hi={high[0],high[1],high[2]};return any;
}
void draw_region_raw(const math::Mat4& vp,int debug,Tint tint) {
    if(!g_ready) return;
    g_region_stats.draw_calls=g_region_stats.drawn_vertices=0;
    for(const auto& c:g_region) if(c.count && region_in_view(c,vp)) {
        submit(vp,math::identity(),debug,tint,&c);g_region_stats.draw_calls+=c.draws.size();g_region_stats.drawn_vertices+=c.count;
    }
}
bool inspect_region_mesh(const std::vector<RegionMap>& maps,const overrides::OverrideSet& set,
                         std::vector<RegionMesh>* out,RegionStats* stats,std::string* error) {
    if(!out || !stats)return false;
    std::vector<RegionCPU> cpu;RegionStats result;
    if(!region_cpu(maps,set,&cpu,&result,error))return false;
    std::vector<RegionMesh> meshes;
    for(const auto& c:cpu) {
        RegionMesh m;m.stats=c.stats;m.vertices.reserve(c.stats.flat_vertices+c.stats.authored_vertices);
        visit_placed(c.mesh,c.placed,c.x,c.z,[&](const Vertex& v){m.vertices.push_back({{v.x,v.y,v.z},v.u,v.v,v.tile,v.pal});});
        meshes.push_back(std::move(m));
    }
    *out=std::move(meshes);*stats=result;return true;
}

void map_size(float* w, float* h) { if (w) *w = g_map_w; if (h) *h = g_map_h; }

bool inspect_authored_mesh(const world::Snapshot& s, const overrides::Pattern& pattern, std::vector<AuthoredVertex>* out,
                           std::vector<size_t>* triangle_parts) {
    if(!out || (!pattern.cutout && pattern.parts.empty()) || !cutout::valid(pattern) || !overrides::valid_parts(pattern)) return false;
    cutout::Art art;
    if(!cutout::compose(s,pattern,&art)) return false;
    std::vector<Vertex> mesh;
    std::vector<size_t> owners;
    if(pattern.parts.empty()) {
        emit_cutout(mesh,art,*pattern.cutout,0,0,0);
        if(triangle_parts) owners.assign(mesh.size()/3,size_t(-1));
    } else if(!emit_parts(mesh,art,pattern,0,0,0,triangle_parts?&owners:nullptr)) return false;
    std::vector<AuthoredVertex> result;result.reserve(mesh.size());
    for(const auto& v:mesh) result.push_back({{v.x,v.y,v.z},v.u,v.v,v.tile,v.pal});
    *out=std::move(result);
    if(triangle_parts) *triangle_parts=std::move(owners);
    return true;
}
bool inspect_diorama_mesh(const world::Snapshot& s,const overrides::OverrideSet& set,
                          std::vector<AuthoredVertex>* out,DioramaStats* stats) {
    if(!out || !stats) return false;
    std::vector<Vertex> mesh;DioramaStats result;
    if(!build_authored_diorama(s,set,&mesh,&result)) return false;
    std::vector<AuthoredVertex> vertices;vertices.reserve(mesh.size());
    for(const auto& v:mesh) vertices.push_back({{v.x,v.y,v.z},v.u,v.v,v.tile,v.pal});
    *out=std::move(vertices);*stats=result;return true;
}

bool build_cutout_preview(const world::Snapshot& s, const overrides::Pattern& pattern,
                          uint64_t* geometry_hash, int* vertices) {
    if (!g_ready || !s.valid || (!pattern.cutout && pattern.parts.empty()) || !cutout::valid(pattern)) return false;
    cutout::Art art;
    if (!cutout::compose(s, pattern, &art)) return false;
    update(s); // upload this snapshot's winning texels/palette before the local mesh
    std::vector<Vertex> mesh;
    if (!pattern.parts.empty()) {if(!emit_parts(mesh,art,pattern,0,0,0)) return false;}
    else emit_cutout(mesh, art, *pattern.cutout, 0, 0, 0);
    g_vertex_count = GLsizei(mesh.size()); g_map_w = float(pattern.w); g_map_h = 1;
    gl::glBindVertexArray(g_vao); gl::glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    gl::glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(mesh.size()*sizeof(Vertex)), mesh.data(), GL_STATIC_DRAW);
    ++g_mesh_upload_count;
    gl::glBindVertexArray(0);
    g_meshed_layout = 0;
    if (geometry_hash) *geometry_hash = hash_geometry(mesh);
    if (vertices) *vertices = int(mesh.size());
    return true;
}

bool build_isolate(const world::Snapshot& s, int rx, int ry, int rw, int rh) {
    if (!g_ready || !s.valid || rw <= 0 || rh <= 0) return false;
    if (rx < 0 || ry < 0 || rx >= s.width || ry >= s.height) return false;
    if (rx + rw > s.width)  rw = s.width  - rx;
    if (ry + rh > s.height) rh = s.height - ry;

    // Enough ground around the subject that the inspector's cameras — the
    // furthest sits 11 cells south — stand ON the map rather than off the edge
    // looking back at a floating slab.
    constexpr int kMargin = 14;
    const std::vector<uint16_t> cov = upper_coverage_table(s);

    // The same "what is this map's ordinary ground" question the silhouette
    // flood asks, and the same answer: the commonest metatile that neither
    // stands nor blocks.
    uint16_t ground = 0;
    {
        std::unordered_map<uint16_t, int> flat;
        for (int my = 0; my < s.height; ++my)
            for (int mx = 0; mx < s.width; ++mx) {
                if (s.cell(mx, my) == world::kGridUndefined) continue;
                if (s.collision(mx, my) != 0) continue;
                const uint16_t g = s.metatile_id(mx, my);
                if (g < cov.size() && cov[g] > 0) continue;
                ++flat[g];
            }
        int best = -1;
        for (const auto& kv : flat)
            if (kv.second > best) { best = kv.second; ground = kv.first; }
    }

    world::Snapshot t = s;                      // keeps tiles, palettes, attributes
    t.width  = rw + kMargin * 2;
    t.height = rh + kMargin * 2;

    const uint16_t elev3 = static_cast<uint16_t>(3u << world::kElevationShift);
    t.grid.assign(static_cast<size_t>(t.width) * t.height,
                  static_cast<uint16_t>(ground | elev3));

    // The subject, copied cell for cell WITH its collision and elevation —
    // those are inputs to the rules being tested, so dropping them would
    // isolate a different object from the one on the map.
    for (int j = 0; j < rh; ++j)
        for (int i = 0; i < rw; ++i) {
            const uint16_t c = s.cell(rx + i, ry + j);
            if (c == world::kGridUndefined) continue;
            t.grid[static_cast<size_t>(j + kMargin) * t.width + (i + kMargin)] = c;
        }

    build_mesh(t);

    // Aim the close shots at the subject's middle. The inspector frames those
    // on the player, and on a synthetic map there is no player.
    g_player_x = static_cast<float>(kMargin) + rw * 0.5f;
    g_player_z = static_cast<float>(kMargin) + rh * 0.5f;
    g_player_y = 0.0f;

    g_meshed_layout = 0;    // so a later real snapshot rebuilds

    std::fprintf(stderr,
                 "[isolate] map cells (%d,%d) %dx%d on ground id %u, "
                 "plot %dx%d\n",
                 rx, ry, rw, rh, ground, t.width, t.height);

    // THE GROUND TRUTH for this rectangle: what is actually in it, as ids.
    //
    // Needed because you cannot count the objects in a render when the bug you
    // are chasing is "objects are being split", and you cannot write an
    // expected-object-count check without knowing the real number. A tree is a
    // 2x2 of (468 469 / 476 477); a house is a 4-wide block. Reading that off
    // the grid is unambiguous in a way that squinting at a picture is not.
    //
    // '.' is ground, '#' marks a cell that stands.
    const std::vector<uint16_t> cov2 = cov;
    for (int j = 0; j < rh; ++j) {
        std::fprintf(stderr, "[cells] row %2d |", ry + j);
        for (int i = 0; i < rw; ++i) {
            const int mx = rx + i, my = ry + j;
            if (s.cell(mx, my) == world::kGridUndefined) {
                std::fprintf(stderr, "    -");
                continue;
            }
            std::fprintf(stderr, " %4u", s.metatile_id(mx, my));
        }
        std::fprintf(stderr, "  |");
        for (int i = 0; i < rw; ++i)
            std::fprintf(stderr, "%c",
                         cell_stands(s, cov2, rx + i, ry + j) ? '#' : '.');
        std::fprintf(stderr, "\n");
    }

    return g_vertex_count > 0;
}

bool build_turntable(const world::Snapshot& s, Turntable* out) {
    if (!g_ready || !s.valid || !out) return false;

    // Distinct metatiles, each with the collision it MOST OFTEN carries on this
    // map. Collision lives in the grid cell, not the metatile, so a tileset
    // audit has to pick one — and the common case is the honest choice. (It is
    // rarely ambiguous: a tree is impassable everywhere it appears.)
    struct Tally { int total = 0; int by_coll[4] = {}; };
    std::unordered_map<uint16_t, Tally> seen;
    for (int my = 0; my < s.height; ++my)
        for (int mx = 0; mx < s.width; ++mx) {
            if (s.cell(mx, my) == world::kGridUndefined) continue;
            Tally& t = seen[s.metatile_id(mx, my)];
            ++t.total;
            ++t.by_coll[s.collision(mx, my) & 3];
        }
    if (seen.empty()) return false;

    // The plot floor: the map's commonest metatile that neither stands nor
    // carries collision. Using the real ground rather than a hardcoded id keeps
    // a cave's floor looking like a cave.
    const std::vector<uint16_t> cov = upper_coverage_table(s);
    uint16_t ground = 0;
    int ground_n = -1;
    for (const auto& kv : seen) {
        const bool flat = kv.second.by_coll[0] == kv.second.total
                       && (kv.first >= cov.size() || cov[kv.first] < kMinUpperStanding);
        if (flat && kv.second.total > ground_n) { ground = kv.first; ground_n = kv.second.total; }
    }

    std::vector<uint16_t> ids;
    ids.reserve(seen.size());
    for (const auto& kv : seen) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());

    // A plot is one cell of the subject with a ring of ground around it.
    //
    // The spacing is set by the CAMERA, not by the geometry. Three cells would
    // be enough to stop two subjects sharing a face and hiding it, but the
    // turntable orbits at renderer.cpp's kDist and views from 45 degrees, so
    // the eye sits kDist/sqrt(2) cells out along each axis — about 4.1. At a
    // pitch of 4 the camera lands exactly ON the diagonal neighbour's plot and
    // renders it as a wall across the foreground. Seven keeps the eye in open
    // ground between plots with room to spare.
    //
    // If kDist ever grows past about 9, this has to grow with it.
    constexpr int kPitch = 7;
    const int cols = static_cast<int>(std::ceil(std::sqrt(
        static_cast<double>(ids.size()))));
    const int rows = static_cast<int>((ids.size() + cols - 1) / cols);

    world::Snapshot t = s;                       // keeps tiles, palettes, attributes
    t.width  = cols * kPitch + 1;
    t.height = rows * kPitch + 1;
    t.grid.assign(static_cast<size_t>(t.width) * t.height, 0);

    const uint16_t elev3 = static_cast<uint16_t>(3u << world::kElevationShift);
    for (auto& c : t.grid) c = static_cast<uint16_t>(ground | elev3);

    out->ids.clear();
    out->collision.clear();
    for (size_t i = 0; i < ids.size(); ++i) {
        const Tally& tl = seen[ids[i]];
        int best = 0;
        for (int c = 1; c < 4; ++c) if (tl.by_coll[c] > tl.by_coll[best]) best = c;

        const int cx = static_cast<int>(i % cols) * kPitch + 1;
        const int cy = static_cast<int>(i / cols) * kPitch + 1;
        t.grid[static_cast<size_t>(cy) * t.width + cx] = static_cast<uint16_t>(
            ids[i] | (static_cast<uint16_t>(best) << world::kCollisionShift) | elev3);

        out->ids.push_back(ids[i]);
        out->collision.push_back(static_cast<uint8_t>(best));
    }

    std::vector<int16_t> col_h;
    build_mesh(t, &col_h);

    out->height.clear();
    for (size_t i = 0; i < ids.size(); ++i) {
        const int cx = static_cast<int>(i % cols) * kPitch + 1;
        const int cy = static_cast<int>(i / cols) * kPitch + 1;
        const size_t k = static_cast<size_t>(cy) * t.width + cx;
        out->height.push_back(k < col_h.size()
                              ? static_cast<uint8_t>(col_h[k]) : 0);
    }

    out->cols = cols;
    out->rows = rows;
    out->spacing = kPitch;

    // The mesh is now the turntable's, and update() only rebuilds on a map
    // CHANGE. Forget which layout was meshed so a later real snapshot rebuilds.
    g_meshed_layout = 0;

    std::fprintf(stderr,
                 "[turntable] %d metatiles on a %dx%d grid, ground id %u\n",
                 static_cast<int>(ids.size()), cols, rows, ground);
    return true;
}

void set_min_unit(int cells) {
    if (cells < 1) cells = 1;
    if (cells > 6) cells = 6;
    g_min_unit = cells;
    g_meshed_layout = 0;   // forces update() to rebuild on the next call
}

void player_cell(float* x, float* y, float* z) {
    if (x) *x = g_player_x;
    if (y) *y = g_player_y;
    if (z) *z = g_player_z;
}

void set_first_person(bool on) { g_fpv = on; }
bool first_person() { return g_fpv; }

void anchor_first_person(float x, float y, float z, float yaw) {
    g_fpv_ax = x; g_fpv_ay = y; g_fpv_az = z; g_fpv_yaw = yaw;
}

void snap_turn(float radians) { g_fpv_yaw += radians; }

void scale_by(float factor) {
    g_board_scale *= factor;
    if (g_board_scale < 0.004f) g_board_scale = 0.004f;
    if (g_board_scale > 0.500f) g_board_scale = 0.500f;
}

void raise(float metres) {
    g_board_y += metres;
    if (g_board_y < -2.0f) g_board_y = -2.0f;
    if (g_board_y >  1.5f) g_board_y =  1.5f;
}

void place(float x, float y, float z, float yaw) {
    g_board_x = x; g_board_y = y; g_board_z = z; g_board_yaw = yaw;
}

void shutdown() {
    clear_region();
    if (gl::loaded()) {
        if (g_vbo) gl::glDeleteBuffers(1, &g_vbo);
        if (g_vao) gl::glDeleteVertexArrays(1, &g_vao);
        if (g_prog) gl::glDeleteProgram(g_prog);
        if (g_tex_tiles) glDeleteTextures(1, &g_tex_tiles);
        if (g_tex_pal) glDeleteTextures(1, &g_tex_pal);
    }
    g_vbo = g_vao = g_prog = g_tex_tiles = g_tex_pal = 0;
    g_vertex_count = 0;
    g_meshed_layout = 0;
    g_ready = false;
}

}  // namespace diorama
}  // namespace vr
