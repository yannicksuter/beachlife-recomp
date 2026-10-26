#pragma once

#include "windows.h"

#include <cstddef>
#include <cstdint>

namespace beachlife::compat::gdi
{
struct DibLayout
{
    int width = 0, height = 0;
    std::size_t stride = 0, bytes = 0;
    unsigned depth = 0;
    bool top_down = false;
    std::uint32_t red = 0, green = 0, blue = 0;
    const RGBQUAD* palette = nullptr;
    unsigned colours = 0;
};

// BI_RGB and BI_BITFIELDS, with Windows' DWORD-aligned scanlines. The caller
// owns the BITMAPINFO, including any palette/masks, for the lifetime of this view.
bool describe_dib(const BITMAPINFO* info, DibLayout& layout);
std::uint32_t dib_pixel(const DibLayout& layout, const void* bits, int x, int y);
} // namespace beachlife::compat::gdi
