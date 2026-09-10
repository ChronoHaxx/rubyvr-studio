#include "cutout.h"
#include "foundation.h"
#include "connected_scene.h"
// gui.cpp — rubyvr_gui, the authoring surface for overrides (§9 step 6).
//
// WHAT THIS IS FOR:
//
//   _docs/voxel-geometry.md states the gap this whole project turns on: for a
//   PROP, "how tall and what shape" is answered by the art's own silhouette;
//   for a BUILDING nothing states it. So that row is authored — and until now,
//   authored by typing metatile ids into JSON by hand.
//
//   This closes the loop: click an object, see the pattern it would produce and
//   every place that pattern matches, change roof_rows, watch the production
//   mesher rebuild, commit, save, reload.
//
// THE SAME MESHER, NOT A PREVIEW OF ONE. This target compiles diorama.cpp, the
// file the game compiles. There is no second geometry path here to drift from
// the first, which is the argument diorama.h already makes about the turntable
// and overrides.h makes about baked geometry. Everything drawn on screen is
// what RubyRecomp.exe would draw.
//
// NOTHING HERE TOUCHES THE MESH. The selection and match highlights are ImGui
// draw-list overlays in SCREEN space, deliberately: a highlight built into the
// geometry would move the eight scene geom= hashes that are this project's only
// defence against a confident regression. The status line shows the live hash
// for the same reason — so "the baseline still holds" is visible at a glance
// rather than remembered.

// SDL's Windows entry point normally macro-renames main() to SDL_main. This is
// a console tool with its own main, so tell SDL to keep its hands off, and call
// SDL_SetMainReady() before SDL_Init — the other half of that contract.
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <chrono>
#include <string>
#include <vector>

#include "capture.h"
#include "decomp_source.h"
#include "pattern_io.h"
#include "platform_io.h"
#include "png_write.h"
#include "snapshot_build.h"
#include "room2d.h"
#include "workspace.h"
#include "group.h"
#include "mesh_audit.h"
#include "environment_preview.h"
#include "coverage_view.h"
#include "voxel_authoring.h"

#include "diorama.h"
#include "terrain.h"
#include <set>
#include "gl_loader.h"
#include "json_scan.h"
#include "overrides.h"
#include "ruby_world.h"
#include "vr_math.h"

namespace {

using vr::diorama::Object;
using vr::diorama::ObjectClass;
using vr::diorama::ObjectModel;
using vr::math::Mat4;
using vr::overrides::OverrideSet;
using vr::overrides::Pattern;
using vr::world::Snapshot;

// ── Camera ───────────────────────────────────────────────────────────────────
//
// Orbit and fly share one pose in MAP CELL units. Orbit turns around the
// target; fly turns that target around the eye, preserving the eye position.

struct Camera {
    float yaw   = 0.6f;
    float pitch = 0.9f;
    float dist  = 30.0f;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    bool orthographic = false;
};

void camera_eye(const Camera& c, float* ex, float* ey, float* ez) {
    const float cp = std::cos(c.pitch);
    *ex = c.tx + std::sin(c.yaw) * cp * c.dist;
    *ey = c.ty + std::sin(c.pitch) * c.dist;
    *ez = c.tz + std::cos(c.yaw) * cp * c.dist;
}

// Vertical half-FOV fixed; horizontal follows the window aspect, so resizing
// widens the view instead of stretching it. The same two numbers viewer.cpp
// uses, returned as ANGLES — the callers take tan() themselves.
void camera_fov(int w, int h, float* fx, float* fy) {
    const float vy = 0.52f;
    *fy = vy;
    *fx = std::atan(std::tan(vy) * static_cast<float>(w) /
                    static_cast<float>(h > 0 ? h : 1));
}

// THE CAMERA BASIS, computed exactly as vr_math::look_at computes it —
// including its degenerate-pole fallback.
//
// Repeating those eight lines looks redundant and is not - but note what it is
// and is not buying. camera_vp() projects through vr::math::look_at(); this
// function INDEPENDENTLY REPRODUCES that convention rather than calling it, so
// the two agreeing is a hand-maintained invariant, not a structural one.
// --selftest proves they agree for its fixed camera and cell: divergence that
// reaches that scenario should fail it, divergence outside it may go
// undetected. If look_at ever changes its up-vector convention or its
// degenerate-pole fallback, this has to change with it - which is why it says
// so out loud rather than being quietly clever.
void camera_basis(const Camera& c, float* f, float* r, float* u) {
    float ex, ey, ez;
    camera_eye(c, &ex, &ey, &ez);

    float fx = c.tx - ex, fy = c.ty - ey, fz = c.tz - ez;
    float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (fl < 1e-6f) fl = 1.0f;
    fx /= fl; fy /= fl; fz /= fl;

    float ux = 0.0f, uy = 1.0f, uz = 0.0f;
    if (std::fabs(fy) > 0.999f) { ux = 0.0f; uy = 0.0f; uz = 1.0f; }

    float rx = fy * uz - fz * uy;
    float ry = fz * ux - fx * uz;
    float rz = fx * uy - fy * ux;
    float rl = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (rl < 1e-6f) rl = 1.0f;
    rx /= rl; ry /= rl; rz /= rl;

    f[0] = fx; f[1] = fy; f[2] = fz;
    r[0] = rx; r[1] = ry; r[2] = rz;
    u[0] = ry * fz - rz * fy;
    u[1] = rz * fx - rx * fz;
    u[2] = rx * fy - ry * fx;
}

void camera_zoom(Camera& c, float steps) {
    c.dist = std::clamp(c.dist * std::pow(.8f, steps), .25f, 300.f);
}

void camera_look(Camera& c, float dx, float dy) {
    float ex, ey, ez;
    camera_eye(c, &ex, &ey, &ez);
    c.yaw -= dx * .006f;
    c.pitch = std::clamp(c.pitch + dy * .005f, -1.50f, 1.50f);
    const float cp = std::cos(c.pitch);
    c.tx = ex - std::sin(c.yaw) * cp * c.dist;
    c.ty = ey - std::sin(c.pitch) * c.dist;
    c.tz = ez - std::cos(c.yaw) * cp * c.dist;
}

void camera_move(Camera& c, float forward, float right, float vertical, float distance) {
    float f[3], r[3], u[3];
    camera_basis(c, f, r, u);
    float delta[3] = {f[0]*forward+r[0]*right, f[1]*forward+r[1]*right+vertical,
                      f[2]*forward+r[2]*right};
    const float length=std::sqrt(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]);
    if(length < 1e-6f) return;
    c.tx += delta[0]*distance/length;
    c.ty += delta[1]*distance/length;
    c.tz += delta[2]*distance/length;
}

void camera_frame(Camera& c, vr::part_geometry::Vec lo, vr::part_geometry::Vec hi, int w, int h) {
    c.tx=(lo.x+hi.x)*.5f; c.ty=(lo.y+hi.y)*.5f; c.tz=(lo.z+hi.z)*.5f;
    const auto span=hi-lo;
    const float radius=std::max(.1f, std::sqrt(vr::part_geometry::dot(span,span))*.5f);
    float fx,fy; camera_fov(w,h,&fx,&fy);
    // A bounding sphere fits at every orientation, including narrow windows.
    c.dist=std::clamp(radius*1.12f/std::sin(std::min(fx,fy)),.25f,300.f);
}

Mat4 camera_vp(const Camera& c, int w, int h) {
    float ex, ey, ez;
    camera_eye(c, &ex, &ey, &ez);
    float fx = 0, fy = 0;
    camera_fov(w, h, &fx, &fy);

    if(c.orthographic) {
        const float half=c.dist*std::tan(fy),aspect=float(w)/std::max(1,h);
        auto p=vr::math::identity();p.m[0]=1/(half*aspect);p.m[5]=1/half;
        p.m[10]=-2/(500.f-.05f);p.m[14]=-(500.f+.05f)/(500.f-.05f);
        return vr::math::multiply(p,vr::math::look_at(ex,ey,ez,c.tx,c.ty,c.tz));
    }

    XrFovf fov{};
    fov.angleLeft = -fx; fov.angleRight = fx;
    fov.angleUp   =  fy; fov.angleDown  = -fy;

    return vr::math::multiply(vr::math::projection(fov, 0.05f, 500.0f),
                              vr::math::look_at(ex, ey, ez, c.tx, c.ty, c.tz));
}

// ── Picking ──────────────────────────────────────────────────────────────────
//
// CELL-BASED, AND IT HAS TO BE. Vertex is {x,y,z,u,v,tile,pal,shade,unit_h} —
// there is no object id anywhere in the mesh, so a triangle hit tells you
// nothing about identity. Putting one there would add a field to all 816k
// vertices and change the geometry; that is a different change and this slice
// does not need it.
//
// So: build the ray, meet the board, floor to a cell, ask the object model.
//
// WHY THERE IS NO MATRIX INVERSE HERE. The textbook unproject inverts the
// view-projection. vr_math.h has no inverse() and it is linked by the GAME — the
// studio should not grow a shared header for its own convenience. It does not
// need to: a perspective ray through a pixel is just the camera basis weighted
// by the tangent of the half-FOV,
//
//     dir = forward + right * tan(fovx) * ndc.x + up * tan(fovy) * ndc.y
//
// built from camera_basis() above, which independently reproduces the convention
// vr::math::look_at() uses for the view matrix.
//
// WHAT THAT DOES AND DOES NOT BUY. --selftest projects a known cell to a pixel
// and picks it back, so it proves the two agree FOR ITS FIXED CAMERA AND CELL.
// A correlated error in both implementations can pass; later divergence that
// affects this scenario should fail, while divergence outside the tested camera
// and cell may remain undetected. It is a useful sampled consistency check, not
// independent visual proof.
//
// WHY THE BOARD IS THE PLANE y = 0, exactly rather than approximately. Measured
// on this map: every one of Route 101's 1190 cells has elevation 0 or 3, and
// height_for() returns 0.0f for both. There is no height field to intersect
// here. A map with real elevation would need per-cell ground heights, and
// height_for's 0.35f step is private to diorama.cpp — a future parameter, not a
// problem this slice has.
//
// AND THE LIMIT, stated because it is visible in use: from an oblique camera
// this picks the GROUND CELL UNDER THE CURSOR, so aiming at a tall building's
// roof lands on the cell behind it. The answer is not more mathematics, it is
// the hover outline drawn every frame — you see the cell you would select before
// you commit to it, so a mis-pick is obvious and one nudge away instead of
// silent. Silent is the failure mode this project keeps paying for.
bool pick_cell(const Camera& c, int win_w, int win_h, float mx, float my,
               int* cell_x, int* cell_y) {
    if (win_w <= 0 || win_h <= 0) return false;

    float ex, ey, ez;
    camera_eye(c, &ex, &ey, &ez);

    float f[3], r[3], u[3];
    camera_basis(c, f, r, u);

    float fovx = 0, fovy = 0;
    camera_fov(win_w, win_h, &fovx, &fovy);

    // Pixel space is y-down; NDC is y-up.
    const float nx = (2.0f * mx / static_cast<float>(win_w)) - 1.0f;
    const float ny = 1.0f - (2.0f * my / static_cast<float>(win_h));
    const float sx = std::tan(fovx) * nx;
    const float sy = std::tan(fovy) * ny;

    float dx = f[0] + r[0] * sx + u[0] * sy;
    float dy = f[1] + r[1] * sx + u[1] * sy;
    float dz = f[2] + r[2] * sx + u[2] * sy;
    const float dl = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dl < 1e-6f) return false;
    dx /= dl; dy /= dl; dz /= dl;

    // Looking up, or along the board: no intersection in front of the eye.
    if (dy > -1e-6f) return false;
    const float t = -ey / dy;
    if (t <= 0.0f) return false;

    // World coordinates ARE cell coordinates: this draws through
    // draw_raw(vp, identity(), debug), and the board's centring translation
    // exists only on the diorama path inside draw().
    *cell_x = static_cast<int>(std::floor(ex + t * dx));
    *cell_y = static_cast<int>(std::floor(ez + t * dz));
    return true;
}

// World point -> window pixel, for the overlay. Mat4 is column-major,
// m[column * 4 + row]. Deliberately local rather than added to vr_math.h.
bool project(const Mat4& vp, float x, float y, float z, int w, int h,
             ImVec2* out) {
    const float cx = vp.m[0] * x + vp.m[4] * y + vp.m[8]  * z + vp.m[12];
    const float cy = vp.m[1] * x + vp.m[5] * y + vp.m[9]  * z + vp.m[13];
    const float cw = vp.m[3] * x + vp.m[7] * y + vp.m[11] * z + vp.m[15];
    if (cw <= 1e-6f) return false;                      // at or behind the eye
    out->x = (cx / cw * 0.5f + 0.5f) * static_cast<float>(w);
    out->y = (1.0f - (cy / cw * 0.5f + 0.5f)) * static_cast<float>(h);
    return true;
}

// WHAT WAS DRAWN, RECORDED WHERE IT WAS DRAWN. The visual probe reports the
// outline coordinates it puts in its sidecar, and a sidecar that recomputed them
// afterwards would be reporting intent rather than observation — it would agree
// with itself even if outline_rect had bailed out and drawn nothing. So the
// polyline is captured at the point AddPolyline receives it, and an early return
// leaves no record, which is exactly the signal the probe needs.
//
// Null in interactive use, so this costs one predictable branch per outline.
struct OutlineRecord {
    const char* role  = "";
    int         x = 0, y = 0, w = 0, h = 0;
    unsigned    col   = 0;
    float       thick = 0.0f;
    ImVec2      p[4];
};
std::vector<OutlineRecord>* g_outline_sink = nullptr;
const char*                 g_outline_role = "";

// A cell rectangle on the board, outlined. Skipped entirely if any corner falls
// behind the eye — a partly-projected quad draws a stripe across the screen.
void outline_rect(ImDrawList* dl, const Mat4& vp, int w, int h,
                  int x, int y, int cw, int ch, ImU32 col, float thick,
                  ImVec2 origin = ImVec2(0, 0)) {
    const float ax = static_cast<float>(x),      az = static_cast<float>(y);
    const float bx = static_cast<float>(x + cw), bz = static_cast<float>(y + ch);
    ImVec2 p[4];
    if (!project(vp, ax, 0.0f, az, w, h, &p[0])) return;
    if (!project(vp, bx, 0.0f, az, w, h, &p[1])) return;
    if (!project(vp, bx, 0.0f, bz, w, h, &p[2])) return;
    if (!project(vp, ax, 0.0f, bz, w, h, &p[3])) return;
    for (auto& point : p) { point.x += origin.x; point.y += origin.y; }
    dl->AddPolyline(p, 4, col, ImDrawFlags_Closed, thick);

    if (g_outline_sink) {
        OutlineRecord r;
        r.role = g_outline_role;
        r.x = x; r.y = y; r.w = cw; r.h = ch;
        r.col = col; r.thick = thick;
        for (int i = 0; i < 4; ++i) r.p[i] = p[i];
        g_outline_sink->push_back(r);
    }
}

// ── State ────────────────────────────────────────────────────────────────────

struct ConnectedWork {
    std::atomic<bool> cancel{false},done{false};std::thread thread;
    studio::connected::Scene scene;
    std::shared_ptr<vr::diorama::PreparedRegion> prepared;
    std::string anchor,error;double milliseconds=0;
    ~ConnectedWork() {cancel=true;if(thread.joinable())thread.join();}
};
struct App {
    // Immutable for the session.
    studio::Decomp decomp;
    Snapshot       snap;

    // Authored state. `working` is what Apply commits into and what Save
    // writes; `draft` is the pattern being edited and is never saved until
    // Apply moves it across.
    studio::Document document;
    OverrideSet& working = document.state.working;
    Pattern& draft = document.state.draft;
    bool& has_draft = document.state.has_draft;
    int& draft_slot = document.state.draft_slot;
    int& origin_x = document.state.origin_x;
    int& origin_y = document.state.origin_y;
    vr::overrides::Claims claims;
    size_t accepted_matches = 0, rejected_matches = 0;
    enum class Action { None, Select, New, Room, Reload, Close, Reauto, Dissolve, Definition, Save, Review };
    Action pending = Action::None;
    int pending_x = -1, pending_y = -1;
    std::string pending_room;
    bool close_ready = false, confirm_dissolve = false;
    bool membership_stroke = false, stroke_erase = false;
    int stroke_x = -1, stroke_y = -1;
    studio::EditorState stroke_before, field_before;
    std::vector<std::string> rooms;
    std::vector<studio::pattern_io::Cell> reset_members;
    bool reset_pending = false;
    uint64_t preview_hash = 0; int preview_vertices = 0;
    std::vector<vr::diorama::AuthoredVertex> pick_mesh;
    std::vector<size_t> pick_parts;
    bool pick_mesh_dirty = true, reveal_selected_part = false;
    Camera room_camera;


    // Derived. Recomputed after every re-mesh.
    ObjectModel                       model;
    std::vector<vr::overrides::Match> draft_matches;
    std::string                       last_stats;

    // SELECTION IS A CELL, NOT AN OBJECT INDEX. A claim withholds cells from
    // the union-find, so applying an override can renumber objects. Holding the
    // cell and re-resolving through object_at is one line and immune to that.
    int& sel_x = document.state.sel_x;
    int& sel_y = document.state.sel_y;
    int sel_obj = -1;

    int edit_roof_rows = -1;       // -1 means "leave this to inference"

    studio::Room2D room, mask_image;
    vr::cutout::Art mask_art;
    studio::Rect mask_canvas;
    int mode = 0; // SEGMENT, MASK, MODEL, DIORAMA
    struct TerrainEditor {
        bool open=false, selecting=false, selected=false;
        int x0=0,y0=0,x1=0,y1=0,pick=0;
        int height=32,step=8,layer=-1,top=-1,side=-1,underlay=-1,thickness=4;
        std::string room;
    } terrain;
    char model_search[128] = {};
    bool models_this_room = true;
    struct Review {
        bool open=false, loaded=false, dirty=true, include_flat=false, include_padding=false;
        int filter=0, selected=-1;
        char map_search[96]{}, search[128]{};
        std::string path="build/coverage/studio/index.json", error, input_fingerprint;
        studio::coverage::Index index;
        studio::coverage::Room room;
        std::vector<size_t> visible;
        OverrideSet baseline;
        studio::coverage::Row target, focused;
        std::string target_map, focused_map;
        int target_width=0,target_height=0;
    } review;
    bool mask_hover = false, mask_stroke = false, mask_erase = true, mask_image_dirty = true;
    struct PixelEditor {
        int tool=0, shape=0, brush=1, face=0; // select / roles / local mask; brush / rectangle / flood
        int start_x=-1,start_y=-1;
        vr::overrides::ArtRegion selection{}, selection_before{};
        std::string group_id;
        bool overlay=true;
        float zoom=1;
        ImVec2 pan{};
        studio::Rect clip;
    } pixels;
    bool neutral=false;
    studio::environment::Phase lighting = studio::environment::Phase::Neutral;
    studio::environment::Sky sky;
    bool sky_ok = true;
    double preview_draw_ms[16]{}; // synchronized samples only in the opt-in probe
    unsigned preview_draw_samples = 0;
    studio::ShellLayout layout;
    std::string& map_id = document.state.map_id;
    std::string tileset_label;
    float pan_x = 0, pan_y = 0;
    int mouse_surface = 0; // 0 chrome, 1 map, 2 production view
    int drag_surface = 0;
    int handle_tool=0; // Move, Size, Rotate. Camera/editor selection state, not persisted.
    struct HandleDrag {
        bool active=false;int axis=0,tool=0;
        ImVec2 press{},screen_axis{};
        vr::part_geometry::Vec origin{},axis_world{},start_ray{};
        float last_angle=0,angle=0;
        studio::EditorState before;
    } handle;
    bool panning = false;
    int list_selection = -1;
    bool show_matches = true, show_grid = true;
    Camera camera;
    bool exploring=false, explore_saved_fly=false, explore_saved_grid=false, explore_saved_terrain=false;
    Camera explore_saved_camera;
    studio::connected::Scene connected;
    std::shared_ptr<ConnectedWork> connected_work;
    std::string connected_anchor,connected_failed;
    size_t stream_updates=0,stream_loaded=0,stream_unloaded=0,stream_reused=0,stream_errors=0,stream_pending_frames=0;
    double stream_build_ms=0,stream_main_ms=0;
    bool fly_mode=false, fly_looking=false;
    float fly_speed=6.f; // map cells / second, Shift multiplies by four
    bool   debug = false;          // false textured, true classification colours

    std::string in_path, out_path;
    std::string status;            // one line, shown in the Scene panel
};

const char* class_word(ObjectClass c) {
    switch (c) {
        case ObjectClass::kProp:      return "prop";
        case ObjectClass::kStructure: return "structure";
        case ObjectClass::kMass:      return "mass";
    }
    return "?";
}

int find_same(const OverrideSet& set, const Pattern& p) {
    for (size_t i = 0; i < set.patterns.size(); ++i)
        if (studio::pattern_io::same_key(set.patterns[i], p)) return int(i);
    return -1;
}

bool populated(const Pattern& p) { return p.w > 0 && p.extent > 0 && p.anchor >= 0; }

// working + the draft spliced in. This is what the mesher is handed, and what
// segment() must be handed with it.
OverrideSet effective(const App& a) {
    OverrideSet set = a.working;
    set.version = a.working.version;
    if (a.has_draft && (a.draft_slot >= 0 || a.document.draft_dirty())) {
        if(a.draft.voxel) set.version=std::max(set.version,vr::overrides::kVoxelVersion);
        if (a.draft_slot >= 0 && a.draft_slot < int(set.patterns.size())) {
            if (populated(a.draft)) set.patterns[size_t(a.draft_slot)] = a.draft;
            else set.patterns.erase(set.patterns.begin() + a.draft_slot);
        } else if (populated(a.draft)) set.patterns.push_back(a.draft);
    }
    return set;
}

struct UpdateJob { const Snapshot* s = nullptr; };

void run_update(void* ctx) {
    vr::diorama::update(*static_cast<UpdateJob*>(ctx)->s);
}

std::string last_stats_line(const std::string& log) {
    std::string found;
    size_t at = 0;
    while (at < log.size()) {
        size_t end = log.find('\n', at);
        if (end == std::string::npos) end = log.size();
        if (log.compare(at, 8, "[stats] ") == 0) {
            found = log.substr(at + 8, end - at - 8);
            while (!found.empty() && found.back() == '\r') found.pop_back();
        }
        at = end + 1;
    }
    return found;
}

std::string stats_value(const std::string& line, const char* key) {
    const std::string needle = std::string(key) + "=";
    const size_t at = line.find(needle);
    if (at == std::string::npos) return {};
    const size_t first = at + needle.size();
    const size_t last = line.find(' ', first);
    return line.substr(first, last == std::string::npos ? last : last - first);
}

void refresh_mask_image(App& a) {
    a.mask_art = {};
    if (!a.has_draft || !populated(a.draft) || !vr::cutout::compose(a.snap, a.draft, &a.mask_art)) {
        a.mask_image.w = a.mask_image.h = 0; a.mask_image.rgba.clear(); return;
    }
    a.mask_image.w = a.mask_art.w; a.mask_image.h = a.mask_art.h;
    a.mask_image.rgba.resize(a.mask_art.pixels.size());
    for (size_t i = 0; i < a.mask_art.pixels.size(); ++i)
        a.mask_image.rgba[i] = !a.draft.voxel && a.draft.cutout && !a.draft.cutout->opacity[i] ? 0 : a.mask_art.pixels[i].rgba;
    a.mask_image_dirty = true;
}

void refresh_matches(App& a) {
    refresh_mask_image(a);
    a.draft_matches.clear();
    a.accepted_matches = a.rejected_matches = 0;
    if (!a.has_draft || !populated(a.draft)) return;
    OverrideSet one;
    one.version = a.draft.voxel ? vr::overrides::kVoxelVersion : a.working.version;
    one.patterns.push_back(a.draft);
    a.draft_matches = vr::overrides::find(a.snap, one);
    OverrideSet preview = a.working;
    const int slot = a.draft_slot >= 0 ? a.draft_slot : int(preview.patterns.size());
    if (a.draft_slot >= 0) preview.patterns[size_t(slot)] = a.draft;
    else preview.patterns.push_back(a.draft);
    const auto resolved = vr::overrides::resolve(a.snap, preview);
    for (const auto& m : resolved.accepted) if (m.pattern == slot) ++a.accepted_matches;
    for (const auto& m : resolved.rejected) if (m.pattern == slot) ++a.rejected_matches;
}

bool source_resolution_needed(const App& a) {
    if (!a.has_draft || a.draft_slot < 0) return false;
    if (a.sel_x < 0) return true;
    if (a.draft.source.room.empty()) return true;
    // A source in another room has not been checked by this snapshot. A
    // definition-list selection leaves no active cell if that room cannot load.
    if (a.draft.source.room != a.map_id) return a.sel_x < 0;
    return std::none_of(a.draft_matches.begin(), a.draft_matches.end(), [&](const auto& m) {
        return m.x == a.draft.source.x && m.y == a.draft.source.y;
    });
}

// THE ONE EXPENSIVE OPERATION, ~105 ms on this map. Every caller is a
// deliberate act by the author; nothing calls this per frame.
//
// Two lines here are contracts rather than conveniences. set_overrides is what
// forces the rebuild — it zeroes g_meshed_layout exactly as set_min_unit does.
// And segment() must be handed THE SAME SET the mesher has, because it is a
// pure function of its arguments rather than a reader of mesher state; hand it
// a different set and the two describe different worlds.
void remesh(App& a) {
    a.pick_mesh_dirty=true;
    vr::diorama::set_build_mode(a.mode==3?vr::diorama::BuildMode::Diorama:vr::diorama::BuildMode::Inferred);
    const OverrideSet set = effective(a);
    vr::diorama::set_overrides(set);

    UpdateJob job{&a.snap};
    // The isolated voxel editor needs only its model. Rebuild the room when
    // returning to a room mode, avoiding a full inferred map for each nudge.
    const std::string log = a.mode==2 && a.has_draft && a.draft.voxel ? "" : studio::capture_stderr(run_update, &job);
    if(!log.empty()) a.last_stats = last_stats_line(log);

    // The console keeps the full record — the [override] match lines and the
    // [diorama] mesh line are what a person reads when a number looks wrong.
    std::fputs(log.c_str(), stderr);

    vr::diorama::segment(a.snap, set, &a.model);
    a.sel_obj = a.model.object_at(a.sel_x, a.sel_y);
    a.claims = vr::overrides::resolve(a.snap, a.working);
    refresh_matches(a);
    if (a.mode == 2) {
        if (!a.has_draft || (!a.draft.cutout && a.draft.parts.empty()) ||
            !vr::diorama::build_cutout_preview(a.snap, a.draft, &a.preview_hash, &a.preview_vertices)) {
            a.mode = 0; a.camera = a.room_camera;
            a.status = "No cutout compatible with this room is selected.";
        }
    }
}

void select_cell(App& a, int x, int y) {
    const int k = a.model.object_at(x, y);
    if (k < 0) {
        a.sel_x = a.sel_y = -1;
        a.sel_obj = -1;
        a.has_draft = false;
        a.draft_slot = -1;
        a.draft_matches.clear();
        a.status = "nothing stands there";
        return;
    }
    a.sel_x = x; a.sel_y = y; a.sel_obj = k;

    const Object& o = a.model.objects[static_cast<size_t>(k)];
    char nm[64];
    std::snprintf(nm, sizeof(nm), "%s-%d-%d", class_word(o.cls), o.x, o.y);

    Pattern p;
    if (!studio::pattern_io::from_object(a.snap, a.model, x, y, nm, &p)) {
        a.has_draft = false;
        a.draft_slot = -1;
        a.status = "could not derive a pattern for that object";
        return;
    }

    // Already authored? Then EDIT that pattern rather than stacking a second
    // one that matches the same cells — two patterns claiming one place is the
    // overlap case overrides.h resolves by origin, and an author who did it by
    // accident would never see why.
    a.draft_slot = find_same(a.working, p);
    if (a.draft_slot >= 0) {
        // Preserve the entire authored definition, including stable identity
        // and source provenance; reconstructing only apply would drop metadata.
        p = a.working.patterns[static_cast<size_t>(a.draft_slot)];
    }

    a.draft = p;
    a.document.state.draft_base = p;
    a.origin_x = o.x; a.origin_y = o.y;
    a.document.state.base_origin_x = o.x; a.document.state.base_origin_y = o.y;
    a.has_draft = true;
    a.edit_roof_rows = p.apply.roof_rows;
    refresh_matches(a);
    a.status.clear();
}

void select_object(App& a, int index) {
    if (index < 0 || index >= int(a.model.objects.size())) return;
    const Object& object = a.model.objects[size_t(index)];
    // A component's bounding-box corner need not itself belong to the component.
    for (int y = object.y; y < object.y + object.extent; ++y)
        for (int x = object.x; x < object.x + object.w; ++x)
            if (a.model.object_at(x, y) == index) { select_cell(a, x, y); return; }
}

// These are UI actions, not UI widgets. Keeping the mutations here means the
// interactive controls and --selftest exercise one path; otherwise a green
// automation mode could drift away from the buttons it claims to verify.
void preview_roof_rows(App& a, int roof_rows) {
    a.edit_roof_rows = std::max(-1, std::min(64, roof_rows));
    if (!a.has_draft || a.draft.apply.roof_rows == a.edit_roof_rows) return;
    const auto before = a.document.state;
    a.draft.apply.roof_rows = a.edit_roof_rows;
    if (a.draft.id.empty()) a.draft.id = a.document.new_id();
    a.document.record(before);
    remesh(a);
}

bool initialize_cutout(App& a) {
    if (!a.has_draft || !populated(a.draft)) { a.status = "Select a nonempty group first."; return false; }
    if (a.draft.cutout) return true;
    vr::cutout::Art art;
    if (!vr::cutout::compose(a.snap, a.draft, &art)) { a.status = "Could not compose source art."; return false; }
    const auto before = a.document.state;
    a.draft.cutout = vr::cutout::original_opacity(art);
    if (a.draft.id.empty()) a.draft.id = a.document.new_id();
    a.document.record(before); remesh(a);
    return true;
}

void remove_cutout(App& a) {
    if (!a.has_draft || !a.draft.cutout) return;
    for (const auto& part:a.draft.parts) if(part.kind==vr::overrides::PartKind::Billboard) {
        a.status="Delete billboard parts before removing their cutout."; return;
    }
    const auto before = a.document.state;
    a.draft.cutout.reset(); a.document.record(before); remesh(a);
}

bool apply_draft(App& a) {
    if (!a.has_draft || !populated(a.draft)) {
        a.status = "Add at least one cell before applying."; return false;
    }
    if (source_resolution_needed(a)) {
        a.status = "Stored source unresolved. Choose an accepted source placement in the inspector first."; return false;
    }
    const int duplicate = find_same(a.working, a.draft);
    if (duplicate >= 0 && duplicate != a.draft_slot) {
        a.status = "This pattern already has a definition. Select it in the list."; return false;
    }
    OverrideSet check = a.working;
    if(a.draft.voxel) check.version=std::max(check.version,vr::overrides::kVoxelVersion);
    if(!vr::overrides::valid_parts(a.draft)) {a.status="This model has an invalid mask, surface or size. Undo the last edit before applying.";return false;}
    const int slot = a.draft_slot >= 0 ? a.draft_slot : int(check.patterns.size());
    if (a.draft_slot >= 0) check.patterns[size_t(slot)] = a.draft;
    else check.patterns.push_back(a.draft);
    bool source_accepted = false;
    for (const auto& m : vr::overrides::resolve(a.snap, check).accepted)
        if (m.pattern == slot && m.x == a.origin_x && m.y == a.origin_y) source_accepted = true;
    if (!source_accepted) {
        a.status = "The selected instance overlaps an earlier claim or no longer matches."; return false;
    }
    const auto before = a.document.state;
    if(a.draft.voxel) a.working.version=std::max(a.working.version,vr::overrides::kVoxelVersion);
    if (a.draft.id.empty()) a.draft.id = a.document.new_id();
    if (a.draft.source.room.empty()) a.draft.source = {a.map_id, a.origin_x, a.origin_y};
    if (a.draft_slot >= 0) a.working.patterns[size_t(a.draft_slot)] = a.draft;
    else { a.working.patterns.push_back(a.draft); a.draft_slot = int(a.working.patterns.size()) - 1; }
    a.document.state.draft_base = a.draft;
    a.document.state.base_origin_x = a.origin_x; a.document.state.base_origin_y = a.origin_y;
    a.document.record(before);
    remesh(a);
    a.status = "Applied. Save writes the working definitions.";
    return true;
}

void clear_selection(App& a) {
    a.document.state.selected_part.clear();
    a.has_draft = false; a.draft_slot = -1; a.sel_x = a.sel_y = a.sel_obj = -1;
    a.draft = {}; a.document.state.draft_base = {};
    a.draft_matches.clear(); a.accepted_matches = a.rejected_matches = 0;
}

void revert_draft(App& a) {
    const auto before = a.document.state;
    if (a.draft_slot >= 0) {
        a.draft = a.working.patterns[size_t(a.draft_slot)];
        a.document.state.draft_base = a.draft;
        a.origin_x = a.document.state.base_origin_x;
        a.origin_y = a.document.state.base_origin_y;
        a.sel_x = a.origin_x + a.draft.anchor % a.draft.w;
        a.sel_y = a.origin_y + a.draft.anchor / a.draft.w;
    } else clear_selection(a);
    a.edit_roof_rows = a.draft.apply.roof_rows;
    a.document.record(before); remesh(a);
    a.status = "Draft reverted.";
}

