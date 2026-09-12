// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ruby_world.h"
#include <span>

namespace vr::world::live {

// Borrowed immutable regions, sampled only while the guest is quiescent.
// The caller verifies the complete ROM SHA-1 once per immutable ROM lifetime.
struct Memory {
    std::span<const uint8_t> rom, ewram, iwram;
    bool verified_ruby_rev1 = false;
    const uint8_t* read(uint32_t address, size_t bytes) const;
    const uint8_t* read_rom(uint32_t address, size_t bytes, unsigned alignment = 4) const;
};

inline constexpr char kRubySha1[] = "610b96a9c9a7d03d2bafb655e7560ccff1a6d894";
inline constexpr uint32_t kMain = 0x03001770;
inline constexpr uint32_t kOverworldCallback = 0x080543c5; // Thumb function pointer
inline constexpr uint32_t kMapGroups = 0x083085a0;

enum class Status { UnsupportedRom, Unreadable, NonField, InvalidMap,
                    HeaderMismatch, InvalidLayout, InvalidConnections, Field };

struct Scene {
    Status status = Status::Unreadable;
    uint32_t callback1 = 0, callback2 = 0;
    int group = -1, number = -1;
    uint32_t layout = 0, grid = 0, primary_tileset = 0, secondary_tileset = 0;
    int width = 0, height = 0;
    std::vector<ConnectionSlice> connections;
};

// Conservative normal-field detector, not a complete menu/battle mode router.
// On refusal no map identity or connection provenance is returned. Callback
// values remain available for diagnostics. No game data is written or scanned.
Scene inspect(const Memory& memory);
const char* status_name(Status status);

} // namespace vr::world::live
