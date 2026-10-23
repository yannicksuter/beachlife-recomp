#include "startup_resource_rasters.h"

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <limits>

static_assert(CHAR_BIT == 8);
static_assert(sizeof(unsigned short) == 2 && std::numeric_limits<unsigned short>::digits == 16);
static_assert(sizeof(unsigned int) == 4 && std::numeric_limits<unsigned int>::digits == 32);
static_assert(sizeof(int) == 4 && std::numeric_limits<int>::digits == 31);
static_assert(std::numeric_limits<signed char>::min() == -128);
static_assert(std::numeric_limits<signed char>::max() == 127);
static_assert(std::endian::native == std::endian::little);

namespace
{
constexpr std::size_t guard_bytes = 16;
constexpr std::size_t destination_slots = 512;
constexpr std::size_t source_bytes = 134;
constexpr std::size_t palette_entries = 256;

enum class Branch
{
    full,
    partial,
    positive_left
};

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
};

struct ExpectedSpan
{
    std::size_t first;
    std::size_t count;
    unsigned short value;
};

struct LiteralCase
{
    const char* name;
    Branch branch;
    Arguments arguments;
    std::array<unsigned char, source_bytes> packet;
    std::size_t packet_size;
    std::array<ExpectedSpan, 4> spans;
    std::size_t span_count;
};

