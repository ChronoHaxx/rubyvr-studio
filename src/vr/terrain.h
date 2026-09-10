#pragma once
#include <cstdio>
#include "overrides.h"
#include "json_scan.h"

namespace vr::terrain {
using overrides::TerrainMap;
using overrides::TerrainCell;
using overrides::TerrainSurface;
using overrides::TerrainKind;

bool valid(const std::vector<TerrainMap>& maps);
bool read(const json::Value&, std::vector<TerrainMap>*);
void write(std::FILE*, const std::vector<TerrainMap>&);
bool primary_cell(const world::Snapshot&, int x, int y);
bool guard_tile(const world::Snapshot&, uint16_t id, TerrainMap*);

enum class Status { LegacyFlat, Authored, Unresolved, SourceMismatch };
struct Height {
    Status status = Status::LegacyFlat;
    float pixels = 0;
    const TerrainSurface* surface = nullptr;
    bool resolved() const { return status == Status::LegacyFlat || status == Status::Authored; }
};

// Construct once per scene rebuild. All returned pointers borrow the document.
// Rejected source cells stay visible as mismatches, never silently re-author.
struct Resolved {
    int width = 0, height = 0;
    size_t matched = 0, rejected = 0;
    std::vector<const TerrainCell*> cells;
    std::vector<bool> mismatched;
    const TerrainCell* cell(int x, int y) const;
    Height query(int x, int y, int gameplay_layer, float u=.5f, float v=.5f) const;
};
float surface_height(const TerrainSurface&, float u, float v);
int maximum_height(const TerrainSurface&);
Resolved resolve(const world::Snapshot&, const std::vector<TerrainMap>&);
}
