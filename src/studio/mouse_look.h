// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstring>

namespace studio {

// Use window-coordinate drag on WSL so remote-pointer deltas and cursor
// locking cannot drive look. A regular drag requires no locking or warping.
// Keep explicit overrides for native desktops and backend troubleshooting.
inline bool prefer_relative_mouse(bool wsl, const char* preference) {
    if (preference && !std::strcmp(preference, "drag")) return false;
    if (preference && !std::strcmp(preference, "relative")) return true;
    return !wsl;
}

struct MouseLookInput {
    struct Delta { float x = 0, y = 0; };
    bool relative = true;
    bool have_position = false;
    int last_x = 0, last_y = 0;

    void reset() { have_position = false; }
    void position(int x, int y) {
        last_x = x; last_y = y; have_position = true;
    }
    Delta motion(int x, int y, int xrel, int yrel, bool looking) {
        Delta delta;
        if (looking) {
            if (relative) delta = {float(xrel), float(yrel)};
            else if (have_position) delta = {float(x) - float(last_x), float(y) - float(last_y)};
        }
        position(x, y);
        return delta;
    }
};

} // namespace studio