void new_group(App& a) {
    const auto before = a.document.state;
    clear_selection(a);
    a.has_draft = true;
    a.draft.id = a.document.new_id();
    a.draft.name = "New group";
    a.draft.source.room = a.map_id;
    a.document.state.draft_base = a.draft;
    a.document.record(before); remesh(a);
    a.status = "Paint cells in the map. Shift removes cells; Escape changes selection.";
}

bool edit_member(App& a, int x, int y, bool erase) {
    if (!a.has_draft || x < 0 || y < 0 || x >= a.snap.width || y >= a.snap.height) return false;
    if (source_resolution_needed(a)) {
        a.status = "Stored source unresolved. Reselect a source in the inspector before editing member cells."; return false;
    }
    if (!erase && !a.claims.owner.empty()) {
        const int owner = a.claims.owner[size_t(y) * a.snap.width + x];
        if (owner >= 0 && owner != a.draft_slot) {
            a.status = "That cell belongs to another applied group."; return false;
        }
    }
    std::vector<studio::pattern_io::Cell> cells;
    bool present = false;
    for (int k = 0; k < a.draft.cells(); ++k) if (a.draft.mask[size_t(k)]) {
        const int cx = a.origin_x + k % a.draft.w, cy = a.origin_y + k / a.draft.w;
        if (cx == x && cy == y) { present = true; if (erase) continue; }
        cells.push_back({cx, cy});
    }
    if (present != erase) return false; // adding a member or removing a nonmember is a no-op
    if (!erase) cells.push_back({x, y});
    Pattern p;
    if (!cells.empty() && !studio::pattern_io::from_cells(a.snap, cells, a.draft.name.c_str(), &p)) {
        a.status = "Cell unavailable or group exceeds 64 x 64 cells."; return false;
    }
    if (a.draft.cutout || !a.draft.parts.empty()) {
        if (a.membership_stroke) a.reset_members.push_back({x,y});
        a.status = "Changing membership requires resetting the cutout and parts."; return false;
    }
    p.id = a.draft.id.empty() ? a.document.new_id() : a.draft.id;
    p.name = a.draft.name; p.apply = a.draft.apply;
    p.source.room = a.map_id;
    a.origin_x = p.source.x; a.origin_y = p.source.y;
    a.sel_x = cells.empty() ? -1 : p.source.x + p.anchor % p.w;
    a.sel_y = cells.empty() ? -1 : p.source.y + p.anchor / p.w;
    a.draft = std::move(p); a.status.clear();
    return true;
}

bool mask_pixel(App& a, int x, int y) {
    if(a.draft.voxel) return studio::voxel_edit::paint(a.draft,a.mask_art,a.document.state.selected_part,a.pixels.tool,x,y);
    if (x < 0 || y < 0 || x >= a.mask_art.w || y >= a.mask_art.h) return false;
    const size_t i = size_t(y) * a.mask_art.w + x;
    const uint8_t source = a.mask_art.pixels[i].rgba >> 24 ? 1 : 0;
    const uint8_t previous = a.draft.cutout ? a.draft.cutout->opacity[i] : source;
    const uint8_t next = a.mask_erase ? 0 : source;
    if (previous == next) return false;
    if (!a.draft.cutout) a.draft.cutout = vr::cutout::original_opacity(a.mask_art);
    if (a.draft.id.empty()) a.draft.id = a.document.new_id();
    a.draft.cutout->opacity[i] = next;
    a.mask_image.rgba[i] = next ? a.mask_art.pixels[i].rgba : 0;
    a.mask_image_dirty = true; a.status.clear();
    return true;
}

bool canvas_pixel(const App& a, float mx, float my, int* x, int* y) {
    if(a.draft.voxel && !a.pixels.clip.contains(mx,my)) return false;
    if (!a.mask_canvas.contains(mx,my) || a.mask_art.w <= 0 || a.mask_art.h <= 0) return false;
    *x = int((mx-a.mask_canvas.x) * a.mask_art.w / a.mask_canvas.w);
    *y = int((my-a.mask_canvas.y) * a.mask_art.h / a.mask_canvas.h);
    return *x >= 0 && *y >= 0 && *x < a.mask_art.w && *y < a.mask_art.h;
}

void stroke_to(App& a, int x, int y) {
    if(a.mask_stroke && a.draft.voxel) {
        auto& e=a.pixels;
        if(e.start_x<0) {
            e.start_x=x;e.start_y=y;e.selection_before=e.selection;
            if(e.shape==2 && e.tool) studio::voxel_edit::flood(a.draft,a.mask_art,a.document.state.selected_part,e.tool,x,y);
        }
        if(!e.tool || e.shape==1) {
            e.selection={std::min(e.start_x,x),std::min(e.start_y,y),std::abs(x-e.start_x)+1,std::abs(y-e.start_y)+1};
            return;
        }
        if(e.shape==2) return;
    }
    if (a.stroke_x < 0) { a.stroke_x = x; a.stroke_y = y; }
    int cx = a.stroke_x, cy = a.stroke_y;
    const int dx = std::abs(x-cx), sx = cx<x ? 1 : -1;
    const int dy = -std::abs(y-cy), sy = cy<y ? 1 : -1;
    int error = dx+dy;
    for (;;) {
        if (a.mask_stroke) {
            const int radius=a.draft.voxel?a.pixels.brush/2:0;
            for(int yy=cy-radius;yy<=cy+radius;++yy) for(int xx=cx-radius;xx<=cx+radius;++xx) mask_pixel(a,xx,yy);
        }
        else edit_member(a, cx, cy, a.stroke_erase);
        if (cx == x && cy == y) break;
        const int e = 2*error;
        if (e >= dy) { error += dy; cx += sx; }
        if (e <= dx) { error += dx; cy += sy; }
    }
    a.stroke_x = x; a.stroke_y = y;
}

void end_stroke(App& a, bool cancel) {
    if (!a.membership_stroke && !a.mask_stroke) return;
    const bool voxel=a.mask_stroke && a.draft.voxel;
    bool refused=false;
    if(voxel) {
        auto& e=a.pixels;
        if(!cancel && e.start_x>=0 && e.tool && e.shape==1) {
            const auto r=e.selection;
            for(int y=r[1];y<r[1]+r[3];++y) for(int x=r[0];x<r[0]+r[2];++x) mask_pixel(a,x,y);
        }
        if(!cancel && !vr::overrides::valid_parts(a.draft)) {cancel=true;refused=true;}
        if(cancel) e.selection=e.selection_before;
        e.start_x=e.start_y=-1;
    }
    if (cancel) { a.document.state = a.stroke_before; a.reset_members.clear(); }
    if (!cancel && !a.reset_members.empty()) a.reset_pending = true;
    if (!cancel) a.document.record(a.stroke_before);
    a.membership_stroke = a.mask_stroke = false; a.stroke_x = a.stroke_y = -1;
    remesh(a);
    if(refused) a.status="Those pixels supply a solid face. Assign different source art to that face before marking them as ground or shadow.";
}

bool reset_for_membership(App& a) {
    const auto before = a.document.state;
    a.draft.cutout.reset();
    a.draft.parts.clear(); a.draft.voxel.reset(); a.draft.model_seeded=false; a.document.state.selected_part.clear();
    for (const auto& cell : a.reset_members) {
        a.status.clear(); edit_member(a, cell.x, cell.y, a.stroke_erase);
        if (!a.status.empty()) { a.document.state = before; remesh(a); return false; }
    }
    a.document.record(before); a.reset_members.clear(); a.reset_pending = false; remesh(a); return true;
}

bool add_part(App& a, vr::overrides::PartKind kind, bool duplicate=false) {
    if (!a.has_draft || !populated(a.draft) || a.draft.parts.size()>=64) return false;
    if (kind==vr::overrides::PartKind::Billboard && !a.draft.cutout) return false;
    if (kind!=vr::overrides::PartKind::Billboard && std::none_of(a.mask_art.pixels.begin(),a.mask_art.pixels.end(),
        [](const auto& p){return p.rgba>>24;})) { a.status="The source has no opaque box material."; return false; }
    const auto before=a.document.state;
    vr::overrides::Part part;
    part.kind=kind; part.name=kind==vr::overrides::PartKind::Box?"Box":kind==vr::overrides::PartKind::Wedge?"Wedge":"Billboard";
    if(kind==vr::overrides::PartKind::Billboard) part.transform.size={float(a.draft.w),float(a.draft.extent),.125f};
    if(duplicate) {
        const auto selected=std::find_if(a.draft.parts.begin(),a.draft.parts.end(),[&](const auto& p){return p.id==a.document.state.selected_part;});
        if(selected==a.draft.parts.end()) return false;
        part=*selected;
        if(part.name.size()<=251) part.name+=" copy";
    }
    do { part.id="part-"+a.document.new_id(); }
    while(std::any_of(a.draft.parts.begin(),a.draft.parts.end(),[&](const auto& p){return p.id==part.id;}));
    if(a.draft.id.empty()) a.draft.id=a.document.new_id();
    a.document.state.selected_part=part.id; a.draft.parts.push_back(std::move(part));
    if(!vr::overrides::valid_parts(a.draft)) {
        a.document.state=before; a.status="Model exceeds the source-pixel face budget. Use a smaller group or fewer parts."; return false;
    }
    a.draft.model_seeded=true; a.document.record(before); remesh(a); return true;
}

void delete_part(App& a) {
    const auto before=a.document.state;
    auto& parts=a.draft.parts;
    const auto selected=std::find_if(parts.begin(),parts.end(),[&](const auto& p){return p.id==a.document.state.selected_part;});
    if(selected==parts.end()) return;
    parts.erase(selected); a.draft.model_seeded=true;
    a.document.state.selected_part=parts.empty()?"":parts.front().id;
    a.document.record(before); remesh(a);
}

namespace pg = vr::part_geometry;
vr::overrides::Part* selected_part(App& a) {
    for(auto& part:a.draft.parts) if(part.id==a.document.state.selected_part) return &part;
    return nullptr;
}

void stop_fly_look(App& a) {
    if(!a.fly_looking) return;
    a.fly_looking=false;
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_CaptureMouse(SDL_FALSE);
}

bool camera_input_allowed(const App& a, bool typing) {
    return !typing && !a.review.open && a.pending==App::Action::None && !a.reset_pending &&
        !a.handle.active && !a.membership_stroke && !a.mask_stroke && !a.drag_surface && !a.panning;
}

void focus_room(App& a) {
    stop_fly_look(a);
    a.camera.tx=a.snap.width*.5f; a.camera.ty=0; a.camera.tz=a.snap.height*.5f;
    a.camera.yaw=.6f; a.camera.pitch=.9f; a.camera.dist=std::max(a.snap.width,a.snap.height)*1.1f+6;
    a.status="Room framed. Wheel or Zoom -/+ adjusts the distance.";
}

bool focus_selection(App& a) {
    if(!a.has_draft || !populated(a.draft)) { a.status="Select a group to focus its instance."; return false; }
    pg::Vec lo{0,0,-.0625f}, hi{float(a.draft.w),float(a.draft.extent),.0625f};
    std::vector<vr::diorama::AuthoredVertex> vertices;
    const bool authored=a.draft.cutout || !a.draft.parts.empty();
    if(authored && vr::diorama::inspect_authored_mesh(a.snap,a.draft,&vertices) && !vertices.empty()) {
        lo=hi=vertices.front().position;
        for(const auto& v:vertices) {
            lo.x=std::min(lo.x,v.position.x);lo.y=std::min(lo.y,v.position.y);lo.z=std::min(lo.z,v.position.z);
            hi.x=std::max(hi.x,v.position.x);hi.y=std::max(hi.y,v.position.y);hi.z=std::max(hi.z,v.position.z);
        }
    }
    if(a.mode!=2) {
        // An unresolved source has no selected room instance to frame.
        if(source_resolution_needed(a)) { a.status="Resolve the selected source placement before focusing its room instance."; return false; }
        if(authored) {
            const pg::Vec placement{float(a.origin_x),a.mode==3?.002f:0.f,float(a.origin_y+a.draft.extent)-.5f};
            lo=lo+placement;hi=hi+placement;
        } else {
            lo={float(a.origin_x),0,float(a.origin_y)};
            hi={float(a.origin_x+a.draft.w),a.mode==3?.002f:float(a.draft.extent),float(a.origin_y+a.draft.extent)};
        }
    }
    stop_fly_look(a);
    a.camera.yaw=.4f; a.camera.pitch=.3f;
    camera_frame(a.camera,lo,hi,int(a.layout.view.w),int(a.layout.view.h));
    a.status=a.mode==2?"Model framed. Wheel or Zoom -/+ adjusts the distance.":"Selected instance framed. Room restores the whole map.";
    return true;
}
pg::Vec handle_axis(const pg::Transform& t,int axis,int tool) {
    const pg::Vec v=axis==0?pg::Vec{1,0,0}:axis==1?pg::Vec{0,1,0}:pg::Vec{0,0,1};
    if(tool!=2) return pg::rotate(v,t.angles);
    if(axis==0) return pg::rotate(v,{0,t.angles.y,t.angles.z});
    if(axis==1) return pg::rotate(v,{0,0,t.angles.z});
    return v;
}
bool handle_screen(const App& a,pg::Vec p,ImVec2* out) {
    const auto& v=a.layout.view;
    if(!project(camera_vp(a.camera,int(v.w),int(v.h)),p.x,p.y,p.z,int(v.w),int(v.h),out)) return false;
    out->x+=v.x;out->y+=v.y;return true;
}
void model_ray(const App& a,float mx,float my,pg::Vec& eye,pg::Vec& ray) {
    float ex,ey,ez,f[3],r[3],u[3],fx,fy;
    camera_eye(a.camera,&ex,&ey,&ez);camera_basis(a.camera,f,r,u);
    const auto& v=a.layout.view;camera_fov(int(v.w),int(v.h),&fx,&fy);
    const float sx=std::tan(fx)*(2*(mx-v.x)/v.w-1),sy=std::tan(fy)*(1-2*(my-v.y)/v.h);
    eye={ex,ey,ez};ray={f[0]+r[0]*sx+u[0]*sy,f[1]+r[1]*sx+u[1]*sy,f[2]+r[2]*sx+u[2]*sy};
    if(a.camera.orthographic) {
        eye=eye+pg::Vec{r[0]*sx+u[0]*sy,r[1]*sx+u[1]*sy,r[2]*sx+u[2]*sy}*a.camera.dist;
        ray={f[0],f[1],f[2]};
    }
}
bool select_model_part(App& a,float mx,float my) {
    if(a.mode!=2 || !a.has_draft || a.draft.parts.empty()) return false;
    if(a.pick_mesh_dirty) {
        a.pick_mesh.clear();a.pick_parts.clear();
        if(!vr::diorama::inspect_authored_mesh(a.snap,a.draft,&a.pick_mesh,&a.pick_parts)) {
            a.status="Could not inspect this model for selection.";return false;
        }
        a.pick_mesh_dirty=false;
    }
    pg::Vec eye,ray;model_ray(a,mx,my,eye,ray);
    float nearest=500;size_t owner=size_t(-1);
    for(size_t i=0;i<a.pick_parts.size();++i) {
        const auto& a0=a.pick_mesh[i*3];const auto& b=a.pick_mesh[i*3+1];const auto& c=a.pick_mesh[i*3+2];
        const auto e1=b.position-a0.position,e2=c.position-a0.position;
        const auto p=pg::cross(ray,e2);const float det=pg::dot(e1,p);
        if(det>=-1e-8f) continue; // Same clockwise front faces as the renderer.
        const auto offset=eye-a0.position;const float u=pg::dot(offset,p)/det;
        if(u<0 || u>1) continue;
        const auto q=pg::cross(offset,e1);const float v=pg::dot(ray,q)/det;
        if(v<0 || u+v>1) continue;
        const float distance=pg::dot(e2,q)/det;
        if(distance<.05f || distance>=nearest) continue;
        const float tu=a0.u+(b.u-a0.u)*u+(c.u-a0.u)*v,tv=a0.v+(b.v-a0.v)*u+(c.v-a0.v)*v;
        const int x=std::clamp(int(tu*8),0,7),y=std::clamp(int(tv*8),0,7);
        const size_t address=size_t(a0.tile)*32+y*4+x/2;
        if(!a.neutral && (address>=a.snap.vram_tiles.size() ||
           ((a.snap.vram_tiles[address]>>((x&1)*4))&15)==0)) continue;
        nearest=distance;owner=a.pick_parts[i];
    }
    if(owner>=a.draft.parts.size()) return false;
    const auto& part=a.draft.parts[owner];
    a.document.state.selected_part=part.id;a.reveal_selected_part=true;
    a.status="Selected "+(part.name.empty()?part.id:part.name)+". Drag a colored handle to edit; Shift+F focuses this part.";
    return true;
}
bool focus_part(App& a) {
    auto* part=selected_part(a);if(a.mode!=2 || !part) return false;
    const auto& t=part->transform;
    pg::Vec lo=t.position,hi=lo;
    for(int i=0;i<8;++i) {
        const auto p=pg::to_group({(i&1)?t.size.x:0,(i&2)?t.size.y:0,(i&4)?t.size.z/2:-t.size.z/2},t);
        lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
        hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
    }
    stop_fly_look(a);
    camera_frame(a.camera,lo,hi,int(a.layout.view.w),int(a.layout.view.h));
    a.status="Selected part framed. F restores the whole model.";
    return true;
}
bool handle_ray(const App& a,float mx,float my,pg::Vec origin,pg::Vec normal,pg::Vec* out) {
    pg::Vec eye,ray;model_ray(a,mx,my,eye,ray);
    const float denominator=pg::dot(normal,ray);
    if(std::abs(denominator)<1e-5f) return false;
    const float distance=pg::dot(normal,origin-eye)/denominator;
    if(distance<=0) return false;
    *out=eye+ray*distance-origin;
    return pg::dot(*out,*out)>1e-8f;
}
struct HandleSegment { ImVec2 a,b;int axis; };
std::vector<HandleSegment> handle_segments(App& a) {
    auto* part=selected_part(a);if(a.mode!=2 || !part) return {};
    const auto& t=part->transform;
    const float length=std::clamp(std::max({t.size.x,t.size.y,t.size.z})*.3f,.75f,2.f);
    std::vector<HandleSegment> result;
    for(int axis=0;axis<3;++axis) {
        if(a.draft.voxel && part->kind==vr::overrides::PartKind::Billboard && a.handle_tool==1 && axis!=2) continue;
        const auto n=handle_axis(t,axis,a.handle_tool);
        if(a.handle_tool!=2) {
            ImVec2 start,end;
            if(handle_screen(a,t.position,&start) && handle_screen(a,t.position+n*length,&end))
                result.push_back({start,end,axis});
        } else {
            auto r=pg::cross(n,std::abs(n.y)<.9f?pg::Vec{0,1,0}:pg::Vec{1,0,0});r=r*(1/std::sqrt(pg::dot(r,r)));
            const auto u=pg::cross(n,r);
            for(int k=0;k<64;++k) {
                const float aa=k*6.28318530718f/64,bb=(k+1)*6.28318530718f/64;
                ImVec2 start,end;
                if(handle_screen(a,t.position+(r*std::cos(aa)+u*std::sin(aa))*length,&start) &&
                   handle_screen(a,t.position+(r*std::cos(bb)+u*std::sin(bb))*length,&end)) result.push_back({start,end,axis});
            }
        }
    }
    return result;
}
bool begin_handle(App& a,float mx,float my) {
    auto* part=selected_part(a);if(a.mode!=2 || !part) return false;
    int axis=-1;float nearest=64;
    for(const auto& segment:handle_segments(a)) {
        const float dx=segment.b.x-segment.a.x,dy=segment.b.y-segment.a.y,length=dx*dx+dy*dy;
        if(length<.0001f) continue;
        const float t=std::clamp(((mx-segment.a.x)*dx+(my-segment.a.y)*dy)/length,0.f,1.f);
        const float x=segment.a.x+t*dx-mx,y=segment.a.y+t*dy-my,distance=x*x+y*y;
        if(distance<nearest) { nearest=distance;axis=segment.axis; }
    }
    if(axis<0) return false;
    App::HandleDrag drag;drag.axis=axis;drag.tool=a.handle_tool;drag.press={mx,my};drag.before=a.document.state;
    drag.origin=part->transform.position;drag.axis_world=handle_axis(part->transform,axis,a.handle_tool);
    ImVec2 origin,end;
    if(!handle_screen(a,drag.origin,&origin) || !handle_screen(a,drag.origin+drag.axis_world,&end)) return false;
    drag.screen_axis={end.x-origin.x,end.y-origin.y};
    if(drag.tool==2) {
        if(!handle_ray(a,mx,my,drag.origin,drag.axis_world,&drag.start_ray)) return false;
    } else if(drag.screen_axis.x*drag.screen_axis.x+drag.screen_axis.y*drag.screen_axis.y<16) return false;
    drag.active=true;a.handle=std::move(drag);SDL_CaptureMouse(SDL_TRUE);
    a.status="Drag handle / Escape cancels. Guide updates now; geometry rebuilds on release.";return true;
}
void update_handle(App& a,float mx,float my) {
    auto& drag=a.handle;if(!drag.active) return;
    auto* part=selected_part(a);if(!part) return;
    const auto it=std::find_if(drag.before.draft.parts.begin(),drag.before.draft.parts.end(),[&](const auto& p){return p.id==part->id;});
    if(it==drag.before.draft.parts.end()) return;
    auto t=it->transform;
    auto precision=[&](float value) {
        const float scale=a.draft.voxel && drag.tool!=2?16.f:10000.f;
        return std::round(value*scale)/scale;
    };
    if(drag.tool==2) {
        pg::Vec ray;if(!handle_ray(a,mx,my,drag.origin,drag.axis_world,&ray)) return;
        const float angle=std::atan2(pg::dot(drag.axis_world,pg::cross(drag.start_ray,ray)),pg::dot(drag.start_ray,ray));
        float delta=angle-drag.last_angle;
        if(delta>3.14159265f) delta-=6.28318530718f;
        if(delta< -3.14159265f) delta+=6.28318530718f;
        drag.angle+=delta;drag.last_angle=angle;
        float* component=drag.axis==0?&t.angles.x:drag.axis==1?&t.angles.y:&t.angles.z;
        *component=std::clamp(precision(*component+drag.angle*57.295779513f),-360.f,360.f);
    } else {
        const float dx=drag.screen_axis.x,dy=drag.screen_axis.y;
        const float amount=((mx-drag.press.x)*dx+(my-drag.press.y)*dy)/(dx*dx+dy*dy);
        if(drag.tool==0) {
            t.position=t.position+drag.axis_world*amount;
            if(drag.axis_world.x!=0) t.position.x=precision(t.position.x);
            if(drag.axis_world.y!=0) t.position.y=precision(t.position.y);
            if(drag.axis_world.z!=0) t.position.z=precision(t.position.z);
            t.position.x=std::clamp(t.position.x,-128.f,128.f);t.position.y=std::clamp(t.position.y,-128.f,128.f);t.position.z=std::clamp(t.position.z,-128.f,128.f);
        } else {
            float* component=drag.axis==0?&t.size.x:drag.axis==1?&t.size.y:&t.size.z;
            *component=std::clamp(precision(*component+amount),1.f/16,64.f);
        }
    }
    if(pg::valid(t)) part->transform=t;
}
void end_handle(App& a,bool cancel) {
    if(!a.handle.active) return;
    const auto before=a.handle.before;a.handle.active=false;SDL_CaptureMouse(SDL_FALSE);
    if(!cancel && a.draft.voxel && !vr::overrides::valid_parts(a.draft)) {cancel=true;a.status="That transform exceeds the model size limit.";}
    if(cancel) { a.document.state=before;a.status="Handle drag cancelled."; }
    else { a.document.record(before);remesh(a);a.status="Handle transform applied."; }
}
void draw_handles(App& a,ImDrawList* dl) {
    auto* part=selected_part(a);if(a.mode!=2 || !part) return;
    const auto& v=a.layout.view;dl->PushClipRect(ImVec2(v.x,v.y),ImVec2(v.x+v.w,v.y+v.h),true);
    const ImU32 colours[]={IM_COL32(245,95,90,255),IM_COL32(110,220,125,255),IM_COL32(100,160,255,255)};
    for(const auto& line:handle_segments(a)) {
        const ImU32 colour=colours[line.axis];
        dl->AddLine(line.a,line.b,colour,a.handle.active && a.handle.axis==line.axis?4.f:2.f);
        if(a.handle_tool==0) dl->AddCircleFilled(line.b,5,colour);
        if(a.handle_tool==1) dl->AddRectFilled(ImVec2(line.b.x-5,line.b.y-5),ImVec2(line.b.x+5,line.b.y+5),colour);
        if(a.handle_tool!=2) dl->AddText(ImVec2(line.b.x+7,line.b.y-7),colour,line.axis==0?"X":line.axis==1?"Y":"Z");
    }
    const auto& t=part->transform;std::array<ImVec2,8> corners;bool visible=true;
    for(int i=0;i<8;++i) {
        pg::Vec p{(i&1)?t.size.x:0,(i&2)?t.size.y:0,(i&4)?t.size.z/2:-t.size.z/2};
        if(part->kind==vr::overrides::PartKind::Wedge && (i&2)) {
            const bool high=part->wedge_axis==0?bool(i&1):bool(i&4);
            p.y=high==(part->wedge_direction>0)?t.size.y:0;
        }
        visible &= handle_screen(a,pg::to_group(p,t),&corners[size_t(i)]);
    }
    if(visible) for(int i=0;i<8;++i) for(int bit:{1,2,4}) if(!(i&bit))
        dl->AddLine(corners[size_t(i)],corners[size_t(i|bit)],IM_COL32(255,220,100,a.handle.active?255:210),2.f);
    dl->PopClipRect();
}

void switch_mode(App& a, int mode) {
    if (mode == a.mode) return;
    if (mode == 2 && (!a.has_draft || (!a.draft.cutout && a.draft.parts.empty()))) { a.status = "Paint a cutout in MASK first."; return; }
    stop_fly_look(a);
    if (a.mode == 2) a.camera = a.room_camera;
    if (mode == 2) {
        if(a.draft.cutout && a.draft.parts.empty() && !a.draft.model_seeded)
            add_part(a,vr::overrides::PartKind::Billboard);
        a.room_camera = a.camera;
        a.camera.tx = a.draft.w*.5f; a.camera.ty = a.draft.extent*.5f; a.camera.tz = 0;
        a.camera.yaw = .4f; a.camera.pitch = .3f; a.camera.dist = std::max(a.draft.w,a.draft.extent)*1.1f+1;
    }
    a.mode = mode; remesh(a);
}

// ── Files ────────────────────────────────────────────────────────────────────

// CANONICAL, not string equality. On Windows tools/x.json and TOOLS\X.JSON are
// one file, and a string compare would wave the second one through — which is
// exactly the case that ends with a committed test fixture silently rewritten.
// On Linux the same job needs the opposite rule for case: build/Foo.json and
// build/foo.json are two files there, and refusing to save to the second
// because the first is the input would be a different bug. platform_io::same_file
// owns that platform split; see platform_io.h.
bool same_file(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    return studio::platform_io::same_file(a, b);
}

bool do_save(App& a) {
    if (same_file(a.out_path, a.in_path)) {
        a.status = "REFUSED: that is the file this session was given (" +
                   a.in_path + "). Choose another path.";
        std::fprintf(stderr, "[gui] refusing to write %s: it is the input file\n",
                     a.out_path.c_str());
        return false;
    }
    if (studio::pattern_io::write(a.out_path.c_str(), a.working)) {
        char buf[600];
        std::snprintf(buf, sizeof(buf), "saved %zu pattern(s) to %s",
                      a.working.patterns.size(), a.out_path.c_str());
        a.status = buf;
        a.document.saved = a.working;
        return true;
    } else {
        a.status = "write failed — see the console";
    }
    return false;
}

void do_reload(App& a) {
    OverrideSet loaded;
    if (!vr::overrides::load(a.out_path.c_str(), &loaded)) {
        a.status = "reload failed — see the console";
        return;
    }
    a.working = std::move(loaded);
    a.document.saved = a.working;
    a.document.clear_history();
    a.has_draft = false;
    a.draft_slot = -1;
    a.draft_matches.clear();
    remesh(a);
    if (a.sel_x >= 0) select_cell(a, a.sel_x, a.sel_y);

    char buf[600];
    std::snprintf(buf, sizeof(buf), "reloaded %zu pattern(s) from %s",
                  a.working.patterns.size(), a.out_path.c_str());
    a.status = buf;
}

// Load all fallible room resources before replacing the current workspace.
bool load_room(App& a, const std::string& id) {
    Snapshot snap; studio::BuildInfo info; studio::Room2D room;
    if (!studio::build_snapshot(a.decomp, id.c_str(), &snap, &info, {}) ||
        !room.build(snap) || (a.room.texture && !room.upload())) {
        room.release(); a.status = "Could not load room " + id; return false;
    }
    a.room.release(); a.room = std::move(room); a.snap = std::move(snap);
    a.tileset_label = info.primary_name + " + " + info.secondary_name;
    a.map_id = id; a.pan_x = a.pan_y = 0;
    a.camera.tx = float(a.snap.width) * .5f;
    a.camera.tz = float(a.snap.height) * .5f;
    a.camera.dist = float(a.snap.height) * 1.1f + 6;
    return true;
}

void history(App& a, bool redo) {
    const std::string old_room = a.map_id;
    if (!(redo ? a.document.redo() : a.document.undo())) return;
    const std::string restored_room = a.map_id;
    if (restored_room != old_room && !load_room(a, restored_room)) {
        if (redo) a.document.undo(); else a.document.redo();
        return;
    }
    a.edit_roof_rows = a.draft.apply.roof_rows;
    remesh(a); a.status = redo ? "Redone." : "Undone.";
}

void select_definition(App& a, int slot) {
    if (slot < 0 || slot >= int(a.working.patterns.size())) return;
    const auto p = a.working.patterns[size_t(slot)];
    // A definition's stored source is its editing provenance. Another accepted
    // occurrence is not evidence that the stored source still exists.
    bool source_room_loaded = p.source.room == a.map_id;
    if (!p.source.room.empty() && p.source.room != a.map_id) {
        source_room_loaded = load_room(a, p.source.room);
        if (source_room_loaded) { clear_selection(a); remesh(a); }
    }
    if (source_room_loaded) {
        for (const auto& m : a.claims.accepted)
            if (m.pattern == slot && m.x == p.source.x && m.y == p.source.y) {
                select_cell(a, m.x + p.anchor % p.w, m.y + p.anchor / p.w); return;
            }
    }
    clear_selection(a); a.has_draft = true; a.draft_slot = slot; a.draft = p;
    a.document.state.draft_base = p;
    a.origin_x = a.document.state.base_origin_x = p.source.x;
    a.origin_y = a.document.state.base_origin_y = p.source.y;
    a.edit_roof_rows = p.apply.roof_rows; refresh_matches(a);
    a.status = "Stored source unresolved or unavailable. The definition is preserved; choose an accepted source placement in the inspector.";
}

bool reselect_source(App& a, int x, int y) {
    if (!a.has_draft || a.draft_slot < 0) return false;
    const auto resolved = vr::overrides::resolve(a.snap, effective(a));
    const bool accepted = std::any_of(resolved.accepted.begin(), resolved.accepted.end(), [&](const auto& m) {
        return m.pattern == a.draft_slot && m.x == x && m.y == y;
    });
    if (!accepted) { a.status = "Choose an accepted instance of this definition."; return false; }
    const auto before = a.document.state;
    a.draft.source = {a.map_id, x, y};
    a.origin_x = x; a.origin_y = y;
    a.sel_x = x + a.draft.anchor % a.draft.w;
    a.sel_y = y + a.draft.anchor / a.draft.w;
    a.document.record(before); remesh(a);
    a.status = "Source reselected in the draft. Apply and Save retain this placement.";
    return true;
}

bool open_review_location(App& a) {
    const auto& r=a.review.target;
    const auto* definition=a.decomp.map(a.review.target_map);
    const auto* layout=definition?a.decomp.layout(definition->layout_id):nullptr;
    if(!layout || layout->width+15!=a.review.target_width || layout->height+14!=a.review.target_height) {
        a.status="Review map bounds no longer match the source. Refresh the coverage snapshot.";return false;
    }
    if(a.map_id!=a.review.target_map && !load_room(a,a.review.target_map)) return false;
    clear_selection(a);switch_mode(a,3);remesh(a);
    // Resolve the recorded occurrence here, never redirect through a model's
    // definition-source room. A rejected match still has a useful map location.
    if(r.model.empty()) select_cell(a,r.anchor_x,r.anchor_y);
    else for(const auto& m:a.claims.accepted) {
        if(m.x==r.x && m.y==r.y && m.pattern>=0 && m.pattern<int(a.working.patterns.size()) &&
           a.working.patterns[size_t(m.pattern)].id==r.model) {
            select_cell(a,r.anchor_x,r.anchor_y);break;
        }
    }
    a.camera.tx=r.x+r.w*.5f;a.camera.tz=r.y+r.h*.5f;a.camera.ty=.7f;
    // Leave context around small/flat targets instead of zooming into a nearby
    // tree's canopy. The source-map outline remains the exact recorded extent.
    a.camera.yaw=.55f;a.camera.pitch=1.05f;a.camera.dist=std::max(8.f,float(std::max(r.w,r.h))*1.3f+3.f);
    a.camera.orthographic=false;a.room_camera=a.camera;
    a.pan_x=std::clamp((r.x+r.w*.5f)*16-a.layout.map.w*.5f,0.f,std::max(0.f,a.room.w-a.layout.map.w));
    a.pan_y=std::clamp((r.y+r.h*.5f)*16-a.layout.map.h*.5f,0.f,std::max(0.f,a.room.h-a.layout.map.h));
    a.review.focused=r;a.review.focused_map=a.map_id;
    a.status="Review location: "+r.label+". Open Review for its evidence and remaining work.";
    return true;
}

