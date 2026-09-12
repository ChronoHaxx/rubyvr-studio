// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace vr::actor {
// Transient, host-owned copy of one pinned Ruby Sprite; never a guest pointer.
struct Source {
    std::array<uint8_t,68> sprite{};
    bool present=false;
    // Raw six-byte Subsprite records from the selected, validated ROM table.
    std::vector<uint8_t> subsprites;
};
enum class Status { Missing, Hidden, Visible, Unsupported, Truncated };
struct Frame {
    Status status=Status::Missing;
    int width=0,height=0;
    int x=0,y=0,x2=0,y2=0,corner_x=0,corner_y=0;
    std::vector<uint8_t> rgba;
};
Frame decode(const Source&, std::span<const uint8_t> obj_vram,
             std::span<const uint16_t> obj_palette, bool mapping_1d);
// Invert the field tilemap's screen-space ring using the sprite's global camera
// offsets, then select the incarnation nearest its destination event tile.
// Sprite y2 is a visual jump/bob, not map motion or terrain height.
struct Position { float x=0,z=0,lift=0; };
Position position(const Frame&, int view_x, int view_y,int base_x,int base_y,
                  int offset_x,int offset_y,int cell_x,int cell_y);
}
