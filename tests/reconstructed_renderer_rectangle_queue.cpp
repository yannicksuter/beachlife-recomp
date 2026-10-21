#include "startup_screen_renderer.h"
#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <span>
#include <type_traits>

namespace
{
using Rectangle = ScreenRectangle;
static_assert(sizeof(int) == 4 && INT_MAX == 2147483647);
static_assert(!std::is_trivially_copyable_v<RendererRectangleQueue>);
static_assert(!std::is_trivially_copyable_v<ScreenRequestRenderer>);
static_assert(std::is_same_v<decltype(&ScreenRequestRenderer::AccumulateRectangle),
    void (ScreenRequestRenderer::*)(Rectangle)>);

struct Literal
{
    const char* name;
    std::initializer_list<Rectangle> seed;
    Rectangle incoming;
    std::initializer_list<Rectangle> expected;
    int alias;
};

// These expected queues are literal observations required by the accepted
// application logic, including its quirks. No geometric oracle runs here.
const Literal literals[] = {
    {"right-fallback", {{5, 5, 10, 10}}, {7, 6, 12, 9}, {{5, 5, 10, 10}, {10, 6, 12, 9}}, -1},
    {"empty", {}, {1, 2, 3, 4}, {{1, 2, 3, 4}}, -1},
    {"duplicate", {{5, 5, 10, 10}}, {5, 5, 10, 10}, {{5, 5, 10, 10}}, -1},
    {"contained", {{5, 5, 10, 10}}, {6, 6, 9, 9}, {{5, 5, 10, 10}}, -1},
    {"swap-last-order",
        {{0, 0, 2, 2}, {20, 0, 22, 2}, {1, 1, 3, 3}, {30, 0, 32, 2}},
        {0, 0, 4, 4},
        {{30, 0, 32, 2}, {20, 0, 22, 2}, {0, 0, 4, 4}},
        -1},
    {"swap-recheck",
        {{6, 6, 7, 7}, {20, 0, 22, 2}, {0, 0, 15, 15}},
        {5, 5, 10, 10},
        {{0, 0, 15, 15}, {20, 0, 22, 2}},
        -1},
    {"remove-all",
        {{6, 6, 7, 7}, {7, 7, 8, 8}, {8, 8, 9, 9}},
        {5, 5, 10, 10},
        {{5, 5, 10, 10}},
        -1},
    {"right-bottom",
        {{5, 5, 10, 10}},
        {7, 7, 12, 12},
        {{5, 5, 10, 10}, {10, 7, 12, 12}, {7, 10, 10, 12}},
        -1},
    {"right-top",
        {{5, 5, 10, 10}},
        {7, 3, 12, 7},
        {{5, 5, 10, 10}, {10, 3, 12, 7}, {7, 3, 10, 5}},
        -1},
    {"right-equal-top", {{5, 5, 10, 10}}, {7, 5, 12, 9}, {{5, 5, 10, 10}, {10, 5, 12, 9}}, -1},
    {"right-spans-height", {{5, 5, 10, 10}}, {7, 3, 12, 12}, {{5, 5, 10, 10}, {7, 3, 12, 12}}, -1},
    {"left-bottom",
        {{5, 5, 10, 10}},
        {3, 7, 7, 12},
        {{5, 5, 10, 10}, {3, 7, 5, 12}, {5, 10, 7, 12}},
        -1},
    {"left-top", {{5, 5, 10, 10}}, {3, 3, 7, 7}, {{5, 5, 10, 10}, {3, 3, 5, 7}, {5, 3, 7, 5}}, -1},
    {"left-fallback", {{5, 5, 10, 10}}, {3, 6, 7, 9}, {{5, 5, 10, 10}, {3, 6, 5, 9}}, -1},
    {"left-equal-top", {{5, 5, 10, 10}}, {3, 5, 7, 12}, {{5, 5, 10, 10}, {3, 5, 7, 12}}, -1},
    {"left-equal-bottom", {{5, 5, 10, 10}}, {3, 10, 7, 12}, {{5, 5, 10, 10}, {3, 10, 7, 12}}, -1},
    {"left-spans-height", {{5, 5, 10, 10}}, {3, 3, 7, 12}, {{5, 5, 10, 10}, {3, 3, 7, 12}}, -1},
    {"bottom-middle", {{5, 5, 10, 10}}, {6, 7, 9, 12}, {{5, 5, 10, 10}, {6, 10, 9, 12}}, -1},
    {"top-middle", {{5, 5, 10, 10}}, {6, 3, 9, 7}, {{5, 5, 10, 10}, {6, 3, 9, 5}}, -1},
    {"bottom-spans-width", {{5, 5, 10, 10}}, {3, 7, 12, 12}, {{5, 5, 10, 10}, {3, 7, 12, 12}}, -1},
    {"top-spans-width", {{5, 5, 10, 10}}, {3, 3, 12, 7}, {{5, 5, 10, 10}, {3, 3, 12, 7}}, -1},
    {"touch-left", {{5, 5, 10, 10}}, {0, 5, 5, 10}, {{5, 5, 10, 10}, {0, 5, 5, 10}}, -1},
    {"touch-right", {{5, 5, 10, 10}}, {10, 5, 15, 10}, {{5, 5, 10, 10}, {10, 5, 15, 10}}, -1},
    {"touch-top", {{5, 5, 10, 10}}, {5, 0, 10, 5}, {{5, 5, 10, 10}, {5, 0, 10, 5}}, -1},
    {"touch-bottom", {{5, 5, 10, 10}}, {5, 10, 10, 15}, {{5, 5, 10, 10}, {5, 10, 10, 15}}, -1},
    {"equal-width-partial", {{5, 5, 10, 10}}, {5, 7, 10, 12}, {{5, 5, 10, 10}, {5, 7, 10, 12}}, -1},
    {"horizontal-cross", {{5, 5, 10, 10}}, {0, 6, 15, 9}, {{5, 5, 10, 10}, {0, 6, 15, 9}}, -1},
    {"vertical-cross", {{5, 5, 10, 10}}, {6, 0, 9, 15}, {{5, 5, 10, 10}, {6, 0, 9, 15}}, -1},
    {"zero-width-empty", {}, {7, 6, 7, 9}, {{7, 6, 7, 9}}, -1},
    {"zero-width-contained", {{5, 5, 10, 10}}, {7, 6, 7, 9}, {{5, 5, 10, 10}}, -1},
    {"zero-height-empty", {}, {6, 7, 9, 7}, {{6, 7, 9, 7}}, -1},
    {"signed-extrema-empty",
        {},
        {INT_MIN, INT_MIN, 2147483647, 2147483647},
        {{INT_MIN, INT_MIN, 2147483647, 2147483647}},
        -1},
    {"signed-extrema-contained",
        {{INT_MIN, INT_MIN, 2147483647, 2147483647}},
        {INT_MIN, 0, 0, 1},
        {{INT_MIN, INT_MIN, 2147483647, 2147483647}},
        -1},
    {"depth-first-first-overlap",
        {{5, 5, 10, 10}, {11, 8, 16, 12}},
        {7, 7, 14, 11},
        {{5, 5, 10, 10}, {11, 8, 16, 12}, {10, 7, 11, 11}, {11, 7, 14, 8}, {7, 10, 10, 11}},
        -1},
    {"depth-first-reversed-seed",
        {{11, 8, 16, 12}, {5, 5, 10, 10}},
        {7, 7, 14, 11},
        {{11, 8, 16, 12}, {5, 5, 10, 10}, {10, 7, 11, 11}, {7, 10, 10, 11}, {11, 7, 14, 8}},
        -1},
    {"by-value-queue-alias",
        {{6, 6, 7, 7}, {20, 0, 22, 2}, {5, 5, 10, 10}},
        {5, 5, 10, 10},
        {{5, 5, 10, 10}, {20, 0, 22, 2}},
        2},
};

constexpr Rectangle growth[] = {{0, 0, 2, 2},
    {4, 0, 6, 2},
    {8, 0, 10, 2},
    {12, 0, 14, 2},
    {16, 0, 18, 2},
    {20, 0, 22, 2},
    {24, 0, 26, 2},
    {28, 0, 30, 2},
    {32, 0, 34, 2},
    {36, 0, 38, 2},
    {40, 0, 42, 2},
    {44, 0, 46, 2}};
constexpr Rectangle covering = {-1, -1, 47, 3};

struct Frame
{
    std::array<unsigned char, 32> before;
    ScreenRequestRenderer renderer;
    std::array<unsigned char, 32> after;
};

const char* current_case;
std::size_t sessions, calls, checks;

[[noreturn]] void fail(const char* reason)
{
    std::fprintf(stderr, "case %s: %s\n", current_case, reason);
    std::exit(2);
}

void check(bool value, const char* reason)
{
    ++checks;
    if (!value)
    {
        fail(reason);
    }
}

bool same_rectangle(const Rectangle& first, const Rectangle& second)
{
    return first.left == second.left && first.top == second.top && first.right == second.right &&
           first.bottom == second.bottom;
}

template <std::size_t N>
bool same_bytes(const unsigned char (&first)[N], const unsigned char (&second)[N])
{
    return std::equal(first, first + N, second);
}

// Compare actual members, never std::deque representation or renderer padding.
// No renderer constructor, surface provider, or GPU behavior is supplied here.
bool same_prefix(const ScreenRequestRenderer& a, const ScreenRequestRenderer& b)
{
#if defined(BEACHLIFE_DECOMP_SURFACE_TABLE)
    if (a.draw != b.draw || !same_bytes(a.unknown_3c, b.unknown_3c) ||
        a.surfaces_end != b.surfaces_end || a.surfaces_capacity != b.surfaces_capacity)
    {
        return false;
    }
#endif
#if defined(BEACHLIFE_DECOMP_SURFACE_PREPARE)
    if (a.fill_color != b.fill_color)
    {
        return false;
    }
#endif
#if defined(BEACHLIFE_DECOMP_RENDERER_SEA_STATE)
    if (a.sea_state_134 != b.sea_state_134)
    {
        return false;
    }
#endif
#if defined(BEACHLIFE_DECOMP_RENDERER_MARKER_BUFFER)
    if (!same_bytes(a.unknown_a8, b.unknown_a8) || a.buffer_130 != b.buffer_130)
    {
        return false;
    }
#endif
    return a.software_mode == b.software_mode && same_bytes(a.unknown_01, b.unknown_01) &&
           a.width == b.width && a.height == b.height && a.clip_rectangle.x == b.clip_rectangle.x &&
           a.clip_rectangle.y == b.clip_rectangle.y &&
           a.clip_rectangle.width == b.clip_rectangle.width &&
           a.clip_rectangle.height == b.clip_rectangle.height && a.pixels_1c == b.pixels_1c &&
           same_bytes(a.unknown_20, b.unknown_20) && a.format_44 == b.format_44 &&
           same_bytes(a.unknown_48, b.unknown_48) &&
           a.rectangle_suppressed_4d == b.rectangle_suppressed_4d &&
           a.rectangle_enabled_4e == b.rectangle_enabled_4e &&
           a.rectangle_ready_4f == b.rectangle_ready_4f && same_bytes(a.unknown_50, b.unknown_50) &&
           a.clip_mode_54 == b.clip_mode_54 && same_bytes(a.unknown_58, b.unknown_58) &&
           a.unscaled_clip_5c == b.unscaled_clip_5c && a.clip_flag_5d == b.clip_flag_5d &&
           same_bytes(a.unknown_5e, b.unknown_5e) && a.word_60 == b.word_60 &&
           same_bytes(a.unknown_64, b.unknown_64) && a.surfaces == b.surfaces &&
#if !defined(BEACHLIFE_DECOMP_SURFACE_TABLE)
           same_bytes(a.unknown_6c, b.unknown_6c) &&
#endif
           a.current_surface == b.current_surface && a.pixels_locked == b.pixels_locked &&
           same_bytes(a.unknown_79, b.unknown_79);
}

template <std::size_t N> void pattern(unsigned char (&bytes)[N], unsigned int seed)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        bytes[i] = static_cast<unsigned char>(seed + i * 17U);
    }
}

