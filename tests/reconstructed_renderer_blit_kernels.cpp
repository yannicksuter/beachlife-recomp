#include "indexed_palette_blit.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

static_assert(CHAR_BIT == 8);
static_assert(sizeof(unsigned short) == 2 && std::numeric_limits<unsigned short>::digits == 16);
static_assert(sizeof(int) == 4 && std::numeric_limits<int>::digits == 31);
static_assert(sizeof(unsigned int) == 4 && std::numeric_limits<unsigned int>::digits == 32);

namespace
{
constexpr std::size_t guard_size = 8;
constexpr std::size_t source_size = 16;

enum class Kernel
{
    transparent,
    packed,
    masked
};

struct Arguments
{
    int source_stride;
    int destination_stride_or_pitch;
    int source_x;
    int source_y;
    int destination_x;
    int destination_y;
    int width;
    int height;
    unsigned int mask = 0;
};

struct ExpectedWord
{
    std::size_t index;
    unsigned short value;
};

template <typename T, std::size_t N> struct Guarded
{
    std::array<T, guard_size> before;
    std::array<T, N> data;
    std::array<T, guard_size> after;
};

const char* case_id = "setup";
std::size_t cases = 0;
std::size_t calls = 0;
std::size_t checks = 0;
std::array<std::size_t, 3> kernel_calls{};

[[noreturn]] void fail(const char* reason, std::size_t index)
{
    std::fprintf(stderr, "case %s: %s at %zu\n", case_id, reason, index);
    std::exit(2);
}

void require(bool condition, const char* reason, std::size_t index = 0)
{
    ++checks;
    if (!condition)
    {
        fail(reason, index);
    }
}

template <typename T, std::size_t N>
void compare_array(
    const std::array<T, N>& actual, const std::array<T, N>& expected, const char* reason)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        require(actual[i] == expected[i], reason, i);
    }
}

template <typename T, std::size_t N>
void preserved(const Guarded<T, N>& actual, const Guarded<T, N>& expected, const char* reason)
{
    compare_array(actual.before, expected.before, "leading guard differs");
    compare_array(actual.data, expected.data, reason);
    compare_array(actual.after, expected.after, "trailing guard differs");
}

Guarded<unsigned char, source_size> make_source(std::initializer_list<unsigned char> values)
{
    Guarded<unsigned char, source_size> source{};
    source.before.fill(0xa5);
    source.data.fill(0xcc);
    source.after.fill(0x5a);
    if (values.size() > source.data.size())
    {
        fail("fixture source exceeds its live array", values.size());
    }
    std::size_t index = 0;
    for (const auto value : values)
    {
        source.data[index++] = value;
    }
    return source;
}

Guarded<unsigned short, 256> make_palette()
{
    Guarded<unsigned short, 256> palette{};
    palette.before.fill(0x1357);
    palette.after.fill(0x2468);
    palette.data.fill(0xa55a);
    palette.data[0] = 0xdead;
    palette.data[1] = 0x1234;
    palette.data[2] = 0xabcd;
    palette.data[3] = 0x8001;
    palette.data[4] = 0x00ff;
    palette.data[255] = 0xfedc;
    return palette;
}

template <std::size_t N> Guarded<unsigned short, N> make_destination()
{
    Guarded<unsigned short, N> destination{};
    destination.before.fill(0x89ab);
    destination.data.fill(0xa55a);
    destination.after.fill(0xcdef);
    return destination;
}

template <std::size_t N>
void set_expected(Guarded<unsigned short, N>& expected, std::initializer_list<ExpectedWord> changes)
{
    for (const auto& change : changes)
    {
        if (change.index >= expected.data.size())
        {
            fail("fixture expected index exceeds its live array", change.index);
        }
        expected.data[change.index] = change.value;
    }
}

