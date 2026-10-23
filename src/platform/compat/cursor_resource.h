#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace beachlife::compat
{
struct CursorImage
{
    int width, height, hotspot_x, hotspot_y;
    // Top-down, unpremultiplied RGBA bytes, independent of host endianness.
    std::vector<unsigned char> rgba;
};

class UnsupportedCursor : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

// Decode data only: PE RVAs are translated to bounded file offsets, never
// mapped as code. Zero dimensions retain the first group entry's size.
// Malformed/missing resources throw std::runtime_error; valid formats that
// cannot be represented by an SDL colour cursor throw UnsupportedCursor.
CursorImage decode_cursor_resource(
    std::span<const unsigned char> executable, std::uint16_t resource, int width, int height);
} // namespace beachlife::compat
