#include "startup_screen_renderer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && INT_MAX == 2147483647);
static_assert(sizeof(unsigned int) == 4);

namespace
{
using Rectangle = ScreenRectangle;
struct Literal
{
    RenderRectangle clip;
    int mode;
    std::array<unsigned char, 2> flags;
    std::array<unsigned char, 3> publication;
    std::array<unsigned int, 6> alias;
    std::array<int, 6> initial;
    std::array<int, 6> expected;
    bool result;
};

constexpr std::array<Literal, 38> literals = {{
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {110, 30, 0, 0, 5, 10},
        {110, 30, 0, 0, 0, 10},
        true}, // right boundary equality
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {111, 30, 0, 0, 5, 10},
        {111, 30, 0, 0, 5, 10},
        false}, // right boundary rejection
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {30, 100, 0, 0, 10, 5},
        {30, 100, 0, 0, 10, 0},
        true}, // bottom boundary equality
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {30, 101, 0, 0, 10, 5},
        {30, 101, 0, 0, 10, 5},
        false}, // bottom boundary rejection
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {0, 30, 0, 0, 10, 10},
        {10, 30, 10, 0, 10, 10},
        true}, // left endpoint equality
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {-1, 30, 0, 0, 10, 10},
        {-1, 30, 0, 0, 10, 10},
        false}, // left endpoint rejection
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {30, 0, 0, 0, 10, 20},
        {30, 20, 0, 20, 10, 20},
        true}, // top endpoint equality
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {30, -1, 0, 0, 10, 20},
        {30, -1, 0, 0, 10, 20},
        false}, // top endpoint rejection
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {30, 40, 2, 3, 12, 13},
        {30, 40, 2, 3, 12, 13},
        true}, // unchanged interior
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {5, 40, 2, 3, 22, 13},
        {10, 40, 7, 3, 22, 13},
        true}, // left clip
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {105, 40, 2, 3, 22, 13},
        {105, 40, 2, 3, 7, 13},
        true}, // right clip
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {30, 15, 2, 3, 12, 23},
        {30, 20, 2, 8, 12, 23},
        true}, // top clip
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {30, 95, 2, 3, 12, 23},
        {30, 95, 2, 3, 12, 8},
        true}, // bottom clip
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {0, 0, 5, 7, 205, 207},
        {10, 20, 15, 27, 115, 107},
        true}, // all four clipping writes
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {30, 40, 2, 3, 2, 3},
        {30, 40, 2, 3, 2, 3},
        true}, // zero extents inside
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {110, 100, 2, 3, 2, 3},
        {110, 100, 2, 3, 2, 3},
        true}, // zero extents at corner
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {30, 40, 10, 3, 5, 13},
        {30, 40, 10, 3, 5, 13},
        true}, // inverted horizontal source words
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {30, 40, 2, 10, 12, 5},
        {30, 40, 2, 10, 12, 5},
        true}, // inverted vertical source words
    {{10, 20, 5, 9},
        0,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {11, 22, 3, 5, 8, 10},
        {11, 22, 3, 5, 4, 7},
        true}, // logical half of odd dimensions
    {{10, 20, 3, 5},
        2,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {14, 29, 0, 0, 4, 4},
        {14, 29, 0, 0, 2, 1},
        true}, // doubled dimensions
    {{10, 20, 3, 5},
        0,
        {1, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {12, 24, 0, 0, 4, 4},
        {12, 24, 0, 0, 1, 1},
        true}, // unscaled flag short circuit
    {{10, 20, 3, 5},
        2,
        {1, 1},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {12, 24, 0, 0, 4, 4},
        {12, 24, 0, 0, 1, 1},
        true}, // unscaled flag later branch
    {{10, 20, 3, 5},
        7,
        {255, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {12, 24, 0, 0, 4, 4},
        {12, 24, 0, 0, 1, 1},
        true}, // unscaled arbitrary mode without mode read
    {{10, 20, 3, 5},
        -1,
        {1, 255},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {12, 24, 0, 0, 4, 4},
        {12, 24, 0, 0, 1, 1},
        true}, // unscaled arbitrary mode after mode read
    {{0, 0, -1, -2},
        0,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {100, 200, 0, 0, 20, 30},
        {100, 200, 0, 0, 20, 30},
        true}, // logical half of high-bit extents
    {{-10, -20, 20, 40},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {-15, -25, 0, 0, 40, 60},
        {-10, -20, 5, 5, 25, 45},
        true}, // negative clip origins
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 0, 3, 4, 5},
        {5, 30, 777, 0, 15, 10},
        {15, 30, 777, 0, 15, 10},
        true}, // destination x aliases source left
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 0, 5},
        {5, 30, 0, 0, 777, 10},
        {10, 30, 5, 0, 777, 10},
        true}, // destination x aliases source right
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 1, 4, 5},
        {30, 15, 0, 777, 10, 25},
        {30, 25, 0, 777, 10, 25},
        true}, // destination y aliases source top
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 0, 2, 3, 4, 5},
        {5, 777, 0, 0, 40, 40},
        {20, 777, 5, 10, 40, 40},
        true}, // destination axes alias
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {111, 2147483647, (-2147483647 - 1), 2147483647, 2147483647, (-2147483647 - 1)},
        {111, 2147483647, (-2147483647 - 1), 2147483647, 2147483647, (-2147483647 - 1)},
        false}, // first rejection skips unsafe arithmetic
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {30, 101, (-2147483647 - 1), 2147483647, 2147483647, (-2147483647 - 1)},
        {30, 101, (-2147483647 - 1), 2147483647, 2147483647, (-2147483647 - 1)},
        false}, // second rejection skips unsafe arithmetic
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 0, 1},
        {0, 1, 2, 3, 4, 5},
        {30, 40, 2, 3, 12, 13},
        {30, 40, 2, 3, 12, 13},
        true}, // disabled publication 001
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 1, 0},
        {0, 1, 2, 3, 4, 5},
        {30, 40, 2, 3, 12, 13},
        {30, 40, 2, 3, 12, 13},
        true}, // disabled publication 010
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {0, 1, 1},
        {0, 1, 2, 3, 4, 5},
        {30, 40, 2, 3, 12, 13},
        {30, 40, 2, 3, 12, 13},
        true}, // disabled publication 011
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {1, 0, 0},
        {0, 1, 2, 3, 4, 5},
        {30, 40, 2, 3, 12, 13},
        {30, 40, 2, 3, 12, 13},
        true}, // disabled publication 100
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {1, 1, 0},
        {0, 1, 2, 3, 4, 5},
        {30, 40, 2, 3, 12, 13},
        {30, 40, 2, 3, 12, 13},
        true}, // disabled publication 110
    {{10, 20, 100, 80},
        1,
        {0, 0},
        {1, 1, 1},
        {0, 1, 2, 3, 4, 5},
        {30, 40, 2, 3, 12, 13},
        {30, 40, 2, 3, 12, 13},
        true}, // disabled publication 111
}};

