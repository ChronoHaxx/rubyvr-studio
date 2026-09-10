#pragma once

#include <algorithm>
#include <cmath>

namespace studio {

// All editor hit tests use logical window pixels. Only framebuffer_rect converts
// to physical GL pixels, including GL's bottom-left origin.
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

struct PixelRect { int x, y, w, h; };
inline PixelRect framebuffer_rect(Rect r, int window_w, int window_h,
                                  int drawable_w, int drawable_h) {
    const double sx = double(drawable_w) / std::max(1, window_w);
    const double sy = double(drawable_h) / std::max(1, window_h);
    const int x0 = std::clamp(int(std::lround(r.x * sx)), 0, drawable_w);
    const int x1 = std::clamp(int(std::lround((r.x + r.w) * sx)), 0, drawable_w);
    const int y0 = std::clamp(int(std::lround(r.y * sy)), 0, drawable_h);
    const int y1 = std::clamp(int(std::lround((r.y + r.h) * sy)), 0, drawable_h);
    return {x0, drawable_h - y1, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

struct ShellLayout {
    Rect header, groups, workspace, inspector, status, map, view;
    static ShellLayout at(int width, int height) {
        const float w = float(width), h = float(height);
        const float body = std::max(100.0f, h - 118.0f);
        const float centre = std::max(160.0f, w - 460.0f);
        const float map_h = std::floor(body * 0.42f);
        return {{0, 0, w, 76}, {0, 76, 200, body},
                {200, 76, centre, body}, {w - 260, 76, 260, body},
                {0, h - 42, w, 42},
                {208, 108, centre - 16, map_h - 40},
                {208, 76 + map_h + 28, centre - 16, body - map_h - 58}};
    }
};

inline bool map_cell(Rect surface, float pan_x, float pan_y,
                     float mouse_x, float mouse_y, int width, int height,
                     int* x, int* y) {
    if (!surface.contains(mouse_x, mouse_y)) return false;
    *x = int(std::floor((mouse_x - surface.x + pan_x) / 16.0f));
    *y = int(std::floor((mouse_y - surface.y + pan_y) / 16.0f));
    return *x >= 0 && *y >= 0 && *x < width && *y < height;
}

} // namespace studio
