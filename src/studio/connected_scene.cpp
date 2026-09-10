#include "connected_scene.h"
#include <algorithm>
#include <cmath>

namespace studio::connected {
std::vector<vr::diorama::RegionMap> Scene::inputs() const {
    std::vector<vr::diorama::RegionMap> result;
    for(const auto& m:maps)result.push_back({&m.source,m.x,m.z});
    return result;
}
bool build(Decomp& d,const std::string& root,const vr::overrides::OverrideSet& set,Scene* out,std::string* error,const std::atomic<bool>* cancel) {
    if(!out || !error)return false;
    auto fail=[&](const std::string& why){*error=why;return false;};
    Scene result;result.maps.reserve(9);
    Map first;first.id=root;
    if(!build_snapshot(d,root,&first.source,&first.info))return fail("Cannot load the selected map.");
    result.maps.push_back(std::move(first));
    for(size_t i=0;i<result.maps.size();++i) {
        if(cancel && cancel->load())return fail("Map loading cancelled.");
        const auto& current=result.maps[i];const auto* def=d.map(current.id);
        if(!def)return fail("Cannot read map connections.");
        for(const auto& c:def->connections) {
            if(c.direction!="up" && c.direction!="down" && c.direction!="left" && c.direction!="right")continue;
            const auto* neighbor=d.map(c.map_id);const auto* layout=neighbor?d.layout(neighbor->layout_id):nullptr;
            if(!layout)return fail("Cannot read connected map "+c.map_id);
            int64_t x=current.x,z=current.z;
            if(c.direction=="up") {x+=c.offset;z-=layout->height;}
            if(c.direction=="down") {x+=c.offset;z+=current.source.height-14;}
            if(c.direction=="left") {x-=layout->width;z+=c.offset;}
            if(c.direction=="right") {x+=current.source.width-15;z+=c.offset;}
            if(x < -8192 || x > 8192 || z < -8192 || z > 8192)return fail("Connection exceeds the supported world bounds.");
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
        int64_t x=current.x,z=current.z;
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
    for(const auto& m:result.maps)result.known.push_back({m.id,m.x,m.z});
    *out=std::move(result);error->clear();return true;
}
std::string camera_map(const Scene& scene,const std::string& anchor,float x,float z) {
    if(!std::isfinite(x) || !std::isfinite(z))return anchor;
    for(const auto& m:scene.maps) {
        const int w=m.source.width-15,h=m.source.height-14;
        if(w<=0 || h<=0)continue;
        const float margin=std::min(1.f,std::min(w,h)*.25f);
        if(x>=m.x+7+margin && x<m.x+7+w-margin &&
           z>=m.z+7+margin && z<m.z+7+h-margin)return m.id;
    }
    return anchor;
}
bool rebase(Scene* candidate,const Scene& previous,const std::string& anchor,std::string* error) {
    if(!candidate || !error)return false;
    auto fail=[&](const std::string& why){*error=why;return false;};
    const auto old=std::find_if(previous.maps.begin(),previous.maps.end(),[&](const auto& m){return m.id==anchor;});
    const auto root=std::find_if(candidate->maps.begin(),candidate->maps.end(),[&](const auto& m){return m.id==anchor;});
    if(old==previous.maps.end() || root==candidate->maps.end())return fail("The travel anchor is not loaded.");
    const int64_t dx=int64_t(old->x)-root->x,dz=int64_t(old->z)-root->z;
    auto known=previous.known;
    if(known.empty())for(const auto& m:previous.maps)known.push_back({m.id,m.x,m.z});
    // Validate everything before mutating even one offset.
    for(const auto& m:candidate->maps) {
        const int64_t x=int64_t(m.x)+dx,z=int64_t(m.z)+dz;
        if(x < -8192 || x > 8192 || z < -8192 || z > 8192)return fail("Travel exceeds the supported world bounds.");
        const auto shared=std::find_if(previous.maps.begin(),previous.maps.end(),[&](const auto& p){return p.id==m.id;});
        if(shared!=previous.maps.end() && (shared->x!=x || shared->z!=z))return fail("Conflicting world placement at "+m.id);
        const auto seen=std::find_if(known.begin(),known.end(),[&](const auto& p){return p.id==m.id;});
        if(seen!=known.end() && (seen->x!=x || seen->z!=z))return fail("A previously visited map changed world placement: "+m.id);
        if(seen==known.end())known.push_back({m.id,int(x),int(z)});
        if(known.size()>1024)return fail("The visited-map placement limit was reached.");
    }
    for(auto& m:candidate->maps){m.x+=int(dx);m.z+=int(dz);}
    candidate->known=std::move(known);
    error->clear();return true;
}
bool recenter(Decomp& d,const Scene& previous,const std::string& anchor,const vr::overrides::OverrideSet& set,
              Scene* out,std::string* error,const std::atomic<bool>* cancel) {
    if(!out || !error)return false;
    Scene candidate;
    if(!build(d,anchor,set,&candidate,error,cancel) || !rebase(&candidate,previous,anchor,error))return false;
    *out=std::move(candidate);return true;
}
}
