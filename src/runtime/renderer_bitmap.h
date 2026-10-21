#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace beachlife::runtime::renderer_display
{
// Typed host description of the 40-byte BITMAPINFOHEADER/one-RGBQUAD shape.
// It is not a Win32 handle or a DirectDraw object. No default member initializer
// is permitted: initialization clears only the header of the 44-byte allocation.
struct BitmapInfoHeader
{
    std::uint32_t biSize;
    std::int32_t biWidth;
    std::int32_t biHeight;
    std::uint16_t biPlanes;
    std::uint16_t biBitCount;
    std::uint32_t biCompression;
    std::uint32_t biSizeImage;
    std::int32_t biXPelsPerMeter;
    std::int32_t biYPelsPerMeter;
    std::uint32_t biClrUsed;
    std::uint32_t biClrImportant;
};

struct BitmapRgbQuad
{
    std::uint8_t blue;
    std::uint8_t green;
    std::uint8_t red;
    std::uint8_t reserved;
};

struct BitmapInfo
{
    BitmapInfoHeader bmiHeader;
    BitmapRgbQuad bmiColors[1];
};

static_assert(std::is_trivial_v<BitmapInfo> && std::is_standard_layout_v<BitmapInfo>);
static_assert(sizeof(BitmapInfoHeader) == 40);
static_assert(offsetof(BitmapInfo, bmiColors) == 40);
static_assert(sizeof(BitmapInfo) == 44);

// A negative biHeight, 16 bits, and BI_RGB (0) describe top-down RGB555 here.
// DIB rows have DWORD-aligned stride. This declaration does not repair the
// original odd-width surface allocation/pitch or read its uninitialized tail.
} // namespace beachlife::runtime::renderer_display