// These change only inputs: expected coordinate words and queue values below
// remain independent literals. Nonzero publication bytes are deliberately not 1.
constexpr Literal published(Literal literal)
{
    literal.publication = {255, 0, 3};
    return literal;
}

constexpr Literal overlap_call = {{0, 0, 100, 100},
    1,
    {0, 0},
    {255, 0, 3},
    {0, 1, 2, 3, 4, 5},
    {7, 7, 0, 0, 7, 4},
    {7, 7, 0, 0, 7, 4},
    true};
constexpr Literal left_call = {{0, 0, 100, 100},
    1,
    {0, 0},
    {255, 0, 3},
    {0, 1, 2, 3, 4, 5},
    {3, 6, 0, 0, 4, 3},
    {3, 6, 0, 0, 4, 3},
    true};

struct PublishedLiteral
{
    const char* name;
    Literal call;
    std::initializer_list<Rectangle> seed;
    std::initializer_list<Rectangle> expected;
};

const PublishedLiteral publication_literals[] = {
    {"clipped-bounds", published(literals[13]), {}, {{10, 20, 110, 100}}},
    {"right-reject-keeps-queue",
        published(literals[1]),
        {{20, 20, 30, 30}, {200, 200, 210, 210}},
        {{20, 20, 30, 30}, {200, 200, 210, 210}}},
    {"bottom-reject-keeps-queue", published(literals[3]), {{20, 20, 30, 30}}, {{20, 20, 30, 30}}},
    {"equality-publishes-zero-width", published(literals[0]), {}, {{110, 30, 110, 40}}},
    {"alias-left-publishes-reloaded-words", published(literals[26]), {}, {{15, 30, 15, 40}}},
    {"alias-axes-publishes-after-both-writes", published(literals[29]), {}, {{20, 20, 55, 50}}},
    {"logical-half-high-bit-extents", published(literals[24]), {}, {{100, 200, 120, 230}}},
    {"active-unscaled-arbitrary-mode", published(literals[22]), {}, {{12, 24, 13, 25}}},
    {"contained-keeps-order",
        published(literals[8]),
        {{20, 30, 50, 60}, {200, 200, 210, 210}},
        {{20, 30, 50, 60}, {200, 200, 210, 210}}},
    {"cover-removes-swapped-last",
        published(literals[13]),
        {{30, 30, 40, 40}, {200, 200, 210, 210}, {50, 50, 60, 60}},
        {{200, 200, 210, 210}, {10, 20, 110, 100}}},
    {"right-before-bottom",
        published(literals[8]),
        {{25, 35, 35, 45}},
        {{25, 35, 35, 45}, {35, 40, 40, 50}, {30, 45, 35, 50}}},
    {"first-overlap-depth-first",
        overlap_call,
        {{5, 5, 10, 10}, {11, 8, 16, 12}},
        {{5, 5, 10, 10}, {11, 8, 16, 12}, {10, 7, 11, 11}, {11, 7, 14, 8}, {7, 10, 10, 11}}},
    {"reversed-first-overlap",
        overlap_call,
        {{11, 8, 16, 12}, {5, 5, 10, 10}},
        {{11, 8, 16, 12}, {5, 5, 10, 10}, {10, 7, 11, 11}, {7, 10, 10, 11}, {11, 7, 14, 8}}},
    {"left-fallback-retains-incoming-bottom",
        left_call,
        {{5, 5, 10, 10}},
        {{5, 5, 10, 10}, {3, 6, 5, 9}}},
};