void execute_pending(App& a) {
    const auto action = a.pending; a.pending = App::Action::None;
    const auto before = a.document.state;
    switch (action) {
        case App::Action::Select: select_cell(a, a.pending_x, a.pending_y); break;
        case App::Action::New: new_group(a); return;
        case App::Action::Room:
            if (!load_room(a, a.pending_room)) return;
            clear_selection(a); remesh(a); break;
        case App::Action::Definition: select_definition(a, a.pending_x); break;
        case App::Action::Reload: do_reload(a); return;
        case App::Action::Close: a.close_ready = true; return;
        case App::Action::Save: do_save(a); return;
        case App::Action::Review: if(!open_review_location(a)) return;break;
        case App::Action::Reauto:
            clear_selection(a); remesh(a);
            a.status = "Proposals regenerated around the applied definitions."; break;
        case App::Action::Dissolve:
            if (a.draft_slot >= 0) a.working.patterns.erase(a.working.patterns.begin() + a.draft_slot);
            clear_selection(a); remesh(a);
            a.status = "Definition dissolved; its instances return to inference."; break;
        case App::Action::None: return;
    }
    if (a.mode == 2 && (action == App::Action::Select || action == App::Action::Definition)) remesh(a);
    // Selection alone is navigation; room transitions and authoring actions are reversible.
    if (action != App::Action::Select) a.document.record(before);
}

bool needs_saved_decision(const App& a) {
    return (a.pending == App::Action::Reload || a.pending == App::Action::Close) && a.document.unsaved();
}

void request_action(App& a, App::Action action, int x = -1, int y = -1, std::string room = {}) {
    if (a.pending != App::Action::None) return;
    a.pending = action; a.pending_x = x; a.pending_y = y; a.pending_room = std::move(room);
    a.confirm_dissolve = action == App::Action::Dissolve;
    if (!a.document.draft_dirty() && !needs_saved_decision(a) && !a.confirm_dissolve) execute_pending(a);
}

