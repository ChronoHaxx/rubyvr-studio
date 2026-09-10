#include "connected_scene.h"
#include <algorithm>

namespace studio::connected {
std::vector<vr::diorama::RegionMap> Scene::inputs() const {
    std::vector<vr::diorama::RegionMap> result;
    for(const auto& m:maps)result.push_back({&m.source,m.x,m.z});
    return result;
}
bool build(Decomp& d,const std::string& root,const vr::overrides::OverrideSet& set,Scene* out,std::string* error) {
    if(!out || !error)return false;
    auto fail=[&](const std::string& why){*error=why;return false;};
    Scene result;result.maps.reserve(9);
    Map first;first.id=root;
    if(!build_snapshot(d,root,&first.source,&first.info))return fail("Cannot load the selected map.");
    result.maps.push_back(std::move(first));
    for(size_t i=0;i<result.maps.size();++i) {
        const auto& current=result.maps[i];const auto* def=d.map(current.id);
        if(!def)return fail("Cannot read map connections.");
        for(const auto& c:def->connections) {
            if(c.direction!="up" && c.direction!="down" && c.direction!="left" && c.direction!="right")continue;
            const auto* neighbor=d.map(c.map_id);const auto* layout=neighbor?d.layout(neighbor->layout_id):nullptr;
            if(!layout)return fail("Cannot read connected map "+c.map_id);
            int x=current.x,z=current.z;
            if(c.direction=="up") {x+=c.offset;z-=layout->height;}
            if(c.direction=="down") {x+=c.offset;z+=current.source.height-14;}
            if(c.direction=="left") {x-=layout->width;z+=c.offset;}
            if(c.direction=="right") {x+=current.source.width-15;z+=c.offset;}
            auto found=std::find_if(result.maps.begin(),result.maps.end(),[&](const auto& m){return m.id==c.map_id;});
            if(found!=result.maps.end()) {
                if(found->x!=x || found->z!=z)return fail("Conflicting connection offsets at "+c.map_id);
                continue;
            }
            const bool terrain_frontier=!set.terrain.empty() && std::none_of(set.terrain.begin(),set.terrain.end(),[&](const auto& m){return m.group==neighbor->group && m.number==neighbor->number;});
            const int hops=set.terrain.empty()?1:2;
            if(current.depth>=hops || result.maps.size()>=9 || terrain_frontier) {
                result.frontiers.push_back(current.id+" -> "+c.map_id+(terrain_frontier?" (terrain not authored)":" (preview limit)"));continue;
            }
            Map next;next.id=c.map_id;next.x=x;next.z=z;next.depth=current.depth+1;
            if(!build_snapshot(d,next.id,&next.source,&next.info))return fail("Cannot load connected map "+next.id);
            result.maps.push_back(std::move(next));
        }
    }
    // Recheck every edge whose endpoints were ultimately loaded. A neighbour
    // visited later may have supplied a second path to an earlier frontier.
    for(const auto& current:result.maps)for(const auto& c:d.map(current.id)->connections) {
        const auto found=std::find_if(result.maps.begin(),result.maps.end(),[&](const auto& m){return m.id==c.map_id;});
        if(found==result.maps.end())continue;
        int x=current.x,z=current.z;
        if(c.direction=="up"){x+=c.offset;z-=found->source.height-14;}
        else if(c.direction=="down"){x+=c.offset;z+=current.source.height-14;}
        else if(c.direction=="left"){x-=found->source.width-15;z+=c.offset;}
        else if(c.direction=="right"){x+=current.source.width-15;z+=c.offset;}
        else continue;
        if(found->x!=x || found->z!=z)return fail("Conflicting connection offsets at "+c.map_id);
    }
    result.frontiers.erase(std::remove_if(result.frontiers.begin(),result.frontiers.end(),[&](const auto& f){
        return std::any_of(result.maps.begin(),result.maps.end(),[&](const auto& m){return f.find(" -> "+m.id+" (")!=std::string::npos;});
    }),result.frontiers.end());
    *out=std::move(result);error->clear();return true;
}
}
