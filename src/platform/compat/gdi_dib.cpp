#include "gdi_dib.h"

#include <bit>
#include <cstring>
#include <limits>

namespace beachlife::compat::gdi
{
namespace
{
std::uint32_t little_word(const BYTE* bytes, unsigned count)
{
    std::uint32_t result = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        result |= std::uint32_t(bytes[i]) << (8 * i);
    }
    return result;
}
bool valid_mask(std::uint32_t mask, unsigned depth)
{
    if (!mask || (depth < 32 && mask >> depth))
    {
        return false;
    }
    const auto shifted = mask >> std::countr_zero(mask);
    return (shifted & (shifted + 1)) == 0;
}
std::uint32_t component(std::uint32_t pixel, std::uint32_t mask)
{
    const unsigned shift = std::countr_zero(mask);
    const std::uint32_t channel = (pixel & mask) >> shift;
    const unsigned width = std::popcount(mask);
    if (width >= 8)
    {
        return channel >> (width - 8);
    }
    // Repeat the high bits, as for RGB555's (r << 3) | (r >> 2).
    // This also agrees with the existing renderer's RGB555 conversion.
    std::uint32_t expanded = channel;
    unsigned bits = width;
    while (bits < 8)
    {
        expanded = (expanded << width) | channel;
        bits += width;
    }
    return expanded >> (bits - 8);
}
} // namespace

bool describe_dib(const BITMAPINFO* info, DibLayout& layout)
{
    if (!info)
    {
        return false;
    }
    const auto& h = info->bmiHeader;
    if (h.biSize < sizeof(BITMAPINFOHEADER) || h.biWidth <= 0 || !h.biHeight ||
        h.biHeight == std::numeric_limits<LONG>::min() || h.biPlanes != 1)
    {
        return false;
    }
    if (h.biBitCount != 1 && h.biBitCount != 4 && h.biBitCount != 8 && h.biBitCount != 16 &&
        h.biBitCount != 24 && h.biBitCount != 32)
    {
        return false;
    }
    if (h.biCompression != BI_RGB && h.biCompression != 3)
    {
        return false;
    }
    if (h.biCompression == 3 && h.biBitCount != 16 && h.biBitCount != 32)
    {
        return false;
    }
    DibLayout result;
    result.width = h.biWidth;
    result.height = h.biHeight < 0 ? -h.biHeight : h.biHeight;
    result.top_down = h.biHeight < 0;
    result.depth = h.biBitCount;
    const std::uint64_t stride = ((std::uint64_t(result.width) * result.depth + 31) / 32) * 4;
    if (stride > std::numeric_limits<std::size_t>::max() / result.height)
    {
        return false;
    }
    result.stride = static_cast<std::size_t>(stride);
    result.bytes = result.stride * result.height;
    if (result.depth <= 8)
    {
        result.colours = h.biClrUsed ? h.biClrUsed : 1U << result.depth;
        if (result.colours > 1U << result.depth)
        {
            return false;
        }
        result.palette =
            reinterpret_cast<const RGBQUAD*>(reinterpret_cast<const BYTE*>(info) + h.biSize);
    }
    if (result.depth == 16)
    {
        result.red = 0x7c00;
        result.green = 0x03e0;
        result.blue = 0x001f;
    }
    else
    {
        result.red = 0xff0000;
        result.green = 0x00ff00;
        result.blue = 0x0000ff;
    }
    if (h.biCompression == 3)
    {
        // BITMAPINFOHEADER stores three following DWORDs; V2+ headers embed them.
        if (h.biSize != 40 && h.biSize < 52)
        {
            return false;
        }
        const auto* masks = reinterpret_cast<const BYTE*>(info) + 40;
        result.red = little_word(masks, 4);
        result.green = little_word(masks + 4, 4);
        result.blue = little_word(masks + 8, 4);
        if (!valid_mask(result.red, result.depth) || !valid_mask(result.green, result.depth) ||
            !valid_mask(result.blue, result.depth) || (result.red & result.green) ||
            (result.red & result.blue) || (result.green & result.blue))
        {
            return false;
        }
    }
    layout = result;
    return true;
}

std::uint32_t dib_pixel(const DibLayout& layout, const void* bits, int x, int y)
{
    const auto row = layout.top_down ? y : layout.height - 1 - y;
    const auto* bytes = static_cast<const BYTE*>(bits) + std::size_t(row) * layout.stride;
    if (layout.depth <= 8)
    {
        const std::size_t bit = std::size_t(x) * layout.depth;
        const unsigned shift = 8 - layout.depth - bit % 8;
        const unsigned index = (bytes[bit / 8] >> shift) & ((1U << layout.depth) - 1);
        if (index >= layout.colours)
        {
            return 0xff000000;
        }
        const auto& p = layout.palette[index];
        return std::uint32_t(p.rgbRed) | (std::uint32_t(p.rgbGreen) << 8) |
               (std::uint32_t(p.rgbBlue) << 16) | 0xff000000;
    }
    const auto value = little_word(bytes + std::size_t(x) * (layout.depth / 8), layout.depth / 8);
    return component(value, layout.red) | (component(value, layout.green) << 8) |
           (component(value, layout.blue) << 16) | 0xff000000;
}
} // namespace beachlife::compat::gdi
