// pattern_io.h — deriving an override pattern from an object, and writing one out.
//
// ONE WRITER FOR ONE FORMAT, and that is the whole reason this file exists
// rather than the two callers each rolling their own.
//
//   The harness writes patterns through --emit-pattern; the GUI writes them
//   from a click. If those were two pieces of code they would drift, and the
//   drift would show up as a file that loads but means something slightly
//   different from the fixture the tests trust. overrides.h already makes this
//   argument about geometry ("a second geometry path would drift and start
//   answering about itself"); a second SERIALISER of the same format is the
//   same mistake one layer down.
//
// PURE, AND NO GL. from_object() reads a Snapshot and an ObjectModel, both of
// which the studio can produce without a window — so an author can generate an
// override file with nothing rendering.
//
// THE IN-MEMORY TYPE IS overrides::Pattern, deliberately. It already carries
// name, w, extent, ids, mask, tiles and apply, and it is what overrides::find()
// matches with — so a pattern this file builds can be matched immediately,
// without a save-and-reload round trip to give it meaning.

#pragma once

#include "diorama.h"
#include "overrides.h"
#include "ruby_world.h"

namespace studio {
namespace pattern_io {

struct Cell {
    int x = 0, y = 0;
    bool operator==(const Cell&) const = default;
};

// Canonical membership over arbitrary cells, independent of segmentation.
// Duplicates are harmless; undefined/out-of-map cells and empty sets fail.
// Nonmember ids are zero. source.x/y reports the bounding-box origin.
bool from_cells(const vr::world::Snapshot& s, const std::vector<Cell>& cells,
                const char* name, vr::overrides::Pattern* out);

bool same_key(const vr::overrides::Pattern& a, const vr::overrides::Pattern& b);

// Derive a pattern from the object covering (cell_x, cell_y).
//
// The signature and bounding box come straight from the Object; the MASK comes
// from cell_object membership, and the `tiles` block from the definition of
// every metatile the mask actually references. See overrides.h for why each of
// those three is load-bearing.
//
// `apply` is filled with the object's INFERRED class and nothing else — every
// other field is left at "leave this to inference" for the caller to set. A
// writer that guessed which properties the author meant to pin would be
// authoring on their behalf.
//
// `anchor` is set here rather than at load time, so the result is immediately
// usable with overrides::find(). A Pattern with anchor < 0 is not matchable.
//
// Returns false, having said why, if no object covers that cell.
bool from_object(const vr::world::Snapshot& s,
                 const vr::diorama::ObjectModel& model,
                 int cell_x, int cell_y,
                 const char* name,
                 vr::overrides::Pattern* out);

// Write a complete override file. Always stamps this build's kVersion — the
// file means what this build means, and load() refuses anything else.
//
// Optional apply fields are emitted only when they were actually set, so a
// pattern that authors one property does not silently pin the rest.
bool write(const char* path, const vr::overrides::OverrideSet& set);

}  // namespace pattern_io
}  // namespace studio