void initialize(Frame& frame, unsigned short* pixels, ScreenSurfacePosition** surfaces)
{
    frame.before.fill(0xa5);
    frame.after.fill(0x5a);
    auto& r = frame.renderer;
    r.software_mode = 0xc1;
    pattern(r.unknown_01, 0x12);
    r.width = 800;
    r.height = 600;
    r.clip_rectangle = {-13, 17, 37, 41};
    r.pixels_1c = pixels;
    pattern(r.unknown_20, 0x23);
#if defined(BEACHLIFE_DECOMP_SURFACE_TABLE)
    r.draw = nullptr;
    pattern(r.unknown_3c, 0x23U + 28U * 17U);
#endif
    r.format_44 = 0x12345678U;
    pattern(r.unknown_48, 0x34);
    r.rectangle_suppressed_4d = 0x45;
    r.rectangle_enabled_4e = 0x56;
    r.rectangle_ready_4f = 0x67;
    pattern(r.unknown_50, 0x78);
    r.clip_mode_54 = -29;
    pattern(r.unknown_58, 0x89);
    r.unscaled_clip_5c = 0x9a;
    r.clip_flag_5d = 0xab;
    pattern(r.unknown_5e, 0xbc);
    r.word_60 = std::bit_cast<unsigned int>(std::array<unsigned char, 4>{0xde, 0xef, 0x00, 0x11});
    pattern(r.unknown_64, 0x22);
    r.surfaces = surfaces;
#if defined(BEACHLIFE_DECOMP_SURFACE_TABLE)
    r.surfaces_end = nullptr;
    r.surfaces_capacity = nullptr;
#else
    pattern(r.unknown_6c, 0xcd);
#endif
    r.current_surface = -37;
    r.pixels_locked = 0xde;
    pattern(r.unknown_79, 0xef);
#if defined(BEACHLIFE_DECOMP_SURFACE_PREPARE)
    r.fill_color = std::bit_cast<unsigned short>(std::array<unsigned char, 2>{0x00, 0x11});
#endif
    // Aggregate initialization already constructed the real native deque.
    // Only this generated setup seeds queue values and synchronizes its count.
    r.dirty_rectangle_count = 0;
#if defined(BEACHLIFE_DECOMP_RENDERER_MARKER_BUFFER)
    pattern(r.unknown_a8, 0x91);
    r.buffer_130 = nullptr;
#endif
#if defined(BEACHLIFE_DECOMP_RENDERER_SEA_STATE)
    r.sea_state_134 = nullptr;
#endif
}

