// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "free_walk.h"
namespace vr::free_walk::runtime {
void input(Point direction,bool viewer_controls=true);
void reset();
bool available();
struct Stats { bool owned=false; double x=0,z=0; unsigned long long ticks=0,entries=0,handoffs=0; };
Stats stats();
}