// This modal protects the three distinct layers: draft, applied working set, saved file.
void pending_panel(App& a) {
    if (a.reset_pending) {
        ImGui::OpenPopup("Reset cutout for membership?");
        if (ImGui::BeginPopupModal("Reset cutout for membership?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Changing member cells resets this group's cutout and parts.");
            ImGui::TextUnformatted("The reset and membership stroke can be undone together.");
            if (ImGui::Button("Reset and change cells") && reset_for_membership(a)) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { a.reset_members.clear(); a.reset_pending = false; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        return;
    }
    if (a.pending == App::Action::None) return;
    ImGui::OpenPopup("Finish current edit");
    if (!ImGui::BeginPopupModal("Finish current edit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    bool advance = false;
    if (a.document.draft_dirty()) {
        ImGui::TextUnformatted("This group has unapplied changes.");
        if (ImGui::Button("Apply")) advance = apply_draft(a);
        ImGui::SameLine();
        if (ImGui::Button("Discard draft")) { revert_draft(a); advance = true; }
    } else if (needs_saved_decision(a)) {
        ImGui::TextUnformatted("The working definitions have unsaved changes.");
        if (ImGui::Button("Save")) advance = do_save(a);
        ImGui::SameLine();
        if (ImGui::Button("Discard working changes")) {
            // The requested Reload/Close consumes the choice without changing the saved marker.
            execute_pending(a); ImGui::CloseCurrentPopup();
        }
    } else if (a.confirm_dissolve) {
        ImGui::TextUnformatted("Dissolve this definition and all of its instances?");
        ImGui::TextUnformatted("This can be undone. Saved files change only on Save.");
        if (ImGui::Button("Dissolve definition")) { a.confirm_dissolve = false; advance = true; }
    } else advance = true;
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) { a.pending = App::Action::None; ImGui::CloseCurrentPopup(); }
    if (advance && a.pending != App::Action::None && !a.document.draft_dirty() &&
        !needs_saved_decision(a) && !a.confirm_dissolve) {
        execute_pending(a); ImGui::CloseCurrentPopup();
    }
    if (!a.status.empty()) ImGui::TextWrapped("%s", a.status.c_str());
    ImGui::EndPopup();
}

// ── Non-interactive acceptance ───────────────────────────────────────────────

bool contains(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

int selftest_fail(int passed, const char* check) {
    std::fprintf(stderr, "[gui-selftest] FAIL after %d checks: %s\n", passed,
                 check);
    return 1;
}

// Drive one real authoring round-trip without depending on a window automation
// API being able to discover SDL/ImGui. This deliberately uses pick_cell,
// select_cell, preview_roof_rows, apply_draft, do_save and do_reload — the same
// functions reached by the mouse and buttons — and leaves the output behind so
// rubyvr_studio or RubyRecomp can consume it as the next acceptance layer.
bool capture_cutout_png(App& a, const std::string& path, float yaw=.4f);
bool capture_room_png(App& a,const std::string& path);

int voxel_ui_selftest(App& a,const char* path);

bool terrain_edit(App&,int);
int run_selftest(App& a) {
    int passed = studio::document_selftest(a.snap, a.out_path.c_str());
    if (passed < 0) return selftest_fail(0, "authored document");
    auto require = [&](bool condition, const char* check) {
        if (condition) ++passed;
        else std::fprintf(stderr, "[gui-selftest] check failed: %s\n", check);
        return condition;
    };

    {
        Camera c; c.yaw=0;c.pitch=0;c.dist=10;
        float ex,ey,ez,ax,ay,az;
        camera_eye(c,&ex,&ey,&ez);
        camera_look(c,75,-100);
        camera_eye(c,&ax,&ay,&az);
        if(!require(std::fabs(ax-ex)+std::fabs(ay-ey)+std::fabs(az-ez)<1e-4f && c.pitch<0,
                    "fly look preserves the eye and can look above the horizon")) return selftest_fail(passed,"fly look");
        camera_move(c,1,1,1,2);
        camera_eye(c,&ax,&ay,&az);
        if(!require(std::fabs(std::sqrt((ax-ex)*(ax-ex)+(ay-ey)*(ay-ey)+(az-ez)*(az-ez))-2)<1e-4f,
                    "diagonal fly movement has the same requested world speed")) return selftest_fail(passed,"fly movement");
        const Camera before=c;
        camera_move(c,0,0,0,100);
        if(!require(c.tx==before.tx && c.ty==before.ty && c.tz==before.tz,
                    "released movement keys leave the camera stationary")) return selftest_fail(passed,"fly idle");
        camera_zoom(c,100);const float nearest=c.dist;camera_zoom(c,-100);
        if(!require(nearest==.25f && c.dist==300,"zoom clamps close inspection and far framing safely")) return selftest_fail(passed,"camera zoom");
        const pg::Vec lo{-5,-2,-8},hi{9,6,4};
        camera_frame(c,lo,hi,400,800);
        bool inside=true;
        for(int i=0;i<8;++i) {
            ImVec2 p;inside &= project(camera_vp(c,400,800),(i&1)?hi.x:lo.x,(i&2)?hi.y:lo.y,(i&4)?hi.z:lo.z,400,800,&p) &&
                              p.x>=0 && p.y>=0 && p.x<400 && p.y<800;
        }
        if(!require(inside,"Focus frames all bounding corners in a narrow viewport")) return selftest_fail(passed,"camera focus");
        App gate;
        bool isolated=camera_input_allowed(gate,false) && !camera_input_allowed(gate,true);
        gate.pending=App::Action::Save;isolated &= !camera_input_allowed(gate,false);gate.pending=App::Action::None;
        for(bool* blocked:{&gate.reset_pending,&gate.handle.active,&gate.membership_stroke,&gate.mask_stroke,&gate.panning}) {
            *blocked=true;isolated &= !camera_input_allowed(gate,false);*blocked=false;
        }
        gate.drag_surface=2;isolated &= !camera_input_allowed(gate,false);
        if(!require(isolated,"camera input is isolated from typing, modals, handles and painting")) return selftest_fail(passed,"camera isolation");
    }

    const studio::Rect surface{208, 108, 400, 212};
    int map_x = -1, map_y = -1;
    if (!require(studio::map_cell(surface, 32, 48, 216, 116, 35, 34, &map_x, &map_y) &&
                     map_x == 2 && map_y == 3, "2D picking includes the pan offset") ||
        !require(!studio::map_cell(surface, 0, 0, 207, 116, 35, 34, &map_x, &map_y) &&
                     !studio::map_cell(surface, 0, 0, 608, 116, 35, 34, &map_x, &map_y),
                 "2D picking rejects both outside edges") ||
        !require(!studio::map_cell(surface, 560, 0, 216, 116, 35, 34, &map_x, &map_y),
                 "2D picking rejects cells beyond the snapshot"))
        return selftest_fail(passed, "workspace coordinates");
    const auto physical = studio::framebuffer_rect({208, 356, 804, 291}, 1280, 720, 1920, 1080);
    if (!require(physical.x == 312 && physical.y == 109 && physical.w == 1206 && physical.h == 437,
                 "fractional framebuffer scaling rounds shared edges and flips GL Y"))
        return selftest_fail(passed, "framebuffer coordinates");

    if (!require(contains(a.last_stats, "geom=f3a1bd5a56a98d41"),
                 "unmodified Route 101 geometry hash"))
        return selftest_fail(passed, "baseline");
    if (!require(contains(a.last_stats, "vertices=816132"),
                 "unmodified Route 101 vertex count") ||
        !require(a.model.objects.size() == 65, "65 baseline objects"))
        return selftest_fail(passed, "baseline model");

    bool all_rows_select = true;
    for (int i = 0; i < int(a.model.objects.size()); ++i) {
        select_object(a, i);
        all_rows_select = all_rows_select && a.sel_obj == i && a.has_draft;
    }
    if (!require(all_rows_select, "every list action selects a member of its own object"))
        return selftest_fail(passed, "object list");

    // Project the centre of the known Oldale house cell, then feed that pixel
    // through the production picker. This checks the camera/pick contract as
    // well as the object-selection path without hard-coding mouse coordinates.
    constexpr int kWidth = 1600, kHeight = 950;
    ImVec2 pixel;
    const Mat4 vp = camera_vp(a.camera, kWidth, kHeight);
    int cell_x = -1, cell_y = -1;
    if (!require(project(vp, 21.5f, 0.0f, 0.5f, kWidth, kHeight, &pixel),
                 "project Oldale house cell") ||
        !require(pick_cell(a.camera, kWidth, kHeight, pixel.x, pixel.y,
                           &cell_x, &cell_y),
                 "pick projected Oldale house cell") ||
        !require(cell_x == 21 && cell_y == 0,
                 "project/pick round-trip returns cell 21,0"))
        return selftest_fail(passed, "picking");

    select_cell(a, cell_x, cell_y);
    if (!require(a.has_draft, "selection derives a draft") ||
        !require(a.sel_obj >= 0, "selection resolves an object") ||
        !require(a.draft_matches.size() == 1, "derived pattern has one match"))
        return selftest_fail(passed, "selection");
    const Object& before = a.model.objects[static_cast<size_t>(a.sel_obj)];
    if (!require(before.x == 21 && before.y == 0 && before.w == 4 &&
                     before.footprint_rows == 3 && before.extent == 4,
                 "selected object is the 21,0 4x3 extent-4 house") ||
        !require(before.cls == ObjectClass::kStructure && before.has_door,
                 "selected object is a structure with a door") ||
        !require(!before.authored.active, "baseline object is inferred"))
        return selftest_fail(passed, "selected object");

    preview_roof_rows(a, 1);
    const int preview_index = a.model.object_at(21, 0);
    if (!require(preview_index >= 0, "preview preserves the selected cell") ||
        !require(contains(a.last_stats, "geom=fe1e4771b3a1ff19"),
                 "preview geometry hash"))
        return selftest_fail(passed, "preview geometry");
    const Object& preview = a.model.objects[static_cast<size_t>(preview_index)];
    if (!require(preview.authored.active, "preview marks object authored") ||
        !require(preview.authored.roof_rows == 1 &&
                     preview.authored.rise == -1.0f &&
                     preview.authored.height == -1,
                 "preview values come from the draft apply block"))
        return selftest_fail(passed, "preview authored fields");

    apply_draft(a);
    if (!require(a.working.patterns.size() == 1,
                 "Apply commits exactly one pattern") ||
        !require(a.working.patterns[0].apply.roof_rows == 1,
                 "applied pattern retains roof_rows=1"))
        return selftest_fail(passed, "apply");

    do_save(a);
    if (!require(contains(a.status, "saved 1 pattern"), "Save succeeds"))
        return selftest_fail(passed, "save");

    OverrideSet disk;
    if (!require(vr::overrides::load(a.out_path.c_str(), &disk),
                 "saved file parses") ||
        !require(disk.patterns.size() == 1 &&
                     disk.patterns[0].apply.roof_rows == 1,
                 "saved file contains the applied pattern"))
        return selftest_fail(passed, "saved file");
    if (!require(!disk.patterns[0].id.empty() && disk.patterns[0].source.room == a.map_id &&
                     disk.patterns[0].source.x == 21 && disk.patterns[0].source.y == 0,
                 "GUI Apply and Save persist identity and source placement"))
        return selftest_fail(passed, "saved editor identity");

    do_reload(a);
    const int reload_index = a.model.object_at(21, 0);
    if (!require(a.working.patterns.size() == 1, "Reload restores one pattern") ||
        !require(reload_index >= 0, "Reload preserves the selected cell") ||
        !require(contains(a.last_stats, "geom=fe1e4771b3a1ff19"),
                 "Reload preserves preview geometry"))
        return selftest_fail(passed, "reload");
    const Object& reloaded = a.model.objects[static_cast<size_t>(reload_index)];
    if (!require(reloaded.authored.active &&
                     reloaded.authored.roof_rows == 1,
                 "Reload restores authored state"))
        return selftest_fail(passed, "reloaded object");
    if (!require(a.has_draft && a.draft.id == disk.patterns[0].id &&
                     a.draft.source == disk.patterns[0].source,
                 "reselecting after Reload preserves complete editor metadata"))
        return selftest_fail(passed, "reloaded editor identity");

    // Phase 2 uses the same actions as the real widgets and gesture release.
    // Preserve the original round-trip artifact for the batch parity stage.
    const auto round_trip_document = a.document;
    const std::string round_trip_path = a.out_path;
    {
        // A remaining global match must never silently replace stale editing
        // provenance. Use two identical houses to separate those conditions.
        const auto source_snapshot = a.snap;
        a.snap.width = 9; a.snap.height = 4;
        a.snap.grid.assign(36, vr::world::kGridUndefined);
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
            const auto cell = source_snapshot.grid[size_t(y) * source_snapshot.width + 21 + x];
            a.snap.grid[size_t(y) * 9 + x] = cell;
            a.snap.grid[size_t(y) * 9 + 5 + x] = cell;
        }
        Pattern original = disk.patterns[0];
        original.source = {a.map_id, 5, 0};
        a.working.patterns = {original};
        clear_selection(a); remesh(a); select_definition(a, 0);
        if (!require(a.claims.accepted.size() == 2 && a.origin_x == 5 && a.origin_y == 0 &&
                     a.draft == original && !source_resolution_needed(a),
                     "definition selection uses stored source even when an earlier accepted occurrence exists"))
            return selftest_fail(passed, "stored source selection");
        a.snap.grid[5] = vr::world::kGridUndefined;
        remesh(a); select_definition(a, 0);
        const auto unresolved = a.document.state;
        if (!require(a.claims.accepted.size() == 1 && a.claims.accepted[0].x == 0 &&
                     a.has_draft && a.draft_slot == 0 && a.draft == original &&
                     a.working.patterns[0] == original && a.sel_x == -1 && a.sel_y == -1 &&
                     a.origin_x == 5 && source_resolution_needed(a),
                     "missing stored source remains unresolved despite another accepted occurrence") ||
            !require(!edit_member(a, 0, 0, false) && !apply_draft(a) && a.document.state == unresolved,
                     "unresolved source cannot silently author cells or apply through another occurrence") ||
            !require(!reselect_source(a, 5, 0) && a.document.state == unresolved,
                     "source reselection refuses the missing placement"))
            return selftest_fail(passed, "unresolved source preservation");
        Pattern repaired = original; repaired.source = {a.map_id, 0, 0};
        a.document.clear_history();
        if (!require(reselect_source(a, 0, 0) && a.draft == repaired &&
                     a.working.patterns[0] == original && a.document.draft_dirty() &&
                     !source_resolution_needed(a) && a.document.undo_count() == 1,
                     "explicit source reselection changes only draft provenance as one transaction"))
            return selftest_fail(passed, "explicit source reselection");
        const auto resolved = a.document.state;
        history(a, false);
        if (!require(a.document.state == unresolved && source_resolution_needed(a),
                     "undo source reselection restores the unresolved definition"))
            return selftest_fail(passed, "source reselection undo");
        history(a, true);
        if (!require(a.document.state == resolved && !source_resolution_needed(a),
                     "redo source reselection restores the explicit chosen placement"))
            return selftest_fail(passed, "source reselection redo");
        a.out_path = round_trip_path + ".reselected-source.json";
        OverrideSet repaired_disk;
        if (!require(apply_draft(a) && do_save(a) && vr::overrides::load(a.out_path.c_str(), &repaired_disk) &&
                     repaired_disk.patterns.size() == 1 && repaired_disk.patterns[0] == repaired,
                     "Apply and Save retain explicit source repair with complete definition identity"))
            return selftest_fail(passed, "source reselection persistence");
        a.snap = source_snapshot; a.document = round_trip_document;
        a.out_path = round_trip_path; remesh(a);
    }
    a.out_path += ".groups.json";
    a.document.clear_history();
    new_group(a);
    if (!require(a.has_draft && !populated(a.draft) && !a.document.draft_dirty() &&
                 effective(a) == a.working && !apply_draft(a), "empty new draft cannot enter the matcher") ||
        !require(!edit_member(a, 21, 0, false) && !populated(a.draft),
                 "membership rejects cells owned by another applied definition")) return selftest_fail(passed, "empty/conflicting draft");
    select_definition(a, 0);
    request_action(a, App::Action::Dissolve);
    if (!require(a.pending == App::Action::Dissolve && a.working.patterns.size() == 1,
                 "Dissolve waits for a concrete definition-wide choice")) return selftest_fail(passed, "dissolve request");
    a.confirm_dissolve = false; execute_pending(a);
    if (!require(a.working.empty(), "Dissolve removes the definition")) return selftest_fail(passed, "dissolve");
    history(a, false);
    if (!require(a.working.patterns.size() == 1, "Undo restores dissolved definition and selection")) return selftest_fail(passed, "dissolve undo");
    history(a, true);
    new_group(a);
    const auto blank = a.document.state;
    const size_t undo_before_stroke = a.document.undo_count();
    a.membership_stroke = true; a.stroke_before = blank; a.stroke_erase = false;
    stroke_to(a, 21, 0); stroke_to(a, 24, 0); end_stroke(a, false);
    if (!require(a.draft.w == 4 && a.draft.extent == 1 && a.draft.mask == std::vector<uint8_t>(4, 1) &&
                 a.document.undo_count() == undo_before_stroke + 1, "interpolated drag adds all cells as one transaction"))
        return selftest_fail(passed, "membership stroke");
    const auto row = a.document.state;
    a.membership_stroke = true; a.stroke_before = row; a.stroke_erase = true;
    stroke_to(a, 22, 0); end_stroke(a, false);
    if (!require(a.draft.mask == std::vector<uint8_t>({1,0,1,1}), "erase creates a real membership hole"))
        return selftest_fail(passed, "erase stroke");
    history(a, false);
    if (!require(a.document.state == row, "Undo restores exact membership before erase")) return selftest_fail(passed, "membership undo");
    history(a, true);
    const auto sparse = a.document.state;
    a.membership_stroke = true; a.stroke_before = sparse; a.stroke_erase = false;
    stroke_to(a, 21, 3); end_stroke(a, true);
    if (!require(a.document.state == sparse, "cancelled stroke preserves the entire draft")) return selftest_fail(passed, "cancel stroke");
    request_action(a, App::Action::Room, -1, -1, "MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F");
    if (!require(a.pending == App::Action::Room && a.map_id == "MAP_ROUTE101" && a.document.state == sparse,
                 "dirty room navigation waits without changing draft or snapshot")) return selftest_fail(passed, "dirty navigation");
    a.pending = App::Action::None; // the modal's Cancel choice
    a.draft.apply.cls = vr::overrides::ApplyClass::kProp;
    a.draft.apply.height = 2;
    if (!require(apply_draft(a) && do_save(a), "sparse group applies and saves")) return selftest_fail(passed, "sparse save");
    const auto saved_sparse = a.working;
    if (!require(studio::pattern_io::write((round_trip_path + ".sparse.json").c_str(), saved_sparse), "write sparse runtime parity fixture")) return selftest_fail(passed, "sparse parity fixture");
    const std::string saved_sparse_hash = stats_value(a.last_stats, "geom");
    std::fprintf(stdout, "[gui-sparse] geom=%s\n", saved_sparse_hash.c_str());
    if (!require(saved_sparse_hash != "f3a1bd5a56a98d41", "sparse fixture changes production geometry")) return selftest_fail(passed, "sparse geometry");
    do_reload(a);
    if (!require(a.working == saved_sparse && stats_value(a.last_stats, "geom") == saved_sparse_hash &&
                 a.model.object_at(22,0) != a.model.object_at(21,0),
                 "reload preserves sparse identity, membership and production geometry")) return selftest_fail(passed, "sparse reload");
    const std::string old_tilesets = a.tileset_label;
    if (!require(a.room.build(a.snap) && a.room.upload(), "room texture exists before navigation")) return selftest_fail(passed, "room texture");
    request_action(a, App::Action::Room, -1, -1, "MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F");
    if (!require(a.map_id == "MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F" && !a.has_draft &&
                 a.tileset_label != old_tilesets && a.room.texture && a.room.w == a.snap.width * 16 &&
                 a.working == saved_sparse && !a.document.unsaved(),
                 "room navigation replaces texture and tileset without losing saved definitions")) return selftest_fail(passed, "room navigation");
    history(a, false);
    if (!require(a.map_id == "MAP_ROUTE101" && a.has_draft && a.working == saved_sparse &&
                 stats_value(a.last_stats, "geom") == saved_sparse_hash,
                 "undo room navigation restores the exact authored room")) return selftest_fail(passed, "room undo");
    const auto before_failure = a.document.state;
    const auto old_texture = a.room.texture;
    if (!require(!load_room(a, "MAP_DOES_NOT_EXIST") && a.document.state == before_failure && a.room.texture == old_texture,
                 "failed room load preserves state and texture")) return selftest_fail(passed, "room failure");
    request_action(a, App::Action::Reauto);
    if (!require(!a.has_draft && a.working == saved_sparse, "Re-auto preserves authored definitions")) return selftest_fail(passed, "re-auto");
    a.pending = App::Action::Dissolve; select_definition(a, 0); execute_pending(a);
    if (!require(a.working.empty() && do_save(a), "the last definition can be dissolved and an empty document saved"))
        return selftest_fail(passed, "empty save");
    history(a, false);
    if (!require(a.document.unsaved(), "undo after Save compares working state with the saved marker")) return selftest_fail(passed, "saved history marker");
    request_action(a, App::Action::Close);
    if (!require(a.pending == App::Action::Close && !a.close_ready && needs_saved_decision(a),
                 "closing unsaved definitions waits for Save, Discard or Cancel")) return selftest_fail(passed, "dirty close");
    a.pending = App::Action::None;
    a.document = round_trip_document; a.out_path = round_trip_path;
    remesh(a); a.room.release();

    auto save_cutout_fixture = [&](const char* label) {
        const auto saved_document = a.document;
        const auto saved_path = a.out_path;
        a.out_path += std::string(".") + label + ".json";
        const bool ok = (!a.has_draft || apply_draft(a)) && do_save(a);
        if (ok) std::fprintf(stdout,"[gui-%s] geom=%s\n",label,stats_value(a.last_stats,"geom").c_str());
        a.document = saved_document; a.out_path = saved_path; remesh(a);
        return ok;
    };
    // Both structure and prop definitions use the shared standing-cutout path.
    const auto before_mask_tests = a.document;
    a.working = {}; clear_selection(a); remesh(a);
    for (const auto cell : {studio::pattern_io::Cell{21,0}, studio::pattern_io::Cell{7,1}}) {
        select_cell(a, cell.x, cell.y);
        const std::string inferred_hash = stats_value(a.last_stats, "geom");
        if (!require(initialize_cutout(a) && a.draft.cutout.has_value(), "initialize a standing cutout from source opacity"))
            return selftest_fail(passed, "cutout initialization");
        const auto original_mask = *a.draft.cutout;
        const std::string full_hash = stats_value(a.last_stats, "geom");
        if (!require(full_hash != inferred_hash, "cutout replaces inferred geometry for this object class"))
            return selftest_fail(passed, "standing cutout replacement");
        if (!require(!edit_member(a, cell.x, cell.y, true) && *a.draft.cutout == original_mask,
                     "membership cannot silently discard an existing cutout")) return selftest_fail(passed, "cutout membership guard");
        // Export a painted hole using the same brush path as the canvas.
        const std::string image_base = a.out_path + (cell.x == 21 ? ".cutout-house" : ".cutout-prop");
        if (!require(capture_cutout_png(a,image_base+"-before.png"), "capture standing cutout before painting")) return selftest_fail(passed,"cutout image");
        const auto before_fixture = a.document;
        a.mask_stroke = true; a.mask_erase = true; a.stroke_before = a.document.state;
        for (int y=8;y<16;++y) { stroke_to(a,8,y); stroke_to(a,15,y); }
        end_stroke(a,false);
        if (!require(capture_cutout_png(a,image_base+"-after.png"), "capture standing cutout after painting")) return selftest_fail(passed,"painted cutout image");
        if (!require(save_cutout_fixture(cell.x == 21 ? "cutout-house" : "cutout-prop"), "painted cutout fixture applies and saves through GUI actions"))
            return selftest_fail(passed,"cutout runtime fixture");
        a.document = before_fixture; remesh(a);
        const auto before_reset = a.document.state;
        a.membership_stroke = true; a.stroke_before = before_reset; a.stroke_erase = true;
        stroke_to(a,cell.x,cell.y); end_stroke(a,false);
        if (!require(a.reset_pending && a.document.state == before_reset, "membership waits for reset/cancel without destroying opacity"))
            return selftest_fail(passed,"membership reset request");
        a.reset_members.clear(); a.reset_pending = false; // Cancel in the same modal
        a.membership_stroke = true; a.stroke_before = before_reset; a.stroke_erase = true;
        stroke_to(a,cell.x,cell.y); end_stroke(a,false);
        if (!require(reset_for_membership(a) && !a.draft.cutout, "reset and membership change commit together"))
            return selftest_fail(passed,"membership reset");
        history(a,false);
        if (!require(a.document.state == before_reset, "Undo restores membership and mask together")) return selftest_fail(passed,"membership reset undo");
        uint64_t isolated_hash=0; int isolated_vertices=0;
        if (!require(vr::diorama::build_cutout_preview(a.snap,a.draft,&isolated_hash,&isolated_vertices) && isolated_vertices > 0,
                     "direct definition preview builds the shared cutout geometry")) return selftest_fail(passed,"cutout isolate");
        remesh(a);
        const auto before_empty = a.document.state;
        a.draft.cutout->opacity.assign(a.draft.cutout->opacity.size(), 0);
        a.document.record(before_empty); remesh(a);
        const std::string empty_hash = stats_value(a.last_stats, "geom");
        if (cell.x == 21 && !require(save_cutout_fixture("cutout-empty"), "all-transparent fixture applies and saves"))
            return selftest_fail(passed,"empty runtime fixture");
        if (!require(empty_hash != full_hash && empty_hash != inferred_hash,
                     "all-transparent cutout emits neither original nor inferred object art"))
            return selftest_fail(passed, "empty cutout geometry");
        history(a, false);
        if (!require(*a.draft.cutout == original_mask && stats_value(a.last_stats,"geom") == full_hash,
                     "Undo restores full cutout opacity and production geometry")) return selftest_fail(passed, "cutout undo");
        const auto saved_topology = a.draft;
        a.draft.cutout->opacity.assign(a.draft.cutout->opacity.size(),0);
        a.draft.cutout->opacity[1+a.draft.cutout->w] = 1;
        vr::diorama::build_cutout_preview(a.snap,a.draft,nullptr,&isolated_vertices);
        if (!require(isolated_vertices == 36, "one cutout pixel emits six exposed faces")) return selftest_fail(passed,"cutout face topology");
        a.draft.cutout->opacity[2+a.draft.cutout->w] = 1;
        vr::diorama::build_cutout_preview(a.snap,a.draft,nullptr,&isolated_vertices);
        if (!require(isolated_vertices == 60, "adjacent cutout pixels omit their two buried faces")) return selftest_fail(passed,"cutout shared faces");
        a.draft.cutout->opacity[2+a.draft.cutout->w] = 0;
        a.draft.cutout->opacity[3+a.draft.cutout->w] = 1;
        vr::diorama::build_cutout_preview(a.snap,a.draft,nullptr,&isolated_vertices);
        if (!require(isolated_vertices == 72, "disjoint pixels retain independent closed extrusions")) return selftest_fail(passed,"cutout islands");
        a.draft = saved_topology;
        remove_cutout(a);
        // Identity remains metadata, so removal returns to the exact inferred geometry.
        if (!require(stats_value(a.last_stats,"geom") == inferred_hash,
                     "removing the last authored cutout returns to unchanged inference")) return selftest_fail(passed, "remove cutout");
        clear_selection(a); remesh(a);
    }
    a.document = before_mask_tests; remesh(a);
    const auto before_parts_tests=a.document;
    a.working={};clear_selection(a);remesh(a);select_cell(a,21,0);initialize_cutout(a);
    uint64_t mask_hash=0;int mask_vertices=0;
    vr::diorama::build_cutout_preview(a.snap,a.draft,&mask_hash,&mask_vertices);
    const auto before_seed=a.document.state;
    switch_mode(a,2);
    if(!require(a.draft.parts.size()==1 && a.draft.model_seeded && a.preview_hash==mask_hash && a.preview_vertices==mask_vertices,
                "MODEL seeds one billboard with exactly the previous cutout vertices and hash")) return selftest_fail(passed,"part seed");
    const auto seeded=a.document.state;
    history(a,false);
    if(!require(a.document.state==before_seed,"Undo seed restores the complete mask-only draft")) return selftest_fail(passed,"seed undo");
    history(a,true);
    if(!require(a.document.state==seeded,"Redo restores seeded part identity")) return selftest_fail(passed,"seed redo");
    delete_part(a);switch_mode(a,1);switch_mode(a,2);
    if(!require(a.draft.parts.empty() && a.preview_hash==mask_hash,"explicit last-part deletion returns cutout and suppresses automatic reseeding")) return selftest_fail(passed,"delete seed");
    add_part(a,vr::overrides::PartKind::Billboard);
    const auto one=a.document.state;
    add_part(a,vr::overrides::PartKind::Billboard,true);
    if(!require(a.draft.parts.size()==2 && a.draft.parts[0].id!=a.draft.parts[1].id && a.preview_hash==mask_hash,
                "duplicate gets a new identity and coincident billboards have no duplicate surfaces")) return selftest_fail(passed,"part duplicate");
    delete_part(a);
    if(!require(a.draft.parts==one.draft.parts && a.preview_hash==mask_hash,"delete duplicate preserves original billboard")) return selftest_fail(passed,"part delete");
    add_part(a,vr::overrides::PartKind::Box);
    a.draft.parts.back().transform={{.5f,.25f,-.25f},{2,1.5f,1},{0,27,0}};
    remesh(a);
    if(!require(a.preview_hash!=mask_hash && a.preview_vertices>0,"rotated asymmetric box composes with billboard in production preview")) return selftest_fail(passed,"box composition");
    auto check_focus=[&]() {
        const auto old_camera=a.camera;const auto old_layout=a.layout;
        const auto state=a.document.state;const auto undo=a.document.undo_count();
        const auto hash=a.preview_hash;const auto room_hash=stats_value(a.last_stats,"geom");
        a.layout=studio::ShellLayout::at(1280,720);
        bool ok=focus_selection(a);
        std::vector<vr::diorama::AuthoredVertex> vertices;
        ok &= vr::diorama::inspect_authored_mesh(a.snap,a.draft,&vertices) && !vertices.empty();
        const auto& v=a.layout.view;const auto vp=camera_vp(a.camera,int(v.w),int(v.h));
        for(const auto& vertex:vertices) {
            const auto p=a.mode==2?vertex.position:vertex.position+pg::Vec{float(a.origin_x),.002f,float(a.origin_y+a.draft.extent)-.5f};
            ImVec2 screen;ok &= project(vp,p.x,p.y,p.z,int(v.w),int(v.h),&screen) &&
                screen.x>=0 && screen.y>=0 && screen.x<v.w && screen.y<v.h;
        }
        ok &= a.document.state==state && a.document.undo_count()==undo && a.preview_hash==hash && stats_value(a.last_stats,"geom")==room_hash;
        a.camera=old_camera;a.layout=old_layout;
        return ok;
    };
    if(!require(check_focus(),"Focus Model frames every transformed production vertex without changing draft, history or mesh")) return selftest_fail(passed,"model focus");
    if(!require(capture_cutout_png(a,a.out_path+".parts-house.png"),"capture shared authored house parts") ||
       !require(save_cutout_fixture("parts-house"),"parts fixture uses actual GUI Apply and Save")) return selftest_fail(passed,"parts fixture");
    OverrideSet loaded_parts;
    auto expected_parts=a.draft;
    if(expected_parts.source.room.empty()) expected_parts.source={a.map_id,a.origin_x,a.origin_y};
    if(!require(vr::overrides::load((a.out_path+".parts-house.json").c_str(),&loaded_parts) &&
                loaded_parts.patterns.size()==1 && loaded_parts.patterns[0]==expected_parts,
                "saved parts reload with exact model state")) return selftest_fail(passed,"parts reload");
    const auto before_handle_tests=a.document;
    a.layout=studio::ShellLayout::at(kWidth,kHeight);
    const auto before_handle_hash=a.preview_hash;
    auto begin_axis=[&](int tool,int axis) {
        a.handle_tool=tool;
        for(const auto& segment:handle_segments(a)) if(segment.axis==axis) {
            if(begin_handle(a,segment.b.x,segment.b.y) && a.handle.axis==axis) return true;
            end_handle(a,true);
        }
        return false;
    };
    if(!require(begin_axis(0,0),"pick rotated local X translation handle")) return selftest_fail(passed,"move handle pick");
    update_handle(a,a.handle.press.x+a.handle.screen_axis.x*.5f,a.handle.press.y+a.handle.screen_axis.y*.5f);
    const auto moved=selected_part(a)->transform.position;
    if(!require(std::abs(moved.x-(.5f+.5f*std::cos(27.f*.01745329252f)))<.0002f &&
                std::abs(moved.z-(-.25f-.5f*std::sin(27.f*.01745329252f)))<.0002f && moved.y==.25f &&
                a.preview_hash==before_handle_hash,"rotated local translation updates correct group axes without rebuilding")) return selftest_fail(passed,"rotated move");
    end_handle(a,true);
    if(!require(a.document.state==before_handle_tests.state,"cancel restores exact rotated-handle starting state")) return selftest_fail(passed,"handle cancel");
    if(!require(begin_axis(1,0),"pick rotated size handle")) return selftest_fail(passed,"size handle pick");
    update_handle(a,a.handle.press.x+a.handle.screen_axis.x*.5f,a.handle.press.y+a.handle.screen_axis.y*.5f);
    if(!require(selected_part(a)->transform.size.x==2.5f && selected_part(a)->transform.position==before_handle_tests.state.draft.parts.back().transform.position,
                "size handle equals numeric local width and preserves pivot")) return selftest_fail(passed,"size handle");
    end_handle(a,false);
    const auto resized=a.document.state;
    if(!require(a.document.undo_count()==before_handle_tests.undo_count()+1 && a.preview_hash!=before_handle_hash,
                "size release records one transaction and rebuilds")) return selftest_fail(passed,"size release");
    history(a,false);
    if(!require(a.document.state==before_handle_tests.state,"Undo size restores full state")) return selftest_fail(passed,"size undo");
    history(a,true);
    if(!require(a.document.state==resized,"Redo size restores full state")) return selftest_fail(passed,"size redo");
    a.document=before_handle_tests;remesh(a);a.handle_tool=2;
    bool picked_rotation=false;
    for(const auto& segment:handle_segments(a)) if(segment.axis==2) {
        if(begin_handle(a,segment.a.x,segment.a.y) && a.handle.axis==2) { picked_rotation=true;break; }
        end_handle(a,true);
    }
    if(!require(picked_rotation,"pick Euler Z rotation ring")) return selftest_fail(passed,"rotation pick");
    ImVec2 rotation_end;
    const auto turned=pg::rotate(a.handle.start_ray,{0,0,90});
    if(!require(handle_screen(a,a.handle.origin+turned,&rotation_end),"project quarter-turn endpoint")) return selftest_fail(passed,"rotation endpoint");
    update_handle(a,rotation_end.x,rotation_end.y);
    if(!require(std::abs(selected_part(a)->transform.angles.z-90)<.0002f && selected_part(a)->transform.angles.y==27 &&
                a.preview_hash==before_handle_hash,"ring changes the matching numeric Euler component and retains mesh until release")) return selftest_fail(passed,"rotation handle");
    end_handle(a,false);
    if(!require(a.preview_hash!=before_handle_hash,"rotation release rebuilds production geometry")) return selftest_fail(passed,"rotation release");
    if(!require(save_cutout_fixture("parts-handled"),"handle-authored transforms apply and save through GUI actions")) return selftest_fail(passed,"handle runtime fixture");
    a.document=before_handle_tests;remesh(a);a.handle_tool=0;
    // A separate roof fixture is authored using Add Wedge and Duplicate, then a
    // numeric half-turn. It exercises positive local X slopes under rotation.
    a.draft.parts.clear();a.draft.cutout.reset();a.document.state.selected_part.clear();
    add_part(a,vr::overrides::PartKind::Wedge);
    a.draft.parts[0].transform={{0,0,0},{2,1,4},{0,0,0}};
    a.draft.parts[0].art_region={0,0,64,32}; // the fixture's two roof-art rows
    add_part(a,vr::overrides::PartKind::Wedge,true);
    a.draft.parts[1].transform={{4,0,0},{2,1,4},{0,180,0}};remesh(a);
    if(!require(save_cutout_fixture("wedge-roof"),"duplicated and rotated wedges apply and save through GUI actions")) return selftest_fail(passed,"wedge fixture");
    for(int yaw=0;yaw<4;++yaw)
        if(!require(capture_cutout_png(a,a.out_path+".wedge-roof-"+std::to_string(yaw)+".png",.4f+yaw*1.57079632679f),
                    "capture authored roof at four yaws")) return selftest_fail(passed,"roof view");
    a.mode=0;a.camera=a.room_camera;a.document=before_parts_tests;remesh(a);
    const auto before_diorama_tests=a.document;
    a.working={};clear_selection(a);switch_mode(a,3);
    const auto flat=vr::diorama::diorama_stats();
    if(!require(flat.geometry_hash==0x5beda80011500bc1ULL && flat.raised_instances==0,
                "GUI DIORAMA equals the frozen flat-only reference") ||
       !require(save_cutout_fixture("diorama-flat"),"empty DIORAMA working document saves") ||
       !require(capture_room_png(a,a.out_path+".diorama-flat.png"),"capture flat-only DIORAMA")) return selftest_fail(passed,"flat diorama");
    select_cell(a,21,0);a.draft.apply.height=200;remesh(a);
    if(!require(vr::diorama::diorama_stats().geometry_hash==flat.geometry_hash && save_cutout_fixture("diorama-membership"),
                "membership-only GUI definition saves without raising the paper room")) return selftest_fail(passed,"membership diorama");
    initialize_cutout(a);a.mask_stroke=true;a.mask_erase=true;a.stroke_before=a.document.state;
    for(int y=8;y<16;++y) {stroke_to(a,8,y);stroke_to(a,15,y);}end_stroke(a,false);
    const auto painted_diorama=a.document;
    if(!require(vr::diorama::diorama_stats().raised_instances==1 && save_cutout_fixture("diorama-cutout"),
                "painted cutout saves as one raised DIORAMA instance")) return selftest_fail(passed,"cutout diorama");
    a.draft.cutout->opacity.assign(a.draft.cutout->opacity.size(),0);remesh(a);
    if(!require(vr::diorama::diorama_stats().raised_instances==0 && vr::diorama::diorama_stats().geometry_hash!=flat.geometry_hash &&
                save_cutout_fixture("diorama-empty"),"empty cutout saves its erased floor art without a raised count")) return selftest_fail(passed,"empty diorama");
    a.document=painted_diorama;remesh(a);
    add_part(a,vr::overrides::PartKind::Billboard);add_part(a,vr::overrides::PartKind::Box);add_part(a,vr::overrides::PartKind::Wedge);
    a.draft.parts.back().transform={{0,2,-1},{2,1,2},{0,0,0}};a.draft.parts.back().art_region={0,0,64,32};remesh(a);
    if(!require(vr::diorama::diorama_stats().raised_instances==1 && save_cutout_fixture("diorama-parts"),
                "three authored parts still count as one accepted group instance") ||
       !require(capture_room_png(a,a.out_path+".diorama-parts.png"),"capture mixed flat room and authored group")) return selftest_fail(passed,"parts diorama");
    if(!require(check_focus(),"Focus Selected frames the translated DIORAMA instance without changing draft, history or mesh")) return selftest_fail(passed,"instance focus");
    switch_mode(a,0);a.document=before_diorama_tests;remesh(a);

    const int voxel_checks=voxel_ui_selftest(a,round_trip_path.c_str());
    if(voxel_checks<0) return selftest_fail(passed,"manual voxel workflow");
    passed+=voxel_checks;

    // Exercise the actual Save refusal using an alias of the same path, but
    // only the alias that is genuinely the same file on this platform. On
    // Windows that is a case/separator variant; on Linux filenames are
    // case-sensitive and a backslash is an ordinary character inside a name,
    // so the honest alias is the same file spelled with a "./" segment. Making
    // the Linux check use the Windows alias would "pass" only by comparing two
    // different paths, which is the opposite of what the guard is for.
    std::string alias = a.out_path;
#ifdef _WIN32
    for (char& ch : alias) {
        if (ch == '/') ch = '\\';
        else if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        else if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
#else
    {
        const size_t slash = alias.find_last_of('/');
        alias.insert(slash == std::string::npos ? 0 : slash + 1, "./");
    }
#endif
    if (!require(same_file(a.out_path, alias),
                 "canonical guard recognizes a path alias"))
        return selftest_fail(passed, "path guard");
    a.in_path = alias;
    do_save(a);
    if (!require(contains(a.status, "REFUSED:"),
                 "Save refuses to overwrite the input file"))
        return selftest_fail(passed, "overwrite refusal");

    {
        const auto prior=vr::diorama::current_overrides();const auto mode=vr::diorama::build_mode();
        auto source=a.snap;
        int x=7,y=7;
        while(!vr::terrain::primary_cell(source,x,y) && y<source.height-7) {if(++x>=source.width-8) {x=7;++y;}}
        vr::overrides::TerrainMap map;map.group=source.map_group;map.number=source.map_number;
        map.width=source.width;map.height=source.height;
        vr::overrides::TerrainCell cell;cell.x=x;cell.y=y;cell.expected=source.cell(x,y);
        vr::overrides::TerrainSurface surface;surface.layer=source.elevation(x,y);surface.height=surface.thickness=16;
        cell.surfaces={surface};map.cells={cell};vr::terrain::guard_tile(source,cell.expected&1023,&map);
        OverrideSet terrain;terrain.version=7;terrain.terrain={map};
        vr::diorama::set_build_mode(vr::diorama::BuildMode::Diorama);vr::diorama::set_overrides(terrain);vr::diorama::update(source);
        if(!require(vr::diorama::diorama_stats().terrain_cells==1,"GPU terrain fixture activates")) return selftest_fail(passed,"terrain cache");
        auto uploads=vr::diorama::mesh_upload_count();vr::diorama::update(source);
        if(!require(vr::diorama::mesh_upload_count()==uploads,"unchanged terrain has zero new GPU mesh uploads")) return selftest_fail(passed,"terrain cache");
        auto other=source;other.map_number=(other.map_number+1)%256;vr::diorama::update(other);
        if(!require(vr::diorama::diorama_stats().terrain_cells==0 && vr::diorama::mesh_upload_count()==uploads+1,"same-layout map identity changes invalidate GPU terrain")) return selftest_fail(passed,"terrain cache");
        vr::diorama::update(source);other=source;other.grid[size_t(y)*source.width+x]^=0x400;vr::diorama::update(other);
        if(!require(vr::diorama::diorama_stats().terrain_rejected==1 && vr::diorama::diorama_stats().terrain_cells==0,"same-map source edit cannot retain stale terrain")) return selftest_fail(passed,"terrain cache");
        other=source;other.metatiles[size_t(cell.expected&1023)*8]^=0x400;vr::diorama::update(other);
        if(!require(vr::diorama::diorama_stats().terrain_rejected==1,"changed material definition invalidates GPU terrain")) return selftest_fail(passed,"terrain cache");
        if(!source.connections.empty()) {
            vr::diorama::update(source);uploads=vr::diorama::mesh_upload_count();other=source;
            other.connections[0].compatible_art=!other.connections[0].compatible_art;vr::diorama::update(other);
            if(!require(vr::diorama::mesh_upload_count()==uploads+1,"connection provenance invalidates the GPU terrain cache")) return selftest_fail(passed,"terrain cache");
            const auto document=a.document;const auto editor=a.terrain;const auto snapshot=a.snap;
            a.snap=source;a.snap.connections[0].compatible_art=false;a.working=terrain;clear_selection(a);
            const auto& copy=a.snap.connections[0];auto neighbour=map;
            neighbour.group=copy.group;neighbour.number=copy.number;neighbour.width=copy.width;neighbour.height=copy.height;
            neighbour.cells[0].x=copy.source_x;neighbour.cells[0].y=copy.source_y;neighbour.cells[0].expected=a.snap.cell(copy.x,copy.y);
            neighbour.tiles.clear();vr::terrain::guard_tile(a.snap,neighbour.cells[0].expected&1023,&neighbour);
            a.working.terrain.push_back(neighbour);
            a.terrain={};a.terrain.selected=true;a.terrain.room=a.map_id;a.terrain.x0=a.terrain.x1=x;a.terrain.y0=a.terrain.y1=y;a.terrain.height=24;
            const bool editable=vr::terrain::resolve(a.snap,a.working.terrain).rejected==1 && terrain_edit(a,0) &&
                a.working.terrain[0].cells[0].surfaces[0].height==24;
            a.document=document;a.terrain=editor;a.snap=snapshot;remesh(a);
            if(!require(editable,"unresolved neighbour art does not block valid primary-map terrain edits")) return selftest_fail(passed,"terrain editing");
        }
        other=source;other.map_group=other.map_number=-1;other.identity_source=Snapshot::IdentitySource::Unknown;vr::diorama::update(other);
        if(!require(vr::diorama::diorama_stats().terrain_cells==0,"unknown capture identity clears authored terrain")) return selftest_fail(passed,"terrain cache");
        vr::diorama::set_overrides(prior);vr::diorama::set_build_mode(mode);vr::diorama::update(a.snap);
    }
    std::fprintf(stdout,
                 "[gui-selftest] PASS checks=%d baseline=f3a1bd5a56a98d41 "
                 "preview=fe1e4771b3a1ff19 output=%s\n",
                 passed, a.out_path.c_str());
    return 0;
}

// ── The visual probe ─────────────────────────────────────────────────────────
//
// WHAT --selftest COULD NOT DO, AND WHY THIS EXISTS.
//
//   --selftest projects cell 21,0 to a pixel with project() and feeds that pixel
//   back through pick_cell(). Both sides of that round trip are code under test,
//   so a correlated error in camera_vp/project and camera_basis/pick_cell passes
//   it. The handoff says so in its own words: a sampled consistency check, not
//   independent visual proof.
//
//   This probe changes two things. The click pixel is a LITERAL in a checked-in
//   scenario file — chosen once by eye from a captured frame, never recomputed —
//   so project() is no longer in the click path and drift in either projector
//   now moves the resolved cell off 21,0 and fails. And the frame is written out
//   as a PNG, so the remaining judgement ("is the outline actually on the
//   house") is one Read away for a human or an agent with vision.
//
// WHAT IT DOES AND DOES NOT CLAIM. It exercises the production input and
// rendering path, records the resulting semantic state, and checks that the
// expected selection-colour pixels appear near the projected outline. An agent
// then inspects the PNG for the remaining visual judgement. It does NOT claim
// the picture and the sidecar cannot disagree — they share production state and
// could share a correlated bug. The one-time human confirmation of this probe's
// own frozen frame is therefore load-bearing, and the earlier confirmation of
// the INTERACTIVE window does not stand in for it, because this pins a
// different viewport and camera.
//
// WHY SYNTHETIC SDL EVENTS RATHER THAN A SHARED HANDLER. The interactive loop
// below never reads SDL events to decide anything: it hands them to
// ImGui_ImplSDL2_ProcessEvent and then reads ImGuiIO. So SDL_PushEvent walks the
// whole chain — ProcessEvent, ImGui's input queue, NewFrame's mouse-state
// derivation, the WantCaptureMouse gate, the drag-versus-click test, pick_cell,
// select_cell — with the loop unmodified. Refactoring the handler into a
// function the runner could call instead would be MORE code and LESS coverage:
// it would bypass ProcessEvent and NewFrame, which are exactly the two parts
// injection proves. That is what makes this the smallest non-vacuous option.

// The schedule, and it is a schedule rather than a sequence of calls because
// ImGui's input has a one-frame latency: the input block of iteration k runs
// BEFORE NewFrame() of iteration k, so it sees the state NewFrame(k-1) left.
// Events pushed at the top of iteration k are applied by NewFrame(k) and become
// visible to the input block of iteration k+1.
enum : int {
    kItClickMotion = 0,   // move to the click pixel
    kItClickDown   = 1,   //   input block here first sees MousePos == click_px
    kItClickUp     = 2,   //   input block here sees IsMouseClicked -> press_
    kItParkMotion  = 3,   //   input block here releases -> pick_cell -> select
    kItSettle      = 4,   // hover has moved away from the selection
    kItCapture     = 5,   // steady: CAPTURE
    kItNegAMotion = 6, kItNegADown = 9, kItNegAUp = 10, kItNegARecord = 12,
    kItNegBMotion = 13, kItNegBRecord = 15, kItNegCClick = 16, kItNegCRecord = 18,
    kItChromeMotion = 19, kItChromeDown = 20, kItChromeUp = 21,
    kItChromeWheel = 22, kItChromeRecord = 24,
    kItPanMotion = 25, kItPanDown = 26, kItPanDrag = 27, kItPanUp = 28, kItPanRecord = 30,
    kItOrbitMotion = 31, kItOrbitDown = 32, kItOrbitDrag = 33,
    kItOrbitOutside = 34, kItOrbitUp = 35, kItOrbitRecord = 37,
    kItReleasedMotion = 38, kItReleasedRecord = 40,
    kItEraseRecord = 48, kItUndoRecord = 52, kItRedoRecord = 56, kItCancelRecord = 63,
    kItMaskBefore = 69, kItMaskPaint = 75, kItMaskUndo = 79, kItMaskRedo = 83,
    kItMaskRestore = 90, kItMaskCancel = 100, kItModelPreview = 105,
    kItPartAdded=111, kItPartEdited=122, kItPartUndo=126, kItPartRedo=130,
    kItHandleGuide=134,kItHandleApplied=137,kItHandleUndo=141,kItHandleRedo=145,kItHandleCancelled=157,
    kItDiorama=163,kItDioramaOrbit=171,kItBackToSegment=176,
    kProbeFrames = 177
};

// The selection outline's colour, as three channels, so the pixel check can be
// written against the same number the draw call uses.
constexpr int kSelR = 255, kSelG = 210, kSelB = 60;
constexpr int kSelTolerance = 32;   // per channel
constexpr int kSelCornerSkip = 4;   // pixels of each edge end left unsampled
constexpr double kSelMinRatio = 0.80;

struct ProbeScenario {
    std::string id;
    int         vw = 1600, vh = 950;
    float       framebuffer_scale = 1.0f;
    Camera      cam;
    int click_px[2] = {0, 0};
    int park_px[2]  = {0, 0};
    int neg_click_px[2]    = {0, 0};
    int motion_only_px[2]  = {0, 0};
    int bogus_window_px[2] = {0, 0};

    int         exp_cell[2] = {21, 0};
    int         exp_surface = 0;
    int         exp_x = 0, exp_y = 0;
    int         exp_w = 4, exp_rows = 3, exp_extent = 4;
    std::string exp_class = "structure";
    bool        exp_door = true, exp_authored = false;
    std::string exp_geom;
};

bool pair_px(const vr::json::Value* v, int out[2]) {
    if (!v || !v->is_array() || v->items.size() != 2 ||
        v->items[0].type != vr::json::Value::Type::Number ||
        v->items[1].type != vr::json::Value::Type::Number)
        return false;
    out[0] = v->items[0].as_int();
    out[1] = v->items[1].as_int();
    return true;
}

float num_or(const vr::json::Value* o, const char* key, float def) {
    const vr::json::Value* v = o ? o->find(key) : nullptr;
    return (v && v->type == vr::json::Value::Type::Number)
               ? static_cast<float>(v->number)
               : def;
}

// STRICT JSON, no comments: json_scan is a plain reader and the committed
// scenario is not the place to discover that. Every coordinate is required and
// must be non-zero — a scenario still carrying its placeholders would otherwise
// click the top-left corner and report a confident answer about nothing.
bool load_scenario(const char* path, ProbeScenario* sc) {
    vr::json::Value root;
    if (!vr::json::parse_file(path, &root)) return false;
    if (!root.is_object()) {
        std::fprintf(stderr, "[visual-probe] %s: not a JSON object\n", path);
        return false;
    }
    const vr::json::Value* ver = root.find("version");
    if (!ver || ver->as_int(0) != 1) {
        std::fprintf(stderr, "[visual-probe] %s: version must be 1\n", path);
        return false;
    }
    const vr::json::Value* id = root.find("id");
    if (!id || id->type != vr::json::Value::Type::String || id->string.empty()) {
        std::fprintf(stderr, "[visual-probe] %s: id must be a non-empty string\n",
                     path);
        return false;
    }
    sc->id = id->string;

    const vr::json::Value* vp = root.find("viewport");
    const vr::json::Value* vpw = vp ? vp->find("w") : nullptr;
    const vr::json::Value* vph = vp ? vp->find("h") : nullptr;
    if (!vp || !vp->is_object() || !vpw || !vph ||
        vpw->type != vr::json::Value::Type::Number ||
        vph->type != vr::json::Value::Type::Number) {
        std::fprintf(stderr,
                     "[visual-probe] %s: viewport.w and viewport.h are required numbers\n",
                     path);
        return false;
    }
    sc->vw = vpw->as_int();
    sc->vh = vph->as_int();
    sc->framebuffer_scale = num_or(vp, "framebuffer_scale", 1.0f);
    if (!std::isfinite(sc->framebuffer_scale) || sc->framebuffer_scale < 1 ||
        sc->framebuffer_scale > 2) return false;
    if (sc->vw < 64 || sc->vh < 64 || sc->vw > 8192 || sc->vh > 8192) {
        std::fprintf(stderr,
                     "[visual-probe] %s: viewport must be between 64 and 8192\n",
                     path);
        return false;
    }
    const vr::json::Value* c = root.find("camera");
    const char* camera_keys[] = {"yaw", "pitch", "dist", "tx", "ty", "tz"};
    if (!c || !c->is_object()) {
        std::fprintf(stderr, "[visual-probe] %s: camera object is required\n", path);
        return false;
    }
    for (const char* key : camera_keys) {
        const vr::json::Value* v = c->find(key);
        if (!v || v->type != vr::json::Value::Type::Number) {
            std::fprintf(stderr,
                         "[visual-probe] %s: camera.%s is a required number\n",
                         path, key);
            return false;
        }
    }
    sc->cam.yaw   = num_or(c, "yaw",   sc->cam.yaw);
    sc->cam.pitch = num_or(c, "pitch", sc->cam.pitch);
    sc->cam.dist  = num_or(c, "dist",  sc->cam.dist);
    sc->cam.tx    = num_or(c, "tx",    sc->cam.tx);
    sc->cam.ty    = num_or(c, "ty",    sc->cam.ty);
    sc->cam.tz    = num_or(c, "tz",    sc->cam.tz);
    if (!std::isfinite(sc->cam.yaw) || !std::isfinite(sc->cam.pitch) ||
        !std::isfinite(sc->cam.dist) || !std::isfinite(sc->cam.tx) ||
        !std::isfinite(sc->cam.ty) || !std::isfinite(sc->cam.tz) ||
        sc->cam.dist <= 0.0f) {
        std::fprintf(stderr,
                     "[visual-probe] %s: camera values must be finite and dist positive\n",
                     path);
        return false;
    }

    struct { const char* key; int* dst; } pairs[] = {
        {"click_px", sc->click_px}, {"park_px", sc->park_px},
    };
    for (const auto& p : pairs) {
        if (!pair_px(root.find(p.key), p.dst)) {
            std::fprintf(stderr, "[visual-probe] %s: %s must be [x, y]\n", path,
                         p.key);
            return false;
        }
    }
    const vr::json::Value* neg = root.find("negative");
    if (!pair_px(neg ? neg->find("click_px") : nullptr, sc->neg_click_px) ||
        !pair_px(neg ? neg->find("motion_only_px") : nullptr,
                 sc->motion_only_px) ||
        !pair_px(neg ? neg->find("bogus_window_px") : nullptr,
                 sc->bogus_window_px)) {
        std::fprintf(stderr,
                     "[visual-probe] %s: negative needs click_px, "
                     "motion_only_px and bogus_window_px\n", path);
        return false;
    }

    const int* all[] = {sc->click_px, sc->park_px, sc->neg_click_px,
                        sc->motion_only_px, sc->bogus_window_px};
    for (const int* p : all) {
        if (p[0] <= 0 || p[1] <= 0 || p[0] >= sc->vw || p[1] >= sc->vh) {
            std::fprintf(stderr,
                         "[visual-probe] %s: every coordinate must be inside "
                         "the viewport and non-zero; found (%d,%d)\n",
                         path, p[0], p[1]);
            return false;
        }
    }

    const vr::json::Value* ex = root.find("expect");
    sc->exp_surface = int(num_or(ex, "surface", 0));
    if (sc->exp_surface < 0 || sc->exp_surface > 2) return false;
    if (!ex || !pair_px(ex->find("cell"), sc->exp_cell)) {
        std::fprintf(stderr, "[visual-probe] %s: expect.cell must be [x, y]\n",
                     path);
        return false;
    }
    const vr::json::Value* geom = ex->find("geom");
    const vr::json::Value* o = ex->find("object");
    auto number = [&](const char* key) -> const vr::json::Value* {
        const vr::json::Value* v = o ? o->find(key) : nullptr;
        return v && v->type == vr::json::Value::Type::Number ? v : nullptr;
    };
    const vr::json::Value* ox = number("x");
    const vr::json::Value* oy = number("y");
    const vr::json::Value* ow = number("w");
    const vr::json::Value* rows = number("footprint_rows");
    const vr::json::Value* extent = number("extent");
    const vr::json::Value* cls = o ? o->find("class") : nullptr;
    const vr::json::Value* door = o ? o->find("has_door") : nullptr;
    const vr::json::Value* authored = o ? o->find("authored") : nullptr;
    if (!geom || geom->type != vr::json::Value::Type::String ||
        geom->string.size() != 16 || !ox || !oy || !ow || !rows || !extent ||
        !cls || cls->type != vr::json::Value::Type::String ||
        !door || door->type != vr::json::Value::Type::Bool ||
        !authored || authored->type != vr::json::Value::Type::Bool) {
        std::fprintf(stderr,
                     "[visual-probe] %s: expect.object and 16-digit geom must be complete and typed\n",
                     path);
        return false;
    }
    for (char c : geom->string)
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            std::fprintf(stderr, "[visual-probe] %s: expect.geom must be hexadecimal\n",
                         path);
            return false;
        }
    sc->exp_geom = geom->string;
    sc->exp_x = ox->as_int(); sc->exp_y = oy->as_int();
    sc->exp_w = ow->as_int(); sc->exp_rows = rows->as_int();
    sc->exp_extent = extent->as_int(); sc->exp_class = cls->string;
    sc->exp_door = door->boolean; sc->exp_authored = authored->boolean;
    if (sc->exp_w <= 0 || sc->exp_rows <= 0 || sc->exp_extent <= 0 ||
        sc->exp_class.empty()) {
        std::fprintf(stderr, "[visual-probe] %s: expected object dimensions and class are invalid\n",
                     path);
        return false;
    }
    return true;
}

struct FrameObs {
    size_t raised_instances=0;
    std::string room_hash;
    bool handle_active=false;
    uint64_t preview_hash=0;
    int mode = 0, preview_vertices = 0;
    bool mask_stroke = false;
    Pattern draft;
    size_t undo_count = 0;
    bool membership_stroke = false;
    int   it = 0;
    float mouse_x = 0.0f, mouse_y = 0.0f;
    bool  want_capture_mouse = false;
    int surface = 0;
    Camera camera;
    float pan_x = 0, pan_y = 0;
    int drag_surface = 0;
    bool panning = false, middle_down = false;
    bool  clicked = false;
    bool  button_down = false;
    int   sel_x = -1, sel_y = -1, sel_obj = -1;
    bool  hovering = false;
    int   hover_x = 0, hover_y = 0, hover_obj = -1;
};

struct PanelObs {
    bool  submitted = false, open = false, collapsed = true;
    float px = 0, py = 0, sw = 0, sh = 0;
};

struct NegObs {
    bool recorded = false;
    int  cell[2] = {-1, -1};
    int  obj = -1;
    bool button_down = false;
    int  hover_x = 0, hover_y = 0, hover_obj = -1;
};

struct EventRec {
    int         it = 0;
    const char* type = "";
    int         px = 0, py = 0;
    unsigned    window_id = 0;
};

struct Probe {
    vr::json::Value showcase;
    bool showcasing = false;
    std::FILE* recording = nullptr;
    std::FILE* checkpoints = nullptr;
    bool showcase_ok = true;
    int checkpoint_count = 0;
    const ProbeScenario* sc = nullptr;
    std::string png_path, json_path;
    unsigned    window_id = 0;

    GLuint fbo = 0, color_rb = 0, depth_rb = 0;
    GLenum fb_status = 0;
    int    fbw = 0, fbh = 0;

    int it = 0;

    std::vector<EventRec>      events;
    std::vector<FrameObs>      obs;
    std::vector<OutlineRecord> outlines;
    PanelObs                   panel_scene, panel_selection, panel_groups, panel_workspace;
    int selected_row = -1;
    bool auxiliary_images_ok = true;
    int auxiliary_images = 0;
    Camera capture_camera;
    ObjectModel capture_model;
    std::string capture_stats;
    NegObs                     neg[3];

    // Observed at capture time.
    bool                 captured = false;
    std::vector<uint8_t> rgb;              // top-down, as written to the PNG
    int win_w = 0, win_h = 0, draw_w = 0, draw_h = 0;
    float disp_w = 0, disp_h = 0, fb_scale_x = 0, fb_scale_y = 0;
};

Probe* g_probe = nullptr;

// ── Injection ────────────────────────────────────────────────────────────────
//
// windowID IS NOT OPTIONAL. ImGui_ImplSDL2_ProcessEvent looks the viewport up by
// it and returns false for anything it does not recognise, so an event with the
// wrong id is silently dropped. That is also why negative case C can prove these
// events genuinely traverse the backend: it pushes one with a foreign id and
// requires nothing to happen.
void push_motion(Probe& p, unsigned window_id, int x, int y, int rx=0, int ry=0) {
    SDL_Event e{};
    e.type            = SDL_MOUSEMOTION;
    e.motion.type     = SDL_MOUSEMOTION;
    e.motion.windowID = window_id;
    e.motion.which    = 0;
    e.motion.state    = 0;
    e.motion.x        = x;
    e.motion.y        = y;
    e.motion.xrel     = rx;
    e.motion.yrel     = ry;
    SDL_PushEvent(&e);
    p.events.push_back({p.it, "motion", x, y, window_id});
}

void push_button(Probe& p, unsigned window_id, int x, int y, bool down,
                 uint8_t button = SDL_BUTTON_LEFT) {
    SDL_Event e{};
    e.type            = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.type     = e.type;
    e.button.windowID = window_id;
    e.button.which    = 0;
    e.button.button   = button;
    e.button.state    = down ? SDL_PRESSED : SDL_RELEASED;
    e.button.clicks   = 1;
    e.button.x        = x;
    e.button.y        = y;
    SDL_PushEvent(&e);
    p.events.push_back({p.it, down ? "button_down" : "button_up", x, y,
                        window_id});
}

void push_key(Probe& p, SDL_Scancode scancode, SDL_Keycode sym, bool down, Uint16 mod) {
    SDL_Event e{}; e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.windowID = p.window_id; e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.scancode = scancode; e.key.keysym.sym = sym; e.key.keysym.mod = mod;
    SDL_PushEvent(&e);
    p.events.push_back({p.it, down ? "key-down" : "key-up", int(scancode), int(mod), p.window_id});
}

void probe_pump(Probe& p) {
    if (p.showcasing) {
        for (const auto& event : p.showcase.find("events")->items) {
            if (event.find("frame")->as_int() != p.it) continue;
            const auto& type = event.find("type")->string;
            const int x = int(num_or(&event,"x",0)), y = int(num_or(&event,"y",0));
            if(type=="motion") push_motion(p,p.window_id,x,y,int(num_or(&event,"rx",0)),int(num_or(&event,"ry",0)));
            else if(type=="down" || type=="up") push_button(p,p.window_id,x,y,type=="down",uint8_t(num_or(&event,"button",SDL_BUTTON_LEFT)));
            else if(type=="wheel") {
                SDL_Event e{};e.type=SDL_MOUSEWHEEL;e.wheel.windowID=p.window_id;
                e.wheel.y=int(num_or(&event,"amount",0));
#if SDL_VERSION_ATLEAST(2,0,18)
                e.wheel.preciseY=float(e.wheel.y);
#endif
                SDL_PushEvent(&e);
            } else if(type=="focus-lost") {
                SDL_Event e{};e.type=SDL_WINDOWEVENT;e.window.windowID=p.window_id;
                e.window.event=SDL_WINDOWEVENT_FOCUS_LOST;SDL_PushEvent(&e);
            }
            else if(type=="key-down" || type=="key-up") {
                const auto scan=SDL_Scancode(int(num_or(&event,"scan",0)));
                push_key(p,scan,SDL_GetKeyFromScancode(scan),type=="key-down",Uint16(num_or(&event,"mod",0)));
            } else if(type=="text") {
                SDL_Event e{};e.type=SDL_TEXTINPUT;e.text.windowID=p.window_id;
                std::snprintf(e.text.text,sizeof(e.text.text),"%s",event.find("text")->string.c_str());SDL_PushEvent(&e);
            } else if(type=="quit") { SDL_Event e{};e.type=SDL_QUIT;SDL_PushEvent(&e); }
        }
        return;
    }
    const ProbeScenario& s = *p.sc;
    const unsigned id = p.window_id;
    const auto layout = studio::ShellLayout::at(s.vw, s.vh);
    const int map_x = int(layout.map.x + 200), map_y = int(layout.map.y + 100);
    const int view_x = int(layout.view.x + layout.view.w / 2);
    const int view_y = int(layout.view.y + layout.view.h / 2);
    // Blue Z endpoint frozen after inspecting 1280x720 and 1600x950 parts PNGs.
    const int handle_x=s.vw==1280?537:664,handle_y=s.vw==1280?586:788;
    switch (p.it) {
        case kItClickMotion: push_motion(p, id, s.click_px[0], s.click_px[1]); break;
        case kItClickDown:   push_button(p, id, s.click_px[0], s.click_px[1], true);  break;
        case kItClickUp:     push_button(p, id, s.click_px[0], s.click_px[1], false); break;
        case kItParkMotion:  push_motion(p, id, s.park_px[0], s.park_px[1]); break;

        case kItNegAMotion: push_key(p, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, true, KMOD_NONE); break;
        case 7: push_key(p, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, false, KMOD_NONE); break;
        case 8: push_motion(p, id, s.neg_click_px[0], s.neg_click_px[1]); break;
        case kItNegADown:   push_button(p, id, s.neg_click_px[0], s.neg_click_px[1], true);  break;
        case kItNegAUp:     push_button(p, id, s.neg_click_px[0], s.neg_click_px[1], false); break;

        case kItNegBMotion: push_motion(p, id, s.motion_only_px[0], s.motion_only_px[1]); break;

        // Negative C: a complete, well-formed click carrying a window id this
        // process does not own. ProcessEvent must drop both halves.
        case kItNegCClick:
            push_button(p, 0xFFFFu, s.bogus_window_px[0], s.bogus_window_px[1], true);
            push_button(p, 0xFFFFu, s.bogus_window_px[0], s.bogus_window_px[1], false);
            break;
        // Interaction checks use known shell surfaces, independently of the
        // fixed scene pixel. They verify routing/capture, not projector accuracy.
        // The old output field is now the primary next-step button. Use the
        // inert brand area for this chrome-routing check, not a navigation action.
        case kItChromeMotion: push_motion(p, id, 80, 18); break;
        case kItChromeDown: push_button(p, id, 80, 18, true); break;
        case kItChromeUp: push_button(p, id, 80, 18, false); break;
        case kItChromeWheel: {
            SDL_Event e{}; e.type = SDL_MOUSEWHEEL; e.wheel.windowID = id;
            e.wheel.y = 1; e.wheel.preciseY = 1; SDL_PushEvent(&e);
            p.events.push_back({p.it, "wheel", 80, 18, id}); break;
        }
        case kItPanMotion: push_motion(p, id, map_x, map_y); break;
        case kItPanDown: push_button(p, id, map_x, map_y, true, SDL_BUTTON_MIDDLE); break;
        case kItPanDrag: push_motion(p, id, map_x, map_y - 40); break;
        case kItPanUp: push_button(p, id, map_x, map_y - 40, false, SDL_BUTTON_MIDDLE); break;
        case kItOrbitMotion: push_motion(p, id, view_x, view_y); break;
        case kItOrbitDown: push_button(p, id, view_x, view_y, true); break;
        case kItOrbitDrag: push_motion(p, id, view_x + 16, view_y + 8); break;
        case kItOrbitOutside: push_motion(p, id, 100, 100); break;
        case kItOrbitUp: push_button(p, id, 100, 100, false); break;
        case kItReleasedMotion: push_motion(p, id, view_x, view_y); break;
        // Frozen map pixels after the measured 40-pixel pan: two bottom-row
        // cells of the selected west Pokemon Centre, across every shell size.
        case 41: push_key(p, SDL_SCANCODE_LSHIFT, SDLK_LSHIFT, true, KMOD_LSHIFT); break;
        case 42: push_motion(p, id, 424, 124); break;
        case 43: push_button(p, id, 424, 124, true); break;
        case 44: push_motion(p, id, 440, 124); break;
        case 45: push_button(p, id, 440, 124, false); break;
        case 46: push_key(p, SDL_SCANCODE_LSHIFT, SDLK_LSHIFT, false, KMOD_NONE); break;
        case 49: push_key(p, SDL_SCANCODE_Z, SDLK_z, true, KMOD_LCTRL); break;
        case 50: push_key(p, SDL_SCANCODE_Z, SDLK_z, false, KMOD_NONE); break;
        case 53: push_key(p, SDL_SCANCODE_Y, SDLK_y, true, KMOD_LCTRL); break;
        case 54: push_key(p, SDL_SCANCODE_Y, SDLK_y, false, KMOD_NONE); break;
        case 57: push_motion(p, id, 424, 124); break;
        case 58: push_button(p, id, 424, 124, true); break;
        case 59: push_key(p, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, true, KMOD_NONE); break;
        case 60: push_key(p, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, false, KMOD_NONE); break;
        case 61: push_button(p, id, 424, 124, false); break;
        case 64: push_motion(p,id,534,18); break;
        case 65: push_button(p,id,534,18,true); break;
        case 66: push_button(p,id,534,18,false); break;
        case 67: push_motion(p,id,600,200); break;
        // Canvas inspected at all shell sizes: left=window-252, top=260,
        // 64x64 source pixels displayed at 3.5x. Erase pixels 8..16 of row 8.
        case 70: push_motion(p,id,s.vw-222,290); break;
        case 71: push_button(p,id,s.vw-222,290,true); break;
        case 72: push_motion(p,id,s.vw-194,290); break;
        case 73: push_button(p,id,s.vw-194,290,false); break;
        case 76: push_key(p,SDL_SCANCODE_Z,SDLK_z,true,KMOD_LCTRL); break;
        case 77: push_key(p,SDL_SCANCODE_Z,SDLK_z,false,KMOD_NONE); break;
        case 80: push_key(p,SDL_SCANCODE_Y,SDLK_y,true,KMOD_LCTRL); break;
        case 81: push_key(p,SDL_SCANCODE_Y,SDLK_y,false,KMOD_NONE); break;
        case 84: push_key(p,SDL_SCANCODE_R,SDLK_r,true,KMOD_NONE); break;
        case 85: push_key(p,SDL_SCANCODE_R,SDLK_r,false,KMOD_NONE); break;
        case 86: push_motion(p,id,s.vw-222,290); break;
        case 87: push_button(p,id,s.vw-222,290,true); break;
        case 88: push_button(p,id,s.vw-222,290,false); break;
        case 91: push_key(p,SDL_SCANCODE_E,SDLK_e,true,KMOD_NONE); break;
        case 92: push_key(p,SDL_SCANCODE_E,SDLK_e,false,KMOD_NONE); break;
        case 94: push_button(p,id,s.vw-222,290,true); break;
        case 95: push_motion(p,id,s.vw-194,290); break;
        case 96: push_key(p,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE,true,KMOD_NONE); break;
        case 97: push_key(p,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE,false,KMOD_NONE); break;
        case 98: push_button(p,id,s.vw-194,290,false); break;
        case 101: push_motion(p,id,572,18); break;
        case 102: push_button(p,id,572,18,true); break;
        case 103: push_button(p,id,572,18,false); break;
        // Frozen inspector pixels, checked against the minimum-size MODEL PNG.
        case 106: push_motion(p,id,s.vw-230,320); break;
        case 107: push_button(p,id,s.vw-230,320,true); break;
        case 108: push_button(p,id,s.vw-230,320,false); break;
        case 109: push_motion(p,id,600,200); break;
        case 112: push_motion(p,id,s.vw-230,435); break;
        case 113: push_button(p,id,s.vw-230,435,true); break;
        case 114: push_button(p,id,s.vw-230,435,false); break;
        case 115: push_key(p,SDL_SCANCODE_A,SDLK_a,true,KMOD_LCTRL); break;
        case 116: push_key(p,SDL_SCANCODE_A,SDLK_a,false,KMOD_NONE); break;
        case 117: { SDL_Event e{};e.type=SDL_TEXTINPUT;e.text.windowID=id;std::strcpy(e.text.text,"0.5");SDL_PushEvent(&e);break; }
        case 118: push_key(p,SDL_SCANCODE_RETURN,SDLK_RETURN,true,KMOD_NONE); break;
        case 119: push_key(p,SDL_SCANCODE_RETURN,SDLK_RETURN,false,KMOD_NONE); break;
        case 120: push_motion(p,id,600,200); break;
        case 123: push_key(p,SDL_SCANCODE_Z,SDLK_z,true,KMOD_LCTRL); break;
        case 124: push_key(p,SDL_SCANCODE_Z,SDLK_z,false,KMOD_NONE); break;
        case 127: push_key(p,SDL_SCANCODE_Y,SDLK_y,true,KMOD_LCTRL); break;
        case 128: push_key(p,SDL_SCANCODE_Y,SDLK_y,false,KMOD_NONE); break;
        case 131: push_motion(p,id,handle_x,handle_y); break;
        case 132: push_button(p,id,handle_x,handle_y,true); break;
        case 133: push_motion(p,id,handle_x-25,handle_y+15); break;
        case 135: push_button(p,id,handle_x-25,handle_y+15,false); break;
        case 138: push_key(p,SDL_SCANCODE_Z,SDLK_z,true,KMOD_LCTRL); break;
        case 139: push_key(p,SDL_SCANCODE_Z,SDLK_z,false,KMOD_NONE); break;
        case 142: push_key(p,SDL_SCANCODE_Y,SDLK_y,true,KMOD_LCTRL); break;
        case 143: push_key(p,SDL_SCANCODE_Y,SDLK_y,false,KMOD_NONE); break;
        case 146: push_key(p,SDL_SCANCODE_Z,SDLK_z,true,KMOD_LCTRL); break;
        case 147: push_key(p,SDL_SCANCODE_Z,SDLK_z,false,KMOD_NONE); break;
        case 150: push_motion(p,id,handle_x,handle_y); break;
        case 151: push_button(p,id,handle_x,handle_y,true); break;
        case 152: push_motion(p,id,16,s.vh-5); break;
        case 153: push_key(p,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE,true,KMOD_NONE); break;
        case 154: push_key(p,SDL_SCANCODE_ESCAPE,SDLK_ESCAPE,false,KMOD_NONE); break;
        case 155: push_button(p,id,16,s.vh-5,false); break;
        case 158: push_motion(p,id,630,18); break;
        case 159: push_button(p,id,630,18,true); break;
        case 160: push_button(p,id,630,18,false); break;
        case 161: push_motion(p,id,600,200); break;
        case 165: push_motion(p,id,view_x,view_y); break;
        case 166: push_button(p,id,view_x,view_y,true); break;
        case 167: push_motion(p,id,view_x+60,view_y+20); break;
        case 168: push_button(p,id,view_x+60,view_y+20,false); break;
        case 172: push_motion(p,id,471,18); break;
        case 173: push_button(p,id,471,18,true); break;
        case 174: push_button(p,id,471,18,false); break;
        default: break;
    }
}

void probe_observe(Probe& p, const ImGuiIO& io, const App& a, bool hovering,
                   int hover_x, int hover_y) {
    if(p.showcasing) return; // Showcase checkpoints are observed after the UI draws.
    if (p.it == kItCapture) { p.capture_model = a.model; p.capture_stats = a.last_stats; }
    FrameObs o;
    o.raised_instances=vr::diorama::diorama_stats().raised_instances;o.room_hash=stats_value(a.last_stats,"geom");
    o.handle_active=a.handle.active;o.preview_hash=a.preview_hash;
    o.mode = a.mode; o.mask_stroke = a.mask_stroke; o.preview_vertices = a.preview_vertices;
    o.draft = a.draft; o.undo_count = a.document.undo_count(); o.membership_stroke = a.membership_stroke;
    o.it       = p.it;
    o.mouse_x  = io.MousePos.x;
    o.mouse_y  = io.MousePos.y;
    o.want_capture_mouse = io.WantCaptureMouse;
    o.surface = a.mouse_surface;
    o.camera = a.camera;
    o.pan_x = a.pan_x; o.pan_y = a.pan_y;
    o.drag_surface = a.drag_surface; o.panning = a.panning;
    o.middle_down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    o.clicked  = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    o.button_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    o.sel_x = a.sel_x; o.sel_y = a.sel_y; o.sel_obj = a.sel_obj;
    o.hovering = hovering;
    o.hover_x = hover_x; o.hover_y = hover_y;
    o.hover_obj = hovering ? a.model.object_at(hover_x, hover_y) : -1;
    p.obs.push_back(o);

    int slot = -1;
    if (p.it == kItNegARecord) slot = 0;
    if (p.it == kItNegBRecord) slot = 1;
    if (p.it == kItNegCRecord) slot = 2;
    if (slot >= 0) {
        NegObs& n = p.neg[slot];
        n.recorded = true;
        n.cell[0] = a.sel_x; n.cell[1] = a.sel_y;
        n.obj = a.sel_obj;
        n.button_down = o.button_down;
        n.hover_x = hover_x; n.hover_y = hover_y; n.hover_obj = o.hover_obj;
    }

    // A running trace of where each injected pixel actually landed. This uses
    // pick_cell, which is the thing under test — that is the point. The scenario
    // pins a PIXEL; which cell it resolves to is an output, and printing it is
    // how the pixel gets chosen in the first place without asking project().
    if (hovering) {
        std::fprintf(stderr,
                     "[visual-probe] it=%d px=(%.0f,%.0f) -> cell %d,%d obj #%d"
                     "  sel=%d,%d #%d\n",
                     p.it, o.mouse_x, o.mouse_y, hover_x, hover_y, o.hover_obj,
                     a.sel_x, a.sel_y, a.sel_obj);
    }
}

// ── Framebuffer ──────────────────────────────────────────────────────────────
//
// AN FBO, NOT THE WINDOW'S BACK BUFFER, and the reason is evidence rather than
// preference. vr_layer.cpp brings up a 64x64 HIDDEN window and renderer.cpp
// renders every contact sheet and turntable this project has produced into an
// offscreen FBO much larger than it. Offscreen rendering from a hidden window is
// therefore a proven path here; a hidden window's default framebuffer is not.
// Keeping the window hidden also keeps SDL_GetKeyboardFocus() != our window,
// which is what stops imgui_impl_sdl2's UpdateMouseData from overwriting the
// injected mouse position with the real cursor's.
//
// The cost is stated plainly: this cannot prove the window PRESENTS correctly.
// Only the bound framebuffer differs from the interactive path.
bool probe_fbo_create(Probe& p, int w, int h) {
    p.fbw = w; p.fbh = h;

    vr::gl::glGenRenderbuffers(1, &p.color_rb);
    vr::gl::glBindRenderbuffer(GL_RENDERBUFFER, p.color_rb);
    vr::gl::glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);

    vr::gl::glGenRenderbuffers(1, &p.depth_rb);
    vr::gl::glBindRenderbuffer(GL_RENDERBUFFER, p.depth_rb);
    vr::gl::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    vr::gl::glBindRenderbuffer(GL_RENDERBUFFER, 0);

    vr::gl::glGenFramebuffers(1, &p.fbo);
    vr::gl::glBindFramebuffer(GL_FRAMEBUFFER, p.fbo);
    vr::gl::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_RENDERBUFFER, p.color_rb);
    vr::gl::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                      GL_RENDERBUFFER, p.depth_rb);
    p.fb_status = vr::gl::glCheckFramebufferStatus(GL_FRAMEBUFFER);
    vr::gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (p.fb_status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "[visual-probe] offscreen FBO incomplete: 0x%X\n",
                     static_cast<unsigned>(p.fb_status));
        return false;
    }
    return true;
}