// These are all 31 accepted literal packet/argument/output plans. Repeated
// equal expected words use fixed spans; no packet decoder computes the oracle.
constexpr std::array<LiteralCase, 31> literal_cases{{
    {"F0-D250-two-visible",
        Branch::full,
        {4, 1, 1, 2, 1, 0, 0, 2, 1},
        {2, 1, 2},
        3,
        {{{5, 1, 0x1234u}, {6, 1, 0x5678u}}},
        2},
    {"F1-D250-negative-gap",
        Branch::full,
        {4, 1, 1, 2, 1, 0, 0, 2, 1},
        {1, 1, 255},
        3,
        {{{5, 1, 0x1234u}}},
        1},
    {"F2-unequal-row-stride",
        Branch::full,
        {5, 2, 1, 2, 2, 0, 0, 2, 2},
        {2, 1, 2, 1, 3, 255},
        6,
        {{{7, 1, 0x1234u}, {8, 1, 0x5678u}, {12, 1, 0xffffu}}},
        3},
    {"F3-zero-token",
        Branch::full,
        {8, 0, 0, 1, 1, 0, 0, 1, 1},
        {0, 1, 0},
        3,
        {{{0, 1, 0x0000u}}},
        1},
    {"F4-run-crosses-width",
        Branch::full,
        {8, 0, 0, 2, 1, 0, 0, 2, 1},
        {3, 0, 128, 255},
        4,
        {{{0, 1, 0x0000u}, {1, 1, 0x8000u}, {2, 1, 0xabcdu}}},
        3},
    {"F5-negative-128-gap", Branch::full, {256, 0, 0, 128, 1, 0, 0, 128, 1}, {128}, 1, {{}}, 0},
    {"F6-negative-crops-fastpath",
        Branch::full,
        {4, 1, 1, 2, 1, -3, -2, 2, 5},
        {2, 1, 2},
        3,
        {{{5, 1, 0x1234u}, {6, 1, 0x5678u}}},
        2},
    {"F7-zero-height", Branch::full, {8, 0, 0, 2, 0, 0, 0, 2, 0}, {1, 1}, 2, {{}}, 0},
    {"F8-zero-width", Branch::full, {8, 0, 0, 0, 2, 0, 0, 0, 2}, {1, 1}, 2, {{}}, 0},
    {"F9-palette-byte-index-extremes",
        Branch::full,
        {8, 0, 0, 4, 1, 0, 0, 4, 1},
        {4, 0, 127, 128, 255},
        5,
        {{{0, 1, 0x0000u}, {1, 1, 0x7fffu}, {2, 1, 0x8000u}, {3, 1, 0xabcdu}}},
        4},
    {"C0-right-two",
        Branch::partial,
        {8, 0, 0, 4, 1, 0, 0, 2, 1},
        {4, 1, 2, 3, 4},
        5,
        {{{0, 1, 0x1234u}, {1, 1, 0x5678u}}},
        2},
    {"C1-top-row-consumed-destination-unshifted",
        Branch::partial,
        {4, 1, 1, 2, 2, 0, 1, 2, 2},
        {2, 1, 2, 2, 3, 4},
        6,
        {{{5, 1, 0xffffu}, {6, 1, 0x0001u}}},
        2},
    {"C2-bottom-shortens-source",
        Branch::partial,
        {8, 0, 0, 1, 3, 0, 0, 1, 2},
        {1, 1, 1, 2, 1, 3},
        6,
        {{{0, 1, 0x1234u}, {8, 1, 0x5678u}}},
        2},
    {"C3-empty-after-top-consumption",
        Branch::partial,
        {8, 0, 0, 1, 2, 0, 1, 1, 1},
        {1, 1, 1, 2},
        4,
        {{}},
        0},
    {"C4-negative-right", Branch::partial, {8, 0, 0, 3, 1, 0, 0, -1, 1}, {3, 1, 2, 3}, 4, {{}}, 0},
    {"C5-gap-crosses-right",
        Branch::partial,
        {8, 0, 0, 4, 1, 0, 0, 1, 1},
        {254, 2, 1, 2},
        4,
        {{}},
        0},
    {"C6-gap-before-visible",
        Branch::partial,
        {8, 0, 0, 3, 1, 0, 0, 2, 1},
        {255, 2, 1, 2},
        4,
        {{{1, 1, 0x1234u}}},
        1},
    {"C7-negative-top-adds-rows",
        Branch::partial,
        {8, 0, 0, 1, 3, 0, -1, 1, 2},
        {1, 1, 1, 2, 1, 3},
        6,
        {{{0, 1, 0x1234u}, {8, 1, 0x5678u}, {16, 1, 0xffffu}}},
        3},
    {"C8-bottom-beyond-height",
        Branch::partial,
        {8, 0, 0, 1, 1, 0, 1, 1, 3},
        {1, 1, 1, 2, 1, 3},
        6,
        {{{0, 1, 0x5678u}, {8, 1, 0xffffu}}},
        2},
    {"C9-zero-token-in-both-scans",
        Branch::partial,
        {8, 0, 0, 1, 2, 0, 1, 1, 2},
        {0, 1, 1, 0, 1, 2},
        6,
        {{{0, 1, 0x5678u}}},
        1},
    {"C10-run-crosses-width-and-right",
        Branch::partial,
        {8, 0, 0, 2, 2, 0, 0, 3, 1},
        {4, 1, 2, 3, 4},
        5,
        {{{0, 1, 0x1234u}, {1, 1, 0x5678u}, {2, 1, 0xffffu}}},
        3},
    {"L0-left-one-packed-visible",
        Branch::positive_left,
        {8, 0, 0, 2, 1, 1, 0, 2, 1},
        {2, 1, 2},
        3,
        {{{0, 1, 0x5678u}}},
        1},
    {"L1-skip-top-and-left-gap",
        Branch::positive_left,
        {8, 0, 0, 2, 2, 1, 1, 2, 2},
        {2, 1, 2, 255, 1, 3},
        6,
        {{{0, 1, 0xffffu}}},
        1},
    {"L2-byte-neg128-until-zero",
        Branch::positive_left,
        {256, 0, 0, 128, 1, 200, 0, 128, 1},
        {128},
        1,
        {{}},
        0},
    {"L3-byte-neg128-after-left-exhausted",
        Branch::positive_left,
        {256, 128, 0, 2, 1, 1, 0, 2, 1},
        {1,
            1,
            128,
            127,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            3,
            4},
        134,
        {{{0, 127, 0x5678u}, {127, 1, 0xffffu}, {128, 1, 0x0001u}}},
        3},
    {"L4-negative-height-after-top",
        Branch::positive_left,
        {8, 0, 0, 1, 1, 1, 1, 1, 0},
        {1, 255},
        2,
        {{}},
        0},
    {"L5-zero-width-no-packet-read",
        Branch::positive_left,
        {8, 0, 0, 0, 1, 1, 2, 0, 3},
        {1, 1},
        2,
        {{}},
        0},
    {"L6-right-ignored-by-positive-left",
        Branch::positive_left,
        {8, 0, 0, 4, 1, 1, 0, 0, 1},
        {4, 1, 2, 3, 4},
        5,
        {{{0, 1, 0x5678u}, {1, 1, 0xffffu}, {2, 1, 0x0001u}}},
        3},
    {"L7-left-resets-and-height-does-not-limit",
        Branch::positive_left,
        {3, 1, 1, 3, 1, 1, 0, -4, 2},
        {3, 0, 127, 128, 3, 255, 4, 1},
        8,
        {{{4, 1, 0x7fffu}, {5, 1, 0x8000u}, {7, 1, 0x0001u}, {8, 1, 0x1234u}}},
        4},
    {"L8-left-consumed-by-run-and-gap",
        Branch::positive_left,
        {8, 0, 0, 6, 1, 3, 0, 6, 1},
        {1, 1, 254, 3, 2, 3, 4},
        7,
        {{{0, 1, 0x5678u}, {1, 1, 0xffffu}, {2, 1, 0x0001u}}},
        3},
    {"L9-left-beyond-run-width-overshoot",
        Branch::positive_left,
        {8, 0, 0, 2, 1, 5, 0, 2, 1},
        {3, 1, 2, 3},
        4,
        {{}},
        0},
}};

