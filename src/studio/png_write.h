// png_write.h — an 8-bit RGB PNG, so a rendered frame is something an agent can
// actually open.
//
// WHY THIS IS NOT png4bpp, AND NOT A COPY OF IT. That header spends 38 lines on
// why *decoding* pokeruby's tiles.png is special: the file is 4bpp indexed, the
// palette indices ARE the pixel data, and resolving them to colour is a lossy
// step that punches holes in geometry. None of that applies in this direction.
// Writing is the easy half of PNG — no filtering heuristics, no interlace, no
// colour-type negotiation — so it gets its own small file rather than growing a
// header whose entire argument is about the other direction.
//
// WHY NOT PPM, WHICH THE REPOSITORY ALREADY WRITES. renderer.cpp's
// read_and_write_ppm() has produced every contact sheet and turntable in this
// project, and it would have been zero new code here. But the whole point of the
// visual probe is that *an agent looks at the picture*, and neither Claude's nor
// Codex's image reader opens a PPM. A format nobody can open is not evidence.
//
// WHY NOT A DEPENDENCY. zlib is already linked into these targets — png4bpp.cpp
// inflates with it — so compress2() and crc32() are both to hand. That leaves
// about sixty lines of chunk framing, against vendoring stb_image_write for one
// call. The project's standing posture is zero new dependencies.

#pragma once

#include <cstdint>

namespace studio {
namespace png {

// Write `width` x `height` pixels as a non-interlaced 8-bit RGB PNG.
//
// `rgb_top_down` is width*height*3 bytes, THE FIRST ROW BEING THE TOP OF THE
// IMAGE. That is the opposite of what glReadPixels hands back, and the caller
// does the flip — because the caller is the one that knows it read from GL.
//
// Every row is written with filter type 0 (None). Real encoders pick a filter
// per row to help the compressor; on flat-shaded output the win is small and the
// heuristic is the only genuinely fiddly part of an encoder, so it is left out
// on purpose rather than half-done.
//
// Returns false, having explained itself on stderr, on any file or zlib error.
bool write_rgb(const char* path, int width, int height,
               const uint8_t* rgb_top_down);

}  // namespace png
}  // namespace studio
