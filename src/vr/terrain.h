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
// Borrow coherent snapshots/resolutions constructed once from validated terrain.
// Keep both objects and the original terrain document alive and unchanged,
// including while using Height::surface. Offsets translate backup cells to world.
struct RegionMapView {
    const world::Snapshot* source = nullptr;
    const Resolved* resolved = nullptr;
    int x = 0, z = 0;
};
struct RegionHeight {
    Height height{Status::Unresolved};
    int map_group = -1, map_number = -1;
    int cell_x = -1, cell_y = -1;
    float u = 0, v = 0;
};
// Query primary bodies only: backup [7,width-8) x [7,height-7).
// Invalid regions/outside points have no owner; owned unresolved cells retain it.
// The caller supplies the gameplay layer, never an inferred physical height.
RegionHeight query_region(const std::vector<RegionMapView>& maps,
                          double world_x, double world_z,
                          int gameplay_layer);

float surface_height(const TerrainSurface&, float u, float v);
int maximum_height(const TerrainSurface&);
Resolved resolve(const world::Snapshot&, const std::vector<TerrainMap>&);
}
