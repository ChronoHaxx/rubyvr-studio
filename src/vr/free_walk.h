// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace vr::free_walk {
struct Point { double x=0, z=0; };
// Screen-right / screen-down input, rotated into map coordinates. Diagonals
// have the same speed as straight movement; conflicting keys cancel.
Point direction(uint16_t active_low_keys, double yaw);
int facing(Point vector); // Ruby: south=1, north=2, west=3, east=4; zero=idle
using Blocked = bool (*)(int x,int z,int direction,void* context);
struct Step {
    Point position;
    bool crossed=false, blocked=false;
};
// One fixed guest tick, at most one cell-entry event. Axis sliding checks the
// full body, including both corners. The caller owns game events and cadence.
Step advance(Point position,Point direction,double distance,Blocked,void* context);
}
