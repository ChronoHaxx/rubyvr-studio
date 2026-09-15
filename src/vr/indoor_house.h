// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace vr::indoor_house {
// Bounded first interior: the two floors of May's Littleroot house. Coordinates
// are Ruby's backup-map cells. This is a capability declaration, not map art.
struct Room { int width, height, wall_front; };
inline constexpr Room room(int group,int number) {
    if(group!=1)return {};
    if(number==2)return {26,23,10};
    if(number==3)return {24,22,9};
    return {};
}
inline constexpr bool supports(int group,int number,int width,int height) {
    const auto r=room(group,number);
    return r.width && r.width==width && r.height==height;
}
} // namespace vr::indoor_house