// Called on BOTH the success and failure paths, so a probe that fails an
// assertion still leaves the context clean for the shutdown below it.
void probe_fbo_destroy(Probe& p) {
    vr::gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (p.fbo)      vr::gl::glDeleteFramebuffers(1, &p.fbo);
    if (p.color_rb) vr::gl::glDeleteRenderbuffers(1, &p.color_rb);
    if (p.depth_rb) vr::gl::glDeleteRenderbuffers(1, &p.depth_rb);
    p.fbo = p.color_rb = p.depth_rb = 0;
}

// Read the frame that was just drawn. Every piece of GL state this touches is
// saved and restored, in the order that keeps each restore legal: the read
// buffer is a per-framebuffer selector, so the framebuffer binding has to go
// back first or GL_BACK would be set on an FBO that has no such buffer.
void probe_read_frame(Probe& p) {
    GLint prev_fb = 0, prev_read = 0, prev_align = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fb);
    glGetIntegerv(GL_READ_BUFFER, &prev_read);
    glGetIntegerv(GL_PACK_ALIGNMENT, &prev_align);

    vr::gl::glBindFramebuffer(GL_FRAMEBUFFER, p.fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    // 1600 RGB pixels happen to be a multiple of four bytes, so the default
    // alignment of 4 would work here by luck. Setting it is what stops a future
    // odd width from producing a sheared image nobody can explain.
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    const size_t stride = static_cast<size_t>(p.fbw) * 3;
    std::vector<uint8_t> bottom_up(stride * static_cast<size_t>(p.fbh));
    glReadPixels(0, 0, p.fbw, p.fbh, GL_RGB, GL_UNSIGNED_BYTE,
                 bottom_up.data());

    // GL hands back rows bottom-up; PNG wants them top-down. Same flip as
    // renderer.cpp's read_and_write_ppm.
    p.rgb.resize(bottom_up.size());
    for (int y = 0; y < p.fbh; ++y)
        std::memcpy(&p.rgb[static_cast<size_t>(y) * stride],
                    &bottom_up[static_cast<size_t>(p.fbh - 1 - y) * stride],
                    stride);

    glPixelStorei(GL_PACK_ALIGNMENT, prev_align);
    vr::gl::glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fb));
    glReadBuffer(static_cast<GLenum>(prev_read));

    p.captured = true;
}

bool capture_room_png(App& a,const std::string& path) {
    Probe capture;GLint previous_fb=0,previous_view[4]{};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous_fb);glGetIntegerv(GL_VIEWPORT,previous_view);
    if(!probe_fbo_create(capture,1024,768)) {probe_fbo_destroy(capture);return false;}
    const bool measure=vr::diorama::build_mode()==vr::diorama::BuildMode::Diorama;
    double rebuild_ms=0;
    if(measure) {
        // Force the same complete room rebuild and upload used by the game.
        // Exclude earlier GPU work, document copies and GUI segmentation; the
        // final synchronization includes this rebuild's upload completion.
        auto set=effective(a);glFinish();const auto start=SDL_GetPerformanceCounter();
        vr::diorama::set_overrides(std::move(set));vr::diorama::update(a.snap);glFinish();
        rebuild_ms=1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency();
    }
    vr::gl::glBindFramebuffer(GL_FRAMEBUFFER,capture.fbo);glViewport(0,0,1024,768);glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glClearColor(.07f,.08f,.11f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    Camera camera;camera.tx=a.snap.width*.5f;camera.tz=a.snap.height*.5f;camera.yaw=.6f;camera.pitch=.9f;
    camera.dist=std::max(a.snap.width,a.snap.height)*1.25f+6;
    const auto view=camera_vp(camera,1024,768);
    vr::diorama::draw_raw(view,vr::math::identity(),0);
    if(measure) {
        glFinish();const auto start=SDL_GetPerformanceCounter();
        for(int frame=0;frame<16;++frame) {
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            vr::diorama::draw_raw(view,vr::math::identity(),0);glFinish();
        }
        const double draw_ms=1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency()/16;
        const auto& stats=vr::diorama::diorama_stats();
        const auto vertices=stats.flat_vertices+stats.authored_vertices;
        std::fprintf(stdout,"[diorama-performance] room=%dx%d geom=%016llx vertices=%zu triangles=%zu raised=%zu flat_vertices=%zu authored_vertices=%zu rebuild_ms=%.3f draw_1024x768_ms=%.3f samples=16 synchronized=yes local_desktop=yes\n",
            a.snap.width,a.snap.height,static_cast<unsigned long long>(stats.geometry_hash),vertices,vertices/3,
            stats.raised_instances,stats.flat_vertices,stats.authored_vertices,rebuild_ms,draw_ms);
    }
    probe_read_frame(capture);
    const bool written=studio::png::write_rgb(path.c_str(),1024,768,capture.rgb.data());
    probe_fbo_destroy(capture);vr::gl::glBindFramebuffer(GL_FRAMEBUFFER,GLuint(previous_fb));
    glViewport(previous_view[0],previous_view[1],previous_view[2],previous_view[3]);return written;
}

bool capture_cutout_png(App& a, const std::string& path, float yaw) {
    if (!a.draft.cutout && a.draft.parts.empty()) return false;
    Probe capture; GLint previous_fb=0, previous_view[4]{};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous_fb); glGetIntegerv(GL_VIEWPORT,previous_view);
    if (!probe_fbo_create(capture,512,512)) { probe_fbo_destroy(capture); return false; }
    const auto build_start=SDL_GetPerformanceCounter();
    int vertices=0;
    const bool built = vr::diorama::build_cutout_preview(a.snap,a.draft,nullptr,&vertices);
    const double build_ms=1000.0*(SDL_GetPerformanceCounter()-build_start)/SDL_GetPerformanceFrequency();
    vr::gl::glBindFramebuffer(GL_FRAMEBUFFER,capture.fbo);
    glViewport(0,0,512,512); glDisable(GL_SCISSOR_TEST); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glClearColor(.07f,.08f,.11f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    Camera cam; cam.tx=a.draft.w*.5f; cam.ty=a.draft.extent*.5f; cam.tz=0;
    cam.yaw=yaw; cam.pitch=.3f; cam.dist=std::max(a.draft.w,a.draft.extent)*1.1f+1;
    bool framed=true;
    if(!a.draft.parts.empty()) {
        std::vector<vr::diorama::AuthoredVertex> mesh;
        if(vr::diorama::inspect_authored_mesh(a.snap,a.draft,&mesh) && !mesh.empty()) {
            pg::Vec lo=mesh.front().position,hi=lo;
            for(const auto& vertex:mesh) {const auto p=vertex.position;
                lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);
                hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);}
            cam.tx=(lo.x+hi.x)/2;cam.ty=(lo.y+hi.y)/2;cam.tz=(lo.z+hi.z)/2;
            const auto extent=hi-lo;
            cam.dist=.5f*std::sqrt(pg::dot(extent,extent))/std::sin(.52f)*1.12f;
            for(const auto& vertex:mesh) {
                const auto p=vertex.position;ImVec2 pixel;
                framed &= project(camera_vp(cam,512,512),p.x,p.y,p.z,512,512,&pixel) &&
                    pixel.x>=8 && pixel.x<=504 && pixel.y>=8 && pixel.y<=504;
            }
        }
    }
    if (built) vr::diorama::draw_raw(camera_vp(cam,512,512),vr::math::identity(),0);
    if(built && !a.draft.parts.empty()) {
        glFinish(); const auto start=SDL_GetPerformanceCounter();
        for(int frame=0;frame<16;++frame) {
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            vr::diorama::draw_raw(camera_vp(cam,512,512),vr::math::identity(),0);glFinish();
        }
        const double draw_ms=1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency()/16;
        std::fprintf(stdout,"[parts-performance] vertices=%d triangles=%d rebuild_ms=%.3f draw_512_ms=%.3f samples=16 synchronized=yes\n",
            vertices,vertices/3,build_ms,draw_ms);
    }
    probe_read_frame(capture);
    const bool written = built && framed && studio::png::write_rgb(path.c_str(),512,512,capture.rgb.data());
    probe_fbo_destroy(capture); vr::gl::glBindFramebuffer(GL_FRAMEBUFFER,GLuint(previous_fb));
    glViewport(previous_view[0],previous_view[1],previous_view[2],previous_view[3]);
    remesh(a);
    return written;
}

// ── Reading the captured pixels ──────────────────────────────────────────────

uint64_t fnv1a64(const uint8_t* d, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) {
        h ^= d[i];
        h *= 1099511628211ULL;
    }
    return h;
}

// Distinct colours over a fixed grid — every 32nd pixel in x and y, so 50 x 30
// samples at this size. Its whole purpose is to catch a frame that is empty,
// cleared or black; it is not a similarity measure and is never compared
// against a stored value.
int distinct_colours(const std::vector<uint8_t>& rgb, int w, int h, int stride) {
    std::vector<uint32_t> seen;
    for (int y = 0; y < h; y += stride)
        for (int x = 0; x < w; x += stride) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 3;
            seen.push_back((static_cast<uint32_t>(rgb[i]) << 16) |
                           (static_cast<uint32_t>(rgb[i + 1]) << 8) |
                           static_cast<uint32_t>(rgb[i + 2]));
        }
    std::sort(seen.begin(), seen.end());
    seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    return static_cast<int>(seen.size());
}

bool selection_pixel(const std::vector<uint8_t>& rgb, int w, int h, int x,
                     int y) {
    if (x < 0 || y < 0 || x >= w || y >= h) return false;
    const size_t i = (static_cast<size_t>(y) * w + x) * 3;
    return std::abs(static_cast<int>(rgb[i])     - kSelR) <= kSelTolerance &&
           std::abs(static_cast<int>(rgb[i + 1]) - kSelG) <= kSelTolerance &&
           std::abs(static_cast<int>(rgb[i + 2]) - kSelB) <= kSelTolerance;
}

// THE CHECK THAT TIES THE PICTURE TO THE CLAIM. Walk the four edges of the
// polyline outline_rect actually drew and ask whether the frame contains
// selection-coloured pixels there.
//
// It is a STRUCTURAL SMOKE CHECK, not a golden image. Colours are matched with a
// per-channel tolerance because AddPolyline antialiases, and each expected point
// is looked for in a 5x5 neighbourhood because a rasteriser is free to place a
// 3px line a pixel either way. The ends of each edge are skipped: corners are
// where two antialiased strokes overlap and the blend is least predictable.
//
// What it catches: ImGui not rendering at all, a capture taken before
// RenderDrawData, a stale frame, and an outline that is not where the sidecar
// says it is.
void sample_selection_outline(const std::vector<uint8_t>& rgb, int w, int h,
                              const ImVec2 p[4], int* sampled, int* matched) {
    *sampled = 0;
    *matched = 0;
    for (int e = 0; e < 4; ++e) {
        const ImVec2 a = p[e], b = p[(e + 1) % 4];
        const float dx = b.x - a.x, dy = b.y - a.y;
        const int n = static_cast<int>(std::sqrt(dx * dx + dy * dy));
        for (int s = kSelCornerSkip; s <= n - kSelCornerSkip; ++s) {
            const float t  = static_cast<float>(s) / static_cast<float>(n);
            const int   sx = static_cast<int>(a.x + dx * t + 0.5f);
            const int   sy = static_cast<int>(a.y + dy * t + 0.5f);
            ++*sampled;
            bool hit = false;
            for (int oy = -2; oy <= 2 && !hit; ++oy)
                for (int ox = -2; ox <= 2 && !hit; ++ox)
                    if (selection_pixel(rgb, w, h, sx + ox, sy + oy)) hit = true;
            if (hit) ++*matched;
        }
    }
}

const OutlineRecord* find_outline(const Probe& p, const char* role) {
    for (const OutlineRecord& r : p.outlines)
        if (!std::strcmp(r.role, role)) return &r;
    return nullptr;
}

// A 100px tick scale along the top and left edges, probe mode only. It is what
// makes the frozen coordinates choosable BY EYE from the artifact instead of by
// asking project() — which is the whole point of freezing them.
void draw_ruler(ImDrawList* dl, int w, int h) {
    const ImU32 dim = IM_COL32(255, 255, 255, 70);
    const ImU32 lit = IM_COL32(255, 255, 255, 140);
    char label[16];
    for (int x = 0; x < w; x += 100) {
        const bool major = (x % 200) == 0;
        dl->AddLine(ImVec2(static_cast<float>(x), 0.0f),
                    ImVec2(static_cast<float>(x), major ? 12.0f : 6.0f),
                    major ? lit : dim, 1.0f);
        if (major && x > 0) {
            std::snprintf(label, sizeof(label), "%d", x);
            dl->AddText(ImVec2(static_cast<float>(x) + 2.0f, 1.0f), lit, label);
        }
    }
    for (int y = 0; y < h; y += 100) {
        const bool major = (y % 200) == 0;
        dl->AddLine(ImVec2(0.0f, static_cast<float>(y)),
                    ImVec2(major ? 12.0f : 6.0f, static_cast<float>(y)),
                    major ? lit : dim, 1.0f);
        if (major && y > 0) {
            std::snprintf(label, sizeof(label), "%d", y);
            dl->AddText(ImVec2(1.0f, static_cast<float>(y) + 2.0f), lit, label);
        }
    }
}

// ── The sidecar ──────────────────────────────────────────────────────────────
//
// EVERY FIELD IS READ BACK, NEVER THE VALUE THAT WAS INTENDED. The mouse
// position is what ImGui reported on the frame the click was processed, not what
// was injected; the outline polygon comes from the sink inside outline_rect, not
// from a second call to project(); the dimensions come from four separate
// sources so that a disagreement between them IS the finding.
//
// Paths inside the file are relative to the file itself, so the artifact pair
// stays movable. The absolute paths go to the terminal, where an agent needs
// something it can paste straight into a Read.

const char* base_name(const std::string& path) {
    size_t at = path.find_last_of("/\\");
    return at == std::string::npos ? path.c_str() : path.c_str() + at + 1;
}

void json_string(std::FILE* f, const std::string& value) {
    std::fputc('"', f);
    for (unsigned char c : value) {
        switch (c) {
            case '"': std::fputs("\\\"", f); break;
            case '\\': std::fputs("\\\\", f); break;
            case '\b': std::fputs("\\b", f); break;
            case '\f': std::fputs("\\f", f); break;
            case '\n': std::fputs("\\n", f); break;
            case '\r': std::fputs("\\r", f); break;
            case '\t': std::fputs("\\t", f); break;
            default:
                if (c < 0x20) std::fprintf(f, "\\u%04x", c);
                else std::fputc(c, f);
                break;
        }
    }
    std::fputc('"', f);
}

#include "gui_showcase.inl"

void json_outline(std::FILE* f, const OutlineRecord& r, bool last) {
    std::fprintf(f,
                 "    { \"role\": \"%s\", \"cell\": [%d, %d], "
                 "\"size\": [%d, %d], \"colour\": \"%02x%02x%02x\", "
                 "\"thickness\": %.1f, \"poly\": [",
                 r.role, r.x, r.y, r.w, r.h,
                 (r.col >> IM_COL32_R_SHIFT) & 0xFF,
                 (r.col >> IM_COL32_G_SHIFT) & 0xFF,
                 (r.col >> IM_COL32_B_SHIFT) & 0xFF,
                 static_cast<double>(r.thick));
    for (int i = 0; i < 4; ++i)
        std::fprintf(f, "%s[%.2f, %.2f]", i ? ", " : "",
                     static_cast<double>(r.p[i].x),
                     static_cast<double>(r.p[i].y));
    std::fprintf(f, "] }%s\n", last ? "" : ",");
}

void json_panel(std::FILE* f, const char* name, const PanelObs& p, int fbw,
                int fbh, bool last) {
    const bool intersects = p.px < fbw && p.py < fbh && p.px + p.sw > 0 &&
                            p.py + p.sh > 0;
    std::fprintf(f,
                 "    \"%s\": { \"submitted\": %s, \"visible\": %s, "
                 "\"collapsed\": %s, \"pos\": [%.1f, %.1f], "
                 "\"size\": [%.1f, %.1f], \"intersects_framebuffer\": %s }%s\n",
                 name, p.submitted ? "true" : "false", p.open ? "true" : "false",
                 p.collapsed ? "true" : "false", static_cast<double>(p.px),
                 static_cast<double>(p.py), static_cast<double>(p.sw),
                 static_cast<double>(p.sh), intersects ? "true" : "false",
                 last ? "" : ",");
}

