#pragma once
#include "snapshot_build.h"
#include "diorama.h"
#include <atomic>

namespace studio::connected {
struct Map { std::string id; int x=0,z=0,depth=0; vr::world::Snapshot source; BuildInfo info; };
struct Scene {
    struct Origin {std::string id;int x=0,z=0;};
    std::vector<Map> maps;
    std::vector<std::string> frontiers;
    // Small placement records survive unloading; source/mesh data do not.
    std::vector<Origin> known;
    std::vector<vr::diorama::RegionMap> inputs() const;
};
// At most nine maps: two cardinal hops with authored terrain, one without.
// Stop at the authored region's frontier rather than guessing continuation heights.
bool build(Decomp&,const std::string& root,const vr::overrides::OverrideSet&,Scene*,std::string* error,
           const std::atomic<bool>* cancel=nullptr);
// Use the initial view's fixed world origin. Inset body bounds prevent seam
// chatter; a camera outside every body (including an overview) keeps its anchor.
std::string camera_map(const Scene&,const std::string& anchor,float x,float z);
bool rebase(Scene*,const Scene& previous,const std::string& anchor,std::string* error);
bool recenter(Decomp&,const Scene&,const std::string& anchor,const vr::overrides::OverrideSet&,
              Scene*,std::string* error,const std::atomic<bool>* cancel=nullptr);
}
