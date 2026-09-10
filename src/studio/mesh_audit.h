#pragma once
#include "diorama.h"

namespace studio {
struct MeshAudit {
    size_t triangles=0, degenerate=0, duplicate=0, boundary=0, nonmanifold=0, winding=0, components=0;
    double volume=0;
    bool closed_connected() const { return triangles && !degenerate && !duplicate && !boundary && !nonmanifold && !winding && components==1; }
};
// Quantizes positions to 1e-6 cell. Uses triangle topology, not a rendered image.
MeshAudit audit_mesh(const std::vector<vr::diorama::AuthoredVertex>&);
}