bool write_sidecar(const App& a, const Probe& p, const FrameObs& click,
                   const FrameObs& cap, int distinct, int sampled, int matched) {
    std::FILE* f = std::fopen(p.json_path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "[visual-probe] cannot write %s\n",
                     p.json_path.c_str());
        return false;
    }
    const ProbeScenario& s = *p.sc;
    float ex, ey, ez, fx, fy;
    camera_eye(p.capture_camera, &ex, &ey, &ez);
    camera_fov(int(a.layout.view.w), int(a.layout.view.h), &fx, &fy);

    std::fputs("{\n  \"scenario\": ", f);
    json_string(f, s.id);
    std::fputs(",\n  \"probe_version\": 1,\n", f);
    std::fprintf(f, "  \"window\":        { \"w\": %d, \"h\": %d },\n",
                 p.win_w, p.win_h);
    std::fprintf(f, "  \"drawable\":      { \"w\": %d, \"h\": %d },\n",
                 p.draw_w, p.draw_h);
    std::fprintf(f,
                 "  \"imgui_display\": { \"w\": %.0f, \"h\": %.0f, "
                 "\"fb_scale\": [%.3f, %.3f] },\n",
                 static_cast<double>(p.disp_w), static_cast<double>(p.disp_h),
                 static_cast<double>(p.fb_scale_x),
                 static_cast<double>(p.fb_scale_y));
    std::fprintf(f,
                 "  \"fbo\": { \"w\": %d, \"h\": %d, \"status\": \"%s\" },\n",
                 p.fbw, p.fbh,
                 p.fb_status == GL_FRAMEBUFFER_COMPLETE
                     ? "GL_FRAMEBUFFER_COMPLETE" : "INCOMPLETE");
    std::fprintf(f,
                 "  \"camera\": { \"yaw\": %.6f, \"pitch\": %.6f, "
                 "\"dist\": %.6f, \"target\": [%.3f, %.3f, %.3f], "
                 "\"eye\": [%.3f, %.3f, %.3f], \"fov\": [%.6f, %.6f] },\n",
                 static_cast<double>(p.capture_camera.yaw),
                 static_cast<double>(p.capture_camera.pitch),
                 static_cast<double>(p.capture_camera.dist),
                 static_cast<double>(p.capture_camera.tx),
                 static_cast<double>(p.capture_camera.ty),
                 static_cast<double>(p.capture_camera.tz), static_cast<double>(ex),
                 static_cast<double>(ey), static_cast<double>(ez),
                 static_cast<double>(fx), static_cast<double>(fy));

    std::fprintf(f, "  \"events\": [\n");
    for (size_t i = 0; i < p.events.size(); ++i) {
        const EventRec& e = p.events[i];
        std::fprintf(f,
                     "    { \"it\": %d, \"type\": \"%s\", \"px\": [%d, %d], "
                     "\"window_id\": %u }%s\n",
                     e.it, e.type, e.px, e.py, e.window_id,
                     i + 1 == p.events.size() ? "" : ",");
    }
    std::fprintf(f, "  ],\n");

    std::fprintf(f,
                 "  \"observed\": { \"click_it\": %d, \"mouse_pos\": "
                 "[%.1f, %.1f], \"want_capture_mouse\": %s, "
                 "\"capture_it\": %d },\n",
                 click.it, static_cast<double>(click.mouse_x),
                 static_cast<double>(click.mouse_y),
                 click.want_capture_mouse ? "true" : "false", cap.it);
    std::fprintf(f,
                 "  \"resolved\": { \"cell\": [%d, %d], "
                 "\"object_index\": %d },\n",
                 cap.sel_x, cap.sel_y, cap.sel_obj);

    if (cap.sel_obj >= 0 &&
        cap.sel_obj < static_cast<int>(p.capture_model.objects.size())) {
        const Object& o = p.capture_model.objects[static_cast<size_t>(cap.sel_obj)];
        std::fprintf(f,
                     "  \"object\": { \"x\": %d, \"y\": %d, \"w\": %d, "
                     "\"footprint_rows\": %d, \"extent\": %d, "
                     "\"class\": \"%s\", \"has_door\": %s,\n"
                     "               \"authored\": { \"active\": %s, "
                     "\"roof_rows\": %d, \"rise\": %.3f, \"height\": %d } },\n",
                     o.x, o.y, o.w, o.footprint_rows, o.extent,
                     class_word(o.cls), o.has_door ? "true" : "false",
                     o.authored.active ? "true" : "false",
                     o.authored.roof_rows,
                     static_cast<double>(o.authored.rise), o.authored.height);
    }

    std::fprintf(f, "  \"surfaces\": { \"click_owner\": %d, \"selected_row\": %d, \"view\": [%.0f,%.0f,%.0f,%.0f], \"map\": [%.0f,%.0f,%.0f,%.0f] },\n",
        click.surface, p.selected_row, double(a.layout.view.x), double(a.layout.view.y),
        double(a.layout.view.w), double(a.layout.view.h), double(a.layout.map.x),
        double(a.layout.map.y), double(a.layout.map.w), double(a.layout.map.h));
    std::fprintf(f, "  \"outlines\": [\n");
    for (size_t i = 0; i < p.outlines.size(); ++i)
        json_outline(f, p.outlines[i], i + 1 == p.outlines.size());
    std::fprintf(f, "  ],\n");

    std::fprintf(f, "  \"panels\": {\n");
    json_panel(f, "Header", p.panel_scene, p.fbw, p.fbh, false);
    json_panel(f, "Groups", p.panel_groups, p.fbw, p.fbh, false);
    json_panel(f, "Workspace", p.panel_workspace, p.fbw, p.fbh, false);
    json_panel(f, "Inspector", p.panel_selection, p.fbw, p.fbh, true);
    std::fprintf(f, "  },\n");

    const std::string geom = stats_value(p.capture_stats, "geom");
    std::fprintf(f, "  \"geom\": \"%s\",\n", geom.c_str());

    const double ratio = sampled ? static_cast<double>(matched) / sampled : 0.0;
    std::fprintf(f,
                 "  \"pixels\": { \"digest_algorithm\": "
                 "\"fnv1a64-rgb-top-down\",\n"
                 "               \"digest\": \"%016llx\",\n"
                 "               \"sample_stride\": 32, "
                 "\"distinct_colours_sampled\": %d,\n"
                 "               \"selection_outline\": { \"sampled\": %d, "
                 "\"matched\": %d, \"ratio\": %.4f } },\n",
                 static_cast<unsigned long long>(
                     fnv1a64(p.rgb.data(), p.rgb.size())),
                 distinct, sampled, matched, ratio);

    static const char* kNegNames[3] = {"wrong_pixel", "motion_without_click",
                                       "foreign_window_id"};
    std::fprintf(f, "  \"negatives\": [\n");
    for (int i = 0; i < 3; ++i)
        std::fprintf(f,
                     "    { \"case\": \"%s\", \"cell\": [%d, %d], "
                     "\"object_index\": %d, \"hover_cell\": [%d, %d], "
                     "\"hover_object\": %d, \"buttons_down\": %d }%s\n",
                     kNegNames[i], p.neg[i].cell[0], p.neg[i].cell[1],
                     p.neg[i].obj, p.neg[i].hover_x, p.neg[i].hover_y,
                     p.neg[i].hover_obj, p.neg[i].button_down ? 1 : 0,
                     i == 2 ? "" : ",");
    std::fprintf(f, "  ],\n");

    std::fputs("  \"interaction_frames\": [\n", f);
    bool first_interaction = true;
    for (const auto& frame : p.obs) {
        if (frame.it != kItChromeRecord && frame.it != kItPanRecord &&
            frame.it != kItOrbitRecord && frame.it != kItReleasedRecord && frame.it != kItEraseRecord &&
            frame.it != kItUndoRecord && frame.it != kItRedoRecord && frame.it != kItCancelRecord &&
            frame.it != kItMaskBefore && frame.it != kItMaskPaint && frame.it != kItMaskUndo &&
            frame.it != kItMaskRedo && frame.it != kItMaskRestore && frame.it != kItMaskCancel && frame.it != kItModelPreview &&
            frame.it!=kItPartAdded && frame.it!=kItPartEdited && frame.it!=kItPartUndo && frame.it!=kItPartRedo &&
            frame.it!=kItHandleGuide && frame.it!=kItHandleApplied && frame.it!=kItHandleUndo && frame.it!=kItHandleRedo && frame.it!=kItHandleCancelled &&
            frame.it!=kItDiorama && frame.it!=kItDioramaOrbit && frame.it!=kItBackToSegment) continue;
        if (!first_interaction) std::fputs(",\n", f);
        first_interaction = false;
        std::fprintf(f, "    { \"it\": %d, \"surface\": %d, \"object\": %d, "
            "\"pan\": [%.1f,%.1f], \"camera\": [%.6f,%.6f,%.6f], "
            "\"drag_owner\": %d, \"panning\": %s, \"buttons_down\": %s, \"members\": %zu, \"undo\": %zu, \"mode\": %d, \"cutout_opaque\": %d }",
            frame.it, frame.surface, frame.sel_obj, double(frame.pan_x), double(frame.pan_y),
            double(frame.camera.yaw), double(frame.camera.pitch), double(frame.camera.dist),
            frame.drag_surface, frame.panning ? "true" : "false",
            frame.button_down || frame.middle_down ? "true" : "false",
            size_t(std::count(frame.draft.mask.begin(), frame.draft.mask.end(), uint8_t(1))), frame.undo_count, frame.mode,
            frame.draft.cutout ? int(std::count(frame.draft.cutout->opacity.begin(),frame.draft.cutout->opacity.end(),uint8_t(1))) : -1);
    }
    std::fputs("\n  ],\n", f);
    std::fputs("  \"artifacts\": { \"image\": ", f);
    json_string(f, base_name(p.png_path));
    std::fputs(" }\n}\n", f);
    const bool stream_ok = !std::ferror(f);
    const int close_result = std::fclose(f);
    const bool ok = stream_ok && close_result == 0;
    if (!ok)
        std::fprintf(stderr, "[visual-probe] %s: sidecar write failed\n",
                     p.json_path.c_str());
    return ok;
}

// ── The assertions ───────────────────────────────────────────────────────────

// THE FAILURE PATH REPORTS THE ARTIFACTS TOO. A probe that only produced its
// picture when everything already agreed would be useless at the one moment the
// picture is worth most — when the scenario is stale and somebody has to look at
// the frame to see what moved.
int probe_fail(const Probe& p, int passed, const char* check) {
    const ProbeScenario& s = *p.sc;
    std::fprintf(stderr, "[visual-probe] FAIL after %d checks: %s\n", passed,
                 check);
    std::fprintf(stderr,
                 "[visual-probe]   scenario=%s camera yaw=%.3f pitch=%.3f "
                 "dist=%.3f target=%.2f,%.2f,%.2f\n",
                 s.id.c_str(), static_cast<double>(s.cam.yaw),
                 static_cast<double>(s.cam.pitch),
                 static_cast<double>(s.cam.dist), static_cast<double>(s.cam.tx),
                 static_cast<double>(s.cam.ty), static_cast<double>(s.cam.tz));
    for (const FrameObs& o : p.obs)
        if (o.it == kItCapture)
            std::fprintf(stderr,
                         "[visual-probe]   resolved cell %d,%d object #%d\n",
                         o.sel_x, o.sel_y, o.sel_obj);
    const std::string abs_png = studio::platform_io::absolute_path(p.png_path);
    const std::string abs_json = studio::platform_io::absolute_path(p.json_path);
    std::fprintf(stderr, "[visual-probe]   image=%s state=%s\n", abs_png.c_str(),
                 abs_json.c_str());
    return 1;
}

int probe_report(App& a, Probe& p) {
    const ProbeScenario& s = *p.sc;
    int passed = 0;
    auto require = [&](bool condition, const char* check) {
        if (condition) ++passed;
        else std::fprintf(stderr, "[visual-probe] check failed: %s\n", check);
        return condition;
    };

    const FrameObs  none;
    const FrameObs* click = nullptr;
    const FrameObs* cap   = nullptr;
    const FrameObs* before_release = nullptr;
    for (const FrameObs& o : p.obs) {
        if (o.clicked && !click)  click = &o;
        if (o.it == kItCapture)   cap = &o;
        if (o.it == kItClickUp)   before_release = &o;
    }

    if (!require(p.captured, "a frame was captured"))
        return probe_fail(p, passed, "capture");

    // ARTIFACTS FIRST, ASSERTIONS SECOND. Everything below can fail; none of it
    // changes what was rendered, and the picture is the thing a person needs in
    // order to fix a stale scenario.
    const OutlineRecord* sel = find_outline(p, "selection");
    const int distinct = distinct_colours(p.rgb, p.fbw, p.fbh, 32);
    int sampled = 0, matched = 0;
    if (sel) {
        ImVec2 physical[4];
        for (int i = 0; i < 4; ++i)
            physical[i] = ImVec2(sel->p[i].x * p.fb_scale_x, sel->p[i].y * p.fb_scale_y);
        sample_selection_outline(p.rgb, p.fbw, p.fbh, physical, &sampled, &matched);
    }
    const bool wrote_png =
        studio::png::write_rgb(p.png_path.c_str(), p.fbw, p.fbh, p.rgb.data());
    const bool wrote_json =
        write_sidecar(a, p, click ? *click : none, cap ? *cap : none, distinct,
                      sampled, matched);
    if (!require(wrote_png, "the PNG was written") ||
        !require(wrote_json, "the sidecar was written"))
        return probe_fail(p, passed, "artifacts");

    if (!require(cap != nullptr, "the capture frame was observed") ||
        !require(click != nullptr, "ImGui reported a click"))
        return probe_fail(p, passed, "schedule");

    // 1. The probe did not perturb the mesher.
    if (!require(stats_value(p.capture_stats, "geom") == s.exp_geom,
                 "unmodified geometry hash"))
        return probe_fail(p, passed, "baseline geometry");

    // 3-5. The event path is real. mouse_pos is what ImGui reported, so this
    // fails if the injected event never reached ProcessEvent or NewFrame.
    if (!require(click->it == kItClickUp,
                 "the click was seen on the scheduled frame") ||
        !require(static_cast<int>(click->mouse_x) == s.click_px[0] &&
                     static_cast<int>(click->mouse_y) == s.click_px[1],
                 "observed mouse position equals the injected pixel") ||
        !require((click->surface == 1 || click->surface == 2) &&
                     (!s.exp_surface || click->surface == s.exp_surface),
                 "the click was owned by a workspace surface"))
        return probe_fail(p, passed, "injected input");
    if (!require(before_release && before_release->sel_obj < 0,
                 "nothing was selected before the release") ||
        !require(cap->sel_obj >= 0, "the release selected an object"))
        return probe_fail(p, passed, "selection timing");

    // 2. What it selected.
    if (!require(cap->sel_x == s.exp_cell[0] && cap->sel_y == s.exp_cell[1],
                 "resolved cell matches the scenario"))
        return probe_fail(p, passed, "resolved cell");
    const Object& o = p.capture_model.objects[static_cast<size_t>(cap->sel_obj)];
    if (!require(o.x == s.exp_x && o.y == s.exp_y &&
                     o.w == s.exp_w && o.footprint_rows == s.exp_rows &&
                     o.extent == s.exp_extent,
                 "selected object footprint matches the scenario") ||
        !require(!std::strcmp(class_word(o.cls), s.exp_class.c_str()) &&
                     o.has_door == s.exp_door,
                 "selected object class and door match the scenario") ||
        !require(o.authored.active == s.exp_authored,
                 "selected object authored state matches the scenario"))
        return probe_fail(p, passed, "selected object");

    // 6-7. The frame's geometry, from four independent sources.
    if (!require(p.win_w == p.draw_w && p.win_h == p.draw_h,
                 "window and drawable sizes agree") ||
        !require(static_cast<int>(p.disp_w) == p.win_w &&
                     static_cast<int>(p.disp_h) == p.win_h,
                 "ImGui display size agrees with the window") ||
        !require(p.fbw == int(std::lround(p.disp_w * p.fb_scale_x)) &&
                     p.fbh == int(std::lround(p.disp_h * p.fb_scale_y)),
                 "framebuffer size agrees with logical display times render scale") ||
        !require(p.fb_status == GL_FRAMEBUFFER_COMPLETE,
                 "framebuffer is complete"))
        return probe_fail(p, passed, "frame geometry");

    // 8-9. The overlays that were actually drawn.
    const OutlineRecord* hover = find_outline(p, "hover-cell");
    if (!require(sel != nullptr, "a selection outline was drawn"))
        return probe_fail(p, passed, "selection outline");
    bool inside = true;
    for (int i = 0; i < 4; ++i)
        if (sel->p[i].x < 0 || sel->p[i].y < 0 || sel->p[i].x >= p.win_w ||
            sel->p[i].y >= p.win_h)
            inside = false;
    if (!require(inside, "every selection outline corner is inside the frame") ||
        !require(hover != nullptr, "a hover outline was drawn") ||
        !require(hover->x != sel->x || hover->y != sel->y,
                 "the hover outline is on a different cell than the selection"))
        return probe_fail(p, passed, "overlay geometry");

    // 10. The panels, from ImGui's own state rather than from pixels — a
    //     "contains non-background pixels" test would pass on an empty panel,
    //     because a panel's own background is not the clear colour.
    const auto layout = studio::ShellLayout::at(s.vw, s.vh);
    const PanelObs* panels[] = {&p.panel_scene, &p.panel_groups, &p.panel_workspace, &p.panel_selection};
    const studio::Rect rects[] = {layout.header, layout.groups, layout.workspace, layout.inspector};
    for (int i = 0; i < 4; ++i) {
        const PanelObs& panel = *panels[i];
        const auto r = rects[i];
        if (!require(panel.submitted && panel.open && !panel.collapsed,
                     "shell region submitted, open and uncollapsed") ||
            !require(panel.px == r.x && panel.py == r.y && panel.sw == r.w && panel.sh == r.h,
                     "shell region has pinned geometry"))
            return probe_fail(p, passed, "shell regions");
    }
    if (!require(p.selected_row == cap->sel_obj, "the selected list row names the selected object"))
        return probe_fail(p, passed, "linked selection");

    // 11-12. The pixels, measured above and asserted here.
    const double ratio = sampled ? static_cast<double>(matched) / sampled : 0.0;
    if (!require(distinct >= 8, "the frame is not blank"))
        return probe_fail(p, passed, "blank frame");
    if (!require(sampled > 0, "the selection outline had sampleable edges") ||
        !require(ratio >= kSelMinRatio,
                 "selection-colour pixels found along the drawn outline")) {
        std::fprintf(stderr,
                     "[visual-probe]   outline sampled=%d matched=%d "
                     "ratio=%.3f (need %.2f)\n",
                     sampled, matched, ratio, kSelMinRatio);
        return probe_fail(p, passed, "selection outline pixels");
    }

    // The negatives. Each was preceded by a release and a settle frame, so a
    // button still down means the schedule leaked state between cases.
    for (int i = 0; i < 3; ++i) {
        char what[64];
        std::snprintf(what, sizeof(what), "negative %d was recorded", i);
        if (!require(p.neg[i].recorded, what) ||
            !require(!p.neg[i].button_down, "no mouse button left down"))
            return probe_fail(p, passed, "negative bookkeeping");
    }
    // A: a different pixel must not select the scenario's object.
    if (!require(p.neg[0].cell[0] != s.exp_cell[0] ||
                     p.neg[0].cell[1] != s.exp_cell[1],
                 "a wrong pixel does not resolve to the expected cell") ||
        !require(p.neg[0].obj >= 0 && p.neg[0].obj != cap->sel_obj,
                 "a wrong pixel does not select the expected object"))
        return probe_fail(p, passed, "negative A: wrong pixel");
    // B: motion alone must not select — and it has to be motion that WOULD have
    //    selected A DIFFERENT OBJECT, or the check proves nothing. Hovering
    //    another cell of the same object would leave the selection unchanged
    //    whether or not the hover wrongly selected, which is a check that cannot
    //    fail rather than a check that passes.
    if (!require(p.neg[1].hover_obj >= 0,
                 "the motion-only pixel hovers an object") ||
        !require(p.neg[1].hover_obj != p.neg[0].obj,
                 "the motion-only pixel hovers a DIFFERENT object than the "
                 "one selected, so a wrong selection would be visible") ||
        !require(p.neg[1].obj == p.neg[0].obj &&
                     p.neg[1].cell[0] == p.neg[0].cell[0] &&
                     p.neg[1].cell[1] == p.neg[0].cell[1],
                 "motion without a click does not change the selection"))
        return probe_fail(p, passed, "negative B: motion without a click");
    // C: a click carrying a foreign window id must be dropped by ProcessEvent.
    if (!require(p.neg[2].obj == p.neg[1].obj &&
                     p.neg[2].cell[0] == p.neg[1].cell[0] &&
                     p.neg[2].cell[1] == p.neg[1].cell[1],
                 "a click with a foreign window id is ignored"))
        return probe_fail(p, passed, "negative C: foreign window id");

    auto observed = [&](int it) -> const FrameObs* {
        for (const auto& frame : p.obs) if (frame.it == it) return &frame;
        return nullptr;
    };
    const auto chrome = observed(kItChromeRecord), pan = observed(kItPanRecord);
    const auto orbit = observed(kItOrbitRecord), released = observed(kItReleasedRecord);
    if (!require(chrome && pan && orbit && released, "interaction frames were observed"))
        return probe_fail(p, passed, "interaction schedule");
    auto same_camera = [](const Camera& x, const Camera& y) {
        return x.yaw == y.yaw && x.pitch == y.pitch && x.dist == y.dist &&
               x.tx == y.tx && x.ty == y.ty && x.tz == y.tz;
    };
    if (!require(chrome->sel_obj == p.neg[2].obj && same_camera(chrome->camera, s.cam),
                 "click and wheel in the output field do not select or move the camera") ||
        !require(pan->pan_y == 40 && pan->pan_x == 0 && !pan->panning && !pan->middle_down,
                 "middle drag pans exactly forty pixels and releases capture") ||
        !require(pan->sel_obj == chrome->sel_obj && same_camera(pan->camera, s.cam),
                 "map panning changes neither object selection nor orbit") ||
        !require(!same_camera(orbit->camera, s.cam) && orbit->sel_obj == pan->sel_obj,
                 "3D drag orbits without selecting") ||
        !require(!orbit->button_down && orbit->drag_surface == 0,
                 "release outside the viewport clears captured drag") ||
        !require(same_camera(released->camera, orbit->camera) && released->drag_surface == 0,
                 "motion after outside release does not continue orbiting"))
        return probe_fail(p, passed, "surface input and drag lifetime");

    const auto erased = observed(kItEraseRecord), undone = observed(kItUndoRecord);
    const auto redone = observed(kItRedoRecord), cancelled = observed(kItCancelRecord);
    if (!require(erased && undone && redone && cancelled, "membership input frames were observed"))
        return probe_fail(p, passed, "membership schedule");
    auto members = [](const Pattern& pattern) { return std::count(pattern.mask.begin(), pattern.mask.end(), uint8_t(1)); };
    if (!require(members(erased->draft) == members(released->draft)-2 &&
                 erased->undo_count == released->undo_count+1 && !erased->membership_stroke,
                 "Shift-drag erases two cells as one completed transaction") ||
        !require(undone->draft == released->draft && undone->undo_count == released->undo_count,
                 "Ctrl-Z restores the complete pre-stroke definition") ||
        !require(redone->draft == erased->draft, "Ctrl-Y restores the sparse definition") ||
        !require(cancelled->draft == redone->draft && cancelled->undo_count == redone->undo_count &&
                 !cancelled->membership_stroke && !cancelled->button_down && cancelled->drag_surface == 0,
                 "Escape during an add stroke restores the definition and releases capture") ||
        !require(same_camera(cancelled->camera, released->camera), "membership editing does not move the camera"))
        return probe_fail(p, passed, "membership input and history");

    if (!require(p.auxiliary_images_ok && p.auxiliary_images == 8, "MASK, MODEL, handles and DIORAMA images were written"))
        return probe_fail(p,passed,"mask image artifacts");
    const auto mask_before = observed(kItMaskBefore), mask_painted = observed(kItMaskPaint);
    const auto mask_undone = observed(kItMaskUndo), mask_redone = observed(kItMaskRedo);
    const auto mask_restored = observed(kItMaskRestore), mask_cancelled = observed(kItMaskCancel);
    const auto model_preview = observed(kItModelPreview);
    const auto part_added=observed(kItPartAdded),part_edited=observed(kItPartEdited),part_undo=observed(kItPartUndo),part_redo=observed(kItPartRedo);
    if(!require(part_added && part_edited && part_undo && part_redo,"MODEL numeric input frames observed")) return probe_fail(p,passed,"part frames");
    if(!require(part_added->draft.parts.size()==2 && part_added->draft.parts.back().kind==vr::overrides::PartKind::Box,
                "inspector Add Box creates an editable production part") ||
       !require(part_edited->draft.parts.size()==2 && part_edited->draft.parts.back().transform.position.x==.5f &&
                part_edited->undo_count==part_added->undo_count+1,"numeric X field commits one transform transaction") ||
       !require(part_undo->draft==part_added->draft && part_redo->draft==part_edited->draft,
                "keyboard undo and redo restore exact numeric part edits") ||
       !require(same_camera(part_redo->camera,part_added->camera),"part inspector input never moves the camera")) return probe_fail(p,passed,"part controls");
    const auto guide=observed(kItHandleGuide),handle_applied=observed(kItHandleApplied),handle_undo=observed(kItHandleUndo),
        handle_redo=observed(kItHandleRedo),handle_cancelled=observed(kItHandleCancelled);
    if(!require(guide && handle_applied && handle_undo && handle_redo && handle_cancelled,"handle input frames observed")) return probe_fail(p,passed,"handle frames");
    if(!require(guide->handle_active && guide->draft.parts.back().transform.position.z>0 && guide->preview_hash==part_redo->preview_hash &&
                guide->undo_count==part_redo->undo_count,"handle guide changes local Z while retaining the last mesh and history") ||
       !require(!handle_applied->handle_active && handle_applied->preview_hash!=part_redo->preview_hash &&
                handle_applied->undo_count==part_redo->undo_count+1,"release commits and rebuilds one handle transaction") ||
       !require(handle_undo->draft==part_redo->draft && handle_redo->draft==handle_applied->draft,"Undo and Redo preserve exact handle transforms") ||
       !require(handle_cancelled->draft==part_redo->draft && !handle_cancelled->handle_active && !handle_cancelled->drag_surface &&
                handle_cancelled->preview_hash==part_redo->preview_hash,"Escape cancels a captured handle after leaving the viewport") ||
       !require(same_camera(handle_applied->camera,part_redo->camera) && same_camera(handle_cancelled->camera,part_redo->camera),
                "captured handles never orbit the camera")) return probe_fail(p,passed,"handle controls");
    const auto paper=observed(kItDiorama),paper_orbit=observed(kItDioramaOrbit),back=observed(kItBackToSegment);
    if(!require(paper && paper_orbit && back,"DIORAMA input frames observed")) return probe_fail(p,passed,"diorama frames");
    if(!require(paper->mode==3 && paper->raised_instances==1 && paper->draft==handle_cancelled->draft,
                "DIORAMA tab raises the accepted authored group without changing its draft") ||
       !require(paper_orbit->mode==3 && !same_camera(paper_orbit->camera,paper->camera) && paper_orbit->draft==paper->draft &&
                paper_orbit->room_hash==paper->room_hash,"DIORAMA orbit changes only the camera") ||
       !require(back->mode==0 && back->draft==paper->draft && back->room_hash!=paper->room_hash,
                "returning to SEGMENT restores inferred build mode without changing authored state")) return probe_fail(p,passed,"diorama controls");
    if (!require(mask_before && mask_painted && mask_undone && mask_redone && mask_restored && mask_cancelled && model_preview,
                 "MASK and MODEL input frames were observed")) return probe_fail(p,passed,"mask schedule");
    if (!require(mask_before->mode == 1 && !mask_before->draft.cutout && mask_painted->draft.cutout &&
                 !mask_painted->mask_stroke && mask_painted->undo_count == mask_before->undo_count+1,
                 "first canvas stroke creates one undoable cutout") ||
        !require(mask_undone->draft == mask_before->draft && mask_redone->draft == mask_painted->draft,
                 "mask Undo/Redo restore exact absence and painted opacity")) return probe_fail(p,passed,"mask history");
    const auto& opacity = mask_painted->draft.cutout->opacity;
    bool nine_erased = true;
    for (int x=8;x<=16;++x) nine_erased = nine_erased && opacity[size_t(8)*64+x] == 0;
    if (!require(nine_erased && opacity[size_t(8)*64+7] && opacity[size_t(8)*64+17], "canvas stroke erases exactly the frozen nine-pixel segment") ||
        !require(mask_restored->draft.cutout && mask_restored->draft.cutout->opacity[size_t(8)*64+8] == 1 &&
                 mask_restored->draft.cutout->opacity[size_t(8)*64+9] == 0, "R restores only the clicked source pixel") ||
        !require(mask_cancelled->draft == mask_restored->draft && !mask_cancelled->mask_stroke && mask_cancelled->drag_surface == 0,
                 "Escape cancels a mask stroke without changing the cutout") ||
        !require(same_camera(mask_cancelled->camera, mask_before->camera), "canvas editing never orbits the camera") ||
        !require(model_preview->mode == 2 && model_preview->preview_vertices > 0 &&
                 model_preview->draft.cutout == mask_cancelled->draft.cutout && model_preview->draft.parts.size()==1 &&
                 model_preview->draft.model_seeded && model_preview->undo_count==mask_cancelled->undo_count+1,
                 "MODEL preserves opacity and seeds one billboard in one undo transaction"))
        return probe_fail(p,passed,"mask brush and production preview");

    const std::string abs_png = studio::platform_io::absolute_path(p.png_path);
    const std::string abs_json = studio::platform_io::absolute_path(p.json_path);
    std::fprintf(stdout,
                 "[visual-probe] PASS scenario=%s checks=%d image=%s state=%s\n",
                 s.id.c_str(), passed, abs_png.c_str(), abs_json.c_str());
    return 0;
}

// ── Panels ───────────────────────────────────────────────────────────────────

void record_panel(PanelObs* out, bool open) {
    out->submitted = true;
    out->open      = open;
    out->collapsed = ImGui::IsWindowCollapsed();
    const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    out->px = pos.x; out->py = pos.y;
    out->sw = size.x; out->sh = size.y;
}


bool shell_begin(const char* name, studio::Rect r, PanelObs* observation = nullptr,
                 bool transparent = false) {
    ImGui::SetNextWindowPos(ImVec2(r.x, r.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(r.w, r.h), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoSavedSettings;
    if (transparent) flags |= ImGuiWindowFlags_NoBackground |
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    const bool open = ImGui::Begin(name, nullptr, flags);
    if (observation && g_probe && g_probe->it == kItCapture)
        record_panel(observation, open);
    return open;
}

ImVec2 corner(studio::Rect r) { return ImVec2(r.x + r.w, r.y + r.h); }

bool mode_button(App& a, const char* label, int mode) {
    if (a.mode == mode) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.91f,.62f,.58f,1));
    const bool clicked = ImGui::Button(label);
    if (a.mode == mode) ImGui::PopStyleColor();
    if (clicked) switch_mode(a,mode);
    if (ImGui::IsItemHovered()) {
        const char* help[]={"1. Select the tiles that belong to an object.",
            "2. Separate object pixels from ground and shadow.",
            "3. Shape the body, roof and details in 3D.",
            "4. Check the model in its room from every side."};
        ImGui::SetTooltip("%s",help[mode]);
    }
    return clicked;
}

void voxel_start(App& a);

void save_changes(App& a) {
    if(a.pending!=App::Action::None) return;
    if(a.document.draft_dirty() && !apply_draft(a)) return;
    request_action(a,App::Action::Save);
}

std::string room_title(const std::string& id) {
    std::string title=id.rfind("MAP_",0)==0?id.substr(4):id;
    bool first=true;
    for(char& ch:title) {
        if(ch=='_') {ch=' ';first=true;}
        else {if(!first && ch>='A' && ch<='Z') ch=char(ch-'A'+'a');first=false;}
    }
    return title;
}

void edit_selected_model(App& a) {
    if(!a.has_draft || !populated(a.draft)) return;
    if(a.draft.voxel || !a.draft.parts.empty()) {
        if(!selected_part(a) && !a.draft.parts.empty()) a.document.state.selected_part=a.draft.parts.front().id;
        switch_mode(a,2);focus_selection(a);
    }
    else {switch_mode(a,1);a.status="First erase ground from the cutout, then choose Start voxel model.";}
}

#include "gui_terrain.inl"
#include "gui_connected.inl"

