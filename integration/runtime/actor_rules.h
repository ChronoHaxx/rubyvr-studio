// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "live_scene.h"
namespace vr::world::live {
// Pinned Ruby rev1 SaveBlock1 templates + flags. Refusal clears all rules.
void capture_actor_rules(const Memory&,Snapshot&,uint64_t epoch);
}
