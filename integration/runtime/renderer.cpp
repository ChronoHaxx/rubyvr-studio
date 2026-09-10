// renderer.cpp — see renderer.h for what a projection layer is and why.

#include "renderer.h"
#include "gl_loader.h"
#include "diorama.h"
#include "vr_math.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace vr {
namespace renderer {
namespace {

// ── shaders ──────────────────────────────────────────────────────────────────

const char* kVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aCol;
uniform mat4 uViewProj;
out vec3 vCol;
void main() {
    vCol = aCol;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

const char* kFrag = R"(#version 330 core
in vec3 vCol;
out vec4 FragColor;
void main() { FragColor = vec4(vCol, 1.0); }
)";

GLuint compile(GLenum stage, const char* src, const char* label) {
    const GLuint s = gl::glCreateShader(stage);
    gl::glShaderSource(s, 1, &src, nullptr);
    gl::glCompileShader(s);

    GLint ok = 0;
    gl::glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        gl::glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
        std::fprintf(stderr, "[renderer] %s shader failed:\n%s\n", label, log);
        gl::glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link_program() {
    const GLuint vs = compile(GL_VERTEX_SHADER, kVert, "vertex");
    if (!vs) return 0;
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kFrag, "fragment");
    if (!fs) { gl::glDeleteShader(vs); return 0; }

    const GLuint p = gl::glCreateProgram();
    gl::glAttachShader(p, vs);
    gl::glAttachShader(p, fs);
    gl::glLinkProgram(p);

    GLint ok = 0;
    gl::glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        gl::glGetProgramInfoLog(p, sizeof(log) - 1, nullptr, log);
        std::fprintf(stderr, "[renderer] link failed:\n%s\n", log);
        gl::glDeleteProgram(p);
    }
    // Attached shaders are reference-counted by the program; deleting the
    // objects here is the normal idiom and does not affect the linked program.
    gl::glDeleteShader(vs);
    gl::glDeleteShader(fs);
    return ok ? p : 0;
}

// ── the debug scene ──────────────────────────────────────────────────────────
//
// A cube with a differently-coloured face per direction, plus a floor grid.
//
// This is not decoration; it is the instrument that tells you the maths is
// right. A single-coloured cube looks correct under a mirrored or transposed
// matrix. Distinct faces make a handedness error obvious (you see the inside,
// or red where green should be), and the grid gives the eye a horizon so
// scale and tracking drift are judgeable at all.

struct Vertex { float x, y, z, r, g, b; };

void push_quad(std::vector<Vertex>& v,
               float ax, float ay, float az, float bx, float by, float bz,
               float cx, float cy, float cz, float dx, float dy, float dz,
               float r, float g, float b) {
    v.push_back({ax, ay, az, r, g, b});
    v.push_back({bx, by, bz, r, g, b});
    v.push_back({cx, cy, cz, r, g, b});
    v.push_back({ax, ay, az, r, g, b});
    v.push_back({cx, cy, cz, r, g, b});
    v.push_back({dx, dy, dz, r, g, b});
}

std::vector<Vertex> build_scene() {
    std::vector<Vertex> v;

    // A 0.3 m cube, 1.5 m ahead. OpenXR is right-handed with -Z forward, so
    // "in front of you" is negative Z — getting that sign wrong puts the cube
    // behind your head, which reads as "nothing rendered".
    const float s = 0.15f;
    const float cz = -1.5f;
    const float cy = 0.0f;

    const float x0 = -s, x1 = s;
    const float y0 = cy - s, y1 = cy + s;
    const float z0 = cz - s, z1 = cz + s;

    push_quad(v, x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1, 0.90f,0.25f,0.25f); // +Z toward you: red
    push_quad(v, x1,y0,z0, x0,y0,z0, x0,y1,z0, x1,y1,z0, 0.25f,0.90f,0.35f); // -Z away: green
    push_quad(v, x1,y0,z1, x1,y0,z0, x1,y1,z0, x1,y1,z1, 0.30f,0.45f,0.95f); // +X right: blue
    push_quad(v, x0,y0,z0, x0,y0,z1, x0,y1,z1, x0,y1,z0, 0.95f,0.80f,0.20f); // -X left: yellow
    push_quad(v, x0,y1,z1, x1,y1,z1, x1,y1,z0, x0,y1,z0, 0.90f,0.55f,0.15f); // +Y top: orange
    push_quad(v, x0,y0,z0, x1,y0,z0, x1,y0,z1, x0,y0,z1, 0.65f,0.30f,0.85f); // -Y bottom: purple

    // Floor grid, 4 m square, 1 m below the LOCAL origin (roughly floor height
    // for a seated or standing user, since LOCAL sits near the head).
    const float gy = -1.0f, half = 2.0f, t = 0.004f;
    for (int i = -4; i <= 4; ++i) {
        const float p = i * 0.5f;
        const float c = (i == 0) ? 0.55f : 0.28f;
        push_quad(v, -half,gy,p-t,  half,gy,p-t,  half,gy,p+t, -half,gy,p+t, c,c,c);
        push_quad(v, p-t,gy,-half,  p+t,gy,-half, p+t,gy,half, p-t,gy,half,  c,c,c);
    }
    return v;
}

// ── state ────────────────────────────────────────────────────────────────────

struct Eye {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    int32_t     w = 0, h = 0;
    std::vector<XrSwapchainImageOpenGLKHR> images;
    GLuint      depth = 0;   // renderbuffer; ours, not the runtime's
};

bool                  g_ready = false;
XrSession             g_session = XR_NULL_HANDLE;
std::vector<Eye>      g_eyes;
GLuint                g_fbo = 0;
GLuint                g_prog = 0, g_vao = 0, g_vbo = 0;
GLint                 g_u_viewproj = -1;
GLsizei               g_vertex_count = 0;

// Pick a swapchain format.
//
// Prefer sRGB, which is what desktop runtimes list first and expect. We do NOT
// enable GL_FRAMEBUFFER_SRGB: our colours are already display-referred (the
// GBA's palette is, and so are these debug colours), so the bytes should land
// in the image unconverted. That matches exactly what the working quad path
// does, which matters — the two layers sit side by side in one frame and any
// mismatch would show as the 3D scene and the game screen disagreeing.
int64_t choose_format(const std::vector<int64_t>& formats) {
    constexpr int64_t kSrgb8Alpha8 = 0x8C43;   // GL_SRGB8_ALPHA8
    constexpr int64_t kRgba8       = 0x8058;   // GL_RGBA8
    for (int64_t want : {kSrgb8Alpha8, kRgba8})
        for (int64_t f : formats)
            if (f == want) return f;
    return formats.empty() ? 0 : formats[0];
}

// Shaders, geometry and the FBO — everything that does not depend on OpenXR.
// Split out so the offline self-test can build the same pipeline the real path
// uses; a self-test against a different pipeline tests nothing.
bool ensure_pipeline() {
    if (g_prog) return true;
    if (!gl::loaded()) return false;

    g_prog = link_program();
    if (!g_prog) return false;
    g_u_viewproj = gl::glGetUniformLocation(g_prog, "uViewProj");

    const std::vector<Vertex> verts = build_scene();
    g_vertex_count = static_cast<GLsizei>(verts.size());

    gl::glGenVertexArrays(1, &g_vao);
    gl::glBindVertexArray(g_vao);
    gl::glGenBuffers(1, &g_vbo);
    gl::glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    gl::glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                     verts.data(), GL_STATIC_DRAW);
    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(0));
    gl::glEnableVertexAttribArray(1);
    gl::glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(sizeof(float) * 3));
    gl::glBindVertexArray(0);

    gl::glGenFramebuffers(1, &g_fbo);
    return true;
}

