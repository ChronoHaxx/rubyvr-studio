// vr_layer.cpp — see vr_layer.h for the timing design.
//
// All three gotchas the spike cost us are applied here deliberately; do not
// "tidy" them away:
//   1. windows.h + unknwn.h BEFORE the OpenXR headers, or openxr_platform.h
//      fails with four "unknown type name 'IUnknown'" errors.
//   2. XR_MAKE_VERSION(1,0,0), NOT XR_CURRENT_API_VERSION — the MSYS2 headers
//      are 1.1 and SteamVR answers XR_ERROR_API_VERSION_UNSUPPORTED (-4).
//   3. xrGetOpenGLGraphicsRequirementsKHR must be called before
//      xrCreateSession even though nothing uses its result.

#include "vr_layer.h"
#include "ruby_world.h"
#include "tileset.h"
#include "map_view.h"
#include "gl_loader.h"
#include "renderer.h"
#include "diorama.h"
#include "viewer.h"
#include "world_io.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <windows.h>
#include <unknwn.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace vr {
namespace {

// ---- shared state between the emulation thread and the VR thread ----------

std::mutex           g_mx;
std::vector<uint8_t> g_staging;      // RGB888, written by frame_sink
int                  g_src_w = 0;
int                  g_src_h = 0;
bool                 g_fresh = false;

// Set by the VR thread when it notices the guest surface changed size; drained
// by the VR thread at the top of the next iteration, outside the frame and
// outside the lock. Guarded by g_mx purely because it lives beside the rest.
int                  g_pending_w = 0;
int                  g_pending_h = 0;

std::atomic<bool> g_running{false};
std::thread       g_thread;

// ---- world snapshot handoff, emulation thread -> VR thread ----------------
//
// A Snapshot is ~70 KB of vectors, so it is handed over by SWAPPING rather than
// copying: std::swap on the struct swaps the vectors' internal pointers and is
// O(1), which means the lock below is held for a few instructions rather than
// for a 70 KB memcpy. Each side keeps its own buffer and they trade.
//
// The buffers ping-pong, so the emulation thread's "previous" snapshot is
// really from two frames ago. That is fine, and deliberately so: the only thing
// capture() uses it for is the layout pointer, and layout_ptr travels in the
// same struct as the metatile tables it describes. They can never disagree.
std::mutex      g_world_mx;
world::Snapshot g_world_pending;
bool            g_world_fresh = false;
// Sticky until the consumer acknowledges it. A quick invalid -> valid sequence
// must not disappear when the latest snapshot replaces an unread invalid one.
bool            g_world_invalidated = false;

// Which image goes on the quad. Toggled with backslash — a key the GBA does not
// have, same rule as the comfort keys below.
bool g_map_mode      = false;
bool g_map_mode_held = false;

// VR-thread-owned. g_world_read is this thread's half of the swap pair, so it
// needs no lock once claimed; g_map_scratch is where map_view draws before the
// row-reversed upload.
world::Snapshot       g_world_read;
std::vector<uint32_t> g_map_scratch;

// ---- main-thread-owned, handed to the VR thread ---------------------------

SDL_Window*   g_win = nullptr;
SDL_GLContext g_ctx = nullptr;

XrInstance  g_instance   = XR_NULL_HANDLE;
XrSystemId  g_system     = XR_NULL_SYSTEM_ID;
XrSession   g_session    = XR_NULL_HANDLE;
XrSpace     g_space      = XR_NULL_HANDLE;   // LOCAL — the world
XrSpace     g_view_space = XR_NULL_HANDLE;   // VIEW  — the head, for recenter
XrSwapchain g_swapchain  = XR_NULL_HANDLE;
int         g_sc_w = 0, g_sc_h = 0;

// ---- comfort: where the screen sits, and how big it is --------------------
//
// Session-only; not persisted. Adjusted from the VR thread by polling SDL's
// keyboard snapshot, which avoids touching the engine's input path entirely —
// every key below is one a Game Boy Advance does not have, so nothing can
// collide with gameplay.
//
//   Home                 recenter in front of you, level
//   End                  toggle head-lock (world-locked <-> follows your head)
//   PageUp / PageDown    push away / pull closer
//   Insert / Delete      bigger / smaller
//   [  /  ]              lower / raise
//   ,  /  .              pitch down / up   (tilt for a reclined chair)
//
// Stored as yaw/pitch/offsets rather than a finished quaternion so each control
// is independent — adjusting height cannot accidentally rotate the screen, and
// the pose is simply rebuilt from these every frame.
float g_quad_dist   = 1.5f;    // metres from the anchor
float g_quad_width  = 0.8f;    // metres
float g_quad_yaw    = 0.0f;    // radians, set by recenter
float g_quad_pitch  = 0.0f;    // radians, manual
float g_quad_height = 0.0f;    // metres, manual offset from the anchor

XrVector3f g_anchor = {0, 0, 0};   // head position captured at last recenter

// Head-lock puts the quad in VIEW space, so it rides your head at a fixed
// distance. Ugly for a 3D world; ideal for a flat screen when you are lying
// down, reclined, or otherwise not upright, because "level" stops meaning
// anything useful in those positions.
bool g_headlock = false;

bool g_recenter_held = false;
bool g_headlock_held = false;
bool g_board_place_held = false;
bool g_fpv_held = false;
bool g_turn_l_held = false;
bool g_turn_r_held = false;

// Standing eye height. Used only to drop the world so the ground lands under
// your feet on entering first person; the headset's own tracking supplies every
// movement after that.
constexpr float kEyeHeight = 1.6f;

// 30 degrees. Small enough not to disorient, large enough that one press is
// clearly a turn rather than a drift.
constexpr float kSnapTurn = 0.5236f;

std::vector<XrSwapchainImageOpenGLKHR> g_images;

// Scratch for the RGB888 -> RGBA8 expansion. The swapchain wants 4 bytes per
// pixel; the guest gives 3. 240x160 makes this 38,400 pixels — trivial.
std::vector<uint8_t> g_rgba;

// One-shot A/B dump, under RUBYVR_DUMP_ATLAS=1.
//
// Writes our map view and the GBA's own framebuffer for the SAME instant, at
// the same size. That pair is the real test of the whole decode chain, and
// having it on disk means Phase 4 can be checked from a flat desktop run —
// without it, the map view only ever exists inside a headset and every
// iteration would cost putting one on.
void dump_ab_once(const world::Snapshot& s, const uint8_t* rgb888, int w, int h) {
    static bool done = false;
    if (done || !s.valid) return;

    const char* want = std::getenv("RUBYVR_DUMP_ATLAS");
    if (!want || want[0] != '1') { done = true; return; }
    done = true;

    std::vector<uint32_t> mine(static_cast<size_t>(w) * h);
    map_view::render(s, mine.data(), w, h);

    const bool a = tileset::dump_ppm("map_ours.ppm", mine.data(), w, h);
    const bool b = tileset::dump_rgb888_ppm("map_theirs.ppm", rgb888, w, h);
    std::fprintf(stderr,
                 "[vr] A/B dump %dx%d: map_ours.ppm %s, map_theirs.ppm %s\n",
                 w, h, a ? "ok" : "FAILED", b ? "ok" : "FAILED");
}

// One-shot diorama render to disk, under RUBYVR_DUMP_SCENE=1.
//
// ONLY when the VR thread is not running, and that restriction is the whole
// reason this works. With no headset, start() bails at xrGetSystem and never
// hands the GL context to the VR thread — so the context is still current on
// THIS thread, which is also the emulation thread, and GL calls from here are
// legal. With a headset the context belongs to the VR thread and touching GL
// here would be a two-threads-one-context bug that corrupts state rather than
// failing cleanly.
void dump_diorama_once(const world::Snapshot& s) {
    static bool done = false;
    if (done || !s.valid) return;
    if (g_running.load(std::memory_order_relaxed)) return;   // VR owns the context

    const char* scene   = std::getenv("RUBYVR_DUMP_SCENE");
    const char* inspect = std::getenv("RUBYVR_INSPECT");
    const char* turn    = std::getenv("RUBYVR_TURNTABLE");
    const char* iso     = std::getenv("RUBYVR_ISOLATE");
    const bool want_scene   = scene   && scene[0]   == '1';
    const bool want_inspect = inspect && inspect[0] == '1';
    const bool want_turn    = turn    && turn[0]    == '1';

    // RUBYVR_ISOLATE=x,y[,w,h] — lift one rectangle of the map onto empty
    // ground and inspect that instead. w,h default to 4x4, which is a building.
    int ix = 0, iy = 0, iw = 4, ih = 4;
    const bool want_iso = iso && std::sscanf(iso, "%d,%d,%d,%d",
                                             &ix, &iy, &iw, &ih) >= 2;
    if (iso && !want_iso)
        std::fprintf(stderr,
                     "[isolate] RUBYVR_ISOLATE=\"%s\" is not x,y[,w,h]\n", iso);

    if (!want_scene && !want_inspect && !want_turn && !want_iso) {
        done = true;
        return;
    }
    done = true;

    if (want_inspect) renderer::inspect("inspect.ppm", s);
    if (want_scene)   renderer::selftest_diorama("diorama.ppm", 900, 700, s);

    // Last, because these throw the map's mesh away to build their own.
    if (want_iso)     renderer::inspect_isolate("isolate.ppm", s, ix, iy, iw, ih);
    if (want_turn)    renderer::inspect_turntable("turntable.ppm", s);
}

// Write the live Snapshot to disk under RUBYVR_SNAP=path. See world_io.h for
// what a .snap is for; this is the only thing that produces one.
//
// PURE I/O, NO GL, so unlike dump_diorama_once above it does not care whether
// the VR thread owns the context. It runs on the emulation thread and writes
// ~100 KB, which is why it fires a bounded number of times and never per frame.
//
// WHY MORE THAN ONE CAPTURE, under RUBYVR_SNAP_N:
//
//   vram_tiles is re-copied every frame precisely so DMA tile animation is
//   free (see the Snapshot::vram_tiles comment), so two captures a second apart
//   are NOT byte-identical — General's tileset callback animates flowers and
//   water in place. Whether that reaches GEOMETRY is a question the data can
//   answer instead of one to argue about: dump several a few frames apart and
//   compare the mesher's geom= hash across them. If the hash is stable,
//   animation provably never reaches geometry and any single capture is a valid
//   reference for the decomp loader to be judged against. If it is not stable,
//   that is a finding about the mesher — and one worth having BEFORE a loader
//   exists to be blamed for it.
void dump_snapshots(const world::Snapshot& s) {
    const char* path = std::getenv("RUBYVR_SNAP");
    if (!path || !path[0] || !s.valid) return;

    // How many, and how far apart. The defaults are one capture, which is what
    // you want when grabbing a reference; the sweep is opt-in.
    static const int want = [] {
        if (const char* e = std::getenv("RUBYVR_SNAP_N")) {
            const int n = std::atoi(e);
            if (n >= 1 && n <= 64) return n;
        }
        return 1;
    }();
    static const int stride = [] {
        if (const char* e = std::getenv("RUBYVR_SNAP_EVERY")) {
            const int n = std::atoi(e);
            if (n >= 1 && n <= 600) return n;
        }
        return 30;   // ~half a second at 60 fps: long enough for a water frame
    }();

    static int written = 0;
    static int countdown = 0;
    if (written >= want) return;
    if (countdown > 0) { --countdown; return; }

    // Index 0 keeps the name it was asked for, so the common single-capture
    // case produces exactly the file the caller named.
    char name[512];
    if (written == 0) std::snprintf(name, sizeof(name), "%s", path);
    else              std::snprintf(name, sizeof(name), "%s.%d", path, written);

    world_io::write(s, name);
    ++written;
    countdown = stride;
}

// Load the authored override file, once, under RUBYVR_OVERRIDES=path.
//
// OFF BY DEFAULT. With no variable set the mesher holds an empty set and
// behaves exactly as it did before overrides existed — which is the property
// the acceptance suite checks, because an authoring feature that changes the
// game when nobody authored anything is a regression wearing a new name.
//
// The GAME reading the same file the studio writes is the whole of "one
// rendering path": authoring outside and validating in the headset needs no
// extra plumbing, because both compile the same mesher and feed it the same
// parameters.
void load_overrides_once() {
    static bool done = false;
    if (done) return;
    done = true;

    const char* path = std::getenv("RUBYVR_OVERRIDES");
    if (!path || !path[0]) return;

    overrides::OverrideSet set;
    if (!overrides::load(path, &set)) {
        // A named file that will not load is an error worth being loud about,
        // not a reason to silently render the inferred world.
        std::fprintf(stderr,
                     "[override] RUBYVR_OVERRIDES=%s failed to load; "
                     "rendering without it\n", path);
        return;
    }
    diorama::set_overrides(std::move(set));
}

bool xr_fail(const char* what, XrResult r) {
    std::fprintf(stderr, "[vr] %s failed: %d\n", what, (int)r);
    return false;
}

bool create_swapchain(int w, int h) {
    if (g_swapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(g_swapchain);
        g_swapchain = XR_NULL_HANDLE;
    }
    uint32_t n = 0;
    if (XR_FAILED(xrEnumerateSwapchainFormats(g_session, 0, &n, nullptr)))
        return false;
    std::vector<int64_t> formats(n);
    if (XR_FAILED(xrEnumerateSwapchainFormats(g_session, n, &n, formats.data())))
        return false;

    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                    XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format      = formats[0];   // runtime lists in preference order
    ci.sampleCount = 1;
    ci.width       = w;
    ci.height      = h;
    ci.faceCount   = 1;
    ci.arraySize   = 1;
    ci.mipCount    = 1;
    XrResult r = xrCreateSwapchain(g_session, &ci, &g_swapchain);
    if (XR_FAILED(r)) return xr_fail("xrCreateSwapchain", r);

    uint32_t ic = 0;
    xrEnumerateSwapchainImages(g_swapchain, 0, &ic, nullptr);
    g_images.assign(ic, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
    xrEnumerateSwapchainImages(
        g_swapchain, ic, &ic,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(g_images.data()));

    g_sc_w = w;
    g_sc_h = h;
    g_rgba.assign(static_cast<size_t>(w) * h * 4, 255);
    return true;
}

// Place the quad in front of wherever the head currently is, at g_quad_dist.
//
// Yaw ONLY. If the screen inherited pitch and roll it would hang at whatever
// angle your head happened to be at when you pressed the key, and a tilted
// horizon is one of the reliable ways to make people ill. Level is worth more
// than faithful.
void recenter(XrTime t) {
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(g_view_space, g_space, t, &loc))) return;
    if (!(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) ||
        !(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
        return;

    // Yaw only, and pitch reset. If the screen inherited your head's pitch and
    // roll it would hang at whatever angle you happened to be at, and a tilted
    // horizon is a reliable way to make people ill. Use `,`/`.` to add pitch
    // back deliberately, or End for head-lock if you are lying down.
    const XrQuaternionf& q = loc.pose.orientation;
    g_quad_yaw = std::atan2(2.0f * (q.w * q.y + q.x * q.z),
                            1.0f - 2.0f * (q.y * q.y + q.z * q.z));
    g_quad_pitch  = 0.0f;
    g_quad_height = 0.0f;
    g_anchor      = loc.pose.position;
}

// Rebuild the world-locked pose from the independent controls.
XrPosef build_pose() {
    const float cy = std::cos(g_quad_yaw   * 0.5f), sy = std::sin(g_quad_yaw   * 0.5f);
    const float cp = std::cos(g_quad_pitch * 0.5f), sp = std::sin(g_quad_pitch * 0.5f);

    XrPosef p{};
    // q_yaw * q_pitch — yaw about world Y, then pitch about the screen's own X.
    p.orientation.x =  cy * sp;
    p.orientation.y =  sy * cp;
    p.orientation.z = -sy * sp;
    p.orientation.w =  cy * cp;

    // OpenXR is right-handed with -Z forward, so forward for a yaw is
    // (-sin yaw, 0, -cos yaw).
    p.position.x = g_anchor.x - std::sin(g_quad_yaw) * g_quad_dist;
    p.position.y = g_anchor.y + g_quad_height;
    p.position.z = g_anchor.z - std::cos(g_quad_yaw) * g_quad_dist;
    return p;
}

// Poll comfort keys. SDL_GetKeyboardState hands back a pointer to state the
// main thread refreshes on pump; reading held keys across threads is benign
// here (worst case a keypress lands one frame late).
void poll_comfort(XrTime t) {
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    if (!k) return;

    // Edge-triggered: recentering every frame while held would drag the screen
    // around with your head instead of placing it once.
    const bool home = k[SDL_SCANCODE_HOME] != 0;
    if (home && !g_recenter_held) recenter(t);
    g_recenter_held = home;

    // Edge-triggered toggle. Head-lock is the answer to "I am lying down" —
    // world-locking assumes an upright body and a meaningful horizon, and
    // neither holds on your back.
    const bool end = k[SDL_SCANCODE_END] != 0;
    if (end && !g_headlock_held) {
        g_headlock = !g_headlock;
        std::fprintf(stderr, "[vr] %s\n",
                     g_headlock ? "head-locked" : "world-locked");
    }
    g_headlock_held = end;

    // Level-triggered, small per-frame steps. At 90 Hz these are roughly
    // 0.45 m/s, 0.36 m/s, 0.27 m/s and 45 deg/s — comfortable hold-to-adjust.
    if (k[SDL_SCANCODE_PAGEUP])       g_quad_dist   += 0.005f;
    if (k[SDL_SCANCODE_PAGEDOWN])     g_quad_dist   -= 0.005f;
    if (k[SDL_SCANCODE_INSERT])       g_quad_width  += 0.004f;
    if (k[SDL_SCANCODE_DELETE])       g_quad_width  -= 0.004f;
    if (k[SDL_SCANCODE_RIGHTBRACKET]) g_quad_height += 0.003f;
    if (k[SDL_SCANCODE_LEFTBRACKET])  g_quad_height -= 0.003f;
    if (k[SDL_SCANCODE_PERIOD])       g_quad_pitch  += 0.0087f;   // ~0.5 deg
    if (k[SDL_SCANCODE_COMMA])        g_quad_pitch  -= 0.0087f;

    // Backslash: swap the quad between the GBA's own framebuffer and the map we
    // draw ourselves from guest memory. Edge-triggered, because holding a
    // toggle should not strobe.
    // Board controls. Distinct from the quad's, because the diorama and the
    // floating screen are two different objects you want to place separately.
    //   = / -    board bigger / smaller
    //   ' / ;    board higher / lower
    //   /        drop the board in front of you, level
    if (k[SDL_SCANCODE_EQUALS]) diorama::scale_by(1.01f);
    if (k[SDL_SCANCODE_MINUS])  diorama::scale_by(1.0f / 1.01f);
    if (k[SDL_SCANCODE_APOSTROPHE]) diorama::raise( 0.004f);
    if (k[SDL_SCANCODE_SEMICOLON])  diorama::raise(-0.004f);

    const bool slash = k[SDL_SCANCODE_SLASH] != 0;
    if (slash && !g_board_place_held) {
        // Same yaw-only rule as the quad's recenter: inheriting head pitch and
        // roll would leave the table tilted, and a tilted horizon is one of the
        // reliable ways to make people ill.
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        if (XR_SUCCEEDED(xrLocateSpace(g_view_space, g_space, t, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
            const XrQuaternionf& q = loc.pose.orientation;
            const float yaw = std::atan2(2.0f * (q.w * q.y + q.x * q.z),
                                         1.0f - 2.0f * (q.y * q.y + q.z * q.z));
            diorama::place(loc.pose.position.x - std::sin(yaw) * 0.9f,
                           loc.pose.position.y - 0.45f,
                           loc.pose.position.z - std::cos(yaw) * 0.9f,
                           yaw);
            std::fprintf(stderr, "[vr] board placed\n");
        }
    }
    g_board_place_held = slash;

    // ` toggles first person. On entry the world is pinned so the player's tile
    // is under your feet: anchored at your head's horizontal position, dropped
    // by eye height, and yawed to wherever you are facing so you start looking
    // the way you were.
    const bool grave = k[SDL_SCANCODE_GRAVE] != 0;
    if (grave && !g_fpv_held) {
        const bool on = !diorama::first_person();
        diorama::set_first_person(on);
        if (on) {
            XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
            if (XR_SUCCEEDED(xrLocateSpace(g_view_space, g_space, t, &loc)) &&
                (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
                const XrQuaternionf& q = loc.pose.orientation;
                const float yaw = std::atan2(2.0f * (q.w * q.y + q.x * q.z),
                                             1.0f - 2.0f * (q.y * q.y + q.z * q.z));
                diorama::anchor_first_person(loc.pose.position.x,
                                             loc.pose.position.y - kEyeHeight,
                                             loc.pose.position.z, yaw);
            }
        }
        std::fprintf(stderr, "[vr] %s\n", on ? "first person" : "diorama");
    }
    g_fpv_held = grave;

    // Snap turn, for chairs that do not swivel. Whole steps, never smooth:
    // rotation you did not initiate with your own neck is the single most
    // reliable way to make someone ill in VR.
    const bool turn_l = k[SDL_SCANCODE_9] != 0;
    const bool turn_r = k[SDL_SCANCODE_0] != 0;
    if (turn_l && !g_turn_l_held) diorama::snap_turn(+kSnapTurn);
    if (turn_r && !g_turn_r_held) diorama::snap_turn(-kSnapTurn);
    g_turn_l_held = turn_l;
    g_turn_r_held = turn_r;

    const bool backslash = k[SDL_SCANCODE_BACKSLASH] != 0;
    if (backslash && !g_map_mode_held) {
        g_map_mode = !g_map_mode;
        std::fprintf(stderr, "[vr] quad shows %s\n",
                     g_map_mode ? "our map view" : "the GBA framebuffer");
    }
    g_map_mode_held = backslash;

    if (g_quad_dist  < 0.30f) g_quad_dist  = 0.30f;   // closer than this and
    if (g_quad_dist  > 8.00f) g_quad_dist  = 8.00f;   // you cannot focus
    if (g_quad_width < 0.15f) g_quad_width = 0.15f;
    if (g_quad_width > 6.00f) g_quad_width = 6.00f;
    if (g_quad_height < -2.0f) g_quad_height = -2.0f;
    if (g_quad_height >  2.0f) g_quad_height =  2.0f;
    if (g_quad_pitch < -1.4f) g_quad_pitch = -1.4f;   // +/- 80 deg; past
    if (g_quad_pitch >  1.4f) g_quad_pitch =  1.4f;   // vertical is useless
}

// ---- the VR thread --------------------------------------------------------

void vr_thread() {
    SDL_GL_MakeCurrent(g_win, g_ctx);   // context released by start()

    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool           session_running = false;

    // Outlives every frame: the projection layer points into it and the runtime
    // dereferences that pointer inside xrEndFrame. See the note at the
    // submission site.
    std::vector<XrCompositionLayerProjectionView> proj_views;

    while (g_running.load(std::memory_order_relaxed)) {
        XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(g_instance, &ev) == XR_SUCCESS) {
            if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                state = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev)->state;
                if (state == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                    bi.primaryViewConfigurationType =
                        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    xrBeginSession(g_session, &bi);
                    session_running = true;
                } else if (state == XR_SESSION_STATE_STOPPING) {
                    xrEndSession(g_session);
                    session_running = false;
                }
            }
            ev = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
        }

        if (!session_running) {
            SDL_Delay(10);   // idle politely until the runtime gives us focus
            continue;
        }

        // Drain a pending resize BEFORE entering the frame. Safe here: no
        // begin/end is in flight and no lock is held across the rebuild.
        {
            int pw = 0, ph = 0;
            {
                std::lock_guard<std::mutex> lk(g_mx);
                pw = g_pending_w;
                ph = g_pending_h;
                g_pending_w = g_pending_h = 0;
            }
            if (pw > 0 && (pw != g_sc_w || ph != g_sc_h)) {
                std::fprintf(stderr, "[vr] surface %dx%d -> %dx%d\n",
                             g_sc_w, g_sc_h, pw, ph);
                if (!create_swapchain(pw, ph)) {
                    std::fprintf(stderr, "[vr] resize failed; stopping\n");
                    break;
                }
            }
        }

        XrFrameState fs{XR_TYPE_FRAME_STATE};
        xrWaitFrame(g_session, nullptr, &fs);   // the compositor's clock
        xrBeginFrame(g_session, nullptr);

        XrCompositionLayerQuad       quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
        XrCompositionLayerProjection proj{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        const XrCompositionLayerBaseHeader* layers[2];
        uint32_t layer_count = 0;

        if (fs.shouldRender) {
            // The 3D scene first: layer order IS composition order, so the
            // projection layer is the world and the quad hangs in front of it.
            //
            // proj_views is hoisted out of the loop deliberately. The layer
            // struct holds a POINTER into it that the runtime reads inside
            // xrEndFrame, so a vector local to this block would be freed while
            // still referenced — and that reads as a crash in the runtime, a
            // long way from the line that caused it.
            if (renderer::ready() &&
                renderer::render_frame(g_space, fs.predictedDisplayTime,
                                       proj, proj_views)) {
                layers[layer_count++] =
                    reinterpret_cast<const XrCompositionLayerBaseHeader*>(&proj);
            }
            // Take the newest complete frame. If the game has not produced one
            // since last time, g_fresh is false and we simply re-present what
            // is already in the swapchain — that repeat IS the 59.7-into-90
            // mismatch, handled by doing nothing.
            int  w = 0, h = 0;
            bool have     = false;
            bool draw_map = false;   // decided under the lock, done outside it
            {
                std::lock_guard<std::mutex> lk(g_mx);
                if (g_fresh && g_src_w > 0) {
                    w = g_src_w;
                    h = g_src_h;
                    if (w != g_sc_w || h != g_sc_h) {
                        // Guest surface resized (widescreen / adaptive_view /
                        // aspect_index). Do NOT rebuild here: we are between
                        // xrBeginFrame and xrEndFrame, holding a lock the
                        // emulation thread wants, and the compositor may still
                        // be reading this swapchain. Record it and rebuild at
                        // the top of the next iteration, before xrWaitFrame.
                        g_pending_w = w;
                        g_pending_h = h;
                        g_fresh = false;   // one frame skipped; next one lands
                    } else if (g_map_mode) {
                        // Map mode: nothing to convert from the staging buffer.
                        // Rendering it takes real CPU time (it touches every
                        // pixel several times), and doing that here would hold
                        // a lock the emulation thread needs every frame. Just
                        // record the decision and draw once the lock is gone.
                        g_fresh  = false;
                        have     = true;
                        draw_map = true;
                    } else {
                        // GL's texture origin is bottom-left; the GBA
                        // framebuffer's is top-left. Row 0 must therefore land
                        // at the BOTTOM of the texture or the image renders
                        // upside down. Flipping costs nothing here because the
                        // RGB888 -> RGBA8 expansion already touches every pixel.
                        const uint8_t* s = g_staging.data();
                        uint8_t*       d = g_rgba.data();
                        for (int y = 0; y < h; ++y) {
                            const uint8_t* srow =
                                s + static_cast<size_t>(h - 1 - y) * w * 3;
                            uint8_t* drow = d + static_cast<size_t>(y) * w * 4;
                            for (int x = 0; x < w; ++x) {
                                drow[x * 4 + 0] = srow[x * 3 + 0];
                                drow[x * 4 + 1] = srow[x * 3 + 1];
                                drow[x * 4 + 2] = srow[x * 3 + 2];
                            }
                        }
                        g_fresh = false;
                        have = true;
                    }
                }
            }

            // Claim the newest world snapshot. Separate lock from g_mx and held
            // for one swap of vector pointers — see the handoff note above.
            bool world_invalidated=false;
            {
                std::lock_guard<std::mutex> lk(g_world_mx);
                if (g_world_fresh) {
                    std::swap(g_world_read, g_world_pending);
                    g_world_fresh = false;
                }
                world_invalidated=g_world_invalidated;
                g_world_invalidated=false;
            }

            // The diorama reads the same snapshot the map view does. Done
            // here, on the VR thread, because it uploads textures and rebuilds
            // vertex buffers — GL work, which belongs where the context is.
            if (renderer::ready()) {
                if(world_invalidated) diorama::update(world::Snapshot{});
                diorama::update(g_world_read);
            }

            if (draw_map) {
                g_map_scratch.resize(static_cast<size_t>(w) * h);
                map_view::render(g_world_read, g_map_scratch.data(), w, h);

                // Same bottom-left origin problem as the framebuffer path, so
                // the same row reversal. map_view draws top-down like every
                // other 2D renderer; GL wants row 0 at the bottom.
                uint8_t* d = g_rgba.data();
                for (int y = 0; y < h; ++y) {
                    const uint32_t* srow =
                        g_map_scratch.data() + static_cast<size_t>(h - 1 - y) * w;
                    std::memcpy(d + static_cast<size_t>(y) * w * 4, srow,
                                static_cast<size_t>(w) * 4);
                }
            }

            if (have) {
                uint32_t idx = 0;
                xrAcquireSwapchainImage(g_swapchain, nullptr, &idx);
                XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                wi.timeout = XR_INFINITE_DURATION;
                xrWaitSwapchainImage(g_swapchain, &wi);

                glBindTexture(GL_TEXTURE_2D, g_images[idx].image);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_sc_w, g_sc_h,
                                GL_RGBA, GL_UNSIGNED_BYTE, g_rgba.data());
                glBindTexture(GL_TEXTURE_2D, 0);

                xrReleaseSwapchainImage(g_swapchain, nullptr);
            }

            poll_comfort(fs.predictedDisplayTime);

            quad.eyeVisibility                    = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain               = g_swapchain;
            quad.subImage.imageRect.extent.width  = g_sc_w;
            quad.subImage.imageRect.extent.height = g_sc_h;

            if (g_headlock) {
                // VIEW space is the head, so a pose of "straight ahead at
                // g_quad_dist" rides with you at any body orientation. Height
                // and pitch still apply, relative to your face rather than the
                // room. Yaw is meaningless here and is ignored.
                quad.space = g_view_space;
                const float cp = std::cos(g_quad_pitch * 0.5f);
                const float sp = std::sin(g_quad_pitch * 0.5f);
                quad.pose.orientation = {sp, 0.0f, 0.0f, cp};
                quad.pose.position    = {0.0f, g_quad_height, -g_quad_dist};
            } else {
                quad.space = g_space;
                quad.pose  = build_pose();
            }
            // Width is the knob; height follows the surface aspect. Keeping
            // width authoritative means a guest resize changes the shape of the
            // screen without changing how big it feels.
            quad.size.width  = g_quad_width;
            quad.size.height = g_quad_width * (float)g_sc_h / (float)g_sc_w;
            layers[layer_count++]   =
                reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
        }

        XrFrameEndInfo fe{XR_TYPE_FRAME_END_INFO};
        fe.displayTime          = fs.predictedDisplayTime;
        fe.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        fe.layerCount           = layer_count;
        fe.layers               = layers;
        xrEndFrame(g_session, &fe);
    }

    SDL_GL_MakeCurrent(g_win, nullptr);
}

}  // namespace

// ---- public API -----------------------------------------------------------

void frame_sink(const uint8_t* rgb888, int w, int h, void*) {
    // EMULATION THREAD. Copy and get out — see the contract in host_window.h.

    // Phase 4.1 — read Ruby's field map out of guest memory.
    //
    // This is the ONLY point in the process where the guest is guaranteed
    // quiescent (present() is called between frames), so it is the only place a
    // consistent snapshot of its memory can be taken. Cheap by construction:
    // memcpys and integer unpacking, no pixel conversion. See ruby_world.h.
    //
    // Passing the PREVIOUS layout pointer is what lets capture() skip re-reading
    // the ROM metatile tables on every frame. Arguments are evaluated before the
    // call, so reading it off `snap` here is the old value, not the new one —
    // but a named local says so out loud rather than relying on that.
    {
        static world::Snapshot snap;
        const uint32_t         prev_layout = snap.layout_ptr;
        if (world::capture(snap, prev_layout)) {
            world::debug_dump(snap);
            load_overrides_once();              // RUBYVR_OVERRIDES=path, once
            dump_snapshots(snap);               // RUBYVR_SNAP=path, N times
            tileset::dump_atlases_once(snap);   // RUBYVR_DUMP_ATLAS=1, once
            dump_ab_once(snap, rgb888, w, h);   // ditto: our map beside theirs
            dump_diorama_once(snap);            // RUBYVR_DUMP_SCENE=1, once

        }
        // Invalid captures are events too: both consumers must drop the old map.
        if (viewer::active()) viewer::frame(snap);
        std::lock_guard<std::mutex> lk(g_world_mx);
        if(!snap.valid) g_world_invalidated=true;
        std::swap(snap, g_world_pending);   // O(1): swaps vector pointers
        g_world_fresh = true;
    }

    const size_t bytes = static_cast<size_t>(w) * h * 3;
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_staging.size() < bytes) g_staging.resize(bytes);
    std::memcpy(g_staging.data(), rgb888, bytes);
    g_src_w = w;
    g_src_h = h;
    g_fresh = true;
}

