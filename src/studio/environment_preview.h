#pragma once

#include "diorama.h"
#include "gl_loader.h"

namespace studio::environment {

// Session-only editor state. No clock, document fields or source palette edits.
enum class Phase { Neutral, Dawn, Noon, Dusk, Night };
const char* name(Phase phase);
vr::diorama::Tint tint(Phase phase);

class Sky {
public:
    // Draw into the caller's current viewport/scissor without writing depth.
    // pitch is positive looking down; half_fov is the vertical half-angle.
    bool draw(Phase phase, float pitch, float half_fov, float cell_pixels,
              bool orthographic);
    void release(); // while the GL context is still current
private:
    bool initialize();
    bool attempted_ = false;
    GLuint program_ = 0, vao_ = 0, ramp_ = 0;
    GLint viewport_ = -1, edge_ = -1, cell_ = -1, palette_ = -1;
    Phase uploaded_ = Phase::Neutral;
};

} // namespace studio::environment
