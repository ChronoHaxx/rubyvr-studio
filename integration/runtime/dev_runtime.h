// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "dev/session.h"
#include <cstdint>
namespace gbarecomp { struct RunOptions; }
namespace vr::dev {
void configure(gbarecomp::RunOptions& options);
bool enabled();
bool pending();
std::optional<rubyvr::dev::Request> take_request();
void complete(bool success, const std::string& error);
void pause(bool value);
bool paused();
void next_frame(uint64_t frame);
bool present_due(bool menu_open);
void pace(int held_fast_forward = 1);
void refresh_scene();
void reset_after_load();
bool accelerated();
}