struct Frame
{
    std::array<unsigned char, 32> before;
    ScreenRequestRenderer renderer;
    std::array<unsigned char, 32> after;
};

struct Words
{
    std::array<unsigned char, 32> before;
    std::array<int, 6> value;
    std::array<unsigned char, 32> after;
};

std::size_t case_index, sessions, calls, checks;

[[noreturn]] void fail(const char* reason)
{
    std::fprintf(stderr, "case %zu: %s\n", case_index, reason);
    std::exit(2);
}

void check(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        fail(reason);
    }
}

bool same_rectangle(const Rectangle& a, const Rectangle& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
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

void verify_queue(const ScreenRequestRenderer& renderer, std::initializer_list<Rectangle> expected)
{
    check(
        renderer.dirty_rectangle_count == static_cast<int>(expected.size()), "dirty count differs");
    check(renderer.dirty_rectangles.values.size() == expected.size(), "queue size differs");
    std::size_t index = 0;
    for (const Rectangle& rectangle : expected)
    {
        check(same_rectangle(renderer.dirty_rectangles.At(static_cast<int>(index)), rectangle),
            "queue rectangle or order differs");
        ++index;
    }
}

void call(Frame& frame, const Literal& literal, std::initializer_list<Rectangle> expected)
{
    auto& renderer = frame.renderer;
    renderer.clip_rectangle = literal.clip;
    renderer.clip_mode_54 = literal.mode;
    renderer.unscaled_clip_5c = literal.flags[0];
    renderer.clip_flag_5d = literal.flags[1];
    renderer.rectangle_enabled_4e = literal.publication[0];
    renderer.rectangle_suppressed_4d = literal.publication[1];
    renderer.rectangle_ready_4f = literal.publication[2];
    // A typed copy owns independent native queue storage. Never copy or inspect
    // the std::deque representation, renderer padding, or unconstructed objects.
    const ScreenRequestRenderer before = renderer;
    Words words{};
    words.before.fill(0x69);
    words.after.fill(0x96);
    words.value = literal.initial;
    const auto& at = literal.alias;
    const bool result = renderer.ClipRectangle(words.value[at[0]],
        words.value[at[1]],
        words.value[at[2]],
        words.value[at[3]],
        words.value[at[4]],
        words.value[at[5]]);
    ++calls;
    check(result == literal.result, "return differs");
    for (std::size_t i = 0; i < words.value.size(); ++i)
    {
        check(words.value[i] == literal.expected[i], "coordinate differs");
    }
    for (std::size_t i = 0; i < words.before.size(); ++i)
    {
        check(words.before[i] == 0x69, "leading word guard differs");
        check(words.after[i] == 0x96, "trailing word guard differs");
        check(frame.before[i] == 0xa5, "leading renderer guard differs");
        check(frame.after[i] == 0x5a, "trailing renderer guard differs");
    }
    check(same_prefix(renderer, before), "unrelated renderer member changed");
    verify_queue(renderer, expected);
    ++case_index;
}

void run(const Literal& literal,
    std::initializer_list<Rectangle> seed,
    std::initializer_list<Rectangle> expected)
{
    ++sessions;
    unsigned short pixels[2] = {0x1234, 0xabcd};
    ScreenSurfacePosition surface{};
    ScreenSurfacePosition* surfaces[1] = {&surface};
    Frame frame{};
    initialize(frame, pixels, surfaces);
    frame.renderer.dirty_rectangles.values = seed;
    frame.renderer.dirty_rectangle_count = static_cast<int>(seed.size());
    call(frame, literal, expected);
    check(pixels[0] == 0x1234 && pixels[1] == 0xabcd && surfaces[0] == &surface,
        "borrowed storage changed");
}

void run_repeated()
{
    ++sessions;
    unsigned short pixels[2] = {0x1234, 0xabcd};
    ScreenSurfacePosition surface{};
    ScreenSurfacePosition* surfaces[1] = {&surface};
    Frame frame{};
    initialize(frame, pixels, surfaces);
    call(frame, published(literals[8]), {{30, 40, 40, 50}});
    call(frame, published(literals[8]), {{30, 40, 40, 50}});
    call(frame, published(literals[13]), {{10, 20, 110, 100}});
    check(pixels[0] == 0x1234 && pixels[1] == 0xabcd && surfaces[0] == &surface,
        "borrowed storage changed");
}
} // namespace

int main()
{
    // The accepted 38 literal argument/alias cases are unchanged. Seed live
    // dirty values to ensure every suppressing combination preserves storage.
    for (const Literal& literal : literals)
    {
        run(literal,
            {{-40, -40, -30, -30}, {200, 200, 210, 210}},
            {{-40, -40, -30, -30}, {200, 200, 210, 210}});
    }
    for (const PublishedLiteral& literal : publication_literals)
    {
        run(literal.call, literal.seed, literal.expected);
    }
    run_repeated();
    std::printf(
        "renderer clipping: %zu sessions, %zu calls, %zu checks\n", sessions, calls, checks);
    return 0;
}
