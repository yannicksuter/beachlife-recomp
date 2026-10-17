#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace beachlife::data {

class FileSource;

struct TgaImage {
    int width = 0, height = 0;
    // Top-left origin, rows in order; RGBA with red in the lowest byte.
    std::vector<std::uint32_t> pixels;
};

// Read an indexed (8/16-bit indices, 24/32-bit palette) or truecolor
// (24/32-bit) TGA, uncompressed or RLE. Both image-origin axes are supported.
// Declared eight-bit alpha is retained; pixels without alpha are opaque.
// Other formats/interleaving are rejected. Limits: 16M pixels, 128 MiB input.
// Uses bounded reads through the VFS; source errors propagate to the caller.
TgaImage read_tga(const FileSource& source, std::string_view logical_path);

} // namespace beachlife::data
