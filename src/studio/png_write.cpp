#include "png_write.h"

#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace studio {
namespace png {
namespace {

void be32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

// length | type | data | CRC32(type + data). The CRC covers the type bytes as
// well as the payload and NOT the length — a detail worth stating because
// getting it wrong produces a file that every viewer rejects with the same
// unhelpful "invalid chunk".
void chunk(std::vector<uint8_t>& out, const char type[4],
           const uint8_t* data, size_t len) {
    be32(out, static_cast<uint32_t>(len));
    const size_t crc_start = out.size();
    out.insert(out.end(), type, type + 4);
    if (len) out.insert(out.end(), data, data + len);
    const uLong c = crc32(crc32(0L, Z_NULL, 0), &out[crc_start],
                          static_cast<uInt>(4 + len));
    be32(out, static_cast<uint32_t>(c));
}

}  // namespace

bool write_rgb(const char* path, int width, int height,
               const uint8_t* rgb_top_down) {
    if (width <= 0 || height <= 0 || !rgb_top_down) {
        std::fprintf(stderr, "[png] refusing to write %dx%d\n", width, height);
        return false;
    }

    // The filtered stream: one filter byte per row, then the row.
    if (static_cast<size_t>(width) >
            (std::numeric_limits<size_t>::max() - 1) / 3 ||
        static_cast<size_t>(height) >
            std::numeric_limits<size_t>::max() /
                (static_cast<size_t>(width) * 3 + 1)) {
        std::fprintf(stderr, "[png] refusing dimensions that overflow storage\n");
        return false;
    }
    const size_t stride = static_cast<size_t>(width) * 3;
    std::vector<uint8_t> raw(static_cast<size_t>(height) * (stride + 1));
    if (raw.size() > std::numeric_limits<uLong>::max()) {
        std::fprintf(stderr, "[png] image is too large for this zlib build\n");
        return false;
    }
    for (int y = 0; y < height; ++y) {
        uint8_t* dst = &raw[static_cast<size_t>(y) * (stride + 1)];
        *dst = 0;  // filter: None
        std::memcpy(dst + 1, rgb_top_down + static_cast<size_t>(y) * stride,
                    stride);
    }

    uLongf zlen = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> z(zlen);
    const int zr = compress2(z.data(), &zlen, raw.data(),
                             static_cast<uLong>(raw.size()),
                             Z_DEFAULT_COMPRESSION);
    if (zr != Z_OK) {
        std::fprintf(stderr, "[png] %s: deflate failed (zlib %d)\n", path, zr);
        return false;
    }

    std::vector<uint8_t> file;
    file.reserve(zlen + 128);

    static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G',
                                          0x0D, 0x0A, 0x1A, 0x0A};
    file.insert(file.end(), kSignature, kSignature + 8);

    std::vector<uint8_t> ihdr;
    be32(ihdr, static_cast<uint32_t>(width));
    be32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);   // bit depth
    ihdr.push_back(2);   // colour type 2 = truecolour RGB
    ihdr.push_back(0);   // compression: deflate, the only defined value
    ihdr.push_back(0);   // filter method 0, the only defined value
    ihdr.push_back(0);   // no interlace
    chunk(file, "IHDR", ihdr.data(), ihdr.size());

    // One IDAT. The spec allows splitting the zlib stream across many, which
    // matters for streaming encoders and not at all here.
    chunk(file, "IDAT", z.data(), zlen);
    chunk(file, "IEND", nullptr, 0);

    std::FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "[png] %s: cannot open for writing\n", path);
        return false;
    }
    const size_t wrote = std::fwrite(file.data(), 1, file.size(), f);
    const int close_result = std::fclose(f);
    if (wrote != file.size() || close_result != 0) {
        std::fprintf(stderr, "[png] %s: write/close failed (%zu of %zu)\n", path,
                     wrote, file.size());
        return false;
    }
    return true;
}

}  // namespace png
}  // namespace studio
