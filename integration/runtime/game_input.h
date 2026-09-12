// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace vr::game_input {
// Runtime-thread boundary: after host key bindings/UI capture, before KEYINPUT
// and recording. Replays already contain guest directions and bypass this.
uint16_t filter(uint16_t active_low_keys, bool host_menu_open);
// The same input boundary with an explicit source, usable by deterministic
// native input drivers. Physical input always uses filter() above.
enum class Source { Inactive, Original, Viewer };
uint16_t filter_from_source(uint16_t active_low_keys, Source source);
void reset();
}