constexpr bool literal_bounds()
{
    std::array<std::size_t, 3> branches{};
    for (const auto& test : literal_cases)
    {
        if (test.packet_size > source_bytes || test.span_count > test.spans.size())
        {
            return false;
        }
        ++branches[static_cast<std::size_t>(test.branch)];
        std::array<bool, destination_slots> written{};
        for (std::size_t i = 0; i < test.span_count; ++i)
        {
            const auto& span = test.spans[i];
            if (span.count == 0 || span.first >= destination_slots ||
                span.count > destination_slots - span.first)
            {
                return false;
            }
            for (std::size_t offset = 0; offset < span.count; ++offset)
            {
                if (written[span.first + offset])
                {
                    return false;
                }
                written[span.first + offset] = true;
            }
        }
    }
    return branches == std::array<std::size_t, 3>{10, 11, 10};
}

static_assert(literal_bounds());

template <typename T, std::size_t N> struct Guarded
{
    std::array<unsigned char, guard_bytes> before{};
    std::array<T, N> data{};
    std::array<unsigned char, guard_bytes> after{};
};

using Destination = Guarded<unsigned short, destination_slots>;
using Palette = Guarded<unsigned short, palette_entries>;
using Stream = Guarded<unsigned char, source_bytes>;

static_assert(offsetof(Destination, data) == guard_bytes);
static_assert(offsetof(Destination, after) == guard_bytes + destination_slots * 2);
static_assert(offsetof(Palette, data) == guard_bytes);
static_assert(offsetof(Palette, after) == guard_bytes + palette_entries * 2);
static_assert(offsetof(Stream, data) == guard_bytes);
static_assert(offsetof(Stream, after) == guard_bytes + source_bytes);

struct Counters
{
    std::size_t cases = 0;
    std::size_t calls = 0;
    std::size_t byte_checks = 0;
    std::array<std::size_t, 3> branches{};
};

template <typename T, std::size_t N> void initialize_guards(Guarded<T, N>& value, unsigned int seed)
{
    for (std::size_t i = 0; i < guard_bytes; ++i)
    {
        value.before[i] = static_cast<unsigned char>(seed + i);
        value.after[i] = static_cast<unsigned char>((seed ^ 0xffu) - i);
    }
}

bool compare_bytes(const char* name,
    const char* region,
    const unsigned char* actual,
    const unsigned char* expected,
    std::size_t size,
    Counters& counters)
{
    for (std::size_t i = 0; i < size; ++i)
    {
        ++counters.byte_checks;
        if (actual[i] != expected[i])
        {
            std::fprintf(stderr,
                "resource raster packed %s: %s byte %zu got 0x%02x, expected 0x%02x\n",
                name,
                region,
                i,
                static_cast<unsigned int>(actual[i]),
                static_cast<unsigned int>(expected[i]));
            return false;
        }
    }
    return true;
}

