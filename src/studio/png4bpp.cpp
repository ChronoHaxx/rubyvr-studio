// png4bpp.cpp — see png4bpp.h for why we need indices and not colours.

#include "png4bpp.h"

#include <zlib.h>

#include <cstdio>
#include <cstring>

namespace studio {
namespace png {
namespace {

const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
         | (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
}

bool slurp(const char* path, std::vector<uint8_t>* out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "[png] cannot open %s\n", path);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        std::fprintf(stderr, "[png] %s is empty\n", path);
        std::fclose(f);
        return false;
    }
    out->resize(static_cast<size_t>(n));
    const size_t got = std::fread(out->data(), 1, out->size(), f);
    std::fclose(f);
    if (got != out->size()) {
        std::fprintf(stderr, "[png] short read of %s\n", path);
        return false;
    }
    return true;
}

// The Paeth predictor, verbatim from the PNG spec. Its whole job is to pick
// whichever of the three neighbours is closest to their linear estimate, so
// spelling out the comparison rather than being clever keeps it checkable.
uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    const int p  = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

// Undo one scanline's filter, in place, given the already-reconstructed row
// above. `bpp` is BYTES per pixel for filtering purposes, which the spec fixes
// at 1 for any bit depth below 8 — the filters work on bytes, not on samples,
// and that is exactly the detail a hand-rolled reader gets wrong.
bool unfilter(uint8_t type, uint8_t* row, const uint8_t* prev, size_t len,
              size_t bpp) {
    switch (type) {
        case 0:   // None
            return true;
        case 1:   // Sub
            for (size_t i = bpp; i < len; ++i)
                row[i] = static_cast<uint8_t>(row[i] + row[i - bpp]);
            return true;
        case 2:   // Up
            for (size_t i = 0; i < len; ++i)
                row[i] = static_cast<uint8_t>(row[i] + prev[i]);
            return true;
        case 3:   // Average
            for (size_t i = 0; i < len; ++i) {
                const int left = (i >= bpp) ? row[i - bpp] : 0;
                row[i] = static_cast<uint8_t>(row[i] + ((left + prev[i]) >> 1));
            }
            return true;
        case 4:   // Paeth
            for (size_t i = 0; i < len; ++i) {
                const uint8_t left = (i >= bpp) ? row[i - bpp] : 0;
                const uint8_t up   = prev[i];
                const uint8_t ul   = (i >= bpp) ? prev[i - bpp] : 0;
                row[i] = static_cast<uint8_t>(row[i] + paeth(left, up, ul));
            }
            return true;
        default:
            std::fprintf(stderr, "[png] unknown filter type %u\n", type);
            return false;
    }
}

}  // namespace