void header_panel(App& a) {
    shell_begin("Studio header", a.layout.header, g_probe ? &g_probe->panel_scene : nullptr);
    if(a.exploring) {
        ImGui::TextUnformatted("CONNECTED AREA");ImGui::SameLine();
        if(ImGui::Button("Return to editing")) leave_connected(a);
        ImGui::Separator();
        ImGui::TextUnformatted("Hold right mouse in the 3D view. WASD moves, Q/E down/up, Shift faster. Release to stop.");
        ImGui::End();return;
    }
    ImGui::TextColored(ImVec4(0.68f, 0.16f, 0.23f, 1), "rubyvr studio");
    ImGui::SameLine(160);
    ImGui::SetNextItemWidth(210);
    if (ImGui::BeginCombo("##room", room_title(a.map_id).c_str())) {
        for (const auto& room : a.rooms) {
            const std::string label=room_title(room)+"##"+room;
            if (ImGui::Selectable(label.c_str(), room == a.map_id))
                request_action(a, App::Action::Room, -1, -1, room);
            if (room == a.map_id) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    auto current = std::find(a.rooms.begin(), a.rooms.end(), a.map_id);
    ImGui::SameLine();
    ImGui::BeginDisabled(current == a.rooms.begin() || current == a.rooms.end());
    if (ImGui::Button("<")) request_action(a, App::Action::Room, -1, -1, *(current-1));
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(current == a.rooms.end() || current+1 == a.rooms.end());
    if (ImGui::Button(">")) request_action(a, App::Action::Room, -1, -1, *(current+1));
    ImGui::EndDisabled();
    ImGui::SameLine(444);
    mode_button(a,"SEGMENT",0);
    ImGui::SameLine();
    mode_button(a,"MASK",1);
    ImGui::SameLine();
    ImGui::BeginDisabled(!a.has_draft || (!a.draft.cutout && a.draft.parts.empty()));
    mode_button(a,"MODEL",2);
    ImGui::EndDisabled();
    ImGui::SameLine(); mode_button(a,"DIORAMA",3);
    ImGui::SameLine(765);
    ImGui::BeginDisabled(!a.document.undo_count() || a.pending != App::Action::None);
    if (ImGui::Button("UNDO")) history(a, false);
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(!a.document.redo_count() || a.pending != App::Action::None);
    if (ImGui::Button("REDO")) history(a, true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool dirty=a.document.unsaved() || a.document.draft_dirty();
    ImGui::TextColored(dirty?ImVec4(.66f,.23f,.12f,1):ImVec4(.20f,.40f,.27f,1),dirty?"Unsaved changes":"Saved");
    ImGui::SameLine();
    if(ImGui::Button("Review...")) a.review.open=true;
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Find missing models, unresolved source groups and failed reviews, then open their map location.");
    ImGui::SameLine();
    if(ImGui::Button(a.terrain.open && a.mode==3?"Objects":"Terrain")) {
        a.terrain.open=!(a.terrain.open && a.mode==3);a.terrain.pick=0;
        if(a.terrain.open) switch_mode(a,3);
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Raise a region, make steps, or restore the floor under an object.");
    ImGui::SameLine();
    ImGui::BeginDisabled(a.mode!=3 || a.document.draft_dirty() || a.pending!=App::Action::None);
    if(ImGui::Button("Explore area")) enter_connected(a);
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Explore full nearby maps. Choose DIORAMA and save model edits first.");
    ImGui::Separator();
    if (ImGui::Button("Save")) save_changes(a);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Save the current model and all applied changes to your personal file. Ctrl+S.");
    ImGui::SameLine();
    if (ImGui::Button("Reload")) request_action(a, App::Action::Reload);
    ImGui::SameLine();
    const char* action=!a.has_draft?"Choose an object":a.mode==0?"Next: cut out pixels":a.mode==1?
        (a.draft.voxel?"Next: shape model":"Start voxel model"):a.mode==2?"View in scene":"Edit selected model";
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.87f,.62f,.55f,1));
    ImGui::BeginDisabled(!a.has_draft || !populated(a.draft) || a.pending!=App::Action::None);
    if(ImGui::Button(action,ImVec2(164,0))) {
        if(a.mode==0) switch_mode(a,1);
        else if(a.mode==1) {if(a.draft.voxel) switch_mode(a,2);else voxel_start(a);}
        else if(a.mode==2) switch_mode(a,3);
        else edit_selected_model(a);
    }
    ImGui::EndDisabled();ImGui::PopStyleColor();
    ImGui::SameLine();
    const char* hint=!a.has_draft?"Click a building or prop in the map to begin.":a.mode==0?
        "1 / Select  -  click to add tiles; Shift removes.":a.mode==1?
        "2 / Mask  -  keep the object; separate ground and shadow.":a.mode==2?
        "3 / Shape  -  select a part, then adjust its depth or size.":
        "4 / Scene  -  orbit to check every side and the ground.";
    ImGui::TextUnformatted(hint);
    ImGui::SameLine(a.layout.header.w-210);
    if(ImGui::Button("File...")) ImGui::OpenPopup("Save file");
    ImGui::SameLine();ImGui::TextDisabled("Ctrl+S to save");
    if(ImGui::BeginPopup("Save file")) {
        ImGui::TextUnformatted("Your editable copy");
        char path[1024];std::snprintf(path,sizeof(path),"%s",a.out_path.c_str());
        ImGui::SetNextItemWidth(560);
        if(ImGui::InputText("##output",path,sizeof(path))) a.out_path=path;
        ImGui::TextWrapped("Save writes here. The source template is read-only.");
        ImGui::EndPopup();
    }
    ImGui::End();
}

void group_panel(App& a) {
    shell_begin("Groups", a.layout.groups, g_probe ? &g_probe->panel_groups : nullptr);
    ImGui::TextUnformatted("ROOM");
    ImGui::TextDisabled("%s",room_title(a.map_id).c_str());
    if (a.room.texture && a.room.w > 0) {
        const float scale = std::min(184.0f / a.room.w, 70.0f / a.room.h);
        ImGui::Image(ImTextureID(a.room.texture), ImVec2(a.room.w * scale, a.room.h * scale));
    }
    ImGui::Separator();
    ImGui::TextUnformatted("MODELS & OBJECTS");
    ImGui::SetNextItemWidth(121);
    ImGui::InputTextWithHint("##model-search","Search...",a.model_search,sizeof(a.model_search));
    ImGui::SameLine();
    if(ImGui::Button(a.models_this_room?"Room##filter":"All##filter",ImVec2(52,0))) a.models_this_room=!a.models_this_room;
    if(ImGui::IsItemHovered()) ImGui::SetTooltip(a.models_this_room?"Showing models placed in this room. Click for the whole library.":"Showing the whole library. Click for this room only.");
    auto matches_search=[&](std::string value) {
        std::string query=a.model_search;
        for(auto* text:{&query,&value}) for(char& c:*text) if(c>='A' && c<='Z') c=char(c-'A'+'a');
        return value.find(query)!=std::string::npos;
    };
    ImGui::BeginChild("object-list", ImVec2(0, -70));
    for (size_t i = 0; i < a.working.patterns.size(); ++i) {
        const auto& p = a.working.patterns[i];
        size_t instances = 0;
        for (const auto& m : a.claims.accepted) if (m.pattern == int(i)) ++instances;
        if(p.ground_only() || !matches_search(p.name) || (a.models_this_room && !instances && a.draft_slot!=int(i))) continue;
        ImGui::PushID(100000 + int(i));
        const std::string label = p.name + "  (" + std::to_string(instances) + ")";
        if (ImGui::Selectable(label.c_str(), a.has_draft && a.draft_slot == int(i)))
            request_action(a, App::Action::Definition, int(i));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%zu placements in this room\nSelect, then choose Edit selected model.", p.name.c_str(), instances);
        ImGui::PopID();
    }
    if (a.has_draft && a.draft_slot < 0 && !a.draft.id.empty())
        ImGui::TextWrapped("* %s (%zu cells)", a.draft.name.c_str(), size_t(std::count(a.draft.mask.begin(), a.draft.mask.end(), uint8_t(1))));
    ImGui::Separator();
    const bool changed = a.list_selection != a.sel_obj;
    for (size_t i = 0; i < a.model.objects.size(); ++i) {
        const Object o = a.model.objects[i];
        if (o.authored.active) continue;
        if(!matches_search(class_word(o.cls))) continue;
        ImGui::PushID(int(i));
        char label[96];
        std::snprintf(label, sizeof(label), "%02zu  %-9s %s\n      %d,%d  %dx%d",
            i, class_word(o.cls), o.authored.active ? "A" : "", o.x, o.y, o.w, o.extent);
        const bool selected = a.sel_obj == int(i);
        if (ImGui::Selectable(label, selected, 0, ImVec2(0, 38))) {
            for (int y = o.y; y < o.y + o.extent; ++y) {
                bool found = false;
                for (int x = o.x; x < o.x + o.w; ++x) if (a.model.object_at(x, y) == int(i)) {
                    request_action(a, App::Action::Select, x, y); found = true; break;
                }
                if (found) break;
            }
            a.pan_x = std::clamp(float(o.x * 16) - a.layout.map.w * 0.4f,
                                0.0f, std::max(0.0f, float(a.room.w) - a.layout.map.w));
            a.pan_y = std::clamp(float(o.y * 16) - a.layout.map.h * 0.4f,
                                0.0f, std::max(0.0f, float(a.room.h) - a.layout.map.h));
        }
        if (selected && changed) ImGui::SetScrollHereY(0.4f);
        if (selected && g_probe && g_probe->it == kItCapture)
            g_probe->selected_row = int(i);
        ImGui::PopID();
    }
    a.list_selection = a.sel_obj;
    ImGui::EndChild();
    ImGui::Separator();
    if (ImGui::Button("NEW")) request_action(a, App::Action::New);
    ImGui::SameLine();
    ImGui::BeginDisabled(!a.has_draft);
    if (ImGui::Button("DISSOLVE")) request_action(a, App::Action::Dissolve);
    ImGui::EndDisabled();
    if (ImGui::Button("RE-AUTO")) request_action(a, App::Action::Reauto);
    ImGui::End();
}

#include "gui_voxel.inl"
#include "gui_coverage.inl"

void mask_controls(App& a) {
    ImGui::TextUnformatted("PIXEL OPACITY");
    if (!populated(a.draft)) { ImGui::TextWrapped("Add member cells in SEGMENT first."); return; }
    if (ImGui::RadioButton("Erase (E)", a.mask_erase)) a.mask_erase = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Restore (R)", !a.mask_erase)) a.mask_erase = false;
    if (a.mask_image_dirty && !a.mask_image.rgba.empty()) {
        if (a.mask_image.upload()) a.mask_image_dirty = false;
    }
    if (a.mask_image.texture && a.mask_art.w > 0) {
        const float scale = std::min(ImGui::GetContentRegionAvail().x / a.mask_art.w, 224.0f / a.mask_art.h);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(a.mask_art.w * scale, a.mask_art.h * scale);
        a.mask_canvas = {origin.x, origin.y, size.x, size.y};
        ImGui::InvisibleButton("mask-pixels", size);
        a.mask_hover = ImGui::IsItemHovered();
        auto dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(origin, ImVec2(origin.x+size.x,origin.y+size.y), true);
        for (int y=0; y<int(size.y); y+=8) for (int x=0; x<int(size.x); x+=8)
            dl->AddRectFilled(ImVec2(origin.x+x,origin.y+y),ImVec2(origin.x+x+8,origin.y+y+8),
                ((x/8+y/8)&1) ? IM_COL32(105,109,115,255) : IM_COL32(160,164,170,255));
        dl->AddImage(ImTextureID(a.mask_image.texture),origin,ImVec2(origin.x+size.x,origin.y+size.y));
        int x=0,y=0;
        if (a.mask_hover && canvas_pixel(a,ImGui::GetIO().MousePos.x,ImGui::GetIO().MousePos.y,&x,&y))
            dl->AddRect(ImVec2(origin.x+x*scale,origin.y+y*scale),ImVec2(origin.x+(x+1)*scale,origin.y+(y+1)*scale),IM_COL32(255,210,60,255));
        dl->PopClipRect();
    }
    size_t opaque=0;
    for (uint32_t pixel : a.mask_image.rgba) if (pixel>>24) ++opaque;
    ImGui::Text("OPAQUE %zu / %zu",opaque,a.mask_image.rgba.size());
    ImGui::TextDisabled(a.draft.cutout ? "M  authored cutout" : "Original source opacity");
    if (ImGui::Button("RESTORE ALL",ImVec2(-1,0))) {
        const auto before = a.document.state;
        a.draft.cutout = vr::cutout::original_opacity(a.mask_art);
        if (a.draft.id.empty()) a.draft.id = a.document.new_id();
        a.document.record(before); remesh(a);
    }
    if (ImGui::Button("USE INFERENCE",ImVec2(-1,0))) remove_cutout(a);
    if (ImGui::Button("APPLY CUTOUT",ImVec2(-1,0))) apply_draft(a);
    if (ImGui::Button("REVERT",ImVec2(-1,0))) revert_draft(a);
    ImGui::TextWrapped("Drag to paint. E erases; R restores source opacity. Escape cancels the current stroke.");
    ImGui::Separator();
    if(ImGui::Button("START VOXEL MODEL",ImVec2(-1,0))) voxel_start(a);
    ImGui::TextWrapped("Starts a new pixel model from this mask. Undo restores the previous parts.");
}

void source_resolution_controls(App& a) {
    if (!source_resolution_needed(a)) return;
    ImGui::TextColored(ImVec4(1.f,.65f,.25f,1.f), "STORED SOURCE UNRESOLVED");
    ImGui::TextWrapped("%s at %d, %d (backup)",
        a.draft.source.room.empty() ? "No stored source room" : a.draft.source.room.c_str(),
        a.draft.source.x, a.draft.source.y);
    ImGui::TextWrapped("The definition is preserved. Choose a verified placement below to update its source, then Apply.");
    const auto resolved = vr::overrides::resolve(a.snap, effective(a));
    bool any = false;
    for (const auto& m : resolved.accepted) if (m.pattern == a.draft_slot) {
        any = true;
        char label[80]; std::snprintf(label, sizeof(label), "USE SOURCE %d, %d##source-%d-%d", m.x, m.y, m.x, m.y);
        if (ImGui::Button(label, ImVec2(-1,0))) { reselect_source(a, m.x, m.y); break; }
    }
    if (!any) ImGui::TextWrapped("No accepted placement in this room. Navigate to a compatible room and select an instance explicitly, or keep or dissolve this definition.");
}

void inspector_panel(App& a) {
    a.mask_hover = false;
    shell_begin("Inspector", a.layout.inspector, g_probe ? &g_probe->panel_selection : nullptr);
    if(a.exploring) {connected_inspector(a);ImGui::End();return;}
    if(a.mode==3 && a.terrain.open) {terrain_inspector(a);ImGui::End();return;}
    if(a.has_draft && a.draft.voxel && (a.mode==1 || a.mode==2)) {voxel_inspector(a);ImGui::End();return;}
    if(a.mode==3) {
        ImGui::TextUnformatted(a.has_draft?"SELECTED OBJECT":"START EDITING");ImGui::Separator();
        if(a.has_draft) {
            ImGui::TextWrapped("%s",a.draft.name.c_str());
            ImGui::TextDisabled("%zu parts / %zu placements",a.draft.parts.size(),a.accepted_matches);
            if(a.mask_image_dirty && !a.mask_image.rgba.empty() && a.mask_image.upload()) a.mask_image_dirty=false;
            if(a.mask_image.texture && a.mask_image.w>0 && a.mask_image.h>0) {
                const float scale=std::min(ImGui::GetContentRegionAvail().x/a.mask_image.w,120.f/a.mask_image.h);
                ImGui::Image(ImTextureID(a.mask_image.texture),ImVec2(a.mask_image.w*scale,a.mask_image.h*scale));
            }
            source_resolution_controls(a);
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.87f,.62f,.55f,1));
            if(ImGui::Button(a.draft.voxel || !a.draft.parts.empty()?"EDIT SHAPE":"MAKE A MODEL",ImVec2(-1,28))) edit_selected_model(a);
            ImGui::PopStyleColor();
            if(ImGui::Button("Edit object / ground pixels",ImVec2(-1,0))) switch_mode(a,1);
            if(ImGui::Button("Focus this object",ImVec2(-1,0))) focus_selection(a);
            ImGui::TextWrapped("Shape the body, roof and details. Return to Scene to check the result in the room.");
            if(ImGui::Button("SAVE CHANGES",ImVec2(-1,28))) save_changes(a);
            ImGui::TextDisabled(a.document.draft_dirty()?"Preview includes your edits":"Saved model / current selection");
            ImGui::Separator();
            ImGui::BeginDisabled(a.pending!=App::Action::None || a.document.draft_dirty() || a.draft_slot<0);
            if(ImGui::Button("Level foundation",ImVec2(-1,26))) level_foundation(a);
            ImGui::EndDisabled();
            ImGui::TextWrapped("Raise a flat pad to the highest ground under the base. Entrance steps may be needed. Ctrl+Z undoes it.");
            if(a.document.draft_dirty()) ImGui::TextWrapped("Save model edits first, then level its foundation.");
        } else {
            ImGui::TextWrapped("Choose a building, tree or prop in the map above, or select a named model on the left.");
            ImGui::Spacing();
            ImGui::TextWrapped("1. Select an object\n2. Separate its pixels\n3. Shape its parts\n4. Check it in the scene");
            ImGui::Spacing();
            ImGui::TextWrapped("The next action appears here when you select something. Your source art stays available above the 3D view.");
        }
        ImGui::Separator();
        const auto& stats=vr::diorama::diorama_stats();
        ImGui::Text("%zu models placed in this room",stats.raised_instances);
        ImGui::TextWrapped("Drag to orbit. Scroll to zoom. Check the back, sides and ground contact.");
        if(ImGui::Button("FIT ROOM",ImVec2(-1,0))) focus_room(a);
        if(ImGui::CollapsingHeader("Scene details")) {
            ImGui::Text("Accepted instances %zu",stats.accepted_instances);
            ImGui::Text("Flat instances %zu",stats.accepted_instances-stats.raised_instances);
            ImGui::Text("Part entries %zu",stats.part_instances);
            ImGui::Text("Triangles %zu",(stats.authored_vertices+stats.flat_vertices)/3);
            ImGui::Text("geom=%s",stats_value(a.last_stats,"geom").c_str());
            if(a.has_draft) {
                ImGui::Text("Source %d, %d (backup)",a.origin_x,a.origin_y);
                ImGui::TextWrapped("%s",a.draft.id.c_str());
                ImGui::Text("Rejected matches %zu",a.rejected_matches);
                if(ImGui::Button("Apply without saving")) apply_draft(a);
                if(ImGui::Button("Revert current edits")) revert_draft(a);
            }
        }
        ImGui::End();return;
    }
    ImGui::TextUnformatted("GROUP / PROPOSAL");
    ImGui::Separator();
    if (!a.has_draft) {
        ImGui::TextWrapped("Select an object in the map, list or 3D view.");
        ImGui::End();
        return;
    }
    auto field = [&](const studio::EditorState& before) {
        if (ImGui::IsItemActivated()) a.field_before = before;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (a.draft.id.empty()) a.draft.id = a.document.new_id();
            a.document.record(a.field_before); remesh(a);
        }
    };
    auto before_field = a.document.state;
    char name[257]; std::snprintf(name, sizeof(name), "%s", a.draft.name.c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##name", name, sizeof(name))) a.draft.name = name;
    field(before_field);
    ImGui::Text("DRAWING     %d x %d", a.draft.w, a.draft.extent);
    const int members = int(std::count(a.draft.mask.begin(), a.draft.mask.end(), uint8_t(1)));
    ImGui::Text("CELLS       %d", members);
    ImGui::Text("SOURCE      %d, %d (backup)", a.origin_x, a.origin_y);
    ImGui::TextDisabled(a.draft_slot >= 0 ? "A  authored definition" : "Proposal / unapplied group");
    source_resolution_controls(a);
    if (a.mode == 1) { ImGui::Separator(); mask_controls(a); ImGui::End(); return; }
    if (a.mode == 2) {
        ImGui::Separator(); ImGui::TextUnformatted("MODEL / PARTS");
        ImGui::Text("%d production vertices",a.preview_vertices);
        ImGui::Text("geom=%016llx",static_cast<unsigned long long>(a.preview_hash));
        ImGui::TextDisabled("Drag to orbit; wheel to zoom");
        for(const auto& part:a.draft.parts) {
            ImGui::PushID(part.id.c_str());
            if(ImGui::Selectable(part.name.c_str(),a.document.state.selected_part==part.id)) a.document.state.selected_part=part.id;
            ImGui::PopID();
        }
        if(ImGui::Button("+ BOX")) add_part(a,vr::overrides::PartKind::Box);
        ImGui::SameLine(); ImGui::BeginDisabled(!a.draft.cutout);
        if(ImGui::Button("+ BILLBOARD")) add_part(a,vr::overrides::PartKind::Billboard);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button("+ WEDGE")) add_part(a,vr::overrides::PartKind::Wedge);
        auto selected=std::find_if(a.draft.parts.begin(),a.draft.parts.end(),[&](const auto& p){return p.id==a.document.state.selected_part;});
        if(selected!=a.draft.parts.end()) {
            if(ImGui::Button("DUPLICATE")) { add_part(a,selected->kind,true); ImGui::End(); return; }
            ImGui::SameLine();
            if(ImGui::Button("DELETE")) { delete_part(a); ImGui::End(); return; }
            ImGui::PushID(selected->id.c_str());
            before_field=a.document.state;
            char label[257]; std::snprintf(label,sizeof(label),"%s",selected->name.c_str());
            ImGui::SetNextItemWidth(-1);
            if(ImGui::InputText("##part_name",label,sizeof(label))) selected->name=label;
            field(before_field);
            auto vector=[&](const char* label,vr::part_geometry::Vec& value,float lo,float hi) {
                const auto before=a.document.state;
                float v[]={value.x,value.y,value.z};
                ImGui::TextUnformatted(label); ImGui::SetNextItemWidth(-1);
                ImGui::PushID(label);
                if(ImGui::InputFloat3("##xyz",v,"%.4f")) {
                    for(float& n:v) if(!std::isfinite(n)) n=lo; else n=std::clamp(n,lo,hi);
                    value={v[0],v[1],v[2]};
                }
                field(before); ImGui::PopID();
            };
            vector("Position / X Y Z",selected->transform.position,-128,128);
            vector("Size / X Y Z",selected->transform.size,1.f/16,64);
            vector("Angles / X Y Z",selected->transform.angles,-360,360);
            if(selected->kind==vr::overrides::PartKind::Wedge) {
                const auto before=a.document.state;
                int axis=selected->wedge_axis==0?0:1;
                if(ImGui::Combo("Slope axis",&axis,"X\0Z\0")) {
                    selected->wedge_axis=axis==0?0:2;a.document.record(before);remesh(a);
                }
                const auto direction_before=a.document.state;
                bool positive=selected->wedge_direction>0;
                if(ImGui::Checkbox("High edge at +axis",&positive)) {
                    selected->wedge_direction=positive?1:-1;a.document.record(direction_before);remesh(a);
                }
            }
            if(selected->kind!=vr::overrides::PartKind::Billboard && ImGui::CollapsingHeader("Source art region")) {
                const auto before=a.document.state;
                auto region=selected->art_region;
                if(region==std::array<int,4>{}) region={0,0,a.draft.w*16,a.draft.extent*16};
                ImGui::TextDisabled("X / Y / Width / Height (pixels)");
                ImGui::SetNextItemWidth(-1);
                if(ImGui::InputInt4("##art_region",region.data())) {
                    region[0]=std::clamp(region[0],0,a.draft.w*16-1);region[1]=std::clamp(region[1],0,a.draft.extent*16-1);
                    region[2]=std::clamp(region[2],1,a.draft.w*16-region[0]);region[3]=std::clamp(region[3],1,a.draft.extent*16-region[1]);
                    selected->art_region=region;
                }
                field(before);
                if(ImGui::Button("Whole drawing")) {
                    const auto before=a.document.state;selected->art_region={};a.document.record(before);remesh(a);
                }
            }
            ImGui::PopID();
        }
        if(!a.draft.parts.empty()) ImGui::TextWrapped("Parts define this model. Inference parameters are inactive.");
        if (ImGui::Button("EDIT MASK",ImVec2(-1,0))) switch_mode(a,1);
        if (ImGui::Button("APPLY",ImVec2(-1,0))) apply_draft(a);
        if (ImGui::Button("REVERT",ImVec2(-1,0))) revert_draft(a);
        ImGui::Separator();
        if(ImGui::Button("START VOXEL MODEL",ImVec2(-1,0))) voxel_start(a);
        ImGui::TextWrapped("Starts a new pixel model from the mask. Undo restores these parts.");
        ImGui::End(); return;
    }
    if (!a.draft.id.empty()) ImGui::TextDisabled("%s", a.draft.id.c_str());
    ImGui::Separator();
    if (ImGui::Button("FIND EVERY INSTANCE", ImVec2(-1, 0))) {
        a.show_matches = true;
        a.status = "Verified matches shown in the current snapshot.";
    }
    ImGui::Text("%zu accepted / %zu rejected", a.accepted_matches, a.rejected_matches);
    ImGui::TextDisabled("Current snapshot only");
    ImGui::TextWrapped("Apply reuses this pattern in every compatible map.");
    ImGui::Separator();
    const char* classes[] = {"Infer", "Prop", "Structure", "Mass"};
    if(!a.draft.parts.empty()) ImGui::TextWrapped("Parts define this model; inference parameters are inactive.");
    ImGui::BeginDisabled(!a.draft.parts.empty());
    int cls = int(a.draft.apply.cls) + 1;
    before_field = a.document.state;
    if (ImGui::Combo("Class", &cls, classes, 4)) {
        a.draft.apply.cls = vr::overrides::ApplyClass(cls-1);
        if (a.draft.id.empty()) a.draft.id = a.document.new_id();
        a.document.record(before_field); remesh(a);
    }
    before_field = a.document.state;
    ImGui::InputInt("Height", &a.draft.apply.height);
    a.draft.apply.height = std::clamp(a.draft.apply.height, -1, 256);
    field(before_field);
    before_field = a.document.state;
    ImGui::InputFloat("Rise", &a.draft.apply.rise, .1f, 1, "%.2f");
    a.draft.apply.rise = std::clamp(a.draft.apply.rise, -1.0f, 64.0f);
    field(before_field);
    ImGui::TextUnformatted("ROOF ROWS");
    ImGui::TextDisabled("-1 leaves the value to inference");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputInt("##roof_rows", &a.edit_roof_rows);
    if (ImGui::IsItemDeactivatedAfterEdit()) preview_roof_rows(a, a.edit_roof_rows);
    ImGui::EndDisabled();
    char apply[72];
    std::snprintf(apply, sizeof(apply), "APPLY TO %zu INSTANCE%s", a.accepted_matches,
                  a.accepted_matches == 1 ? "" : "S");
    if (ImGui::Button(apply, ImVec2(-1, 0))) apply_draft(a);
    if (ImGui::Button("REVERT", ImVec2(-1, 0))) revert_draft(a);
    ImGui::Separator();
    ImGui::Text("TILES IN GROUP  %zu", a.draft.tiles.size());
    for (const auto& tile : a.draft.tiles) ImGui::Text("TILE %u", unsigned(tile.first));
    ImGui::End();
}

void workspace_panel(App& a) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.89f, 0.90f, 1));
    shell_begin("Workspace", a.layout.workspace, g_probe ? &g_probe->panel_workspace : nullptr, true);
    const bool source=a.has_draft && a.draft.voxel && (a.mode==1 || a.mode==2);
    if(source) {
        ImGui::Text("SOURCE DRAWING / %s",a.draft.name.c_str());
        ImGui::SameLine();ImGui::TextDisabled("%d x %d pixels",a.mask_art.w,a.mask_art.h);
    } else {
        ImGui::Text("%s  /  %s", a.map_id.c_str(), a.tileset_label.c_str());
        ImGui::SameLine(); ImGui::TextDisabled("1:1");
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const studio::Rect m = a.layout.map, v = a.layout.view;
    bool map_hover=false;
    if(source) voxel_source_canvas(a,m);
    else {
    const ImVec2 map_min(m.x, m.y), map_max = corner(m);
    ImGui::SetCursorScreenPos(map_min);
    ImGui::InvisibleButton("map-surface", ImVec2(m.w, m.h),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    map_hover = ImGui::IsItemHovered();
    dl->AddRectFilled(map_min, map_max, IM_COL32(30, 33, 36, 255));
    dl->PushClipRect(map_min, map_max, true);
    const ImVec2 image_min(m.x - a.pan_x, m.y - a.pan_y);
    dl->AddImage(ImTextureID(a.room.texture), image_min,
        ImVec2(image_min.x + a.room.w, image_min.y + a.room.h));
    if (a.show_grid) {
        for (int x = 0; x <= a.snap.width; ++x) {
            const float px = image_min.x + x * 16;
            dl->AddLine(ImVec2(px, m.y), ImVec2(px, m.y + m.h), IM_COL32(20, 30, 35, 45));
        }
        for (int y = 0; y <= a.snap.height; ++y) {
            const float py = image_min.y + y * 16;
            dl->AddLine(ImVec2(m.x, py), ImVec2(m.x + m.w, py), IM_COL32(20, 30, 35, 45));
        }
    }
    if(a.mode==3 && a.terrain.open) terrain_overlay(a,dl,image_min);
    if (a.has_draft && a.show_matches && !(a.mode==3 && a.terrain.open))
        for (const auto& match : a.draft_matches)
            dl->AddRect(ImVec2(image_min.x + match.x * 16, image_min.y + match.y * 16),
                ImVec2(image_min.x + (match.x + a.draft.w) * 16,
                       image_min.y + (match.y + a.draft.extent) * 16),
                IM_COL32(80, 155, 250, 255), 0, 0, 2);
    if (a.has_draft && populated(a.draft) && !(a.mode==3 && a.terrain.open)) {
        for (int y = 0; y < a.draft.extent; ++y)
            for (int x = 0; x < a.draft.w; ++x)
                if (a.draft.mask[size_t(y) * a.draft.w + x]) {
                    const ImVec2 p(image_min.x + (a.origin_x + x) * 16, image_min.y + (a.origin_y + y) * 16);
                    dl->AddRectFilled(p, ImVec2(p.x + 16, p.y + 16), IM_COL32(240, 110, 75, 100));
                }
        dl->AddRect(ImVec2(image_min.x + a.origin_x * 16, image_min.y + a.origin_y * 16),
            ImVec2(image_min.x + (a.origin_x + a.draft.w) * 16, image_min.y + (a.origin_y + a.draft.extent) * 16),
            IM_COL32(255, 210, 60, 255), 0, 0, 2);
    }
    if(a.review.focused_map==a.map_id && !a.review.focused.id.empty()) {
        const auto& r=a.review.focused;
        dl->AddRect(ImVec2(image_min.x+r.x*16,image_min.y+r.y*16),
                    ImVec2(image_min.x+(r.x+r.w)*16,image_min.y+(r.y+r.h)*16),
                    IM_COL32(255,105,90,255),0,0,3);
    }
    int hx = 0, hy = 0;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (map_hover && studio::map_cell(m, a.pan_x, a.pan_y, mouse.x, mouse.y,
                                     a.snap.width, a.snap.height, &hx, &hy))
        dl->AddRect(ImVec2(image_min.x + hx * 16, image_min.y + hy * 16),
            ImVec2(image_min.x + (hx + 1) * 16, image_min.y + (hy + 1) * 16),
            IM_COL32(255, 255, 255, 240));
    dl->PopClipRect();
    dl->AddRect(map_min, map_max, IM_COL32(130, 135, 140, 150));
    }
    if (a.mode == 1) {
        a.mouse_surface = a.mask_hover ? 3 : map_hover ? 1 : 0;
        ImGui::End(); ImGui::PopStyleColor(); return;
    }
    ImGui::SetCursorScreenPos(ImVec2(v.x, v.y - 22));
    ImGui::TextUnformatted(a.mode == 2 ? "MODEL / ISOLATED PREVIEW" : a.mode==3?"DIORAMA":"PRODUCTION VIEW");
    ImGui::SetCursorScreenPos(ImVec2(v.x+220,v.y-24));
    ImGui::SetNextItemWidth(160);
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.07f,.08f,.11f,1));
    ImGui::BeginDisabled(a.pending != App::Action::None || a.handle.active || a.fly_looking);
    const std::string light_label=std::string("Light: ")+studio::environment::name(a.lighting);
    if(ImGui::BeginCombo("##lighting-preview",light_label.c_str())) {
        for(int i=0;i<5;++i) {
            const auto phase=studio::environment::Phase(i);
            if(ImGui::Selectable(studio::environment::name(phase),a.lighting==phase)) {
                a.lighting=phase;
                a.preview_draw_samples=0;
            }
        }
        ImGui::EndCombo();
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Lighting preview only. Choose Neutral to reset.\nYour source pixels and saved models keep their original colors.");
    ImGui::EndDisabled();
    ImGui::PopStyleColor();
    // Right-aligned within the old title strip: fixed inspector and handle
    // toolbar coordinates stay intact at every supported window size.
    ImGui::SetCursorScreenPos(ImVec2(v.x+v.w-400,v.y-24));
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.23f,.27f,.32f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.36f,.42f,.48f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(.53f,.30f,.29f,1));
    ImGui::BeginDisabled(!camera_input_allowed(a,ImGui::GetIO().WantTextInput) || a.fly_looking);
    if(ImGui::Button("Zoom -")) camera_zoom(a.camera,-1);
    ImGui::SameLine(); if(ImGui::Button("+##zoom")) camera_zoom(a.camera,1);
    ImGui::SameLine();
    ImGui::BeginDisabled(a.exploring || !a.has_draft || !populated(a.draft));
    if(ImGui::Button(a.mode==2?"Focus Model":"Focus Selected")) focus_selection(a);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(a.mode==2) { ImGui::TextDisabled("F"); }
    else if(a.exploring) {if(ImGui::Button("Fit area")) focus_connected(a);}
    else if(ImGui::Button("Room")) focus_room(a);
    ImGui::SameLine();
    if(ImGui::Checkbox("Fly",&a.fly_mode)) stop_fly_look(a);
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Hold right mouse in the 3D view to look and fly.\nWASD moves; Q/E down/up; Shift moves faster.\nRelease right mouse or press Escape to stop.");
    ImGui::SetCursorScreenPos(ImVec2(v.x, v.y));
    ImGui::InvisibleButton("view-surface", ImVec2(v.w, v.h),ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    a.mouse_surface = source && a.mask_hover ? 3 : map_hover ? 1 : ImGui::IsItemHovered() ? 2 : 0;
    dl->AddRect(ImVec2(v.x, v.y), corner(v), IM_COL32(130, 135, 140, 150));
    draw_handles(a,dl);
    if(a.mode==2) {
        const auto* part=selected_part(a);
        const std::string label=part?"Selected: "+(part->name.empty()?part->id:part->name):"Click a part to select it";
        const char* hint="Click selects / drag orbits / colored handles edit / Shift+F focuses part";
        const ImVec2 pos(v.x+10,v.y+10);
        const float width=std::min(v.w-20,std::max(ImGui::CalcTextSize(label.c_str()).x,ImGui::CalcTextSize(hint).x)+16);
        dl->PushClipRect(ImVec2(v.x,v.y),corner(v),true);
        dl->AddRectFilled(pos,ImVec2(pos.x+width,pos.y+42),IM_COL32(15,20,27,225),4);
        dl->AddText(ImVec2(pos.x+8,pos.y+5),IM_COL32(255,220,100,255),label.c_str());
        dl->AddText(ImVec2(pos.x+8,pos.y+23),IM_COL32(200,210,220,255),hint);
        dl->PopClipRect();
    }
    ImGui::SetCursorScreenPos(ImVec2(v.x, v.y + v.h + 6));
    if(a.mode==2) {
        for(int tool=0;tool<3;++tool) {
            if(tool) ImGui::SameLine();
            if(ImGui::RadioButton(tool==0?"Move":tool==1?"Resize":"Rotate",a.handle_tool==tool)) a.handle_tool=tool;
        }
        if(a.draft.voxel) voxel_view_controls(a);
        else {ImGui::SameLine();ImGui::TextDisabled(a.fly_mode?"RMB look + WASD / Q E vertical / Shift fast":"local axes / Escape cancels / wheel zoom / F focus");}
        ImGui::End();ImGui::PopStyleColor();return;
    }
    ImGui::TextUnformatted("FREE / ROOM"); ImGui::SameLine();
    ImGui::Checkbox("Grid", &a.show_grid); ImGui::SameLine();
    ImGui::Checkbox("Classification", &a.debug);
    ImGui::SameLine(); ImGui::TextDisabled(a.fly_mode?"RMB + WASD / Q E vertical / Shift fast":"drag orbit / wheel zoom / F focus");
    ImGui::End();
    ImGui::PopStyleColor();
}