// Draw the scene with an already-bound framebuffer and viewport.
//
// `debug_only` forces the calibration cube even when a map is available; the
// offline self-test needs it, because a self-test whose subject depends on
// whether a save happens to be loaded is not a test.
void draw_scene(const XrPosef& pose, const XrFovf& fov, bool debug_only) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const math::Mat4 vp = math::multiply(math::projection(fov, 0.05f, 100.0f),
                                        math::view_from_pose(pose));

    // The diorama when there is a map, the calibration cube when there is not.
    // Falling back rather than rendering nothing matters: an empty black scene
    // and a broken renderer look identical through a headset, and the cube says
    // "the 3D path is alive, the world just is not loaded yet".
    if (!debug_only && diorama::ready() && diorama::has_geometry()) {
        diorama::draw(vp);
        return;
    }

    gl::glUseProgram(g_prog);
    gl::glUniformMatrix4fv(g_u_viewproj, 1, GL_FALSE, vp.m);
    gl::glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, g_vertex_count);
    gl::glBindVertexArray(0);
}

}  // namespace

bool ready() { return g_ready; }

bool init(XrInstance instance, XrSystemId system, XrSession session) {
    g_session = session;

    if (!gl::loaded()) {
        std::fprintf(stderr, "[renderer] GL entry points not loaded\n");
        return false;
    }

    // How many eyes, and what resolution does the runtime want for each?
    // recommendedImageRectWidth is the runtime's own answer to "what size gives
    // 1:1 pixels after lens distortion" — it is usually LARGER than the panel,
    // and second-guessing it costs sharpness for no gain.
    uint32_t view_count = 0;
    XrResult r = xrEnumerateViewConfigurationViews(
        instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        0, &view_count, nullptr);
    if (XR_FAILED(r) || view_count == 0) {
        std::fprintf(stderr, "[renderer] no view configurations (%d)\n", (int)r);
        return false;
    }

    std::vector<XrViewConfigurationView> cfg(
        view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    r = xrEnumerateViewConfigurationViews(
        instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        view_count, &view_count, cfg.data());
    if (XR_FAILED(r)) return false;

    uint32_t nfmt = 0;
    xrEnumerateSwapchainFormats(session, 0, &nfmt, nullptr);
    std::vector<int64_t> formats(nfmt);
    xrEnumerateSwapchainFormats(session, nfmt, &nfmt, formats.data());
    const int64_t format = choose_format(formats);

    // RESOLUTION SCALE — the single biggest performance dial in VR.
    //
    // recommendedImageRectWidth is the runtime's "1:1 pixels after lens
    // distortion" figure, and on a Quest 3 over Link it is 2688x2688 PER EYE.
    // That is 14 megapixels a frame at 90 Hz, and cost here is fill-rate bound,
    // not geometry bound: this scene is ~3000 triangles, which is nothing, but
    // the fragment shader runs on every one of those pixels.
    //
    // It is worse than raw pixel count suggests, because the shader uses
    // `discard` for transparent texels. discard DISABLES EARLY-Z, so hidden
    // fragments behind foliage still run the shader instead of being rejected
    // by the depth test first. Overlapping trees therefore cost their full
    // price several times over.
    //
    // Scaling the render target is the standard answer and it is close to free
    // visually — the runtime resamples, and the recommended size is generous.
    // 0.6 gives back roughly 2.8x the fill rate.
    float scale = 0.6f;
    if (const char* env = std::getenv("RUBYVR_RENDER_SCALE")) {
        const float v = static_cast<float>(std::atof(env));
        if (v >= 0.2f && v <= 2.0f) scale = v;
    }

    g_eyes.resize(view_count);
    for (uint32_t i = 0; i < view_count; ++i) {
        Eye& e = g_eyes[i];
        e.w = static_cast<int32_t>(cfg[i].recommendedImageRectWidth  * scale);
        e.h = static_cast<int32_t>(cfg[i].recommendedImageRectHeight * scale);

        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        ci.format      = format;
        ci.sampleCount = 1;
        ci.width       = e.w;
        ci.height      = e.h;
        ci.faceCount   = 1;
        ci.arraySize   = 1;
        ci.mipCount    = 1;
        r = xrCreateSwapchain(session, &ci, &e.swapchain);
        if (XR_FAILED(r)) {
            std::fprintf(stderr, "[renderer] xrCreateSwapchain eye %u: %d\n",
                         i, (int)r);
            return false;
        }

        uint32_t n = 0;
        xrEnumerateSwapchainImages(e.swapchain, 0, &n, nullptr);
        e.images.assign(n, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
        xrEnumerateSwapchainImages(
            e.swapchain, n, &n,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data()));

        // Our own depth. One per eye rather than one shared, because the two
        // eyes can in principle be different sizes.
        gl::glGenRenderbuffers(1, &e.depth);
        gl::glBindRenderbuffer(GL_RENDERBUFFER, e.depth);
        gl::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, e.w, e.h);
        gl::glBindRenderbuffer(GL_RENDERBUFFER, 0);

        std::fprintf(stderr, "[renderer] eye %u: %dx%d, %u images\n",
                     i, e.w, e.h, n);
    }

    if (!ensure_pipeline()) return false;

    // The diorama shares this context and this frame. A failure leaves the
    // calibration cube in place rather than taking the projection layer down.
    if (!diorama::init())
        std::fprintf(stderr, "[renderer] diorama unavailable; debug scene only\n");

    g_ready = true;
    std::fprintf(stderr, "[renderer] ready: %zu eyes, %d vertices\n",
                 g_eyes.size(), (int)g_vertex_count);
    return true;
}

