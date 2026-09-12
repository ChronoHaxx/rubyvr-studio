// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <span>
#include <cstdint>
namespace vr::screen_overlay {
// Top-left RGBA source, nearest sampling, centered aspect-preserving fit.
bool draw(std::span<const uint8_t> rgba,int source_w,int source_h,
          int window_w,int window_h,bool inset);
void shutdown();
}
