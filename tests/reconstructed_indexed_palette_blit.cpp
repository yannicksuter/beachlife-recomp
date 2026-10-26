#include "indexed_palette_blit.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>

static_assert(CHAR_BIT == 8 && sizeof(unsigned char) == 1);
static_assert(sizeof(unsigned short) == 2 && std::numeric_limits<unsigned short>::digits == 16);
static_assert(sizeof(unsigned int) == 4 && std::numeric_limits<unsigned int>::digits == 32);
static_assert(sizeof(int) == 4 && std::numeric_limits<int>::digits == 31);

namespace
{
constexpr std::size_t guard_size = 8;
constexpr std::size_t source_size = 512;
constexpr std::size_t destination_size = 1024;

template <typename T, std::size_t N> struct Guarded
{
    std::array<T, guard_size> before;
    std::array<T, N> data;
    std::array<T, guard_size> after;
};

struct Buffers
{
    Guarded<unsigned char, source_size> source;
    Guarded<unsigned short, 256> palette;
    Guarded<unsigned short, destination_size> destination;
};

struct RectangleCase
{
    int source_stride;
    int destination_stride;
    int source_x;
    int source_y;
    int destination_x;
    int destination_y;
    int width;
    int height;
};

std::size_t cases = 0;
std::size_t calls = 0;
std::size_t checks = 0;
std::size_t expected_writes = 0;

[[noreturn]] void fail(const char* reason, std::size_t index)
{
    std::fprintf(stderr, "case %zu: %s at %zu\n", cases, reason, index);
    std::exit(2);
}

template <typename T, std::size_t N>
void compare_array(
    const std::array<T, N>& actual, const std::array<T, N>& expected, const char* reason)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        ++checks;
        if (actual[i] != expected[i])
        {
            fail(reason, i);
        }
    }
}

template <typename T, std::size_t N>
void preserved(const Guarded<T, N>& actual, const Guarded<T, N>& expected, const char* reason)
{
    compare_array(actual.before, expected.before, "leading guard differs");
    compare_array(actual.data, expected.data, reason);
    compare_array(actual.after, expected.after, "trailing guard differs");
}

Buffers make_buffers()
{
    Buffers buffers{};
    buffers.source.before.fill(0xa5);
    buffers.source.after.fill(0x5a);
    buffers.palette.before.fill(0x1357);
    buffers.palette.after.fill(0x2468);
    buffers.destination.before.fill(0x89ab);
    buffers.destination.after.fill(0xcdef);
    for (std::size_t i = 0; i < source_size; ++i)
    {
        buffers.source.data[i] = static_cast<unsigned char>((i * 73 + 41) & 255);
    }
    for (std::size_t i = 0; i < 256; ++i)
    {
        buffers.palette.data[i] = static_cast<unsigned short>((i * 257) ^ 0xa55a);
    }
    for (std::size_t i = 0; i < destination_size; ++i)
    {
        buffers.destination.data[i] = static_cast<unsigned short>((i * 109) ^ 0x4000);
    }
    return buffers;
}

void run(Buffers& buffers,
    const RectangleCase& rectangle,
    const Guarded<unsigned short, destination_size>& expected)
{
    const auto source_before = buffers.source;
    const auto palette_before = buffers.palette;
    IndexedPaletteBlit(buffers.source.data.data(),
        rectangle.source_stride,
        buffers.destination.data.data(),
        rectangle.destination_stride,
        rectangle.source_x,
        rectangle.source_y,
        rectangle.destination_x,
        rectangle.destination_y,
        rectangle.width,
        rectangle.height,
        buffers.palette.data.data());
    ++calls;
    preserved(buffers.source, source_before, "source byte differs");
    preserved(buffers.palette, palette_before, "palette halfword differs");
    preserved(buffers.destination, expected, "destination halfword differs");
    expected_writes += static_cast<std::size_t>(rectangle.width * rectangle.height);
    ++cases;
}

