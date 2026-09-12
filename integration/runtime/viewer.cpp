// viewer.cpp — see viewer.h for what this is for.

#include "viewer.h"
#include "diorama.h"
#include "actor_render.h"
#include "gl_loader.h"
#include "vr_math.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>

namespace vr {
namespace viewer {
namespace {

SDL_Window* g_win = nullptr;
bool        g_active = false;

// Orbit camera, in map-cell units. Spherical around a target so dragging feels
// like turning an object over rather than flying, which is what you want when
// inspecting a model.
float g_yaw    = 0.6f;      // radians
float g_pitch  = 0.55f;     // radians above the horizon
float g_dist   = 12.0f;     // cells; read the player at native sprite proportions
float g_ty     = 1.0f;      // target height above the ground plane

bool  g_follow = true;      // track the player, or hold over the map centre
int   g_debug  = 0;         // 0 textured, 1 classification colours
int   g_min_unit = 2;

bool  g_b_held = false, g_h_held = false, g_n_held = false, g_m_held = false;

// Edge trigger: true only on the frame a key goes down.
bool pressed(const Uint8* k, SDL_Scancode sc, bool* held) {
    const bool down = k[sc] != 0;
    const bool edge = down && !*held;
    *held = down;
    return edge;
}

// Opt-in bounded capture of the actual native viewer back buffer for review.
// The caller owns the output prefix/directory; no game data is published.
void capture_review(int width,int height) {
    const char* prefix=std::getenv("RUBYVR_VIEWER_CAPTURE");
    if(!prefix || !*prefix)return;
    static unsigned frame=0;
    const unsigned n=frame++;
    if(n>=720 || n%4 || width<=0 || height<=0 || width>4096 || height>4096)return;
    std::vector<uint8_t> pixels(size_t(width)*height*4);
    glReadBuffer(GL_BACK);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    for(int y=0;y<height/2;++y)
        std::swap_ranges(pixels.begin()+size_t(y)*width*4,pixels.begin()+size_t(y+1)*width*4,
                         pixels.begin()+size_t(height-1-y)*width*4);
    auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(),width,height,32,width*4,SDL_PIXELFORMAT_RGBA32);
    char path[2048];const int count=std::snprintf(path,sizeof(path),"%s-%04u.bmp",prefix,n);
    if(surface && count>0 && count<int(sizeof(path)))SDL_SaveBMP(surface,path);
    if(surface)SDL_FreeSurface(surface);
    const auto& a=actor_render::stats();
    std::fprintf(stderr,"[actors] frame=%u visible=%d unsupported=%d unresolved=%d player=%d foot=%.4f,%.4f,%.4f\n",
        n,a.visible,a.unsupported,a.unresolved,a.player,a.player_x,a.player_y,a.player_z);
}

}  // namespace

bool active() { return g_active; }

bool init(SDL_Window* win, bool visible) {
    if (!win) return false;
    g_win = win;

    SDL_SetWindowSize(win, 1280, 800);
    SDL_SetWindowTitle(win, "RubyRecomp - diorama viewer");
    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    if (visible) SDL_ShowWindow(win);

    // Vsync off: this shares a thread with the emulator, and blocking here for
    // the monitor's refresh would pace the GAME to the monitor.
    SDL_GL_SetSwapInterval(0);

    g_active = true;
    std::fprintf(stderr,
                 "[viewer] window open. J/L I/K orbit, U/O zoom, T/G target,\n"
                 "[viewer] N/M min-unit, B debug colours, H follow-player\n");
    return true;
}

void frame(const world::Snapshot& s, bool present) {
    if (!g_active || !g_win) return;
    if (!diorama::ready() && !diorama::init()) return;

    // Controls first, so a re-mesh request lands before update() runs.
    if (const Uint8* k = SDL_GetKeyboardState(nullptr);SDL_GetKeyboardFocus()==g_win && k) {
        if (k[SDL_SCANCODE_J]) g_yaw   -= 0.02f;
        if (k[SDL_SCANCODE_L]) g_yaw   += 0.02f;
        if (k[SDL_SCANCODE_I]) g_pitch += 0.015f;
        if (k[SDL_SCANCODE_K]) g_pitch -= 0.015f;
        if (k[SDL_SCANCODE_U]) g_dist  *= 1.02f;
        if (k[SDL_SCANCODE_O]) g_dist  /= 1.02f;
        if (k[SDL_SCANCODE_T]) g_ty    += 0.08f;
        if (k[SDL_SCANCODE_G]) g_ty    -= 0.08f;

        // Clamp pitch just short of the poles: at exactly straight-down the
        // look-at basis degenerates and the view snaps to an arbitrary roll.
        if (g_pitch >  1.50f) g_pitch =  1.50f;
        if (g_pitch < -0.20f) g_pitch = -0.20f;
        if (g_dist  <  1.0f)  g_dist  =  1.0f;
        if (g_dist  > 200.0f) g_dist  = 200.0f;

        if (pressed(k, SDL_SCANCODE_B, &g_b_held)) {
            g_debug = (g_debug + 1) % 2;
            std::fprintf(stderr, "[viewer] %s\n",
                         g_debug ? "classification colours" : "textured");
        }
        if (pressed(k, SDL_SCANCODE_H, &g_h_held)) {
            g_follow = !g_follow;
            std::fprintf(stderr, "[viewer] %s\n",
                         g_follow ? "following player" : "map centre");
        }
        // Live re-mesh. Seeing the height rule change under the camera is far
        // more informative than comparing two static renders.
        if (pressed(k, SDL_SCANCODE_N, &g_n_held) && g_min_unit > 1) {
            diorama::set_min_unit(--g_min_unit);
            std::fprintf(stderr, "[viewer] min unit %d\n", g_min_unit);
        }
        if (pressed(k, SDL_SCANCODE_M, &g_m_held) && g_min_unit < 6) {
            diorama::set_min_unit(++g_min_unit);
            std::fprintf(stderr, "[viewer] min unit %d\n", g_min_unit);
        }
    } else g_b_held=g_h_held=g_n_held=g_m_held=false;

    diorama::update(s);
    if (!diorama::has_geometry()) {
        SDL_SetWindowTitle(g_win,"RubyRecomp - scene unavailable; use the original game window");
        gl::glBindFramebuffer(GL_FRAMEBUFFER,0);
        glClearColor(0.07f,0.08f,0.11f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (present) SDL_GL_SwapWindow(g_win);
        return;
    }
    char title[160];
    const auto& actors=actor_render::stats();
    std::snprintf(title,sizeof(title),"RubyRecomp - live map %d.%d | %zu connections | %d actors | %d unsupported | %d unresolved",
        s.map_group,s.map_number,s.connections.size(),actors.visible,actors.unsupported,actors.unresolved);
    SDL_SetWindowTitle(g_win,title);

    float mw = 0, mh = 0, px = 0, py = 0, pz = 0;
    diorama::map_size(&mw, &mh);
    diorama::player_cell(&px, &py, &pz);

    const float tx = g_follow ? px : mw * 0.5f;
    const float tz = g_follow ? pz : mh * 0.5f;
    const float ty = (g_follow ? py : 0.0f) + g_ty;

    const float cp = std::cos(g_pitch);
    const float ex = tx + std::sin(g_yaw) * cp * g_dist;
    const float ey = ty + std::sin(g_pitch) * g_dist;
    const float ez = tz + std::cos(g_yaw) * cp * g_dist;

    int w = 1280, h = 800;
    SDL_GL_GetDrawableSize(g_win, &w, &h);
    if (w <= 0 || h <= 0) return;

    // Vertical FOV fixed at ~60 degrees; horizontal follows the window aspect,
    // so resizing widens the view instead of stretching it.
    const float fy = 0.52f;
    const float fx = std::atan(std::tan(fy) * static_cast<float>(w) / h);
    XrFovf fov{};
    fov.angleLeft = -fx; fov.angleRight = fx;
    fov.angleUp   =  fy; fov.angleDown  = -fy;

    gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.07f, 0.08f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const math::Mat4 vp = math::multiply(
        math::projection(fov, 0.05f, 500.0f),
        math::look_at(ex, ey, ez, tx, ty, tz));

    diorama::draw_raw(vp, math::identity(), g_debug);

    if(present)capture_review(w,h);

    if (present) SDL_GL_SwapWindow(g_win);
}

}  // namespace viewer
}  // namespace vr
