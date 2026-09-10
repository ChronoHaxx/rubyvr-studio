#pragma once
#include "snapshot_build.h"
#include "diorama.h"

namespace studio::connected {
struct Map { std::string id; int x=0,z=0,depth=0; vr::world::Snapshot source; BuildInfo info; };
struct Scene {
    std::vector<Map> maps;
    std::vector<std::string> frontiers;
    std::vector<vr::diorama::RegionMap> inputs() const;
};
// At most nine maps: two cardinal hops with authored terrain, one without.
// Stop at the authored region's frontier rather than guessing continuation heights.
bool build(Decomp&,const std::string& root,const vr::overrides::OverrideSet&,Scene*,std::string* error);
}