void invoke(Kernel kernel,
    const Arguments& a,
    const unsigned char* source,
    unsigned short* destination,
    const unsigned short* palette)
{
    switch (kernel)
    {
    case Kernel::transparent:
        IndexedPaletteBlitTransparent(source,
            a.source_stride,
            destination,
            a.destination_stride_or_pitch,
            a.source_x,
            a.source_y,
            a.destination_x,
            a.destination_y,
            a.width,
            a.height,
            palette);
        break;
    case Kernel::packed:
        IndexedPaletteBlitPacked(source,
            a.source_stride,
            destination,
            static_cast<unsigned int>(a.destination_stride_or_pitch),
            a.source_x,
            a.source_y,
            a.destination_x,
            a.destination_y,
            a.width,
            a.height,
            palette);
        break;
    case Kernel::masked:
        IndexedPaletteBlitMasked(source,
            a.source_stride,
            destination,
            a.destination_stride_or_pitch,
            a.source_x,
            a.source_y,
            a.destination_x,
            a.destination_y,
            a.width,
            a.height,
            palette,
            a.mask);
        break;
    }
    ++calls;
    ++kernel_calls[static_cast<std::size_t>(kernel)];
}

template <std::size_t N>
void run(const char* id,
    Kernel kernel,
    const Arguments& arguments,
    std::initializer_list<unsigned char> input,
    std::initializer_list<ExpectedWord> changes,
    bool null_data = false)
{
    case_id = id;
    auto source = make_source(input);
    auto palette = make_palette();
    auto destination = make_destination<N>();
    const auto source_before = source;
    const auto palette_before = palette;
    auto expected = destination;
    set_expected(expected, changes);

    invoke(kernel,
        arguments,
        null_data ? nullptr : source.data.data(),
        null_data ? nullptr : destination.data.data(),
        null_data ? nullptr : palette.data.data());

    preserved(source, source_before, "source byte differs");
    preserved(palette, palette_before, "palette halfword differs");
    preserved(destination, expected, "destination halfword differs");
    ++cases;
}

void run_alias(
    const char* id, Kernel kernel, unsigned int mask, std::initializer_list<ExpectedWord> changes)
{
    case_id = id;
    auto source = make_source({1, 2});
    auto shared = make_palette();
    shared.data[0] = 0x7000;
    shared.data[1] = 0x1357;
    shared.data[2] = 0x2468;
    shared.data[3] = 0x369a;
    shared.data[4] = 0x48bc;
    // The shared array follows its own literal alias setup, not the normal palette.
    shared.data[255] = 0xa55a;
    const auto source_before = source;
    auto expected = shared;
    set_expected(expected, changes);

    // First output replaces palette[2]; the second lookup must read that new word.
    invoke(kernel,
        {2, 0, 0, 0, 0, 0, 2, 1, mask},
        source.data.data(),
        shared.data.data() + 2,
        shared.data.data());

    preserved(source, source_before, "source byte differs");
    preserved(shared, expected, "shared palette/destination halfword differs");
    ++cases;
}

void transparent_cases()
{
    constexpr auto kernel = Kernel::transparent;
    run<16>("T0",
        kernel,
        {5, 6, 0, 0, 0, 0, 5, 1},
        {0, 1, 255, 2, 0},
        {{2, 0x1234}, {4, 0xfedc}, {6, 0xabcd}});
    run<32>("T1",
        kernel,
        {4, 5, 1, 1, 2, 1, 2, 2},
        {9, 1, 0, 9, 9, 2, 3, 9, 9, 255, 4, 9},
        {{14, 0xabcd}, {16, 0x8001}, {24, 0xfedc}, {26, 0x00ff}});
    run<8>("T2", kernel, {2, 0, 0, 0, 0, 0, 2, 2}, {1, 2, 3, 4}, {{0, 0x1234}, {2, 0xabcd}});
    run<1>("T3", kernel, {2, 3, 0, 0, 0, 0, 0, 3}, {}, {}, true);
    run<1>("T4", kernel, {2, 3, 0, 0, 0, 0, 3, 0}, {}, {}, true);
    run_alias("T5", kernel, 0, {{2, 0x1357}, {4, 0x1357}});
}

