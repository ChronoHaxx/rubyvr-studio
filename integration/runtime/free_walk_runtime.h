// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "free_walk.h"
namespace vr::free_walk::runtime {
// Returns the cardinal input for Ruby's original pre-step event handlers.
int input(Point direction,bool viewer_controls=true);
void reset();
bool available();
// Capture on the emulation thread only. Refuses another actor/cell or load epoch.
bool foot_position(int object_index,int cell_x,int cell_z,Point& out);
struct Stats { bool owned=false; double x=0,z=0; unsigned long long ticks=0,entries=0,handoffs=0; };
Stats stats();
}
