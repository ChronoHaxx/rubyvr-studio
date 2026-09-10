// world_io.h — a world::Snapshot on disk, and why that is worth a file format.
//
// WHY THIS EXISTS:
//
//   A Snapshot is the only input the mesher has. It normally comes from
//   capture(), which reads guest memory and therefore needs the ROM, the BIOS,
//   the recompiled game and a running emulator. That is a heavy dependency for
//   something that is, in the end, about 100 KB of plain arrays.
//
//   Writing it to a file breaks that dependency three ways:
//
//     1. THE LOADER BECOMES TESTABLE OFFLINE. rubyvr_studio builds a Snapshot
//        from pokeruby's own data files; proving it equals the live one would
//        otherwise mean running the emulator and the loader in lockstep. With
//        a .snap it is a diff of two files, and it can be re-run in a second
//        without the game.
//
//     2. The studio can open a scene captured from a real session, including
//        states no decomp file describes — a door the player opened, a cut
//        tree, a secret base.
//
//     3. A regression can be pinned to a byte. "The mesh changed" and "the
//        input changed" stop being the same observation.
//
// DELIBERATELY THE WHOLE SNAPSHOT, not the seven fields the mesher reads.
//
//   The mesher touches width/height/grid/metatiles/attributes/vram_tiles/
//   bg_palette and nothing else, so a file carrying only those would satisfy
//   every consumer that exists today. It would also quietly redefine what a
//   Snapshot IS — and the objects[], player_index and view_* fields are exactly
//   what first-person and NPC rendering will need next. A format that drops
//   them makes every capture taken before that day useless.
//
// EXPLICIT LITTLE-ENDIAN, NOT memcpy OF THE STRUCT.
//
//   Snapshot has a fixed-size array member, three vectors and mixed integer
//   widths; its in-memory layout depends on the compiler's padding choices.
//   Dumping the bytes would produce a file that a differently-built binary
//   reads as garbage, and the failure would look like a data bug rather than a
//   format bug. Every field below is read and written a byte at a time, so the
//   file means the same thing to anything that reads it.
//
// VERSIONED FROM DAY ONE, and mismatches are REFUSED rather than guessed at.
//   The same discipline the override format will need. A file from a future
//   version is not "probably compatible"; it is a file we cannot read.

#pragma once

#include <cstdint>

#include "ruby_world.h"

namespace vr {
namespace world_io {

// Bumped whenever the field list or its order changes. read() refuses anything
// it does not recognise instead of interpreting old bytes under new rules.
inline constexpr uint32_t kVersion = 2;

// Write `s` to `path`. Returns false and explains itself on stderr if the file
// cannot be opened or the write is short.
//
// Callable from the emulation thread: it does no allocation beyond a stream
// buffer and never touches guest memory. It is still I/O on the game's critical
// path, so it is driven by RUBYVR_SNAP and fires once, not per frame.
bool write(const world::Snapshot& s, const char* path);

// Read `path` into `out`. Returns false — leaving `out` untouched — on a bad
// magic, an unknown version, a truncated file, or a length field that does not
// match what the format requires.
//
// LENGTHS ARE VALIDATED, NOT TRUSTED. A count field is the classic way a
// corrupt file turns into a gigabyte allocation, and this format's counts are
// all fixed by the game's own constants (kMetatilesTotal * kTilesPerMetatile,
// kTileSheetSize, kPaletteEntries) or bounded by the grid dimensions. Anything
// else is a broken file.
bool read(world::Snapshot& out, const char* path);

}  // namespace world_io
}  // namespace vr
