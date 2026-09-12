// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace vr::camera_input {
enum class Context { Inactive, Original, Menu, Camera };
inline constexpr uint16_t directions = 0x00f0;
// Yaw zero looks north; positive yaw moves the eye toward the east.
int quadrant(float yaw);
uint16_t rotate(uint16_t active_low_keys, int quadrant);

// One quarter-turn per press. Finish held arrow input before applying a turn.
class TurnLatch {
public:
    int update(bool left, bool right, bool walking);
    void reset();
private:
    bool left_ = false, right_ = false, armed_ = true;
    int pending_ = 0;
};

class Mapper {
public:
    uint16_t update(uint16_t active_low_keys, float yaw, Context context);
    void reset();
private:
    Context context_ = Context::Inactive;
    bool initialized_ = false, neutral_required_ = false, latched_ = false;
    int quadrant_ = 0;
};
}
