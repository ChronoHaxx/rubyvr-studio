// SPDX-License-Identifier: GPL-3.0-or-later
#include "free_walk.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <utility>
using namespace vr::free_walk;
namespace {
int failures=0,checks=0;
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL %s\n",why);}}
bool blocked(int x,int z,int,void* data){return static_cast<std::set<std::pair<int,int>>*>(data)->contains({x,z});}
bool near(double a,double b){return std::abs(a-b)<1e-8;}
}
int main(){
    constexpr double pi=3.14159265358979323846;
    auto n=direction(0x3bf,0),w=direction(0x3bf,pi/2),s=direction(0x3bf,pi),e=direction(0x3bf,-pi/2);
    check(near(n.x,0)&&near(n.z,-1)&&facing(n)==2,"north camera forward");
    check(near(w.x,-1)&&near(w.z,0)&&facing(w)==3,"east eye looks west");
    check(near(s.x,0)&&near(s.z,1)&&facing(s)==1,"south camera forward");
    check(near(e.x,1)&&near(e.z,0)&&facing(e)==4,"west eye looks east");
    check(facing(direction(0x30f,0))==0,"opposite keys cancel");
    check(facing(direction(0x3bf,std::numeric_limits<double>::quiet_NaN()))==0,"invalid yaw is idle");
    for(int contact:{1,2,3,4}) {
        const Point into=contact==1?Point{4,1}:contact==2?Point{4,-1}:contact==3?Point{-1,4}:Point{1,4};
        check(contact_facing(into,contact)==contact,"shallow push can aim a native door event along its contact");
        check(contact_facing({-into.x,-into.z},contact)==facing({-into.x,-into.z}),"reversal cancels pending contact");
        const Point parallel=contact<3?Point{1,0}:Point{0,1};
        check(contact_facing(parallel,contact)==facing(parallel),"parallel input cancels pending contact");
        check(contact_facing({},contact)==0,"key release cannot trigger pending contact");
    }
    check(contact_facing({std::numeric_limits<double>::quiet_NaN(),-1},2)==0,"invalid input cannot trigger a door");
    std::set<std::pair<int,int>> walls;
    Point p{10.5,10.5};int entries=0;
    for(int i=0;i<16;++i){auto r=advance(p,n,1.0/16,blocked,&walls);p=r.position;entries+=r.crossed;}
    check(near(p.x,10.5)&&near(p.z,9.5)&&entries==1,"one tile in sixteen ticks, one event");
    p={10.5,10.5};auto diagonal=direction(0x3af,0);
    for(int i=0;i<4;++i)p=advance(p,diagonal,1.0/16,blocked,&walls).position;
    check(near(std::hypot(p.x-10.5,p.z-10.5),.25),"diagonal has same speed, both coordinates change");
    walls.insert({11,10});p={10.5,10.5};
    for(int i=0;i<64;++i)p=advance(p,{1,0},1.0/16,blocked,&walls).position;
    check(p.x<=10.78&&p.x>10.7&&near(p.z,10.5),"body stops before blocked tile without recentring");
    p={10.75,10.5};auto slide=advance(p,{1,1},1.0/16,blocked,&walls);
    check(slide.blocked&&near(slide.position.x,p.x)&&slide.position.z>p.z,"diagonal slides along wall");
    check(slide.blocked_x&&!slide.blocked_z,"sliding retains the wall contact for native special actions");
    // Contact direction must be independent of the camera and of the stronger
    // input axis. A shallow approach still pushes into a ledge while sliding.
    for(int axis:{0,1})for(int sign:{-1,1})for(double slope:{.25,1.0,4.0}) {
        walls.clear();
        for(int along=8;along<=12;++along)
            walls.insert(axis==0?std::pair{10+sign,along}:std::pair{along,10+sign});
        const Point start=axis==0?Point{10.5+sign*.275,10.5}:Point{10.5,10.5+sign*.275};
        const Point input=axis==0?Point{double(sign),slope}:Point{slope,double(sign)};
        const auto hit=advance(start,input,1.0/16,blocked,&walls);
        check(hit.blocked&&hit.blocked_x==(axis==0)&&hit.blocked_z==(axis==1)&&
              (axis==0?near(hit.position.x,start.x)&&hit.position.z>start.z:
                       near(hit.position.z,start.z)&&hit.position.x>start.x),
              "all four contact normals survive diagonal and shallow sliding");
        const auto parallel=advance(start,axis==0?Point{0,1}:Point{1,0},1.0/16,blocked,&walls);
        const auto away=advance(start,axis==0?Point{-double(sign),0}:Point{0,-double(sign)},1.0/16,blocked,&walls);
        check(!parallel.blocked_x&&!parallel.blocked_z&&!away.blocked_x&&!away.blocked_z,
              "parallel and departing motion cannot request a contact handoff");
    }
    walls={{11,9}};p={10.75,10.08};auto corner=advance(p,{1,0},1.0/16,blocked,&walls);
    check(corner.blocked&&near(corner.position.x,p.x),"body corner cannot cut across wall");
    walls.clear();p={10.99,10.99};auto crossing=advance(p,{1,1},1.0/16,blocked,&walls);
    check(crossing.crossed&&int(crossing.position.x)==11&&int(crossing.position.z)==11&&
          near(crossing.position.x-crossing.position.z,0),"corner crossing completes both axes and reports resulting cell once");
    // A sustained diagonal must stay on its analytical line at constant speed,
    // including tile/ring boundaries. Short within-cell tests missed axis loss.
    for(int sx:{-1,1})for(int sz:{-1,1})for(double start:{10.5,10.99}) {
        const Point v{sx/std::sqrt(2.0),sz/std::sqrt(2.0)},begin{start,14.23};p=begin;
        bool straight=true,steady=true;
        for(int i=0;i<512;++i) {
            const auto before=p;p=advance(p,v,1.0/16,blocked,&walls).position;
            straight&=near(p.x,begin.x+v.x*(i+1)/16)&&near(p.z,begin.z+v.z*(i+1)/16);
            steady&=near(std::hypot(p.x-before.x,p.z-before.z),1.0/16);
        }
        check(straight&&steady,"all diagonal signs stay straight and constant across many cell boundaries");
    }
    walls={{11,10},{10,11}};p={10.5,10.5};
    for(int i=0;i<32;++i)p=advance(p,{1,1},1.0/16,blocked,&walls).position;
    check(p.x<10.78&&p.z<10.78,"complete diagonal cannot squeeze through blocked side cells");
    auto two_contacts=advance(p,{1,1},1.0/16,blocked,&walls);
    check(two_contacts.blocked_x&&two_contacts.blocked_z,"blocked corner preserves both candidate directions");
    walls.clear();
    auto capped=advance({10.5,10.5},{1,0},10000,blocked,&walls);
    check(near(capped.position.x,10.5625),"large delta cannot tunnel");
    auto invalid=advance({10.5,10.5},{1,0},1.0/16,nullptr,nullptr);
    check(near(invalid.position.x,10.5),"missing collision query fails closed");
    std::printf("free-walk: %d contract checks, %d failures\n",checks,failures);return failures?1:0;
}