bool render_frame(XrSpace space, XrTime display_time,
                  XrCompositionLayerProjection& out_layer,
                  std::vector<XrCompositionLayerProjectionView>& views) {
    if (!g_ready) return false;

    // Where are the eyes this frame? Poses are predicted for display_time, not
    // sampled now — using "now" is what makes a headset feel laggy.
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime           = display_time;
    li.space                 = space;

    XrViewState vs{XR_TYPE_VIEW_STATE};
    uint32_t    n = 0;
    std::vector<XrView> xr_views(g_eyes.size(), {XR_TYPE_VIEW});
    XrResult r = xrLocateViews(g_session, &li, &vs,
                               static_cast<uint32_t>(xr_views.size()), &n,
                               xr_views.data());
    if (XR_FAILED(r) ||
        !(vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) ||
        !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        return false;   // tracking lost — submit nothing rather than something wrong
    }

    views.assign(n, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});

    for (uint32_t i = 0; i < n && i < g_eyes.size(); ++i) {
        Eye& e = g_eyes[i];

        uint32_t idx = 0;
        if (XR_FAILED(xrAcquireSwapchainImage(e.swapchain, nullptr, &idx)))
            return false;
        XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wi.timeout = XR_INFINITE_DURATION;
        xrWaitSwapchainImage(e.swapchain, &wi);

        gl::glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, e.images[idx].image, 0);
        gl::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                      GL_RENDERBUFFER, e.depth);

        glViewport(0, 0, e.w, e.h);
        // Backfaces are NOT culled: the debug scene's winding is not
        // consistent, and a wrongly-culled scene looks identical to one that
        // failed to render. Phase 6's mesh will want culling on.
        draw_scene(xr_views[i].pose, xr_views[i].fov, /*debug_only=*/false);

        gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
        xrReleaseSwapchainImage(e.swapchain, nullptr);

        views[i].pose                           = xr_views[i].pose;
        views[i].fov                            = xr_views[i].fov;
        views[i].subImage.swapchain             = e.swapchain;
        views[i].subImage.imageArrayIndex       = 0;
        views[i].subImage.imageRect.offset      = {0, 0};
        views[i].subImage.imageRect.extent      = {e.w, e.h};
    }

    out_layer            = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    out_layer.space      = space;
    out_layer.viewCount  = static_cast<uint32_t>(views.size());
    out_layer.views      = views.data();
    return true;
}

