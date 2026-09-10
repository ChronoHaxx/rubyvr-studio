// vr_math.h — 4x4 matrices, column-major, header-only.
//
// COLUMN-MAJOR, indexed m[column * 4 + row], because that is what
// glUniformMatrix4fv expects with transpose = GL_FALSE and what every GL
// reference assumes. Getting this backwards produces a transposed matrix, and a
// transposed matrix still renders — it just renders the wrong thing, usually in
// a way that looks like a tracking bug rather than a maths bug.

#pragma once

#include <cmath>

// windows.h + unknwn.h before the OpenXR platform header; see renderer.h.
#include <windows.h>
#include <unknwn.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace vr {
namespace math {

struct Mat4 { float m[16]; };

inline Mat4 identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

inline Mat4 multiply(const Mat4& a, const Mat4& b) {   // a * b
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

inline Mat4 translation(float x, float y, float z) {
    Mat4 r = identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

inline Mat4 scale(float s) {
    Mat4 r = identity();
    r.m[0] = r.m[5] = r.m[10] = s;
    return r;
}

inline Mat4 rotation_y(float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    Mat4 r = identity();
    r.m[0] = c;  r.m[8]  = s;
    r.m[2] = -s; r.m[10] = c;
    return r;
}

// Projection from OpenXR's four half-angles.
//
// tan() of each angle separately, not of one half-FOV: a headset lens is not
// centred on the eye, so the four are independent and generally asymmetric. The
// off-centre terms are m[8] and m[9], and they are exactly what a textbook
// symmetric perspective() zeroes out. Verified numerically in
// renderer::selftest_dump.
inline Mat4 projection(const XrFovf& fov, float near_z, float far_z) {
    const float tan_l = std::tan(fov.angleLeft);
    const float tan_r = std::tan(fov.angleRight);
    const float tan_d = std::tan(fov.angleDown);
    const float tan_u = std::tan(fov.angleUp);

    const float tan_w = tan_r - tan_l;
    const float tan_h = tan_u - tan_d;

    Mat4 p{};
    p.m[0]  = 2.0f / tan_w;
    p.m[5]  = 2.0f / tan_h;
    p.m[8]  = (tan_r + tan_l) / tan_w;
    p.m[9]  = (tan_u + tan_d) / tan_h;
    p.m[10] = -(far_z + near_z) / (far_z - near_z);
    p.m[11] = -1.0f;
    p.m[14] = -(2.0f * far_z * near_z) / (far_z - near_z);
    return p;
}

// A view matrix aiming from `eye` at `target`. Not needed by the headset path —
// there the runtime supplies the pose — but the offline inspector places its own
// cameras, and "look at that house from over there" is the natural way to ask.
inline Mat4 look_at(float ex, float ey, float ez,
                    float tx, float ty, float tz) {
    // Forward is the direction we look; OpenGL's camera looks down -Z, so the
    // matrix's third basis vector is the NEGATED forward.
    float fx = tx - ex, fy = ty - ey, fz = tz - ez;
    float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (fl < 1e-6f) fl = 1.0f;
    fx /= fl; fy /= fl; fz /= fl;

    // Right = forward x world-up. Degenerate when looking straight down, so
    // fall back to a world Z reference there — an exactly-overhead view is one
    // of the shots the inspector most wants.
    float ux = 0.0f, uy = 1.0f, uz = 0.0f;
    if (std::fabs(fy) > 0.999f) { ux = 0.0f; uy = 0.0f; uz = 1.0f; }

    float rx = fy * uz - fz * uy;
    float ry = fz * ux - fx * uz;
    float rz = fx * uy - fy * ux;
    float rl = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (rl < 1e-6f) rl = 1.0f;
    rx /= rl; ry /= rl; rz /= rl;

    // True up = right x forward.
    const float vx = ry * fz - rz * fy;
    const float vy = rz * fx - rx * fz;
    const float vz = rx * fy - ry * fx;

    Mat4 m = identity();
    m.m[0] = rx;  m.m[4] = ry;  m.m[8]  = rz;
    m.m[1] = vx;  m.m[5] = vy;  m.m[9]  = vz;
    m.m[2] = -fx; m.m[6] = -fy; m.m[10] = -fz;
    m.m[12] = -(rx * ex + ry * ey + rz * ez);
    m.m[13] = -(vx * ex + vy * ey + vz * ez);
    m.m[14] =  (fx * ex + fy * ey + fz * ez);
    return m;
}

// World-to-eye: the inverse of the eye-to-world pose OpenXR hands over.
//
// A rigid transform inverts as transpose-the-rotation, negate-the-rotated-
// translation. A general 4x4 inverse would be slower and less stable for no
// benefit.
inline Mat4 view_from_pose(const XrPosef& pose) {
    const float x = pose.orientation.x, y = pose.orientation.y;
    const float z = pose.orientation.z, w = pose.orientation.w;

    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;

    const float r00 = 1 - 2 * (yy + zz), r01 = 2 * (xy - wz), r02 = 2 * (xz + wy);
    const float r10 = 2 * (xy + wz), r11 = 1 - 2 * (xx + zz), r12 = 2 * (yz - wx);
    const float r20 = 2 * (xz - wy), r21 = 2 * (yz + wx), r22 = 1 - 2 * (xx + yy);

    const float px = pose.position.x, py = pose.position.y, pz = pose.position.z;

    Mat4 v = identity();
    v.m[0] = r00; v.m[4] = r10; v.m[8]  = r20;
    v.m[1] = r01; v.m[5] = r11; v.m[9]  = r21;
    v.m[2] = r02; v.m[6] = r12; v.m[10] = r22;
    v.m[12] = -(r00 * px + r10 * py + r20 * pz);
    v.m[13] = -(r01 * px + r11 * py + r21 * pz);
    v.m[14] = -(r02 * px + r12 * py + r22 * pz);
    return v;
}

}  // namespace math
}  // namespace vr
