#include "room2d.h"
#include "gl_loader.h"
#include "tileset.h"

namespace studio {

bool Room2D::build(const vr::world::Snapshot& s) {
    if (!s.valid || s.width <= 0 || s.height <= 0 ||
        s.width > 1024 || s.height > 1024) return false;
    w = s.width * 16;
    h = s.height * 16;
    rgba.assign(size_t(w) * h, 0xFF24211Eu);
    uint32_t palette[vr::world::kPaletteEntries];
    vr::tileset::expand_palette(s, palette);
    // Same two-pass order as map_view::render; the workspace shows the full
    // backup map instead of wrapping a moving 16x16-cell hardware window.
    for (int pass = 0; pass < 2; ++pass)
        for (int y = 0; y < s.height; ++y)
            for (int x = 0; x < s.width; ++x) {
                const uint16_t raw = s.cell(x, y);
                if (raw == vr::world::kGridUndefined) continue;
                vr::tileset::blit_metatile_pair(rgba.data(), w, h, x * 16, y * 16,
                    s, palette, raw & vr::world::kMetatileIdMask,
                    pass ? vr::tileset::kPairUpper : vr::tileset::kPairLower);
            }
    return true;
}

bool Room2D::upload() {
    if (rgba.empty()) return false;
    GLint max_size = 0, previous = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    if (w > max_size || h > max_size) return false;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    if (!texture) glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba.data());
    const bool ok = glGetError() == GL_NO_ERROR;
    glBindTexture(GL_TEXTURE_2D, unsigned(previous));
    return ok;
}

void Room2D::release() {
    if (texture) glDeleteTextures(1, &texture);
    texture = 0;
}

} // namespace studio
