// gl_loader.cpp — see gl_loader.h for why this is necessary on Windows.

#include "gl_loader.h"

#include <SDL2/SDL.h>

#include <cstdio>

namespace vr {
namespace gl {

#define X(type, name) type name = nullptr;
VR_GL_FUNCTIONS(X)
#undef X

namespace {
bool g_loaded = false;
}

bool loaded() { return g_loaded; }

bool load() {
    if (g_loaded) return true;

    if (!SDL_GL_GetCurrentContext()) {
        std::fprintf(stderr, "[gl] load() with no current context\n");
        return false;
    }

    const char* missing = nullptr;
    int         n_missing = 0;

#define X(type, name)                                                          \
    name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));               \
    if (!name) { if (!missing) missing = #name; ++n_missing; }
    VR_GL_FUNCTIONS(X)
#undef X

    if (missing) {
        // Naming one and counting the rest. A driver that hands back a
        // 1.1-or-2.1 context fails dozens at once, and forty identical lines
        // would bury the version number that is the actual diagnosis.
        std::fprintf(stderr,
                     "[gl] %d entry point(s) missing, first is %s\n"
                     "[gl] context reports GL_VERSION \"%s\"\n",
                     n_missing, missing,
                     reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        return false;
    }

    std::fprintf(stderr, "[gl] loaded, GL_VERSION \"%s\"\n",
                 reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    g_loaded = true;
    return true;
}

}  // namespace gl
}  // namespace vr