namespace {

// A throwaway colour+depth target, rendered into and read straight back.
// Shared by both self-tests so they exercise the same path.
struct Offscreen {
    GLuint tex = 0, depth = 0, fbo = 0;
    bool   ok = false;

    bool create(int w, int h) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        gl::glGenRenderbuffers(1, &depth);
        gl::glBindRenderbuffer(GL_RENDERBUFFER, depth);
        gl::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        gl::glBindRenderbuffer(GL_RENDERBUFFER, 0);

        gl::glGenFramebuffers(1, &fbo);
        gl::glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, tex, 0);
        gl::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                      GL_RENDERBUFFER, depth);

        const GLenum status = gl::glCheckFramebufferStatus(GL_FRAMEBUFFER);
        ok = (status == GL_FRAMEBUFFER_COMPLETE);
        if (!ok) {
            std::fprintf(stderr, "[renderer] offscreen FBO incomplete: 0x%X\n",
                         (unsigned)status);
            gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        return ok;
    }

    void destroy() {
        gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (fbo) gl::glDeleteFramebuffers(1, &fbo);
        if (depth) gl::glDeleteRenderbuffers(1, &depth);
        if (tex) glDeleteTextures(1, &tex);
        fbo = depth = tex = 0;
    }
};

bool read_and_write_ppm(const char* path, int w, int h) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 3);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());

    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    // GL reads back bottom-up; PPM is top-down.
    for (int y = h - 1; y >= 0; --y)
        std::fwrite(px.data() + static_cast<size_t>(y) * w * 3, 1,
                    static_cast<size_t>(w) * 3, f);
    std::fclose(f);
    return true;
}

