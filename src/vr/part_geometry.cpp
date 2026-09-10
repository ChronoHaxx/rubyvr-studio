#include "part_geometry.h"
#include <cmath>

namespace vr::part_geometry {
Vec operator+(Vec a, Vec b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vec operator-(Vec a, Vec b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec operator*(Vec a, float b) { return {a.x*b,a.y*b,a.z*b}; }
float dot(Vec a, Vec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec cross(Vec a, Vec b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
bool valid(const Transform& t) {
    auto range = [](Vec v, float lo, float hi) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
            v.x>=lo && v.x<=hi && v.y>=lo && v.y<=hi && v.z>=lo && v.z<=hi;
    };
    return range(t.position,-128,128) && range(t.size,1.f/16,64) && range(t.angles,-360,360);
}
static Vec axis(Vec p, float degrees, int a) {
    // Exact quarter turns avoid cracks from trigonometric residue at common angles.
    float c, s;
    const float q = degrees / 90.f;
    if (q == std::floor(q)) {
        const int k = (int(q)%4+4)%4;
        constexpr float cs[] = {1,0,-1,0}, sn[] = {0,1,0,-1};
        c=cs[k]; s=sn[k];
    } else { const float r=degrees*0.01745329251994329577f; c=std::cos(r); s=std::sin(r); }
    if (a==0) return {p.x,c*p.y-s*p.z,s*p.y+c*p.z};
    if (a==1) return {c*p.x+s*p.z,p.y,-s*p.x+c*p.z};
    return {c*p.x-s*p.y,s*p.x+c*p.y,p.z};
}
Vec rotate(Vec p, Vec a) { return axis(axis(axis(p,a.x,0),a.y,1),a.z,2); }
Vec inverse_rotate(Vec p, Vec a) { return axis(axis(axis(p,-a.z,2),-a.y,1),-a.x,0); }
Vec to_group(Vec p, const Transform& t) { return rotate(p,t.angles)+t.position; }
Vec to_local(Vec p, const Transform& t) { return inverse_rotate(p-t.position,t.angles); }
Volume prism(Vec lo, Vec hi, const Transform& t) {
    const Vec normals[]={{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
    const Vec points[]={lo,hi,lo,hi,lo,hi};
    Volume result(6);
    for (int i=0;i<6;++i) {
        const Vec n=rotate(normals[i],t.angles);
        result[i]={n,dot(n,to_group(points[i],t))};
    }
    return result;
}
Plane wedge_roof(const Transform& t, int axis, int direction) {
    const float run=axis==0?t.size.x:t.size.z;
    Vec n=axis==0?Vec{-direction*t.size.y/run,1,0}:Vec{0,1,-direction*t.size.y/run};
    const float length=std::sqrt(dot(n,n)); n=n*(1.f/length);
    const Vec point=axis==0?Vec{direction>0?t.size.x:0,t.size.y,0}:
        Vec{0,t.size.y,direction*t.size.z/2};
    n=rotate(n,t.angles);
    return {n,dot(n,to_group(point,t))};
}
Polygon intersect(const Polygon& source, const Volume& volume) {
    constexpr float epsilon=1e-6f;
    Polygon remaining=source;
    for(const auto& plane:volume) {
        Polygon inside;
        for(size_t i=0;i<remaining.size();++i) {
            const Vec a=remaining[i],b=remaining[(i+1)%remaining.size()];
            float da=dot(plane.normal,a)-plane.distance,db=dot(plane.normal,b)-plane.distance;
            if(std::abs(da)<=epsilon) da=0;
            if(std::abs(db)<=epsilon) db=0;
            if(da<=0) inside.push_back(a);
            if((da>0)!=(db>0)) inside.push_back(a+(b-a)*(da/(da-db)));
        }
        remaining=std::move(inside);
        if(remaining.size()<3) return {};
    }
    return remaining;
}
std::vector<Polygon> subtract(const Polygon& source, const Volume& volume, bool owns) {
    constexpr float epsilon=1e-6f;
    if (source.size()<3) return {};
    const Vec normal=cross(source[1]-source[0],source[2]-source[0]);
    // A disjoint plane is an early-out; a coplanar outer face has an explicit owner.
    for (const Plane& plane:volume) {
        bool outside=true, on=true;
        for (Vec v:source) {
            const float d=dot(plane.normal,v)-plane.distance;
            outside &= d>epsilon; on &= std::abs(d)<=epsilon;
        }
        if (outside || (on && owns && dot(normal,plane.normal)>0)) return {source};
    }
    std::vector<Polygon> result;
    Polygon remaining=source;
    for (const Plane& plane:volume) {
        if (remaining.size()<3) break;
        Polygon inside,outside;
        for (size_t i=0;i<remaining.size();++i) {
            const Vec a=remaining[i],b=remaining[(i+1)%remaining.size()];
            float da=dot(plane.normal,a)-plane.distance,db=dot(plane.normal,b)-plane.distance;
            if (std::abs(da)<=epsilon) da=0;
            if (std::abs(db)<=epsilon) db=0;
            const bool ao=da>epsilon,bo=db>epsilon;
            (ao?outside:inside).push_back(a);
            if (ao!=bo) {
                const Vec intersection=a+(b-a)*(da/(da-db));
                inside.push_back(intersection); outside.push_back(intersection);
            }
        }
        if (outside.size()>=3) result.push_back(std::move(outside));
        remaining=std::move(inside);
    }
    return result;
}
} // namespace vr::part_geometry