bool decode(const char* path, Indexed* out) {
    std::vector<uint8_t> raw;
    if (!slurp(path, &raw)) return false;

    if (raw.size() < sizeof(kSignature) ||
        std::memcmp(raw.data(), kSignature, sizeof(kSignature)) != 0) {
        std::fprintf(stderr, "[png] %s is not a PNG\n", path);
        return false;
    }

    int      width = 0, height = 0;
    int      bit_depth = 0, colour_type = 0, interlace = 0;
    bool     saw_ihdr = false;
    std::vector<uint8_t> idat;

    size_t i = sizeof(kSignature);
    while (i + 8 <= raw.size()) {
        const uint32_t len = be32(&raw[i]);
        const char     type[5] = {static_cast<char>(raw[i + 4]),
                                  static_cast<char>(raw[i + 5]),
                                  static_cast<char>(raw[i + 6]),
                                  static_cast<char>(raw[i + 7]), '\0'};
        const size_t   data = i + 8;
        if (data + len + 4 > raw.size()) {
            std::fprintf(stderr, "[png] %s: chunk %s runs past the end\n", path, type);
            return false;
        }

        if (std::strcmp(type, "IHDR") == 0) {
            if (len < 13) {
                std::fprintf(stderr, "[png] %s: short IHDR\n", path);
                return false;
            }
            width       = static_cast<int>(be32(&raw[data + 0]));
            height      = static_cast<int>(be32(&raw[data + 4]));
            bit_depth   = raw[data + 8];
            colour_type = raw[data + 9];
            interlace   = raw[data + 12];
            saw_ihdr    = true;
        } else if (std::strcmp(type, "IDAT") == 0) {
            // IDAT may be split across any number of chunks and the zlib stream
            // spans them, so they concatenate before anything is inflated.
            idat.insert(idat.end(), raw.begin() + data, raw.begin() + data + len);
        } else if (std::strcmp(type, "IEND") == 0) {
            break;
        }
        i = data + len + 4;   // + CRC, which we do not verify: a corrupt file
                              // fails at inflate or at the row count instead.
    }

    if (!saw_ihdr) {
        std::fprintf(stderr, "[png] %s: no IHDR\n", path);
        return false;
    }
    // REFUSE anything outside the one shape a pokeruby tileset can be. Silently
    // half-handling an 8bpp or interlaced sheet would produce plausible-looking
    // wrong pixels, which is the worst possible failure for this pipeline.
    if (bit_depth != 4 || colour_type != 3 || interlace != 0) {
        std::fprintf(stderr,
                     "[png] %s: need 4bpp indexed non-interlaced, got depth %d "
                     "colour type %d interlace %d\n",
                     path, bit_depth, colour_type, interlace);
        return false;
    }
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        std::fprintf(stderr, "[png] %s: implausible size %dx%d\n", path, width, height);
        return false;
    }
    if (idat.empty()) {
        std::fprintf(stderr, "[png] %s: no IDAT\n", path);
        return false;
    }

    // Two 4-bit samples per byte, rounded up, plus one filter byte per row.
    const size_t stride   = static_cast<size_t>((width + 1) / 2);
    const size_t expected = (stride + 1) * static_cast<size_t>(height);

    std::vector<uint8_t> inflated(expected);
    uLongf               got = static_cast<uLongf>(expected);
    const int            zr  = uncompress(inflated.data(), &got,
                                          idat.data(), static_cast<uLong>(idat.size()));
    if (zr != Z_OK || got != expected) {
        std::fprintf(stderr,
                     "[png] %s: inflate failed (zlib %d, %lu of %zu bytes)\n",
                     path, zr, static_cast<unsigned long>(got), expected);
        return false;
    }

    // Defilter row by row. `prev` starts as zeroes, which is what the spec says
    // the row above the first one is.
    std::vector<uint8_t> prev(stride, 0);
    std::vector<uint8_t> row(stride, 0);

    out->width  = width;
    out->height = height;
    out->index.assign(static_cast<size_t>(width) * height, 0);

    for (int y = 0; y < height; ++y) {
        const size_t  base   = static_cast<size_t>(y) * (stride + 1);
        const uint8_t filter = inflated[base];
        std::memcpy(row.data(), &inflated[base + 1], stride);

        if (!unfilter(filter, row.data(), prev.data(), stride, 1)) return false;

        // HIGH NIBBLE IS THE LEFT PIXEL — the opposite of the GBA's packing.
        // See the header comment; this is the single place the PNG's own order
        // is interpreted, and pack_gba_tiles below is the only place it is
        // turned back round.
        uint8_t* dst = &out->index[static_cast<size_t>(y) * width];
        for (int x = 0; x < width; ++x) {
            const uint8_t byte = row[static_cast<size_t>(x) / 2];
            dst[x] = (x & 1) ? static_cast<uint8_t>(byte & 0x0F)
                             : static_cast<uint8_t>(byte >> 4);
        }
        prev.swap(row);
    }
    return true;
}

bool pack_gba_tiles(const Indexed& img, std::vector<uint8_t>* out,
                    int* out_tile_count) {
    if (img.width <= 0 || img.height <= 0 ||
        (img.width % 8) != 0 || (img.height % 8) != 0) {
        std::fprintf(stderr, "[png] %dx%d is not a whole number of 8x8 tiles\n",
                     img.width, img.height);
        return false;
    }
    const int tiles_x = img.width / 8;
    const int tiles_y = img.height / 8;
    const int tiles   = tiles_x * tiles_y;

    out->assign(static_cast<size_t>(tiles) * 32, 0);

    for (int t = 0; t < tiles; ++t) {
        const int ox = (t % tiles_x) * 8;
        const int oy = (t / tiles_x) * 8;
        uint8_t*  dst = out->data() + static_cast<size_t>(t) * 32;

        for (int y = 0; y < 8; ++y) {
            const uint8_t* src =
                &img.index[static_cast<size_t>(oy + y) * img.width + ox];
            for (int x = 0; x < 8; x += 2) {
                // LOW nibble is the LEFT pixel, matching ruby_world.h's
                // description of VRAM and pair_pixel()'s own unpacking.
                dst[y * 4 + x / 2] = static_cast<uint8_t>(
                    (src[x] & 0x0F) | ((src[x + 1] & 0x0F) << 4));
            }
        }
    }

    if (out_tile_count) *out_tile_count = tiles;
    return true;
}

}  // namespace png
}  // namespace studio