// A camera above and in front of the origin, pitched down to look at it.
//
// A quaternion about X of (sin(a/2), 0, 0, cos(a/2)) with NEGATIVE a tilts the
// camera's forward axis downward — the camera looks along -Z, and rotating that
// by a negative angle about +X sends it to (0, sin a, -cos a). Note w is the
// LAST field of XrQuaternionf, an order that trips people up constantly.
XrPosef looking_down(float height, float back, float pitch) {
    XrPosef p{};
    p.position = {0.0f, height, back};
    p.orientation = {std::sin(pitch * 0.5f), 0.0f, 0.0f, std::cos(pitch * 0.5f)};
    return p;
}

XrFovf typical_fov() {
    XrFovf fov{};
    fov.angleLeft = -0.78f; fov.angleRight = 0.78f;
    fov.angleUp   =  0.70f; fov.angleDown  = -0.70f;
    return fov;
}

}  // namespace

// The six-shot sheet, rendered against WHATEVER MESH IS CURRENTLY BUILT.
//
// Split out of inspect() so the isolate path can substitute a different mesh
// and get identical framing. Sharing the shot list is the point: an isolated
// structure has to be judged by the same cameras as the map it came out of, or
// "it looks fine on its own" means nothing.
bool render_sheet(const char* path, const world::Snapshot& s) {
    if (!diorama::has_geometry()) {
        std::fprintf(stderr, "[inspect] no geometry\n");
        return false;
    }

    float mw = 0, mh = 0, px = 0, py = 0, pz = 0;
    diorama::map_size(&mw, &mh);
    diorama::player_cell(&px, &py, &pz);

    // RUBYVR_FOCUS=x,z aims the three close shots at a chosen cell instead of
    // at the player. The save decides where the player stands, and the thing
    // you need to look at is routinely somewhere else — a building at the map's
    // north edge is cut off in every default frame, which is exactly the
    // situation this was written for. Height stays the player's: it is the
    // ground plane, and the point is to stand ON the map, not above it.
    if (const char* f = std::getenv("RUBYVR_FOCUS")) {
        float fx = 0, fz = 0;
        if (std::sscanf(f, "%f,%f", &fx, &fz) == 2) {
            px = fx;
            pz = fz;
            std::fprintf(stderr, "[inspect] focus override: cell (%.1f,%.1f)\n", px, pz);
        } else {
            std::fprintf(stderr, "[inspect] RUBYVR_FOCUS=\"%s\" is not x,z\n", f);
        }
    }

    // Cameras are placed in raw MAP CELL coordinates, so the model matrix is
    // identity and "put the eye 12 cells south of the player" means exactly
    // that. Framing shots around the player rather than the map centre keeps
    // the interesting part — wherever the save happens to be — in view.
    struct Shot {
        const char* name;
        float ex, ey, ez;    // eye, relative to the player unless absolute
        float tx, ty, tz;    // target
        bool  absolute;      // true: eye/target are map coordinates
        int   debug;
    };

    const float cx = mw * 0.5f, cz = mh * 0.5f;
    const Shot shots[9] = {
        // Whole map from above: is the footprint right, are there holes?
        {"overhead",   cx, mh * 1.15f, cz + 0.01f,  cx, 0, cz,  true,  0},
        // The classic diorama angle.
        {"oblique",    cx, mh * 0.62f, cz + mh * 0.80f,  cx, 0, cz,  true,  0},
        // Same angle, classification colours: grey ground, green 1, yellow 2,
        // red 3+. This is the one that says WHY the mesh looks how it does.
        {"classes",    cx, mh * 0.62f, cz + mh * 0.80f,  cx, 0, cz,  true,  1},
        // Close over the player, high enough to read individual structures.
        {"near-high",  0, 9.0f, 11.0f,   0, 0, 0,  false, 0},
        // Low and close: the angle that exposes flat sides and wrong heights.
        {"near-low",   0, 4.0f, 9.0f,    0, 1.0f, 0,  false, 0},
        // TRUE first person: the eye stands exactly where the player stands and
        // looks north. An earlier version placed it a few cells south "to see
        // the player", which put the camera inside the forest the moment trees
        // grew past a metre — the tile came back as a wall of green and said
        // nothing. Where the player is, is the whole question.
        {"eye-level",  0, 1.6f, 0.0f,    0, 1.3f, -8.0f,  false, 0},
        // The three the south-facing shots above cannot show at all, because
        // every one of them looks north across the subject. A defect on a face
        // the player never walks up to — the east wall behind a tree, a gap
        // under the north eave — is invisible until something looks from a
        // different side. Same distance and height as near-low, rotated: west
        // and east are the flanks, back is the far (north) side turned to face
        // the camera instead of away from it.
        {"side-west",  -9.0f, 4.0f, 0.0f,   0, 1.0f, 0,  false, 0},
        {"side-east",   9.0f, 4.0f, 0.0f,   0, 1.0f, 0,  false, 0},
        {"back",        0, 4.0f, -9.0f,     0, 1.0f, 0,  false, 0},
    };

    constexpr int kTileW = 440, kTileH = 330, kCols = 3, kRows = 3;
    const int W = kTileW * kCols, H = kTileH * kRows;

    Offscreen off;
    if (!off.create(W, H)) return false;

    glViewport(0, 0, W, H);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.06f, 0.07f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    const math::Mat4 identity = math::identity();

    XrFovf fov{};
    fov.angleLeft = -0.62f; fov.angleRight = 0.62f;
    fov.angleUp   =  0.50f; fov.angleDown  = -0.50f;

    for (int i = 0; i < 9; ++i) {
        const Shot& sh = shots[i];
        const int col = i % kCols, row = i / kCols;

        // Scissor as well as viewport: the clear must not wipe the tiles that
        // are already drawn beside this one.
        glViewport(col * kTileW, (kRows - 1 - row) * kTileH, kTileW, kTileH);
        glScissor (col * kTileW, (kRows - 1 - row) * kTileH, kTileW, kTileH);
        glEnable(GL_SCISSOR_TEST);
        glClear(GL_DEPTH_BUFFER_BIT);

        const float ex = sh.absolute ? sh.ex : px + sh.ex;
        const float ey = sh.absolute ? sh.ey : py + sh.ey;
        const float ez = sh.absolute ? sh.ez : pz + sh.ez;
        const float tx = sh.absolute ? sh.tx : px + sh.tx;
        const float ty = sh.absolute ? sh.ty : py + sh.ty;
        const float tz = sh.absolute ? sh.tz : pz + sh.tz;

        const math::Mat4 vp = math::multiply(
            math::projection(fov, 0.05f, 400.0f),
            math::look_at(ex, ey, ez, tx, ty, tz));

        diorama::draw_raw(vp, identity, sh.debug);
    }
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, W, H);

    const bool ok = read_and_write_ppm(path, W, H);
    off.destroy();

    std::fprintf(stderr,
                 "[inspect] %s %s  map %.0fx%.0f  focus (%.2f,%.2f)  "
                 "tiles: overhead oblique classes / near-high near-low eye-level / "
                 "side-west side-east back\n",
                 ok ? "wrote" : "FAILED", path, mw, mh, px, pz);
    return ok;
}

