#include "renderer_queue_test_state.h"
#include "startup_screen_renderer.h"
#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <span>
#include <type_traits>

namespace
{
struct RendererFixtureFailure
{
};

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
    renderer_owner_test::Bootstrap renderer_bootstrap;
    std::array<unsigned char, 32> before;
    ScreenRequestRenderer renderer{renderer_bootstrap.video(), 8, 4, 1};
    std::array<unsigned char, 32> after;
    renderer_owner_test::RestoreOwnedState renderer_restore{renderer, renderer_bootstrap};
};

const char* current_case;
std::size_t sessions, calls, checks;

[[noreturn]] void fail(const char* reason)
{
    std::fprintf(stderr, "case %s: %s\n", current_case, reason);
    throw RendererFixtureFailure{};
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
// Genuine renderer ownership is restored before destruction; these are non-owning observations.
bool same_prefix(const ScreenRequestRenderer& a, const renderer_queue_test::Snapshot& b)
{
    auto expected = b;
    // Queue size, value order and count are checked separately against literals.
    expected.dirty_rectangles.values = a.dirty_rectangles.values;
    expected.dirty_rectangle_count = a.dirty_rectangle_count;
    return renderer_queue_test::equal(a, expected);
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
    renderer_queue_test::initialize(r, 0x12);
    r.software_mode = 0xc1;
    r.pixels_1c = pixels;
    r.surface_storage.assign(surfaces, surfaces + 1);
    r.dirty_rectangles.values.clear();
    r.dirty_rectangle_count = 0;
}

void verify(const Frame& frame,
    const renderer_queue_test::Snapshot& before,
    std::span<const Rectangle> expected)
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
            throw RendererFixtureFailure{};
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
    const renderer_queue_test::Snapshot before = r;
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
    const renderer_queue_test::Snapshot before = frame.renderer;
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
    try
    {
        for (const auto& literal : literals)
        {
            run(literal);
        }
        run_growth();
        check(sessions == 37, "session count differs");
        check(calls == 49, "top-level call count differs");
        std::printf("renderer rectangle queue: %zu sessions, %zu calls, %zu checks\n",
            sessions,
            calls,
            checks);
        return 0;
    }
    catch (const RendererFixtureFailure&)
    {
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "renderer fixture host failure: %s\n", error.what());
        return 3;
    }
}