void packed_cases()
{
    constexpr auto kernel = Kernel::packed;
    run<8>("P0",
        kernel,
        {5, 12, 0, 0, 0, 0, 5, 1},
        {0, 1, 255, 2, 0},
        {{1, 0x1234}, {2, 0xfedc}, {3, 0xabcd}});
    run<16>("P1",
        kernel,
        {4, 11, 1, 1, 2, 12345, 2, 2},
        {9, 1, 0, 9, 9, 2, 3, 9, 9, 255, 4, 9},
        {{2, 0xabcd}, {3, 0x8001}, {7, 0xfedc}, {8, 0x00ff}});
    run<4>("P2", kernel, {2, 1, 0, 0, 0, 0, 2, 2}, {1, 2, 3, 4}, {{0, 0x1234}, {1, 0xabcd}});
    run<1>("P3", kernel, {2, 7, 0, 0, 0, 0, 0, 3}, {}, {}, true);
    run<1>("P4", kernel, {2, 7, 0, 0, 0, 0, 3, 0}, {}, {}, true);
    run_alias("P5", kernel, 0, {{2, 0x1357}, {3, 0x1357}});
    run<8>("P6", kernel, {1, 6, 0, 0, 1, INT_MIN, 1, 1}, {255}, {{1, 0xfedc}});
    run<8>("P7", kernel, {1, 6, 0, 0, 1, INT_MAX, 1, 1}, {255}, {{1, 0xfedc}});
}

void masked_cases()
{
    constexpr auto kernel = Kernel::masked;
    run<16>("M0",
        kernel,
        {5, 6, 0, 0, 0, 0, 5, 1, 0xabcd0f0fU},
        {0, 1, 255, 2, 0},
        {{2, 0x0204}, {4, 0x0e0c}, {6, 0x0b0d}});
    run<32>("M1",
        kernel,
        {4, 5, 1, 1, 2, 1, 2, 2, 0x0000ffffU},
        {9, 1, 0, 9, 9, 2, 3, 9, 9, 255, 4, 9},
        {{14, 0xabcd}, {16, 0x8001}, {24, 0xfedc}, {26, 0x00ff}});
    run<8>("M2",
        kernel,
        {2, 0, 0, 0, 0, 0, 2, 2, 0x000000ffU},
        {1, 2, 3, 4},
        {{0, 0x0034}, {2, 0x00cd}});
    run<1>("M3", kernel, {2, 3, 0, 0, 0, 0, 0, 3, 0xffffffffU}, {}, {}, true);
    run<1>("M4", kernel, {2, 3, 0, 0, 0, 0, 3, 0, 0x00000000U}, {}, {}, true);
    run_alias("M5", kernel, 0x00000fffU, {{2, 0x0357}, {4, 0x0357}});
    run<8>("M6",
        kernel,
        {3, 3, 0, 0, 0, 0, 3, 1, 0x00000000U},
        {0, 1, 255},
        {{2, 0x0000}, {4, 0x0000}});
    run<8>("M7",
        kernel,
        {3, 3, 0, 0, 0, 0, 3, 1, 0xffff0000U},
        {0, 1, 255},
        {{2, 0x0000}, {4, 0x0000}});
}
} // namespace

int main()
{
    transparent_cases();
    packed_cases();
    masked_cases();
    case_id = "final";
    require(cases == 22, "case count differs");
    require(calls == 22, "call count differs");
    require(kernel_calls[0] == 6, "transparent call count differs");
    require(kernel_calls[1] == 8, "packed call count differs");
    require(kernel_calls[2] == 8, "masked call count differs");
    std::printf("renderer blit kernels: %zu cases, %zu calls, %zu checks\n", cases, calls, checks);
    return 0;
}