bool inspect(const char* path, const world::Snapshot& s) {
    if (!gl::loaded() || !s.valid) return false;
    if (!diorama::ready() && !diorama::init()) return false;
    diorama::update(s);
    return render_sheet(path, s);
}

// ── Isolate one structure ────────────────────────────────────────────────────
//
// One rectangle of the map, alone on empty ground.
//
// THE POINT IS THAT NOTHING ELSE IS THERE. On the real map every structure has
// twenty neighbours, so a defect in one of them is indistinguishable from an
// interaction between two — a face suppressed by a neighbour that should not
// have been standing, a run that merged with the one beside it, a silhouette
// flooded from a side that should have been closed. Lifted onto plain ground
// there is nothing else it could be, which is what makes fixing one thing at a
// time possible at all.
//
// Same idea as the tileset turntable, one scale up: that isolates a metatile,
// this isolates a building.
bool inspect_isolate(const char* path, const world::Snapshot& s,
                     int x, int y, int w, int h) {
    if (!gl::loaded() || !s.valid) return false;
    if (!diorama::ready() && !diorama::init()) return false;

    diorama::update(s);                       // uploads the tile sheet
    if (!diorama::build_isolate(s, x, y, w, h)) {
        std::fprintf(stderr, "[isolate] nothing at %d,%d %dx%d\n", x, y, w, h);
        return false;
    }
    return render_sheet(path, s);
}

