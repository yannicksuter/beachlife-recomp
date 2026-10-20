#include "render_mask_setup.h"
#include "startup_resource_rasters.h"

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <type_traits>

namespace
{
static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(sizeof(int) == 4 && USHRT_MAX == 0xffffU && UINT_MAX == 0xffffffffU);
static_assert(std::endian::native == std::endian::little);
using Raster = void (*)(unsigned short*,
    const unsigned char*,
    const unsigned short*,
    unsigned int,
    int,
    int,
    int,
    int,
    int,
    int,
    int,
    int,
    unsigned int,
    int);
static_assert(std::is_same_v<decltype(&ResourceRasterAt00402880), Raster>);
static_assert(std::is_same_v<decltype(render_quarter_mask), unsigned short>);
static_assert(std::is_same_v<decltype(render_half_mask), unsigned short>);
static_assert(std::is_same_v<decltype(render_full_mask), unsigned short>);

constexpr std::size_t slots = 160;
using Storage = std::array<unsigned short, slots * 2 + 4>;
const char* current_case = "start";
unsigned calls = 0;
unsigned setup_calls = 0;
unsigned checks = 0;
unsigned words_checked = 0;

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(
            stderr, "resource-depth-raster failure: case=%s reason=%s\n", current_case, reason);
        std::exit(2);
    }
}

struct GlobalState
{
    unsigned int format;
    unsigned short quarter;
    unsigned short half;
    unsigned short* pixels;
    unsigned short full;
    unsigned short green;
    unsigned short sum_bias;
    unsigned short quarter_bias;
    unsigned short half_bias;
    unsigned char green_levels;
    unsigned short green_bits;
    unsigned short red_shift;
    unsigned short red;
    unsigned int width;
    unsigned short blue;

    bool operator==(const GlobalState&) const = default;
};

GlobalState globals()
{
    return {render_mask_format,
        render_quarter_mask,
        render_half_mask,
        render_mask_pixels,
        render_full_mask,
        render_green_mask,
        render_sum_bias,
        render_quarter_bias,
        render_half_bias,
        render_green_levels,
        render_green_bits,
        render_red_shift,
        render_red_mask,
        render_mask_width,
        render_blue_mask};
}

struct Arguments
{
    unsigned int stride;
    int x;
    int y;
    int width;
    int height;
    int left;
    int top;
    int right;
    int bottom;
    unsigned int flags;
    int frame;
};

struct Pixel
{
    std::size_t slot;
    unsigned int value;
};

void put(Storage& storage, Pixel pixel)
{
    if (pixel.slot >= slots)
    {
        std::abort();
    }
    std::memcpy(storage.data() + 2 + pixel.slot * 2, &pixel.value, sizeof(pixel.value));
}

void run(const char* name,
    Arguments args,
    unsigned int format,
    std::initializer_list<unsigned char> packet_stream,
    std::initializer_list<Pixel> initial,
    std::initializer_list<Pixel> changed,
    bool palette_alias = false)
{
    current_case = name;
    Storage storage{};
    storage.front() = 0x1357;
    storage[1] = 0x2468;
    storage[storage.size() - 2] = 0x9753;
    storage.back() = 0x8642;
    for (std::size_t slot = 0; slot < slots; ++slot)
    {
        put(storage, {slot, 0x00001234});
    }
    for (Pixel pixel : initial)
    {
        put(storage, pixel);
    }
    Storage expected = storage;
    for (Pixel pixel : changed)
    {
        put(expected, pixel);
    }

    std::array<unsigned char, 32> source;
    source.fill(0xa5);
    if (packet_stream.size() > source.size() - 4)
    {
        std::abort();
    }
    std::size_t index = 2;
    for (unsigned char value : packet_stream)
    {
        source[index++] = value;
    }
    const auto source_before = source;
    std::array<unsigned short, 64> palette{};
    palette[1] = 0xabcd;
    const auto palette_before = palette;
    SetupRenderMasks(nullptr, args.stride, format);
    ++setup_calls;
    const GlobalState state_before = globals();
    unsigned short* destination = storage.data() + 2;
    ResourceRasterAt00402880(destination,
        packet_stream.size() == 0 ? nullptr : source.data() + 2,
        palette_alias ? destination : palette.data(),
        args.stride,
        args.x,
        args.y,
        args.width,
        args.height,
        args.left,
        args.top,
        args.right,
        args.bottom,
        args.flags,
        args.frame);
    ++calls;

    for (std::size_t word = 0; word < storage.size(); ++word)
    {
        ++checks;
        ++words_checked;
        if (storage[word] != expected[word])
        {
            std::fprintf(stderr,
                "resource-depth-raster failure: case=%s reason=pixel word=%zu actual=%04x "
                "expected=%04x\n",
                current_case,
                word,
                static_cast<unsigned>(storage[word]),
                static_cast<unsigned>(expected[word]));
            std::exit(2);
        }
    }
    require(source == source_before, "source-guards");
    require(palette == palette_before, "separate-palette");
    require(globals() == state_before, "global-state");
}