void verify(
    const Frame& frame, const ScreenRequestRenderer& before, std::span<const Rectangle> expected)
{
    const auto& r = frame.renderer;
    check(r.dirty_rectangle_count == static_cast<int>(expected.size()), "dirty count differs");
    check(r.dirty_rectangles.values.size() == expected.size(), "queue size differs");
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        ++checks;
        if (!same_rectangle(r.dirty_rectangles.At(static_cast<int>(i)), expected[i]))
        {
            std::fprintf(stderr, "case %s: queue rectangle differs at %zu\n", current_case, i);
            std::exit(2);
        }
    }
    check(same_prefix(r, before), "unrelated renderer member changed");
    check(std::all_of(frame.before.begin(),
              frame.before.end(),
              [](unsigned char b)
              {
                  return b == 0xa5;
              }),
        "leading guard changed");
    check(std::all_of(frame.after.begin(),
              frame.after.end(),
              [](unsigned char b)
              {
                  return b == 0x5a;
              }),
        "trailing guard changed");
}

void run(const Literal& literal)
{
    current_case = literal.name;
    ++sessions;
    unsigned short pixels[2] = {0x1234, 0xabcd};
    ScreenSurfacePosition surface{};
    ScreenSurfacePosition* surfaces[1] = {&surface};
    Frame frame{};
    initialize(frame, pixels, surfaces);
    auto& r = frame.renderer;
    for (const auto& value : literal.seed)
    {
        r.dirty_rectangles.PushBack(value);
    }
    r.dirty_rectangle_count = static_cast<int>(literal.seed.size());
    // A genuine typed copy creates independent native storage for the snapshot.
    const ScreenRequestRenderer before = r;
    Rectangle incoming = literal.incoming;
    ++calls;
    if (literal.alias >= 0)
    {
        // This is a by-value argument. The source queue element may be popped
        // during the call; no reference to it is used afterward.
        r.AccumulateRectangle(r.dirty_rectangles.At(literal.alias));
    }
    else
    {
        r.AccumulateRectangle(incoming);
        check(same_rectangle(incoming, literal.incoming), "incoming value changed");
    }
    verify(frame, before, {literal.expected.begin(), literal.expected.size()});
    check(pixels[0] == 0x1234 && pixels[1] == 0xabcd && surfaces[0] == &surface,
        "borrowed storage changed");
}

void run_growth()
{
    current_case = "growth-and-remove";
    ++sessions;
    unsigned short pixels[2] = {0x1234, 0xabcd};
    ScreenSurfacePosition surface{};
    ScreenSurfacePosition* surfaces[1] = {&surface};
    Frame frame{};
    initialize(frame, pixels, surfaces);
    const ScreenRequestRenderer before = frame.renderer;
    for (std::size_t i = 0; i < std::size(growth); ++i)
    {
        ++calls;
        frame.renderer.AccumulateRectangle(growth[i]);
        verify(frame, before, {growth, i + 1});
    }
    ++calls;
    frame.renderer.AccumulateRectangle(covering);
    verify(frame, before, {&covering, 1});
    check(pixels[0] == 0x1234 && pixels[1] == 0xabcd && surfaces[0] == &surface,
        "borrowed storage changed");
}
} // namespace

int main()
{
    for (const auto& literal : literals)
    {
        run(literal);
    }
    run_growth();
    check(sessions == 37, "session count differs");
    check(calls == 49, "top-level call count differs");
    std::printf(
        "renderer rectangle queue: %zu sessions, %zu calls, %zu checks\n", sessions, calls, checks);
    return 0;
}
