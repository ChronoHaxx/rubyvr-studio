#include "environment_preview.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// Adapted from DRAMALESS_SHAPE lib/Sky.lua and lib/DayNight.lua at
// 5ccc0e25417bd8b16cefd50cf08f61c44223e3d4 (MIT, Stahltier and contributors).
// Retained notice: LICENSES/DRAMALESS_SHAPE-MIT.txt. See docs/reuse-review.md.
// The band palettes and checker transition follow that source. This desktop
// pass omits its game clock, sun/moon, shadows and LÖVE resource handling.
namespace studio::environment {
namespace {
namespace gl = vr::gl;

// Horizon first, zenith last. Exact six-band phase palettes from DayNight.
constexpr unsigned char palettes[4][6][3] = {
    {{248,216,152},{248,176,136},{232,136,144},{176,104,168},{112,80,168},{64,64,136}},
    {{184,216,248},{144,192,248},{104,160,240},{72,128,224},{48,96,200},{40,72,168}},
    {{248,200,112},{248,152,96},{232,104,96},{184,80,136},{120,64,152},{56,48,120}},
    {{88,104,160},{64,80,136},{48,56,112},{32,40,88},{16,24,64},{8,8,40}},
};
constexpr unsigned char tints[4][3] = {
    {255,216,192}, {255,255,255}, {255,192,168}, {120,136,192}
};

const char* vertex = R"(#version 330 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
const char* fragment = R"(#version 330 core
uniform sampler2D uPalette;
uniform vec4 uViewport;
uniform float uEdge;
uniform float uCell;
out vec4 FragColor;
void main() {
    // Viewport-local, top-down display pixels, including high-DPI scaling.
    vec2 sc = vec2(gl_FragCoord.x - uViewport.x,
                   uViewport.w - (gl_FragCoord.y - uViewport.y));
    float row = floor(sc.y / uCell) * uCell;
    float pos = clamp(row / max(uEdge, 1.0), 0.0, 1.0) * 6.0;
    float band = min(floor(pos), 5.0);
    float parity = mod(floor(sc.x / uCell) + floor(sc.y / uCell), 2.0);
    if (band < 5.0 && fract(pos) > 0.6 && parity < 0.5) band += 1.0;
    // One texel per band avoids uniform-array indexing/budget assumptions.
    FragColor = vec4(texture(uPalette, vec2((5.5-band)/6.0, 0.5)).rgb, 1.0);
}
)";

GLuint compile(GLenum type, const char* source) {
    GLuint shader = gl::glCreateShader(type);
    gl::glShaderSource(shader, 1, &source, nullptr);
    gl::glCompileShader(shader);
    GLint ok = 0;
    gl::glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        gl::glGetShaderInfoLog(shader, sizeof(log)-1, nullptr, log);
        std::fprintf(stderr, "[environment] shader failed: %s\n", log);
        gl::glDeleteShader(shader);
        return 0;
    }
    return shader;
}
} // namespace

const char* name(Phase phase) {
    switch (phase) {
        case Phase::Dawn: return "Dawn";
        case Phase::Noon: return "Noon";
        case Phase::Dusk: return "Dusk";
        case Phase::Night: return "Night";
        default: return "Neutral";
    }
}

vr::diorama::Tint tint(Phase phase) {
    const int i = int(phase)-1;
    if (i < 0 || i >= 4) return {};
    // Keep 35% of the untinted source for editor readability, especially at
    // night. Multiplication preserves black outlines; no additive black lift.
    return {.35f + .65f*tints[i][0]/255.f, .35f + .65f*tints[i][1]/255.f,
            .35f + .65f*tints[i][2]/255.f};
}

bool Sky::initialize() {
    if (attempted_) return program_ != 0;
    attempted_ = true;
    const GLuint vs = compile(GL_VERTEX_SHADER, vertex);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
    if (!vs || !fs) {
        if (vs) gl::glDeleteShader(vs);
        if (fs) gl::glDeleteShader(fs);
        return false;
    }
    program_ = gl::glCreateProgram();
    gl::glAttachShader(program_, vs); gl::glAttachShader(program_, fs);
    gl::glLinkProgram(program_);
    gl::glDeleteShader(vs); gl::glDeleteShader(fs);
    GLint ok = 0;
    gl::glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        gl::glGetProgramInfoLog(program_, sizeof(log)-1, nullptr, log);
        std::fprintf(stderr, "[environment] link failed: %s\n", log);
        gl::glDeleteProgram(program_); program_ = 0;
        return false;
    }
    viewport_ = gl::glGetUniformLocation(program_, "uViewport");
    edge_ = gl::glGetUniformLocation(program_, "uEdge");
    cell_ = gl::glGetUniformLocation(program_, "uCell");
    palette_ = gl::glGetUniformLocation(program_, "uPalette");
    gl::glGenVertexArrays(1, &vao_);
    glGenTextures(1, &ramp_);
    return true;
}

bool Sky::draw(Phase phase, float pitch, float half_fov, float cell_pixels,
               bool orthographic) {
    if (phase == Phase::Neutral) return true;
    if (int(phase) < 1 || int(phase) > 4 || !initialize()) return false;

    GLint viewport[4], previous_program, previous_vao, previous_active, previous_texture;
    GLboolean depth_write;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previous_active);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write);
    const bool depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE);
    const bool blend = glIsEnabled(GL_BLEND);
    gl::glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    glBindTexture(GL_TEXTURE_2D, ramp_);
    if (uploaded_ != phase) {
        GLint alignment;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // RGB row is eighteen bytes
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 6, 1, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, palettes[int(phase)-1]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        uploaded_ = phase;
    }
    // Use the projected horizon when it is in frame. As in Sky.region, a
    // downward/orthographic survey gets a short banded backdrop over haze.
    const float horizon = viewport[3]*.5f*(1.f-std::tan(pitch)/std::tan(half_fov));
    const float edge = !orthographic && horizon > 0 ? std::min(horizon, float(viewport[3]))
                                                                  : viewport[3]*.23f;
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    gl::glUseProgram(program_); gl::glBindVertexArray(vao_);
    gl::glUniform4f(viewport_, float(viewport[0]), float(viewport[1]), float(viewport[2]), float(viewport[3]));
    gl::glUniform1f(edge_, edge);
    gl::glUniform1f(cell_, std::max(1.f, std::floor(cell_pixels+.5f)));
    gl::glUniform1i(palette_, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    gl::glUseProgram(GLuint(previous_program)); gl::glBindVertexArray(GLuint(previous_vao));
    glBindTexture(GL_TEXTURE_2D, GLuint(previous_texture));
    gl::glActiveTexture(GLenum(previous_active));
    glDepthMask(depth_write);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (blend) glEnable(GL_BLEND);
    return true;
}

void Sky::release() {
    if (program_) gl::glDeleteProgram(program_);
    if (vao_) gl::glDeleteVertexArrays(1, &vao_);
    if (ramp_) glDeleteTextures(1, &ramp_);
    program_ = vao_ = ramp_ = 0;
    attempted_ = false; uploaded_ = Phase::Neutral;
}
} // namespace studio::environment