bool start() {
    world::reset_capture();
    // start() runs from main() BEFORE gbarecomp::run_game(), which is where the
    // engine calls SDL_Init. SDL_CreateWindow before video init just fails and
    // would silently disable VR with no clue why, so bring the subsystem up
    // ourselves. SDL refcounts this; the engine's later SDL_Init is unaffected.
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[vr] SDL video init failed: %s\n", SDL_GetError());
        return false;
    }

    // A hidden window purely to own a GL context. OpenXR's GL binding needs a
    // context that already exists; the game's own presentation is D3D11 via
    // SDL_Renderer and is completely unrelated to this one.
    //
    // ASK FOR 3.3 CORE. Phases 2-4 asked for nothing and got whatever the
    // driver felt like, which was fine when the only GL call was
    // glTexSubImage2D. The projection renderer needs shaders, VBOs, VAOs and
    // framebuffer objects, and "whatever the driver felt like" is not a
    // contract. 3.3 is the floor for `layout(location=)` in GLSL and is
    // universally available on anything that can drive a headset.
    //
    // The attributes must be set before BOTH the window and the context: the
    // window's pixel format is chosen from them.
    auto make_window_and_context = [](bool request_33) -> bool {
        SDL_GL_ResetAttributes();
        if (request_33) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                                SDL_GL_CONTEXT_PROFILE_CORE);
        }
        g_win = SDL_CreateWindow("vr-ctx", SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED, 64, 64,
                                 SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!g_win) return false;
        g_ctx = SDL_GL_CreateContext(g_win);
        if (!g_ctx) {
            SDL_DestroyWindow(g_win);
            g_win = nullptr;
            return false;
        }
        return true;
    };

    if (!make_window_and_context(true)) {
        // Fall back rather than disabling VR outright: a driver that refuses a
        // core profile may still hand back a compatibility context new enough
        // for everything here, and gl::load() below is the real test of that.
        std::fprintf(stderr, "[vr] no 3.3 core context (%s); retrying default\n",
                     SDL_GetError());
        if (!make_window_and_context(false)) return false;
    }

    // Entry points beyond GL 1.1 must be resolved against a CURRENT context.
    // It is current here, between creation and the MakeCurrent(nullptr) that
    // hands it to the VR thread. See gl_loader.h for why this dance exists.
    const bool have_gl = gl::load();

    // RUBYVR_INSPECT builds the offline contact sheet instead of a VR session.
    //
    // It deliberately stops here rather than continuing into xrCreateInstance.
    // The inspector renders from THIS thread, which is also the emulation
    // thread, and that is only legal while no VR thread owns the GL context.
    // Skipping the session outright means inspection works even with SteamVR
    // running, instead of silently doing nothing.
    if (const char* vw = std::getenv("RUBYVR_VIEWER")) {
        if (vw[0] == '1' && have_gl) {
            viewer::init(g_win,true,world::source_map);
            return false;   // no XR session; the frame sink drives the window
        }
    }

    if (const char* insp = std::getenv("RUBYVR_INSPECT")) {
        if (insp[0] == '1') {
            std::fprintf(stderr, "[vr] inspect mode: no XR session\n");
            return false;
        }
    }

    if (const char* turn = std::getenv("RUBYVR_TURNTABLE")) {
        if (turn[0] == '1') {
            std::fprintf(stderr, "[vr] turntable mode: no XR session\n");
            return false;
        }
    }

    if (std::getenv("RUBYVR_ISOLATE")) {
        std::fprintf(stderr, "[vr] isolate mode: no XR session\n");
        return false;
    }

    // Offline proof that the shaders, the FBO path and above all the projection
    // matrix are right — before any headset is involved. Runs here because the
    // context is current and, crucially, because everything below this point
    // needs an OpenXR runtime that a desktop machine does not have.
    if (have_gl) {
        const char* want = std::getenv("RUBYVR_DUMP_SCENE");
        if (want && want[0] == '1') {
            renderer::selftest_dump("scene_symmetric.ppm", 512, 512, false);
            renderer::selftest_dump("scene_asymmetric.ppm", 512, 512, true);
        }
    }

    const char* exts[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(ici.applicationInfo.applicationName, "RubyRecomp VR");
    ici.applicationInfo.apiVersion   = XR_MAKE_VERSION(1, 0, 0);   // see header
    ici.enabledExtensionCount        = 1;
    ici.enabledExtensionNames        = exts;
    XrResult r = xrCreateInstance(&ici, &g_instance);
    if (XR_FAILED(r)) {
        // No runtime installed is the normal desktop case, not an error.
        std::fprintf(stderr, "[vr] no OpenXR runtime (%d); running flat\n", (int)r);
        return false;
    }

    XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = xrGetSystem(g_instance, &sgi, &g_system);
    if (XR_FAILED(r)) return xr_fail("xrGetSystem", r);

    PFN_xrGetOpenGLGraphicsRequirementsKHR get_reqs = nullptr;
    xrGetInstanceProcAddr(g_instance, "xrGetOpenGLGraphicsRequirementsKHR",
                          reinterpret_cast<PFN_xrVoidFunction*>(&get_reqs));
    XrGraphicsRequirementsOpenGLKHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
    if (!get_reqs || XR_FAILED(get_reqs(g_instance, g_system, &reqs)))
        return xr_fail("xrGetOpenGLGraphicsRequirements", XR_ERROR_RUNTIME_FAILURE);

    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    SDL_GetWindowWMInfo(g_win, &wm);

    XrGraphicsBindingOpenGLWin32KHR bind{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
    bind.hDC   = GetDC(wm.info.win.window);
    bind.hGLRC = (HGLRC)g_ctx;

    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next     = &bind;
    sci.systemId = g_system;
    r = xrCreateSession(g_instance, &sci, &g_session);
    if (XR_FAILED(r)) return xr_fail("xrCreateSession", r);

    XrReferenceSpaceCreateInfo rsci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;   // world-locked
    rsci.poseInReferenceSpace.orientation.w = 1.0f;
    r = xrCreateReferenceSpace(g_session, &rsci, &g_space);
    if (XR_FAILED(r)) return xr_fail("xrCreateReferenceSpace", r);

    // A second space tracking the head, used only to answer "where are you
    // looking?" when recentering. The quad itself always lives in LOCAL, which
    // is what makes it world-locked rather than stuck to your face.
    XrReferenceSpaceCreateInfo vci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    vci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    vci.poseInReferenceSpace.orientation.w = 1.0f;
    r = xrCreateReferenceSpace(g_session, &vci, &g_view_space);
    if (XR_FAILED(r)) return xr_fail("xrCreateReferenceSpace(VIEW)", r);

    if (!create_swapchain(240, 160)) return false;

    // The 3D renderer. Everything it makes — swapchains, depth buffers, the
    // FBO, shaders, the vertex buffer — needs the context current, so it is
    // built here rather than on the VR thread.
    //
    // A failure is NOT fatal. The quad layer is independent and already works;
    // losing the projection layer should cost the 3D scene, not the whole of
    // VR. renderer::ready() gates the per-frame submission.
    if (have_gl) {
        if (!renderer::init(g_instance, g_system, g_session))
            std::fprintf(stderr, "[vr] 3D renderer unavailable; quad only\n");
    } else {
        std::fprintf(stderr, "[vr] modern GL unavailable; quad only\n");
    }

    // Release the context here so the VR thread can claim it.
    SDL_GL_MakeCurrent(g_win, nullptr);
    g_running.store(true, std::memory_order_relaxed);
    g_thread = std::thread(vr_thread);
    std::fprintf(stderr, "[vr] started\n");
    return true;
}

void stop() {
    if (!g_running.exchange(false) && !viewer::active()) return;
    if (g_thread.joinable()) g_thread.join();

    // The VR thread released the context on its way out, so nothing is current
    // on any thread right now. Claim it here: renderer::shutdown deletes GL
    // objects, and GL calls with no current context are silently ignored, which
    // would leak every buffer and renderbuffer without a word.
    SDL_GL_MakeCurrent(g_win, g_ctx);
    viewer::shutdown();
    renderer::shutdown();

    if (g_swapchain)  xrDestroySwapchain(g_swapchain);
    if (g_view_space) xrDestroySpace(g_view_space);
    if (g_space)      xrDestroySpace(g_space);
    if (g_session)    xrDestroySession(g_session);
    if (g_instance)  xrDestroyInstance(g_instance);
    if (g_ctx)       SDL_GL_DeleteContext(g_ctx);
    if (g_win)       SDL_DestroyWindow(g_win);
    g_swapchain = XR_NULL_HANDLE;
    g_view_space = XR_NULL_HANDLE;
    g_space = XR_NULL_HANDLE;
    g_session = XR_NULL_HANDLE;
    g_instance = XR_NULL_HANDLE;
    g_ctx = nullptr;
    g_win = nullptr;
}

}  // namespace vr