void literal_case()
{
    auto buffers = make_buffers();
    constexpr std::array<unsigned char, 20> source = {
        250, 251, 252, 253, 254, 40, 1, 128, 0, 41, 42, 255, 2, 7, 43, 44, 45, 46, 47, 48};
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        buffers.source.data[i] = source[i];
    }
    buffers.palette.data.fill(0x7777);
    buffers.palette.data[0] = 0x1357;
    buffers.palette.data[1] = 0x2468;
    buffers.palette.data[2] = 0x55aa;
    buffers.palette.data[7] = 0xaa55;
    buffers.palette.data[128] = 0x8001;
    buffers.palette.data[255] = 0xffff;
    buffers.destination.data.fill(0x6a6a);

    auto expected = buffers.destination;
    expected.data[14] = 0x2468;
    expected.data[16] = 0x8001;
    expected.data[18] = 0x1357;
    expected.data[26] = 0xffff;
    expected.data[28] = 0x55aa;
    expected.data[30] = 0xaa55;
    run(buffers, {5, 6, 1, 1, 1, 1, 3, 2}, expected);
}

void all_palette_indices()
{
    auto buffers = make_buffers();
    auto expected = buffers.destination;
    // A 16-by-16 grid assigns each palette index its own independently addressed
    // destination slot. The unused halfwords and row padding retain their patterns.
    for (std::size_t index = 0; index < 256; ++index)
    {
        const auto row = index / 16;
        const auto column = index % 16;
        buffers.source.data[(row + 1) * 19 + column + 1] = static_cast<unsigned char>(index);
        expected.data[((row + 2) * 23 + column + 3) * 2] = buffers.palette.data[index];
    }
    run(buffers, {19, 23, 1, 1, 3, 2, 16, 16}, expected);
}

void varied_rectangles()
{
    constexpr std::array<RectangleCase, 6> rectangles = {{
        {4, 7, 0, 0, 0, 0, 1, 1},
        {9, 11, 2, 1, 3, 2, 5, 3},
        {13, 8, 6, 2, 1, 1, 6, 4},
        {17, 23, 0, 3, 5, 0, 11, 2},
        {31, 19, 12, 0, 0, 4, 16, 1},
        {6, 5, 1, 2, 1, 1, 4, 5},
    }};
    for (const auto& rectangle : rectangles)
    {
        auto buffers = make_buffers();
        auto expected = buffers.destination;
        // Enumerate destination halfwords in ascending address order, selecting
        // by rectangle membership. This oracle has no reverse-row cursor recurrence.
        for (std::size_t halfword = 0; halfword < destination_size; ++halfword)
        {
            const int slot = static_cast<int>(halfword / 2);
            const int x = slot % rectangle.destination_stride;
            const int y = slot / rectangle.destination_stride;
            if (halfword % 2 == 0 && x >= rectangle.destination_x &&
                x < rectangle.destination_x + rectangle.width && y >= rectangle.destination_y &&
                y < rectangle.destination_y + rectangle.height)
            {
                const int source_x = rectangle.source_x + x - rectangle.destination_x;
                const int source_y = rectangle.source_y + y - rectangle.destination_y;
                const auto index = buffers.source.data[static_cast<std::size_t>(
                    source_y * rectangle.source_stride + source_x)];
                expected.data[halfword] = buffers.palette.data[index];
            }
        }
        run(buffers, rectangle, expected);
    }
}

void zero_extents()
{
    constexpr std::array<RectangleCase, 6> rectangles = {{
        {7, 11, 0, 0, 0, 0, 0, 0},
        {7, 11, 1, 0, 2, 0, 4, 0},
        {7, 11, 1, 0, 2, 0, 0, 4},
        {7, 11, 1, 3, 2, 2, 0, 0},
        {7, 11, 1, 3, 2, 2, 4, 0},
        {7, 11, 1, 3, 2, 2, 0, 4},
    }};
    for (const auto& rectangle : rectangles)
    {
        auto buffers = make_buffers();
        const auto expected = buffers.destination;
        run(buffers, rectangle, expected);
    }
}
} // namespace

int main()
{
    literal_case();
    all_palette_indices();
    varied_rectangles();
    zero_extents();
    std::printf("indexed palette blit: %zu cases, %zu calls, %zu expected writes, %zu checks\n",
        cases,
        calls,
        expected_writes,
        checks);
    return 0;
}