// ── Tileset turntable ────────────────────────────────────────────────────────
//
// One block per metatile, four yaws per block. Four is not decoration: the fold
// rule puts the SAME art on all four sides of a standing unit, so "does this
// metatile look right" is a question about four faces, and a single view cannot
// answer it. The 2x2 reads north-west, north-east, south-east, south-west
// clockwise, so a face that is wrong on one side only is obvious as an odd
// quadrant rather than something you have to go looking for.
//
// The manifest goes to stderr and tools/turntable.py labels the sheet from it.
// Labels have to be added outside GL — there is no text renderer here and a
// bitmap font would be a subsystem to maintain for six characters a tile.
bool inspect_turntable(const char* path, const world::Snapshot& s) {
    if (!gl::loaded() || !s.valid) return false;
    if (!diorama::ready() && !diorama::init()) return false;

    // update() first: the tile sheet and palettes have to be uploaded, and
    // build_turntable then replaces the mesh it built.
    diorama::update(s);

    diorama::Turntable tt;
    if (!diorama::build_turntable(s, &tt)) {
        std::fprintf(stderr, "[turntable] nothing to render\n");
        return false;
    }
    if (!diorama::has_geometry()) {
        std::fprintf(stderr, "[turntable] no geometry\n");
        return false;
    }

    constexpr int kSub = 110;              // one yaw
    constexpr int kBlock = kSub * 2;       // one metatile
    const int W = tt.cols * kBlock, H = tt.rows * kBlock;
    if (W <= 0 || H <= 0 || W > 8192 || H > 8192) {
        std::fprintf(stderr, "[turntable] sheet would be %dx%d; refusing\n", W, H);
        return false;
    }

    Offscreen off;
    if (!off.create(W, H)) return false;

    glViewport(0, 0, W, H);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.06f, 0.07f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    const math::Mat4 identity = math::identity();

    // A NARROW lens, not a close camera. The subject has to fill its tile while
    // the plots either side stay out of frame, and at the inspector's 70-degree
    // field the only way to fill the tile is to stand close enough that the
    // neighbours crowd in behind it — which is what the first sheet looked
    // like. Backing off and narrowing the lens gets the same subject size with
    // a third of the surrounding ground.
    XrFovf fov{};
    fov.angleLeft = -0.30f; fov.angleRight = 0.30f;
    fov.angleUp   =  0.30f; fov.angleDown  = -0.30f;

    // Far enough for a two-cell unit to fit with headroom, high enough to see
    // the top face — the two things a metatile audit is actually about.
    constexpr float kDist = 5.8f, kHigh = 2.2f;
    const float yaws[4] = {                       // NW, NE, SE, SW clockwise
        2.35619f, -2.35619f, -0.78540f, 0.78540f
    };

    for (size_t i = 0; i < tt.ids.size(); ++i) {
        const int pc = static_cast<int>(i) % tt.cols;
        const int pr = static_cast<int>(i) / tt.cols;
        const float cx = static_cast<float>(pc * tt.spacing + 1) + 0.5f;
        const float cz = static_cast<float>(pr * tt.spacing + 1) + 0.5f;

        for (int q = 0; q < 4; ++q) {
            const int sx = pc * kBlock + (q % 2) * kSub;
            const int sy = H - (pr * kBlock + (q / 2) * kSub) - kSub;

            glViewport(sx, sy, kSub, kSub);
            glScissor (sx, sy, kSub, kSub);
            glEnable(GL_SCISSOR_TEST);
            glClear(GL_DEPTH_BUFFER_BIT);

            const float ex = cx + std::sin(yaws[q]) * kDist;
            const float ez = cz + std::cos(yaws[q]) * kDist;
            const math::Mat4 vp = math::multiply(
                math::projection(fov, 0.05f, 400.0f),
                math::look_at(ex, kHigh, ez, cx, 1.05f, cz));

            diorama::draw_raw(vp, identity, 0);
        }
    }
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, W, H);

    const bool ok = read_and_write_ppm(path, W, H);
    off.destroy();

    std::fprintf(stderr, "[turntable] grid %dx%d block %d\n",
                 tt.cols, tt.rows, kBlock);
    for (size_t i = 0; i < tt.ids.size(); ++i)
        std::fprintf(stderr, "[turntable] plot %3d id %4u coll %u stands %u\n",
                     static_cast<int>(i), tt.ids[i], tt.collision[i], tt.height[i]);
    std::fprintf(stderr, "[turntable] %s %s (%dx%d)\n",
                 ok ? "wrote" : "FAILED", path, W, H);
    return ok;
}

