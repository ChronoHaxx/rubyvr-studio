#pragma once
#include <string>
#include "overrides.h"

namespace studio::foundation {
struct Result {
    int x0=0,y0=0,x1=0,y1=0,height=0;
    size_t changed_cells=0;
    std::string message;
};
// Explicit, atomic terrain edit for one accepted rigid voxel instance. The pad
// bounds the model's lowest faces plus its placement anchor, rounded to cells.
// Preserves model/source art; refuses water, decks, neighbours and stale source.
bool level(const vr::world::Snapshot&, const vr::overrides::OverrideSet&,
           const vr::overrides::Match&, vr::overrides::OverrideSet* out, Result*);
}
