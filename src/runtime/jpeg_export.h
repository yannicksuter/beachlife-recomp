#pragma once

#include <cstdint>
#include <cstdio>

namespace beachlife::runtime::jpeg
{

// Numeric categories retained at the former codec boundary. Native failures do
// not reproduce every IJL error path or its allocation and timing behavior.
enum class Result : int
{
    success = 0,
    invalid_properties = -8,
    close_failed = -9,
    open_failed = -10,
    write_failed = -23,
    encode_failed = -24
};

// Borrows tightly packed, top-down RGB bytes and a host-native output path.
// The caller supplies at least width * height * 3 readable bytes. Valid sizes
// fit JPEG's 16-bit dimensions and the encoder's signed-int indexing; quality
// is 1..100. Encoding always uses Y 2x2, Cb/Cr 1x1 sampling, including quality100.
// Validation precedes opening. Opening truncates, failures retain any partial
// output, and the real FILE is closed before the result is returned.
// The boot build maps the game's Windows paths (photos\\...) into the player's
// folder by installing an open function; without one, paths are host paths.
using OpenFunction = std::FILE* (*)(const char* path, const char* mode);
void set_open_function(OpenFunction open) noexcept;

Result write_rgb420_file(const char* path,
    const unsigned char* pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t quality) noexcept;

} // namespace beachlife::runtime::jpeg
