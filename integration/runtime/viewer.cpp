// viewer.cpp — see viewer.h for what this is for.

#include "viewer.h"
#include "diorama.h"
#include "actor_render.h"
#include "gl_loader.h"
#include "vr_math.h"
#include "camera_input.h"

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

// Follow camera in map-cell units. Grid gameplay uses cardinal yaw only;
// continuous yaw belongs with the later continuous-movement integration.
float g_yaw    = 0.0f;      // north-up gameplay view
float g_pitch  = 0.9f;      // radians above the horizon
float g_dist   = 12.0f;     // cells; read the player at native sprite proportions
float g_ty     = 1.0f;      // target height above the ground plane

bool  g_follow = true;      // track the player, or hold over the map centre
int   g_debug  = 0;         // 0 textured, 1 classification colours
int   g_min_unit = 2;

bool  g_b_held = false, g_h_held = false, g_n_held = false, g_m_held = false, g_r_held = false;
bool g_camera_relative = true;
uint64_t g_control_time = 0;
camera_input::TurnLatch g_turn;

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
    const char* cadence=std::getenv("RUBYVR_VIEWER_CAPTURE_STEP");
    const unsigned step=cadence?unsigned(std::clamp(std::atoi(cadence),4,60)):4;
    if(n>=180*step || n%step || width<=0 || height<=0 || width>4096 || height>4096)return;
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
bool focused() { return g_active && SDL_GetKeyboardFocus()==g_win; }
float yaw_radians() { return g_yaw; }
void set_yaw_radians(float yaw) {
    if(std::isfinite(yaw)) g_yaw=camera_input::quadrant(yaw)*1.570796327f;
}
bool camera_relative() { return g_camera_relative; }
void set_camera_relative(bool enabled) { g_camera_relative=enabled; }
void reset_camera() { g_yaw=0;g_pitch=0.9f;g_dist=12;g_ty=1;g_follow=true;g_turn.reset(); }

bool init(SDL_Window* win, bool visible) {
    if (!win) return false;
    g_win = win;
    reset_camera();g_control_time=SDL_GetTicks64();

    SDL_SetWindowSize(win, 1280, 800);
    SDL_SetWindowTitle(win, "RubyRecomp - diorama viewer");
    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    if (visible) SDL_ShowWindow(win);

    // Vsync off: this shares a thread with the emulator, and blocking here for
    // the monitor's refresh would pace the GAME to the monitor.
    SDL_GL_SetSwapInterval(0);

    g_active = true;
    std::fprintf(stderr,
                 "[viewer] window open. J/L turn 90 degrees, I/K tilt, U/O zoom, T/G target,\n"
                 "[viewer] N/M min-unit, B debug colours, H follow-player, R north-up\n"
                 "[viewer] Focus this window to walk relative to its camera; menus keep original directions.\n");
    return true;
}

void frame(const world::Snapshot& s, bool present) {
    if (!g_active || !g_win) return;
    if (!diorama::ready() && !diorama::init()) return;

    // Controls first, so a re-mesh request lands before update() runs.
    const auto now=SDL_GetTicks64();
    const float dt=std::min(float(now-g_control_time)/1000.f,0.05f);
    g_control_time=now;
    if (const Uint8* k = SDL_GetKeyboardState(nullptr);focused() && k) {
        if (pressed(k,SDL_SCANCODE_R,&g_r_held)) reset_camera();
        const bool walking=k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_DOWN] ||
            k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_RIGHT];
        const int turn=g_turn.update(k[SDL_SCANCODE_J],k[SDL_SCANCODE_L],walking);
        if(turn) set_yaw_radians(g_yaw+turn*1.570796327f);
        if (k[SDL_SCANCODE_I]) g_pitch += 0.9f*dt;
        if (k[SDL_SCANCODE_K]) g_pitch -= 0.9f*dt;
        const float zoom=std::pow(1.02f,60*dt);
        if (k[SDL_SCANCODE_U]) g_dist  *= zoom;
        if (k[SDL_SCANCODE_O]) g_dist  /= zoom;
        if (k[SDL_SCANCODE_T]) g_ty    += 4.8f*dt;
        if (k[SDL_SCANCODE_G]) g_ty    -= 4.8f*dt;
        set_yaw_radians(g_yaw);

        // Clamp pitch just short of the poles: at exactly straight-down the
        // look-at basis degenerates and the view snaps to an arbitrary roll.
        if (g_pitch >  1.50f) g_pitch =  1.50f;
        if (g_pitch <  0.15f) g_pitch =  0.15f;
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
    } else {
        g_b_held=g_h_held=g_n_held=g_m_held=g_r_held=false;
        g_turn.reset();
    }

    diorama::update(s);
    if (!diorama::has_geometry()) {
        SDL_SetWindowTitle(g_win,"RubyRecomp - scene unavailable; use the original game window");
        gl::glBindFramebuffer(GL_FRAMEBUFFER,0);
        glClearColor(0.07f,0.08f,0.11f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (present) SDL_GL_SwapWindow(g_win);
        return;
    }
    char title[256];
    const auto& actors=actor_render::stats();
    const char* compass[]={"N","W","S","E"};
    std::snprintf(title,sizeof(title),"RubyRecomp - live map %d.%d | %d actors | Up=%s (field) | Arrows: walk | J/L: turn 90 | R: north-up | %s",
        s.map_group,s.map_number,actors.visible,compass[g_camera_relative?camera_input::quadrant(g_yaw):0],
        focused()?"3D controls":"Focus here to play");
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