bool selftest_diorama(const char* path, int w, int h, const world::Snapshot& s) {
    if (!gl::loaded() || !s.valid) return false;
    if (!diorama::ready() && !diorama::init()) return false;

    diorama::update(s);
    if (!diorama::has_geometry()) {
        std::fprintf(stderr, "[renderer] selftest_diorama: no geometry\n");
        return false;
    }

    Offscreen off;
    if (!off.create(w, h)) return false;

    glViewport(0, 0, w, h);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // The board's default placement is 0.9 m ahead and 0.45 m down, so this
    // camera sits above and slightly behind the viewer's usual spot and looks
    // at it — roughly how you would stand over a table.
    const XrPosef pose = looking_down(0.45f, 0.35f, -0.62f);
    const math::Mat4 vp = math::multiply(math::projection(typical_fov(), 0.02f, 100.0f),
                                         math::view_from_pose(pose));
    diorama::draw(vp);

    const bool ok = read_and_write_ppm(path, w, h);
    off.destroy();

    std::fprintf(stderr, "[renderer] selftest_diorama wrote %s (%s)\n",
                 path, ok ? "ok" : "FAILED");
    return ok;
}

bool selftest_dump(const char* path, int w, int h, bool asymmetric) {
    if (!ensure_pipeline()) {
        std::fprintf(stderr, "[renderer] selftest: no pipeline\n");
        return false;
    }

    // Scratch colour target and depth. Torn down at the end — this must not
    // leave state behind that the real path would then inherit.
    GLuint tex = 0, depth = 0, fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    gl::glGenRenderbuffers(1, &depth);
    gl::glBindRenderbuffer(GL_RENDERBUFFER, depth);
    gl::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    gl::glBindRenderbuffer(GL_RENDERBUFFER, 0);

    gl::glGenFramebuffers(1, &fbo);
    gl::glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tex, 0);
    gl::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, depth);

    const GLenum status = gl::glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "[renderer] selftest FBO incomplete: 0x%X\n",
                     (unsigned)status);
        gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    // Identity pose: at the LOCAL origin, looking down -Z, which is where the
    // cube was placed. A quaternion of (0,0,0,1) is "no rotation" — note w is
    // LAST in OpenXR's XrQuaternionf, a field order that has caught many people.
    XrPosef pose{};
    pose.orientation.w = 1.0f;

    // Roughly a headset's field of view. Radians, and the left/down angles are
    // NEGATIVE — they are directions from the optical axis, not magnitudes.
    XrFovf fov{};
    if (asymmetric) {
        fov.angleLeft  = -0.95f;   // wider to the left than the right, so a
        fov.angleRight =  0.60f;   // correct projection pushes the cube RIGHT
        fov.angleUp    =  0.85f;
        fov.angleDown  = -0.85f;
    } else {
        fov.angleLeft  = -0.78f;
        fov.angleRight =  0.78f;
        fov.angleUp    =  0.78f;
        fov.angleDown  = -0.78f;
    }

    glViewport(0, 0, w, h);
    draw_scene(pose, fov, /*debug_only=*/true);

    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 3);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);

    gl::glDeleteFramebuffers(1, &fbo);
    gl::glDeleteRenderbuffers(1, &depth);
    glDeleteTextures(1, &tex);

    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    // GL reads back bottom-up; PPM is top-down.
    for (int y = h - 1; y >= 0; --y)
        std::fwrite(px.data() + static_cast<size_t>(y) * w * 3, 1,
                    static_cast<size_t>(w) * 3, f);
    std::fclose(f);

    std::fprintf(stderr, "[renderer] selftest wrote %s (%dx%d, %s fov)\n",
                 path, w, h, asymmetric ? "asymmetric" : "symmetric");
    return true;
}

void shutdown() {
    diorama::shutdown();
    if (gl::loaded()) {
        if (g_vbo) gl::glDeleteBuffers(1, &g_vbo);
        if (g_vao) gl::glDeleteVertexArrays(1, &g_vao);
        if (g_prog) gl::glDeleteProgram(g_prog);
        if (g_fbo) gl::glDeleteFramebuffers(1, &g_fbo);
        for (Eye& e : g_eyes)
            if (e.depth) gl::glDeleteRenderbuffers(1, &e.depth);
    }
    for (Eye& e : g_eyes)
        if (e.swapchain) xrDestroySwapchain(e.swapchain);

    g_eyes.clear();
    g_vbo = g_vao = g_prog = g_fbo = 0;
    g_ready = false;
}

}  // namespace renderer
}  // namespace vr
