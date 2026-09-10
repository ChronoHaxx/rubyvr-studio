// snapshot_build.h — decomp data assembled into the map the GAME would hold.
//
// LAYER 1. Its input is layer 0's raw structures; its output is a
// world::Snapshot indistinguishable from one capture() took off a running
// game. It has no GL, no SDL and no knowledge of the mesher — it produces the
// same plain data capture() does, and everything downstream stays unable to
// tell the two apart. That is the whole architectural requirement.
//
// WHAT IT HAS TO REPRODUCE, and each of these is a place to get it wrong:
//
//   THE BACKUP MAP. The mesher sees gBackupMapLayout, not the layout: the
//   current map plus seven metatiles of every neighbour stitched around it, so
//   the game can fill the player's view at the edges. That is
//   InitMapLayoutData in src/fieldmap.c — fill with MAPGRID_UNDEFINED, copy the
//   layout in at (7, 7), then run the connections. Reproduced below rather
//   than approximated, including the decomp's own asymmetry: the EAST fill is
//   eight columns wide where the WEST fill is seven, which is exactly why
//   MAP_OFFSET_W is 2*7+1 and MAP_OFFSET_H is 2*7.
//
//   THE TILESET SLOTS. The game loads a primary tileset at metatile 0 and tile
//   0, a secondary at metatile 512 and tile 512, and assumes both are 512 long
//   whether or not they are. Petalburg has 144 metatiles and 160 tiles, so the
//   live snapshot's tables past those points are ROM overrun and stale VRAM.
//   WE ZERO-FILL. Reproducing garbage would be inventing; the comparison
//   harness names the difference instead.
//
//   THE PALETTE RULES, which are the subtle ones, because getting them wrong
//   changes GEOMETRY rather than colour. LoadTilesetPalette (src/fieldmap.c)
//   forces BG entry 0 to black for a primary tileset and loads the SECONDARY's
//   palettes starting at its own index 6 — files 06.pal through 11.pal, not
//   00.pal. diorama.cpp's ground_colours() adds every resolved colour to the
//   silhouette flood's background set INCLUDING colour index 0, so a wrong
//   palette seeds a different background, the flood reaches different pixels,
//   and the carve/box decision quietly changes.

#pragma once

#include <string>

#include "decomp_source.h"
#include "ruby_world.h"

namespace studio {

// What layer 1 actually assembled. The comparison harness uses these counts to
// classify a difference by CAUSE rather than asserting hardcoded numbers: "ids
// at or above 656 are past Petalburg's 144 metatiles" is a statement it can
// derive, and it stays true when the map changes tileset.
struct BuildInfo {
    int         backup_w = 0, backup_h = 0;
    std::string primary_name, secondary_name;
    int         primary_metatiles = 0, secondary_metatiles = 0;
    int         primary_tiles = 0, secondary_tiles = 0;

    // First metatile id and first tile index that no tileset actually defines.
    // Everything from here up is zero in our Snapshot and garbage in a live
    // one, by construction rather than by accident.
    int first_undefined_metatile = 0;
    int first_undefined_tile = 0;
};

struct BuildOptions {
    // THE BISECT. When set, vram_tiles and bg_palette are taken from this
    // Snapshot instead of from the decomp's own tileset files.
    //
    // The loader has two halves that fail for different reasons — assembling
    // the MAP (stitching, offsets, connection arithmetic) and assembling the
    // TILESET (PNG decode, nibble order, palette slots). A mesh that differs
    // tells you one of them is wrong and not which. Borrowing the tile and
    // palette half from a known-good capture turns one ambiguous failure into
    // two named ones, at the cost of a single flag.
    const vr::world::Snapshot* borrow_tiles_from = nullptr;
};

// Assemble the backup map for `map_id` ("MAP_ROUTE101"). Returns false, having
// said why, if the map, its layout or either tileset cannot be read.
bool build_snapshot(Decomp& d, const std::string& map_id,
                    vr::world::Snapshot* out, BuildInfo* info = nullptr,
                    const BuildOptions& opt = {});

}  // namespace studio
