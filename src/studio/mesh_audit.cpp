#include "mesh_audit.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <set>

namespace studio {
MeshAudit audit_mesh(const std::vector<vr::diorama::AuthoredVertex>& mesh) {
    using namespace vr::part_geometry;
    using Point=std::array<int64_t,3>;
    using Edge=std::array<size_t,2>;
    MeshAudit result;result.triangles=mesh.size()/3;
    if(mesh.size()%3) ++result.degenerate;
    std::map<Point,size_t> vertices;
    std::vector<size_t> ids;
    for(const auto& v:mesh) {
        const auto p=v.position;
        if(!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) { ++result.degenerate;return result; }
        Point key={int64_t(std::llround(p.x*1000000.0)),int64_t(std::llround(p.y*1000000.0)),int64_t(std::llround(p.z*1000000.0))};
        auto [it,inserted]=vertices.emplace(key,vertices.size());ids.push_back(it->second);
    }
    std::vector<size_t> parents(result.triangles);std::iota(parents.begin(),parents.end(),0);
    auto root=[&](size_t i) { while(parents[i]!=i) { parents[i]=parents[parents[i]];i=parents[i]; } return i; };
    struct Use { size_t triangle; bool direction; };
    std::map<Edge,std::vector<Use>> edges;
    std::set<std::array<size_t,3>> triangles;
    for(size_t t=0;t<result.triangles;++t) {
        const size_t i=t*3;
        const Vec a=mesh[i].position,b=mesh[i+1].position,c=mesh[i+2].position;
        const auto n=cross(b-a,c-a);
        if(dot(n,n)<1e-16f) ++result.degenerate;
        result.volume-=double(dot(a,cross(b,c)))/6; // production clockwise winding
        std::array<size_t,3> tri={ids[i],ids[i+1],ids[i+2]};std::sort(tri.begin(),tri.end());
        if(!triangles.insert(tri).second) ++result.duplicate;
        for(int k=0;k<3;++k) {
            const size_t a=ids[i+k],b=ids[i+(k+1)%3];
            edges[{std::min(a,b),std::max(a,b)}].push_back({t,a<b});
        }
    }
    for(const auto& [edge,uses]:edges) {
        if(uses.size()==1) ++result.boundary;
        else if(uses.size()!=2) ++result.nonmanifold;
        if(uses.size()==2 && uses[0].direction==uses[1].direction) ++result.winding;
        for(size_t i=1;i<uses.size();++i) parents[root(uses[i].triangle)]=root(uses[0].triangle);
    }
    std::set<size_t> components;
    for(size_t t=0;t<result.triangles;++t) components.insert(root(t));
    result.components=components.size();
    return result;
}
}
