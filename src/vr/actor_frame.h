// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace vr::actor {
struct DirectionImage {
    std::array<uint8_t,512> tiles{}; // bounded original 4bpp image, up to 32x32
    uint16_t byte_count=256;
    uint16_t image=0;
    bool hflip=false,vflip=false;
};
// Transient, host-owned copy of one pinned Ruby Sprite; never a guest pointer.
struct Source {
    std::array<uint8_t,68> sprite{};
    bool present=false;
    // Capture verified the owning live event is off-screen, not script-hidden.
    // This only bypasses Ruby's 2D draw cull; the original Sprite stays intact.
    bool viewport_culled=false;
    bool fixed_pose=false; // event/script explicitly disabled directional animation
    // Raw six-byte Subsprite records from the selected, validated ROM table.
    std::vector<uint8_t> subsprites;
    // Optional same-phase art for a verified directional field profile.
    // Indexed S/N/W/E, not a second animation clock. Zero facing means absent.
    uint8_t world_facing=0;
    uint8_t displayed_anim=0,displayed_phase=0;
    bool pending_flip_transition=false;
    std::array<DirectionImage,4> directions{};
};
// Capture-side only, after the caller's Ruby rev1 ROM-identity gate. Refuses
// unknown profiles and a phase whose source bytes/flips do not match live OBJ.
bool capture_player_directions(Source&,std::span<const uint8_t> rom,
                               std::span<const uint8_t> obj_vram,bool mapping_1d);
// pending_copies: at most 64 pinned 12-byte SpriteCopyRequest records, captured
// only while Ruby's queue is pending. Consumed here; no pointers reach rendering.
bool capture_object_directions(Source&,std::span<const uint8_t> rom,
                               std::span<const uint8_t> obj_vram,bool mapping_1d,
                               uint8_t graphics_id,
                               std::span<const uint8_t> pending_copies={});
// Capture-side owner/visibility gate, using the pinned 0x24-byte ObjectEvent.
// An inactive, hidden or recycled slot cannot revive a copied Sprite.
bool bind_event(Source&,std::span<const uint8_t> object_event,unsigned slot);
enum class Status { Missing, Hidden, Visible, Unsupported, Truncated };
struct Frame {
    Status status=Status::Missing;
    int width=0,height=0;
    int x=0,y=0,x2=0,y2=0,corner_x=0,corner_y=0;
    std::vector<uint8_t> rgba;
};
Frame decode(const Source&, std::span<const uint8_t> obj_vram,
             std::span<const uint16_t> obj_palette, bool mapping_1d);
Frame decode_direction(const Source&,std::span<const uint8_t> obj_vram,
                       std::span<const uint16_t> obj_palette,bool mapping_1d,
                       uint8_t apparent_facing);
// Screen-right in map coordinates, from the actual view/model transform.
uint8_t apparent_facing(uint8_t world_facing,float right_x,float right_z);
// Invert the field tilemap's screen-space ring using the sprite's global camera
// offsets, then select the incarnation nearest its destination event tile.
// Sprite y2 is a visual jump/bob, not map motion or terrain height.
struct Position { float x=0,z=0,lift=0; };
Position position(const Frame&, int view_x, int view_y,int base_x,int base_y,
                  int offset_x,int offset_y,int cell_x,int cell_y);
}