template <typename T, std::size_t N>
bool preserved(const char* name,
    const char* region,
    const Guarded<T, N>& actual,
    const Guarded<T, N>& expected,
    Counters& counters)
{
    return compare_bytes(
               name, region, actual.before.data(), expected.before.data(), guard_bytes, counters) &&
           compare_bytes(name,
               region,
               reinterpret_cast<const unsigned char*>(actual.data.data()),
               reinterpret_cast<const unsigned char*>(expected.data.data()),
               sizeof(T) * N,
               counters) &&
           compare_bytes(
               name, region, actual.after.data(), expected.after.data(), guard_bytes, counters);
}

bool run_case(const LiteralCase& test, Counters& counters)
{
    Destination destination;
    Palette palette;
    Stream stream;
    initialize_guards(destination, 0x31);
    initialize_guards(palette, 0x52);
    initialize_guards(stream, 0x73);

    // Packed pixels are genuine unsigned-short elements throughout the call.
    destination.data.fill(0xcdef);
    palette.data.fill(0x4567);
    palette.data[0] = 0x0000;
    palette.data[1] = 0x1234;
    palette.data[2] = 0x5678;
    palette.data[3] = 0xffff;
    palette.data[4] = 0x0001;
    palette.data[127] = 0x7fff;
    palette.data[128] = 0x8000;
    palette.data[255] = 0xabcd;
    stream.data.fill(0xe3);
    for (std::size_t i = 0; i < test.packet_size; ++i)
    {
        stream.data[i] = test.packet[i];
    }

    // Literal output spans determine bytes independently of packet processing.
    std::array<unsigned char, destination_slots * 2> expected{};
    for (std::size_t slot = 0; slot < destination_slots; ++slot)
    {
        expected[slot * 2] = 0xef;
        expected[slot * 2 + 1] = 0xcd;
    }
    for (std::size_t i = 0; i < test.span_count; ++i)
    {
        const auto& span = test.spans[i];
        for (std::size_t offset = 0; offset < span.count; ++offset)
        {
            const std::size_t slot = span.first + offset;
            expected[slot * 2] = static_cast<unsigned char>(span.value);
            expected[slot * 2 + 1] = static_cast<unsigned char>(span.value >> 8);
        }
    }
    const auto leading = destination.before;
    const auto trailing = destination.after;
    const auto palette_before = palette;
    const auto stream_before = stream;
    const auto& a = test.arguments;

    ++counters.cases;
    ++counters.calls;
    ++counters.branches[static_cast<std::size_t>(test.branch)];
    ResourceRaster1Packed(destination.data.data(),
        stream.data.data(),
        palette.data.data(),
        a.stride,
        a.x,
        a.y,
        a.width,
        a.height,
        a.left,
        a.top,
        a.right,
        a.bottom);

    return compare_bytes(test.name,
               "destination leading guard",
               destination.before.data(),
               leading.data(),
               guard_bytes,
               counters) &&
           compare_bytes(test.name,
               "destination trailing guard",
               destination.after.data(),
               trailing.data(),
               guard_bytes,
               counters) &&
           preserved(test.name, "palette", palette, palette_before, counters) &&
           preserved(test.name, "source", stream, stream_before, counters) &&
           compare_bytes(test.name,
               "destination",
               reinterpret_cast<const unsigned char*>(destination.data.data()),
               expected.data(),
               expected.size(),
               counters);
}
} // namespace

int main()
{
    Counters counters;
    for (const auto& test : literal_cases)
    {
        if (!run_case(test, counters))
        {
            return 2;
        }
    }
    std::printf("resource raster packed: %zu cases, %zu calls, %zu full, %zu partial, "
                "%zu positive-left, %zu byte checks\n",
        counters.cases,
        counters.calls,
        counters.branches[0],
        counters.branches[1],
        counters.branches[2],
        counters.byte_checks);
    return 0;
}