void full_group()
{
    constexpr Arguments full{6, 0, 0, 4, 1, 0, 0, 4, 1, 0x12340040U, 0};
    run("full565",
        full,
        0,
        {4, 1, 65, 129, 193},
        {{0, 0x00101234}, {1, 0x00501234}, {2, 0x00401234}},
        {{0, 0x0050abcd}, {1, 0x00507b4e}, {2, 0x00505af0}, {3, 0x00503272}});
    run("reject565",
        full,
        0,
        {4, 1, 65, 129, 193},
        {{0, 0x00601234}, {1, 0x00601234}, {2, 0x00601234}, {3, 0x00601234}},
        {{0, 0x0000ffff}, {3, 0x0000ffff}});
    run("full555",
        full,
        0xffffffffU,
        {4, 1, 65, 129, 193},
        {{0, 0x00101234}, {1, 0x00501234}, {2, 0x00401234}},
        {{0, 0x0050abcd}, {1, 0x0050234e}, {2, 0x00501ef0}, {3, 0x00501672}});
    run("reject555",
        full,
        0xffffffffU,
        {4, 1, 65, 129, 193},
        {{0, 0x00601234}, {1, 0x00601234}, {2, 0x00601234}, {3, 0x00601234}},
        {{0, 0x00007fff}, {3, 0x00007fff}});
    run("full-zero", {2, 0, 0, 1, 1, 0, 0, 1, 1, 0x40, 0}, 0, {0, 1}, {}, {{0, 0x0050abcd}});
}

void crop_group()
{
    constexpr Arguments left{6, 0, 0, 4, 1, 1, 0, 1, 1, 0x40, 0};
    constexpr Arguments right{6, 0, 0, 4, 1, 0, 0, 2, 1, 0x40, 0};
    run("left-ignores-right",
        left,
        0,
        {0, 4, 1, 65, 129, 193},
        {},
        {{0, 0x00507b4e}, {1, 0x00505af0}, {2, 0x00503272}});
    run("right-two", right, 0, {0, 4, 1, 65, 129, 193}, {}, {{0, 0x0050abcd}, {1, 0x00507b4e}});
    run("left-rejected",
        left,
        0,
        {0, 4, 1, 65, 129, 193},
        {{0, 0x00601234}, {1, 0x00601234}, {2, 0x00601234}},
        {});
    run("right-rejected",
        right,
        0,
        {0, 4, 1, 65, 129, 193},
        {{0, 0x00601234}, {1, 0x00601234}},
        {});
    run("left-byte-gap",
        {132, 0, 0, 129, 1, 1, 0, 0, 1, 0x40, 0},
        0,
        {128, 1, 1},
        {},
        {{127, 0x0050abcd}});
}

void timing_alias_group()
{
    run("top-depth",
        {3, 1, 1, 1, 10, 0, 1, 1, 3, 0x30, -1},
        0,
        {1, 1, 1, 1, 1, 1},
        {},
        {{4, 0x0040abcd}, {7, 0x0050abcd}});
    // Palette entry1 is the first pixel's genuine high WORD. Its new depth is
    // observed by the next payload. A two-pixel packet also overshoots width1.
    run("palette-depth-alias",
        {3, 0, 0, 1, 1, 0, 0, 1, 1, 0x40, 0},
        0,
        {2, 1, 1},
        {{0, 0x00011234}},
        {{0, 0x00500001}, {1, 0x00500050}},
        true);
    run("empty-full", {3, 0, 0, 1, 0, 0, 0, 1, 0, 0x40, 0}, 0, {}, {}, {});
    run("top-before-empty", {3, 0, 0, 1, 5, 0, 1, 1, 1, 0x30, -1}, 0, {1, 1}, {}, {});
}
} // namespace

int main()
{
    full_group();
    crop_group();
    timing_alias_group();
    SetupRenderMasks(nullptr, 0, 0);
    ++setup_calls;
    std::printf(
        "{\"groups\":3,\"calls\":%u,\"setup_calls\":%u,\"checks\":%u,\"words_checked\":%u}\n",
        calls,
        setup_calls,
        checks,
        words_checked);
}
