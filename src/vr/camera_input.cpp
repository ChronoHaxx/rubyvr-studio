// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera_input.h"
#include <cmath>
#include <numbers>

namespace vr::camera_input {
int quadrant(float yaw) {
    if (!std::isfinite(yaw)) return 0;
    const double angle = std::remainder(double(yaw), 2 * std::numbers::pi);
    const int q = int(std::floor(angle / (std::numbers::pi / 2) + 0.5));
    return (q + 4) % 4;
}
uint16_t rotate(uint16_t keys, int q) {
    constexpr uint16_t compass[] = {0x40, 0x10, 0x80, 0x20}; // N E S W
    const uint16_t held = uint16_t(~keys) & directions;
    uint16_t result = keys | directions;
    q = (q % 4 + 4) % 4;
    for (int i = 0; i < 4; ++i)
        if (held & compass[i]) result &= uint16_t(~compass[(i - q + 4) % 4]);
    return result;
}
int TurnLatch::update(bool left, bool right, bool walking) {
    const int turn = (right && !right_ ? 1 : 0) - (left && !left_ ? 1 : 0);
    left_ = left; right_ = right;
    if (!armed_) {
        if (!left && !right) armed_ = true;
        return 0;
    }
    if (left && right) pending_ = 0;
    else pending_ = (pending_ + turn + 4) % 4;
    if (walking) return 0;
    const int ready = pending_;
    pending_ = 0;
    return ready;
}
void TurnLatch::reset() {
    left_ = right_ = false; armed_ = false; pending_ = 0;
}
void Mapper::reset() {
    initialized_ = false;
    neutral_required_ = true;
    latched_ = false;
}
uint16_t Mapper::update(uint16_t keys, float yaw, Context context) {
    const bool held = ((~keys) & directions) != 0;
    if (initialized_ && context != context_ && held) neutral_required_ = true;
    if (context != context_) latched_ = false;
    context_ = context;
    initialized_ = true;
    if (!held) { neutral_required_ = false; latched_ = false; }
    if (context == Context::Inactive) {
        if (held) neutral_required_ = true;
        return keys | 0x03ff;
    }
    // A focus/menu transition or checkpoint load must not turn a held direction
    // into a fresh gameplay/menu press. Other buttons keep their original bits.
    if (neutral_required_) return keys | directions;
    if (context != Context::Camera || !std::isfinite(yaw)) return keys;
    if (held && !latched_) { quadrant_ = quadrant(yaw); latched_ = true; }
    // Orbiting during a held walk never silently changes the walking direction.
    return rotate(keys, quadrant_);
}
}