void status_panel(App& a) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 3));
    shell_begin("Status", a.layout.status);
    ImGui::Text("%s%s",a.has_draft?a.draft.name.c_str():"Choose an object to begin",
        a.document.draft_dirty()?"  /  editing preview":"");
    if (!a.status.empty()) ImGui::TextUnformatted(a.status.c_str());
    else ImGui::TextDisabled(!a.has_draft?"Click the map or a model in the list. Room / All switches the model library.":a.mode==0?
        "Click tiles to add them; Shift removes. Choose Next: cut out pixels when the object is selected.":a.mode==1?
        "Object pixels become the model. Ground stays on the floor. E erases; R restores.":a.mode==2?
        "Click a part in the model or list. Drag colored handles to edit; drag elsewhere to orbit. Ctrl+S saves.":
        "Edit shape returns to the selected model. Drag to orbit; scroll to zoom; F focuses the selection.");
    ImGui::End();
    ImGui::PopStyleVar();
}

#include "gui_asset_review.inl"

void style_studio() {
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = style.ChildRounding = style.FrameRounding = 0;
    style.WindowPadding = ImVec2(8, 8);
    style.ItemSpacing = ImVec2(8, 6);
    style.WindowBorderSize = 1;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.935f, 0.92f, 1);
    style.Colors[ImGuiCol_Button] = ImVec4(0.85f, 0.82f, 0.80f, 1);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.91f, 0.71f, 0.68f, 1);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.82f, 0.48f, 0.47f, 1);
    style.Colors[ImGuiCol_Header] = ImVec4(0.88f, 0.71f, 0.67f, 1);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.91f, 0.80f, 0.76f, 1);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.68f, 0.16f, 0.23f, 1);
}

}  // namespace

int main(int argc, char** argv) {
    const char* decomp_root = "third_party/pokeruby";
    const char* map_id      = "MAP_ROUTE101";
    const char* in_path     = nullptr;
    const char* initial_mode=nullptr;
    bool initial_connected=false;
    // DEFAULTS INTO build/, NOT THE REPO ROOT. This tree is a separate
    // checkout of someone else's work; build/ is gitignored and the root is
    // not, so a default that landed here would show up as an untracked file
    // in a repository this project has no business writing to.
    const char* out_path    = "build/rubyvr-overrides.json";
    const char* probe_path  = nullptr;
    const char* showcase_path = nullptr;
    const char* review_path = "build/coverage/studio/index.json";
    const char* asset_review = nullptr;
    const char* room_review = nullptr;
    const char* probe_out   = "build/rubyvr-gui-probe";
    bool selftest = false;

    for (int i = 1; i < argc; ++i) {
        const char* s = argv[i];
        auto next = [&](const char** dst) {
            if (i + 1 < argc) *dst = argv[++i];
            else std::fprintf(stderr, "[gui] %s needs a value\n", s);
        };
        if      (!std::strcmp(s, "--decomp"))    next(&decomp_root);
        else if (!std::strcmp(s, "--map"))       next(&map_id);
        else if (!std::strcmp(s, "--connected")) initial_connected=true;
        else if (!std::strcmp(s, "--overrides")) next(&in_path);
        else if (!std::strcmp(s, "--mode")) { next(&initial_mode);if(!initial_mode) return 2; }
        else if (!std::strcmp(s, "--out"))       next(&out_path);
        else if (!std::strcmp(s, "--selftest"))  selftest = true;
        else if (!std::strcmp(s, "--probe"))     next(&probe_path);
        else if (!std::strcmp(s, "--showcase"))  next(&showcase_path);
        else if (!std::strcmp(s, "--review-index")) next(&review_path);
        else if (!std::strcmp(s, "--asset-review")) next(&asset_review);
        else if (!std::strcmp(s, "--room-review")) next(&room_review);
        else if (!std::strcmp(s, "--probe-out")) next(&probe_out);
        else {
            std::fprintf(stderr,
                "usage: rubyvr_gui [--decomp <pokeruby>] [--map MAP_ROUTE101]\n"
                "                  [--overrides <file>] [--out <file>]\n"
                "                  [--mode inferred|diorama]\n"
                "                  [--connected]\n"
                "                  [--review-index <coverage/index.json>]\n"
                "                  [--room-review <capture.png>]\n"
                "                  [--showcase <events.json> --probe <scenario.json>]\n"
                "                  [--selftest]\n"
                "                  [--probe <scenario.json> "
                "[--probe-out <prefix>]]\n"
                "\n"
                "  --overrides  an existing override file to open. READ ONLY:\n"
                "               this tool never writes back to it.\n"
                "  --out        where Save writes. Editable in the UI, and\n"
                "               refused if it resolves to the input file.\n"
                "               Default: build/rubyvr-overrides.json\n"
                "  --selftest   run the Route 101 authoring round-trip through\n"
                "               the real GUI actions, print one PASS/FAIL line,\n"
                "               and exit. Cannot be combined with --overrides.\n"
                "  --probe      drive one scripted click through the real event\n"
                "               loop in a hidden window, capture the rendered\n"
                "               frame, and write <prefix>.png and <prefix>.json.\n"
                "               Default prefix: build/rubyvr-gui-probe\n");
            return 2;
        }
    }

    if(initial_connected) {
        if(selftest || asset_review || room_review || (probe_path && !showcase_path) ||
           (initial_mode && std::strcmp(initial_mode,"diorama"))) {
            std::fprintf(stderr,"[connected] use --connected with the normal DIORAMA view or its showcase\n");return 2;
        }
        initial_mode="diorama";
    }
    if(asset_review && (!in_path || selftest || probe_path || showcase_path)) return 2;
    if(room_review && (!in_path || selftest || probe_path || showcase_path || asset_review)) return 2;
    if(initial_mode && std::strcmp(initial_mode,"inferred") && std::strcmp(initial_mode,"diorama")) {
        std::fprintf(stderr,"[gui] --mode must be inferred or diorama\n");return 2;
    }
    if(showcase_path && (!probe_path || selftest)) {std::fprintf(stderr,"[showcase] requires --probe; excludes --selftest\n");return 2;}
    if(initial_mode && !std::strcmp(initial_mode,"diorama") && (selftest || (probe_path && !showcase_path))) {
        std::fprintf(stderr,"[gui] selftest/probe must start in inferred mode\n");return 2;
    }
    if (selftest && in_path) {
        std::fprintf(stderr,
                     "[gui-selftest] --overrides is not allowed; the check "
                     "starts from the inferred baseline\n");
        return 2;
    }
    if (probe_path && (selftest || (in_path && !showcase_path))) {
        std::fprintf(stderr,
                     "[visual-probe] --probe cannot be combined with "
                     "--selftest or --overrides; it starts from the inferred "
                     "baseline\n");
        return 2;
    }

    // Load the scenario BEFORE SDL, because it owns the viewport the window is
    // created at. A probe whose window disagreed with its scenario would put the
    // frozen click pixel in a different place than the one it was chosen in.
    ProbeScenario scenario;
    Probe         probe;
    int           win_w = 1600, win_h = 950;
    if (probe_path) {
        if (!load_scenario(probe_path, &scenario)) {
            std::fprintf(stderr, "[visual-probe] %s failed to load\n",
                         probe_path);
            return 1;
        }
        win_w = scenario.vw;
        win_h = scenario.vh;
        probe.sc        = &scenario;
        probe.png_path  = std::string(probe_out) + ".png";
        probe.json_path = std::string(probe_out) + ".json";
        if(showcase_path && !load_showcase(showcase_path,probe)) {std::fprintf(stderr,"[showcase] invalid script\n");return 2;}
    }

    App app;
    app.map_id = map_id;
    app.in_path  = in_path ? in_path : "";
    app.out_path = out_path;
    if(!review_path) return 2;
    app.review.path=review_path;

    if (!app.decomp.open(decomp_root)) return 1;
    studio::BuildInfo info;
    if (!studio::build_snapshot(app.decomp, map_id, &app.snap, &info, {}))
        return 1;

    app.tileset_label = info.primary_name + " + " + info.secondary_name;

    // A FAILED LOAD IS FATAL, matching compare.cpp's policy. A tool that was
    // asked to open an override file and quietly opened the inferred world
    // instead would have the author editing against geometry the file does not
    // describe — silently, and for as long as nobody checked.
    if (in_path && !vr::overrides::load(in_path, &app.working)) {
        std::fprintf(stderr, "[gui] --overrides %s failed to load\n", in_path);
        return 1;
    }

    app.document.saved = app.working;
    app.review.baseline=app.working;
    app.review.input_fingerprint=studio::coverage::file_fingerprint(app.in_path);
    app.rooms = app.decomp.map_ids();

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[gui] SDL video init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    // HIDDEN FOR THE PROBE TOO, and not only to stay off the desktop. A shown
    // window takes keyboard focus, and imgui_impl_sdl2's UpdateMouseData then
    // feeds SDL_GetGlobalMouseState into AddMousePosEvent on every frame the
    // buttons are up — silently replacing the injected pixel with wherever the
    // real cursor happens to be. Hidden keeps SDL_GetKeyboardFocus() elsewhere,
    // and that is what makes the injection deterministic.
    SDL_Window* win = SDL_CreateWindow(
        "rubyvr_studio - override authoring",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
            ((selftest || probe_path || asset_review || room_review) ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN));
    if (!win) {
        std::fprintf(stderr, "[gui] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(win, 1280, 720);
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        std::fprintf(stderr, "[gui] SDL_GL_CreateContext failed: %s\n",
                     SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(win, ctx);
    SDL_GL_SetSwapInterval((selftest || probe_path || asset_review || room_review) ? 0 : 1);

    if (!vr::gl::load() || !vr::diorama::init()) {
        std::fprintf(stderr, "[gui] could not bring up the mesher's GL objects\n");
        if (vr::diorama::ready()) vr::diorama::shutdown();
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    if(asset_review) {
        const int result=run_asset_review(app,asset_review);
        vr::diorama::shutdown();SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(win);SDL_Quit();return result;
    }
    if(room_review) {
        app.mode=3;vr::diorama::set_build_mode(vr::diorama::BuildMode::Diorama);
        const bool ok=capture_room_png(app,room_review);
        vr::diorama::shutdown();SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(win);SDL_Quit();return ok?0:1;
    }
    if (probe_path) {
        // THE SCENARIO'S CAMERA, NOT THE AUTO-FRAMING BELOW. The frozen click
        // pixel is only meaningful under the camera it was chosen in, and the
        // auto-framing depends on snap.height — so a different map would
        // silently re-aim the probe at whatever now sits under that pixel.
        app.camera = scenario.cam;
        probe.window_id = SDL_GetWindowID(win);
        if (!probe_fbo_create(probe, int(std::lround(win_w * scenario.framebuffer_scale)),
                                     int(std::lround(win_h * scenario.framebuffer_scale)))) {
            probe_fbo_destroy(probe);
            vr::diorama::shutdown();
            SDL_GL_DeleteContext(ctx);
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 1;
        }
        g_probe = &probe;
        if(probe.showcasing && !showcase_open(probe)) {
            probe_fbo_destroy(probe);vr::diorama::shutdown();SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(win);SDL_Quit();return 1;
        }
    } else {
        // Frame the whole map, looking down at it.
        app.camera.tx   = static_cast<float>(app.snap.width)  * 0.5f;
        app.camera.tz   = static_cast<float>(app.snap.height) * 0.5f;
        app.camera.dist = static_cast<float>(app.snap.height) * 1.1f + 6.0f;
    }

    if(initial_mode && !std::strcmp(initial_mode,"diorama")) app.mode=3;
    remesh(app);
    std::fprintf(stderr, "[gui] baseline %s\n", app.last_stats.c_str());
    if(initial_connected && !enter_connected(app)) {
        std::fprintf(stderr,"[connected] launch refused: %s\n",app.status.c_str());
        vr::diorama::shutdown();SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(win);SDL_Quit();return 1;
    }

    if (selftest) {
        const int result = run_selftest(app);
        vr::diorama::shutdown();
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return result;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Fixed regions have no persisted window geometry, interactively or in probes.
    ImGui::GetIO().IniFilename = nullptr; // Fixed shell never reads stored geometry.
    style_studio();
    if (!app.room.build(app.snap) || !app.room.upload()) {
        std::fprintf(stderr, "[gui] room texture creation failed\n");
        app.room.release();
    app.mask_image.release();
        ImGui::DestroyContext();
        vr::diorama::shutdown();
        SDL_GL_DeleteContext(ctx); SDL_DestroyWindow(win); SDL_Quit();
        return 1;
    }
    ImGui_ImplSDL2_InitForOpenGL(win, ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    bool  running    = true;
    int& drag_surface = app.drag_surface;
    bool& panning = app.panning;
    float press_x    = 0.0f, press_y = 0.0f;
    float drag_moved = 0.0f;

    while (running) {
        // THE ONLY THING THE PROBE ADDS BEFORE THE LOOP'S OWN WORK: events into
        // SDL's queue. Everything below this line is the interactive path,
        // unmodified, which is what makes the probe evidence about it.
        if (g_probe) probe_pump(probe);

        SDL_Event e;
        float look_dx=0,look_dy=0,wheel_steps=0;
        bool focus_lost=false;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if(e.type==SDL_MOUSEMOTION && e.motion.windowID==SDL_GetWindowID(win) && app.fly_looking) {
                look_dx+=float(e.motion.xrel);look_dy+=float(e.motion.yrel);
            }
            if(e.type==SDL_MOUSEWHEEL && e.wheel.windowID==SDL_GetWindowID(win)) {
#if SDL_VERSION_ATLEAST(2,0,18)
                const float steps=e.wheel.preciseY;
#else
                const float steps=float(e.wheel.y);
#endif
                wheel_steps+=e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-steps:steps;
            }
            if (e.type == SDL_WINDOWEVENT && e.window.windowID == SDL_GetWindowID(win) &&
                e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                end_stroke(app, true); end_handle(app,true); drag_surface = 0; panning = false;
                if(app.terrain.selecting) {app.terrain.selecting=false;app.terrain.selected=false;}
                stop_fly_look(app);focus_lost=true;
            }
            if (e.type == SDL_QUIT) { stop_fly_look(app);end_handle(app,true);end_stroke(app,true);request_action(app, App::Action::Close); }
            if (e.type == SDL_WINDOWEVENT &&
                e.window.event == SDL_WINDOWEVENT_CLOSE &&
                e.window.windowID == SDL_GetWindowID(win)) {
                stop_fly_look(app);end_handle(app,true);end_stroke(app,true);request_action(app, App::Action::Close);
            }
        }

        ImGuiIO& io = ImGui::GetIO();

        int ww = 0, wh = 0;
        SDL_GetWindowSize(win, &ww, &wh);
        int dw = ww, dh = wh;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        if (ww <= 0 || wh <= 0) continue;
        const int render_w = g_probe ? probe.fbw : dw;
        const int render_h = g_probe ? probe.fbh : dh;


        app.layout = studio::ShellLayout::at(ww, wh);
        if (app.mode == 1) app.layout.map.h = app.layout.workspace.h - 40;
        const studio::Rect view = app.layout.view;
        const float mx = io.MousePos.x, my = io.MousePos.y;
        const int surface = app.pending == App::Action::None && !app.reset_pending && !app.review.open &&
            (!app.exploring || app.mouse_surface==2) ? app.mouse_surface : 0;
        // Surface ownership is observed from ImGui's actual hovered items on
        // the same NewFrame that produced this input. A captured viewport is
        // legitimate UI input; WantCaptureMouse does not mean "ignore it".
        const bool camera_allowed=!focus_lost && camera_input_allowed(app,io.WantTextInput);
        const bool escape=ImGui::IsKeyPressed(ImGuiKey_Escape) && !io.WantTextInput;
        const bool stopped_flying=app.fly_looking && (escape || !camera_allowed || !ImGui::IsMouseDown(ImGuiMouseButton_Right));
        if(stopped_flying) stop_fly_look(app);
        if(app.fly_mode && camera_allowed && surface==2 && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
           ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !stopped_flying) {
            app.camera.orthographic=false;
            app.fly_looking=true;
            // Hidden SDL probes consume the same xrel/yrel events without
            // capturing the user's cursor. Only a focused visible editor may
            // request native relative mode, so look has no desktop-edge limit.
            if(SDL_GetKeyboardFocus()==win && (SDL_GetWindowFlags(win)&SDL_WINDOW_SHOWN)) {
                SDL_SetRelativeMouseMode(SDL_TRUE);SDL_CaptureMouse(SDL_TRUE);
            }
            look_dx=look_dy=0;
        }
        if(app.fly_looking) {
            camera_look(app.camera,look_dx,look_dy);
            const float forward=float(ImGui::IsKeyDown(ImGuiKey_W))-float(ImGui::IsKeyDown(ImGuiKey_S));
            const float right=float(ImGui::IsKeyDown(ImGuiKey_D))-float(ImGui::IsKeyDown(ImGuiKey_A));
            const float vertical=float(ImGui::IsKeyDown(ImGuiKey_E))-float(ImGui::IsKeyDown(ImGuiKey_Q));
            camera_move(app.camera,forward,right,vertical,std::min(io.DeltaTime,.1f)*app.fly_speed*(io.KeyShift?4.f:1.f));
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && surface && !io.WantTextInput && !app.fly_looking && !focus_lost) {
            drag_surface = surface; press_x = mx; press_y = my; drag_moved = 0;
            if(surface==2 && app.mode==2) begin_handle(app,mx,my);
            if(surface==1 && app.mode==3 && app.terrain.open) terrain_press(app,mx,my);
            if (surface == 3 && (app.mode == 1 || app.draft.voxel)) {
                app.mask_stroke = true; app.stroke_before = app.document.state;
                app.stroke_x = app.stroke_y = -1;
                app.pixels.start_x=app.pixels.start_y=-1;
            }
            if (surface == 1 && app.has_draft && app.mode == 0) {
                app.membership_stroke = true; app.stroke_before = app.document.state;
                app.stroke_erase = io.KeyShift; app.stroke_x = app.stroke_y = -1;
            }
        }
        if (drag_surface && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float dx = io.MouseDelta.x, dy = io.MouseDelta.y;
            drag_moved += std::fabs(dx) + std::fabs(dy);
            if (app.mask_stroke) {
                int px=0,py=0;
                if (canvas_pixel(app,mx,my,&px,&py)) stroke_to(app,px,py);
                else app.stroke_x = app.stroke_y = -1;
            }
            if (app.membership_stroke) {
                int cx = 0, cy = 0;
                if (studio::map_cell(app.layout.map, app.pan_x, app.pan_y, mx, my,
                                     app.snap.width, app.snap.height, &cx, &cy)) stroke_to(app, cx, cy);
                else app.stroke_x = app.stroke_y = -1;
            }
            if(app.handle.active) update_handle(app,mx,my);
            if(app.terrain.selecting) terrain_motion(app,mx,my);
            if (drag_surface == 2 && !app.handle.active && !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                app.camera.yaw -= dx * 0.006f;
                app.camera.pitch += dy * 0.005f;
            }
        }
        if (drag_surface && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            int cx = 0, cy = 0;
            if(app.mode==3 && app.terrain.open) app.terrain.selecting=false;
            else if(app.handle.active) end_handle(app,false);
            else if (app.membership_stroke || app.mask_stroke) end_stroke(app, false);
            else if (drag_moved < 4.0f && surface == drag_surface && app.mode==2 && drag_surface==2) {
                select_model_part(app,press_x,press_y);
            } else if (!app.exploring && drag_moved < 4.0f && surface == drag_surface) {
                const bool hit = drag_surface == 1
                    ? studio::map_cell(app.layout.map, app.pan_x, app.pan_y,
                        press_x, press_y, app.snap.width, app.snap.height, &cx, &cy)
                    : view.contains(press_x, press_y) &&
                      pick_cell(app.camera, int(view.w), int(view.h),
                                press_x - view.x, press_y - view.y, &cx, &cy);
                if (hit && !(app.mode == 2 && drag_surface == 2)) request_action(app, App::Action::Select, cx, cy);
            }
            drag_surface = 0;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && surface == 1) panning = true;
        if (panning && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            app.pan_x = std::clamp(app.pan_x - io.MouseDelta.x, 0.0f,
                std::max(0.0f, float(app.room.w) - app.layout.map.w));
            app.pan_y = std::clamp(app.pan_y - io.MouseDelta.y, 0.0f,
                std::max(0.0f, float(app.room.h) - app.layout.map.h));
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) panning = false;
        // ImGui clears io.MouseWheel in EndFrame, before this loop observes its
        // previous-frame input. Preserve the same-window SDL delta explicitly.
        if (surface == 2 && wheel_steps != 0 && camera_allowed && !app.fly_looking)
            camera_zoom(app.camera,wheel_steps);
        if (escape && !stopped_flying) {
            if(app.exploring) leave_connected(app);
            else {
            if(app.terrain.selecting) {app.terrain.selecting=false;app.terrain.selected=false;}
            app.terrain.pick=0;
            if(app.review.open) app.review.open=false;
            else if(app.handle.active) end_handle(app,true);
            else if (app.membership_stroke || app.mask_stroke) end_stroke(app, true);
            else if (!drag_surface && !panning) request_action(app, App::Action::Select);
            }
            drag_surface = 0; panning = false;
        }
        if (!app.exploring && !io.WantTextInput && !app.review.open && app.pending == App::Action::None && !app.reset_pending && !drag_surface && !app.fly_looking && !stopped_flying) {
            if(surface==2 && camera_allowed && !io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F)) {
                if(io.KeyShift && app.mode==2) focus_part(app);else focus_selection(app);
            }
            if (app.mode == 1 && ImGui::IsKeyPressed(ImGuiKey_E)) app.mask_erase = true;
            if (app.mode == 1 && ImGui::IsKeyPressed(ImGuiKey_R)) app.mask_erase = false;
            if(app.has_draft && app.draft.voxel && (app.mode==1 || app.mode==2)) {
                if(!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E)) app.pixels.tool=4;
                if(!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R)) app.pixels.tool=5;
                if(!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_B)) app.pixels.tool=0;
                if(ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) voxel_cycle(app,-1);
                if(ImGui::IsKeyPressed(ImGuiKey_RightBracket)) voxel_cycle(app,1);
                if(app.mode==2 && ImGui::IsKeyPressed(ImGuiKey_Delete)) delete_part(app);
                if(app.mode==2 && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
                    if(auto* part=selected_part(app)) add_part(app,part->kind,true);
                }
                if(app.mode==2 && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                    if(auto* part=selected_part(app)) {
                        if(part->kind==vr::overrides::PartKind::Billboard) voxel_split(app);
                        else voxel_surface(app,false);
                    }
                }
                if(app.mode==2 && surface==2 && !io.KeyCtrl && !io.KeyAlt) {
                    if(ImGui::IsKeyPressed(ImGuiKey_0)) voxel_preset(app,0);
                    if(ImGui::IsKeyPressed(ImGuiKey_1)) voxel_preset(app,1);
                    if(ImGui::IsKeyPressed(ImGuiKey_3)) voxel_preset(app,3);
                    if(ImGui::IsKeyPressed(ImGuiKey_7)) voxel_preset(app,5);
                }
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) history(app, io.KeyShift);
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) history(app, true);
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) save_changes(app);
        }
        const float pitch_limit=app.camera.orthographic?1.57079633f:1.50f;
        app.camera.pitch = std::clamp(app.camera.pitch, -pitch_limit, pitch_limit);
        app.camera.dist = std::clamp(app.camera.dist, .25f, 300.0f);
        const Mat4 vp = camera_vp(app.camera, int(view.w), int(view.h));
        int hover_x = 0, hover_y = 0;
        const bool hovering = surface == 1
            ? studio::map_cell(app.layout.map, app.pan_x, app.pan_y, mx, my,
                               app.snap.width, app.snap.height, &hover_x, &hover_y)
            : surface == 2 && view.contains(mx, my) &&
              pick_cell(app.camera, int(view.w), int(view.h), mx - view.x, my - view.y,
                         &hover_x, &hover_y);
        if (g_probe) probe_observe(probe, io, app, hovering, hover_x, hover_y);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        if (g_probe) {
            io.DeltaTime = 1.0f / (probe.showcasing?30.0f:60.0f);
            // A larger offscreen target exercises the same scaling path as a
            // high-DPI drawable without claiming this window's monitor changed.
            io.DisplayFramebufferScale = ImVec2(float(render_w) / ww, float(render_h) / wh);
        }
        ImGui::NewFrame();
        update_connected(app);
        header_panel(app);
        ImGui::BeginDisabled(app.exploring);
        group_panel(app);
        ImGui::EndDisabled();
        inspector_panel(app);
        workspace_panel(app);
        status_panel(app);
        review_panel(app);
        pending_panel(app);
        if (app.close_ready) running = false;

        // The overlay, in SCREEN space. Every match first, the selection over
        // it, the hover thinnest — so "show every match" costs the geometry
        // nothing and the eight scene hashes cannot move because of the UI.
        ImDrawList* dl = ImGui::GetForegroundDrawList();

        // Arm the sink only on the frame that is captured, so the sidecar
        // describes the frame in the PNG rather than the last one drawn.
        if (g_probe && !probe.showcasing && probe.it == kItCapture) {
            probe.outlines.clear();
            g_outline_sink = &probe.outlines;
            draw_ruler(dl, ww, wh);
        }

        if (app.mode == 0) {
        dl->PushClipRect(ImVec2(view.x, view.y), corner(view), true);
        g_outline_role = "match";
        if (app.has_draft && app.show_matches) {
            for (const vr::overrides::Match& m : app.draft_matches)
                outline_rect(dl, vp, int(view.w), int(view.h), m.x, m.y,
                             app.draft.w, app.draft.extent,
                             IM_COL32(90, 170, 255, 200), 2.0f, ImVec2(view.x, view.y));
        }
        g_outline_role = "selection";
        if (app.has_draft && populated(app.draft)) {
            outline_rect(dl, vp, int(view.w), int(view.h), app.origin_x, app.origin_y, app.draft.w, app.draft.extent,
                         IM_COL32(255, 210, 60, 255), 3.0f, ImVec2(view.x, view.y));
        }
        g_outline_role = "hover-object";
        if (hovering) {
            const int hk = app.model.object_at(hover_x, hover_y);
            if (hk >= 0) {
                const Object& h = app.model.objects[static_cast<size_t>(hk)];
                outline_rect(dl, vp, int(view.w), int(view.h), h.x, h.y, h.w, h.extent,
                             IM_COL32(255, 255, 255, 110), 1.5f, ImVec2(view.x, view.y));
            }
            g_outline_role = "hover-cell";
            outline_rect(dl, vp, int(view.w), int(view.h), hover_x, hover_y, 1, 1,
                         IM_COL32(255, 255, 255, 200), 1.5f, ImVec2(view.x, view.y));
        }
        dl->PopClipRect();
        }
        g_outline_sink = nullptr;

        if(g_probe && probe.showcasing) showcase_caption(probe,app);

        ImGui::Render();

        // The probe's FBO stands in for the window's back buffer, and it stands
        // in for the WHOLE frame — the mesh and ImGui both land in it, because
        // a capture that caught only one of them would be a picture of a claim
        // nobody made.
        vr::gl::glBindFramebuffer(GL_FRAMEBUFFER, g_probe ? probe.fbo : 0);
        glViewport(0, 0, render_w, render_h);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glClearColor(0.07f, 0.08f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Identity model: world units ARE map cells, which is what makes the
        // pick above a plain floor() rather than an inverse transform.
        const auto gl_view = studio::framebuffer_rect(view, ww, wh, render_w, render_h);
        glViewport(gl_view.x, gl_view.y, gl_view.w, gl_view.h);
        glEnable(GL_SCISSOR_TEST);
        glScissor(gl_view.x, gl_view.y, gl_view.w, gl_view.h);
        const bool measure_preview = g_probe && probe.showcasing &&
            num_or(&probe.showcase,"measure_environment",0)!=0;
        if(measure_preview) glFinish();
        const auto draw_start=measure_preview?SDL_GetPerformanceCounter():0;
        if(app.mode!=1) {
            float fx,fy;camera_fov(int(view.w),int(view.h),&fx,&fy);
            const float cell_pixels=gl_view.h/(32.f*app.camera.dist*std::tan(fy));
            app.sky_ok=app.sky.draw(app.lighting,app.camera.pitch,fy,cell_pixels,app.camera.orthographic);
            if(!app.sky_ok) app.status="Sky preview unavailable. Choose Neutral to reset; see the console for details.";
        }
        if(app.exploring)
            vr::diorama::draw_region_raw(vp,app.neutral?2:app.debug?1:0,studio::environment::tint(app.lighting));
        else if (app.mode != 1 && vr::diorama::has_geometry())
            vr::diorama::draw_raw(vp, vr::math::identity(), app.neutral ? 2 : app.debug ? 1 : 0,
                                  studio::environment::tint(app.lighting));
        if(measure_preview) {
            glFinish();
            app.preview_draw_ms[app.preview_draw_samples++%16]=
                1000.0*(SDL_GetPerformanceCounter()-draw_start)/SDL_GetPerformanceFrequency();
        }
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, render_w, render_h);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (g_probe) {
            if(probe.showcasing) {
                // A test-only barrier keeps replay time still while the real
                // worker runs. Input/rendering and result publication still use
                // the ordinary event loop; barriers require released controls.
                if(showcase_wait_stream(probe,app)) {SDL_Delay(1);continue;}
                showcase_frame(probe,app);
                if(++probe.it>=probe.showcase.find("frames")->as_int()) running=false;
                continue;
            }
            // AFTER both draws, in the same iteration, with no swap anywhere in
            // probe mode — so there is no queued frame to read by mistake and
            // no question of which one this is.
            if (probe.it == kItCapture) {
                probe.capture_camera = app.camera;
                probe.win_w = ww; probe.win_h = wh;
                probe.draw_w = dw; probe.draw_h = dh;
                probe.disp_w = io.DisplaySize.x;
                probe.disp_h = io.DisplaySize.y;
                probe.fb_scale_x = io.DisplayFramebufferScale.x;
                probe.fb_scale_y = io.DisplayFramebufferScale.y;
                probe_read_frame(probe);
            }
            if (probe.it == kItMaskBefore || probe.it == kItMaskPaint || probe.it == kItModelPreview || probe.it==kItPartRedo ||
                probe.it==kItHandleGuide || probe.it==kItHandleApplied || probe.it==kItDiorama || probe.it==kItDioramaOrbit) {
                auto original = std::move(probe.rgb); probe_read_frame(probe);
                const char* suffix = probe.it == kItMaskBefore ? ".mask-before.png" : probe.it == kItMaskPaint ? ".mask-painted.png" :
                    probe.it==kItPartRedo ? ".parts.png" : probe.it==kItHandleGuide ? ".handle-guide.png" :
                    probe.it==kItHandleApplied ? ".handle-applied.png" : probe.it==kItDiorama ? ".diorama.png" :
                    probe.it==kItDioramaOrbit ? ".diorama-orbit.png" : ".model.png";
                probe.auxiliary_images_ok = studio::png::write_rgb((probe.png_path + suffix).c_str(),probe.fbw,probe.fbh,probe.rgb.data()) && probe.auxiliary_images_ok;
                ++probe.auxiliary_images;
                probe.rgb = std::move(original);
            }
            if (++probe.it >= kProbeFrames) running = false;
        } else {
            SDL_GL_SwapWindow(win);
        }
    }

    // The assertions run before ImGui is torn down, because they read the panel
    // observations it produced — and the GL objects are released on the failing
    // path as well as the passing one.
    int exit_code = 0;
    stop_fly_look(app);
    if (g_probe) {
        exit_code = probe.showcasing?showcase_report(probe,app):probe_report(app, probe);
        g_probe = nullptr;
        probe_fbo_destroy(probe);
    }

    app.room.release();
    app.mask_image.release();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    app.sky.release();
    vr::diorama::shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return exit_code;
}
