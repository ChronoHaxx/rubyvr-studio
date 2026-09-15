// SPDX-License-Identifier: GPL-3.0-or-later
// Independent geometry checks for vr::billboard. Expected values come from the
// stated world conventions and from explicit world-space rotations, not from
// the component's own output.
#include "billboard.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

using vr::billboard::Basis;
using vr::billboard::Vec3;
using vr::billboard::basis;
using vr::billboard::vertices;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const char* what, int line) {
    ++checks;
    if(!ok) {
        ++failures;
        std::printf("FAIL line %d: %s\n", line, what);
    }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

bool near(float a, float b, float eps=1e-5f) { return std::fabs(a-b) <= eps; }
bool near3(Vec3 a, Vec3 b, float eps=1e-5f) {
    return near(a.x,b.x,eps) && near(a.y,b.y,eps) && near(a.z,b.z,eps);
}
float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
Vec3 at(const std::array<float,30>& v, int k) {
    return {v[k*5+0], v[k*5+1], v[k*5+2]};
}
float uu(const std::array<float,30>& v, int k) { return v[k*5+3]; }
float vv(const std::array<float,30>& v, int k) { return v[k*5+4]; }

// Spec-derived independent axes: eye offset direction and horizontal right.
Vec3 eye_dir(float yaw, float pitch) {
    return {std::sin(yaw)*std::cos(pitch), std::sin(pitch), std::cos(yaw)*std::cos(pitch)};
}
Vec3 right_spec(float yaw) { return {std::cos(yaw), 0.f, -std::sin(yaw)}; }
Vec3 up_spec(float yaw, float pitch) { return cross(eye_dir(yaw,pitch), right_spec(yaw)); }

void testCardinal() {
    // Hardcoded convention anchors: yaw 0 looks north with east screen-right.
    CHECK(near3(basis(0,0).right, {1,0,0}));
    CHECK(near3(basis(0,0).up, {0,1,0}));
    // Positive yaw moves the eye east; at yaw pi/2 screen-right is north.
    CHECK(near3(basis(1.5707963f,0).right, {0,0,-1}, 1e-5f));
    CHECK(near3(basis(1.5707963f,0).up, {0,1,0}));
    CHECK(near3(basis(3.1415927f,0).right, {-1,0,0}, 1e-5f));
    CHECK(near3(basis(4.7123890f,0).right, {0,0,1}, 1e-5f));
}

void testPitch() {
    // Positive pitch (eye above, looking down): top leans north (-Z), bottom
    // anchor untouched. Negative pitch leans it south.
    const Basis up_p = basis(0,0.5f);
    CHECK(up_p.up.z < 0.f);
    CHECK(up_p.up.y > 0.f);
    const Basis down_p = basis(0,-0.5f);
    CHECK(down_p.up.z > 0.f);
    // Steep finite pitch clamps to 1.50 and matches an independent angle.
    CHECK(near3(basis(0,9).up, up_spec(0,1.50f), 1e-5f));
    CHECK(near3(basis(0,-9).up, up_spec(0,-1.50f), 1e-5f));
}

void testNonfiniteAngles() {
    const float nan = std::nanf("");
    const float inf = std::numeric_limits<float>::infinity();
    const float yaws[3] = {nan, inf, -inf};
    const float pitches[3] = {0.f, nan, inf};
    for(int i=0;i<3;++i) {
        CHECK(near3(basis(yaws[i],0.f).right, {1,0,0}));
        CHECK(near3(basis(0.f,pitches[i]).up, {0,1,0}));
    }
}

void testWrapAndLoop() {
    for(int i=0;i<32;++i) {
        const float yaw = i*2.f*3.14159265358979323846f/32.f;
        for(int j=0;j<13;++j) {
            const float pitch = -1.5f + j*0.25f;
            const Basis b = basis(yaw,pitch);
            const Vec3 r = right_spec(yaw), u = up_spec(yaw,pitch), n = eye_dir(yaw,pitch);
            CHECK(near3(b.right, r, 1e-5f));
            CHECK(near3(b.up, u, 1e-5f));
            // Unit, orthogonal, no roll, normal faces the eye.
            CHECK(near(dot(b.right,b.right),1.f,1e-5f));
            CHECK(near(dot(b.up,b.up),1.f,1e-5f));
            CHECK(near(dot(b.right,b.up),0.f,1e-5f));
            CHECK(near(b.right.y,0.f));
            CHECK(near3(cross(b.right,b.up), n, 1e-5f));

            // A noncentral source anchor. The card must remain an exact
            // width x height rectangle in the camera plane, anchored at feet.
            const Vec3 foot = {1.5f,2.25f,3.75f};
            const float left = -2.5f, width = 4.f, height = 5.f;
            std::array<float,30> out{};
            CHECK(vertices(b,foot,left,width,height,out));
            float amin=1e30f,amax=-1e30f,bmin=1e30f,bmax=-1e30f;
            for(int k=0;k<6;++k) {
                const Vec3 rel = sub(at(out,k),foot);
                CHECK(near(dot(rel,n),0.f,1e-4f)); // in the camera plane
                const float a = dot(rel,r), bb = dot(rel,u);
                amin=std::fmin(amin,a); amax=std::fmax(amax,a);
                bmin=std::fmin(bmin,bb); bmax=std::fmax(bmax,bb);
            }
            CHECK(near(amin,left,1e-4f));
            CHECK(near(amax,left+width,1e-4f));
            CHECK(near(bmin,0.f,1e-4f));
            CHECK(near(bmax,height,1e-4f)); // aspect: width/height preserved

            // Bottom anchor, including the noncentral left offset.
            const Vec3 bl = {foot.x+left*r.x,foot.y+left*r.y,foot.z+left*r.z};
            CHECK(near3(at(out,5), bl, 1e-4f));
            CHECK(near(dot(sub(at(out,5),foot),u),0.f,1e-4f));
            const Vec3 bex = {foot.x+(left+width)*r.x,foot.y+(left+width)*r.y,foot.z+(left+width)*r.z};
            CHECK(near3(at(out,2), bex, 1e-4f));
            CHECK(near3(at(out,4), bex, 1e-4f));
            // Heights on both columns.
            CHECK(near3(at(out,0), {bl.x+height*u.x,bl.y+height*u.y,bl.z+height*u.z}, 1e-4f));
            CHECK(near3(at(out,1), {bex.x+height*u.x,bex.y+height*u.y,bex.z+height*u.z}, 1e-4f));
        }
    }
    // Wraparound by exact periods.
    const float two_pi = 2.f*3.14159265358979323846f;
    CHECK(near3(basis(0,0.3f).right, basis(two_pi,0.3f).right));
    CHECK(near3(basis(0,0.3f).up, basis(-two_pi,0.3f).up));
    const Basis b0 = basis(0.7f,0.4f);
    std::array<float,30> a{}, b{};
    CHECK(vertices(b0,{0,0,0},-1,2,3,a));
    CHECK(vertices(basis(0.7f+two_pi,0.4f),{0,0,0},-1,2,3,b));
    for(size_t i=0;i<a.size();++i) CHECK(near(a[i],b[i],1e-4f));
}

void testUvAndWinding() {
    const Basis b = basis(0.9f,0.4f);
    std::array<float,30> out{};
    CHECK(vertices(b,{0,0,0},-1.5f,3.f,4.f,out));
    // UV top-left=(0,0), bottom-right=(1,1), existing triangle order.
    const float eu[12] = {0,0, 1,0, 1,1, 0,0, 1,1, 0,1};
    for(int k=0;k<6;++k) {
        CHECK(uu(out,k)==eu[k*2+0]);
        CHECK(vv(out,k)==eu[k*2+1]);
    }
    const Vec3 n = eye_dir(0.9f,0.4f);
    const Vec3 n0 = cross(sub(at(out,1),at(out,0)), sub(at(out,2),at(out,0)));
    const Vec3 n1 = cross(sub(at(out,4),at(out,3)), sub(at(out,5),at(out,3)));
    CHECK(n0.x*n1.x+n0.y*n1.y+n0.z*n1.z > 0.f); // same facing
    const float area = std::sqrt(dot(n0,n0));
    CHECK(area > 1e-6f);
    CHECK(near3({n0.x/area,n0.y/area,n0.z/area}, {-n.x,-n.y,-n.z}, 1e-4f));
}

void testInvalidLeavesOutput() {
    const Basis good = basis(0.3f,0.2f);
    const Basis nan_basis{{std::nanf(""),0,0},{0,1,0}};
    const Basis zero_basis{{0,0,0},{0,0,0}};
    const Basis skewed_basis{{1,0,0},{0,1,0.5f}};
    const Basis scaled_basis{{2,0,0},{0,1,0}};
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::nanf("");
    const float big = 3.0e38f;

    struct Case { const char* name; Basis b; Vec3 foot; float left,width,height; };
    const Case cases[] = {
        {"zero width", good, {0,0,0}, 0,0,1},
        {"negative width", good, {0,0,0}, 0,-1,1},
        {"zero height", good, {0,0,0}, 0,1,0},
        {"negative height", good, {0,0,0}, 0,1,-2},
        {"nan foot", good, {nan,0,0}, 0,1,1},
        {"inf foot", good, {0,inf,0}, 0,1,1},
        {"nonfinite basis", nan_basis, {0,0,0}, 0,1,1},
        {"zero basis", zero_basis, {0,0,0}, 0,1,1},
        {"skewed basis", skewed_basis, {0,0,0}, 0,1,1},
        {"scaled basis", scaled_basis, {0,0,0}, 0,1,1},
        {"nan left", good, {0,0,0}, nan,1,1},
        {"inf height", good, {0,0,0}, 0,1,inf},
        {"left+width overflow", good, {0,0,0}, big,big,1},
        {"coord overflow", good, {big,0,0}, big,1,1},
    };
    for(const auto& c : cases) {
        std::array<float,30> out{};
        for(size_t i=0;i<out.size();++i) out[i] = 9000.f + float(i);
        std::array<float,30> before = out;
        const bool ok = vertices(c.b,c.foot,c.left,c.width,c.height,out);
        check(!ok, c.name, __LINE__);
        check(out==before, c.name, __LINE__);
    }
}

}

int main() {
    testCardinal();
    testPitch();
    testNonfiniteAngles();
    testWrapAndLoop();
    testUvAndWinding();
    testInvalidLeavesOutput();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
