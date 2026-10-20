#include "startup_resource_rasters.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace
{
constexpr std::size_t slots = 35;
using Pixels = std::array<unsigned short, slots * 2>;
using Expected = std::array<std::uint32_t, slots>;

std::uint32_t load(const Pixels& pixels, std::size_t slot)
{
    std::uint32_t value;
    std::memcpy(&value, pixels.data() + slot * 2, sizeof(value));
    return value;
}

Expected initialize(Pixels& pixels)
{
    Expected expected{};
    for (std::size_t slot = 0; slot < slots; ++slot)
    {
        const auto value = static_cast<std::uint32_t>(
            0x8a000000u + static_cast<unsigned int>(slot) * 0x10000u + 0x3100u + slot);
        std::memcpy(pixels.data() + slot * 2, &value, sizeof(value));
        expected[slot] = value;
    }
    return expected;
}

void verify(const Pixels& pixels, const Expected& expected)
{
    for (std::size_t slot = 0; slot < slots; ++slot)
    {
        if (load(pixels, slot) != expected[slot])
        {
            throw std::runtime_error("resource-raster2 literal pixels differ");
        }
    }
}
} // namespace

int main()
{
    try
    {
        std::array<unsigned short, 256> palette{};
        for (unsigned int i = 0; i < palette.size(); ++i)
        {
            palette[i] = static_cast<unsigned short>(0x8000u | (i << 8) | i);
        }

        // Two rows: a new run uses width in its first low word; consecutive
        // literals use the palette, while a skip resets the first-word rule.
        Pixels pixels{};
        auto expected = initialize(pixels);
        const unsigned char rows[] = {2, 3, 4, 255, 1, 5, 1, 6, 3, 7, 8, 9, 254};
        expected[8] = 0x8a080005u;
        expected[9] = 0x8a098404u;
        expected[11] = 0x8a0b0005u;
        expected[12] = 0x8a0c8606u;
        expected[15] = 0x8a0f0005u;
        expected[16] = 0x8a108808u;
        expected[17] = 0x8a118909u;
        ResourceRaster2(pixels.data(), rows, palette.data(), 7, 1, 1, 5, 2, 0, 0, 5, 2);
        verify(pixels, expected);

        // Actual clipped fallback writes the full first-format DWORDs,
        // including their ffff upper halves, and leaves the third pixel alone.
        expected = initialize(pixels);
        const unsigned char clipped[] = {3, 1, 2, 3};
        expected[8] = 0xffff8101u;
        expected[9] = 0xffff8202u;
        ResourceRaster2(pixels.data(), clipped, palette.data(), 7, 1, 1, 3, 1, 0, 0, 2, 1);
        verify(pixels, expected);

        // A zero header takes the original byte-underflow path and consumes
        // two literal indices. Zero height then leaves this buffer unchanged.
        expected = initialize(pixels);
        const unsigned char zero_header[] = {0, 10, 11};
        expected[0] = 0x8a000002u;
        expected[1] = 0x8a018b0bu;
        ResourceRaster2(pixels.data(), zero_header, palette.data(), 7, 0, 0, 2, 1, 0, 0, 2, 1);
        verify(pixels, expected);
        ResourceRaster2(pixels.data(), zero_header, palette.data(), 7, 0, 0, 2, 0, 0, 0, 2, 0);
        verify(pixels, expected);

        std::puts("groups=3 calls=4 checked-slots=140 resource-raster2=pass");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    }
}
