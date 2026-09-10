#pragma once
#include <array>
#include <vector>

namespace vr::part_geometry {
struct Vec {
    float x = 0, y = 0, z = 0;
    bool operator==(const Vec&) const = default;
};
Vec operator+(Vec, Vec);
Vec operator-(Vec, Vec);
Vec operator*(Vec, float);
float dot(Vec, Vec);
Vec cross(Vec, Vec);
struct Transform {
    Vec position{}, size{1,1,1}, angles{};
    bool operator==(const Transform&) const = default;
};
bool valid(const Transform&);
Vec rotate(Vec point, Vec degrees);
Vec inverse_rotate(Vec point, Vec degrees);
Vec to_group(Vec local, const Transform&);
Vec to_local(Vec group, const Transform&);

// A plane's interior is dot(normal, point) <= distance. Normals are unit length.
struct Plane { Vec normal; float distance; };
using Polygon = std::vector<Vec>;
using Volume = std::vector<Plane>;
Volume prism(Vec lower, Vec upper, const Transform&);
Plane wedge_roof(const Transform&, int axis, int direction);
Polygon intersect(const Polygon&, const Volume&);

// Subtract one closed convex solid from an outward-wound surface. Coplanar
// exposed faces belong to the earlier part. Opposite adjoining faces disappear.
std::vector<Polygon> subtract(const Polygon&, const Volume&, bool owns_coplanar);
} // namespace vr::part_geometry
