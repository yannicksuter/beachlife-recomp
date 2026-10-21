#include "renderer_queue_test_state.h"
#include "startup_screen_clip.h"
#include "startup_screen_renderer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <type_traits>

static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && sizeof(unsigned int) == 4);
static_assert(INT_MIN == (-2147483647 - 1) && INT_MAX == 2147483647);
static_assert(static_cast<int>(0xffffffffU) == -1);
static_assert(std::is_aggregate_v<ScreenRequestRenderer>);
static_assert(!std::is_polymorphic_v<ScreenRequestRenderer>);

namespace
{
using Rectangle = ScreenRectangle;
using Renderer = ScreenRequestRenderer;

struct Failure
{
    const char* name;
    const char* message;
};

struct Counters
{
    std::size_t calls = 0;
    std::size_t checks = 0;
    std::size_t aliases = 0;
    unsigned int groups = 0;
} counters;

void require(bool condition, const char* name, const char* message)
{
    ++counters.checks;
    if (!condition)
    {
        // After the retail call only: ordinary stack unwinding retains real
        // deque cleanup and restores the borrowed global even for the control.
        throw Failure{name, message};
    }
}

template <class T> struct Guarded
{
    std::array<unsigned char, 16> before{};
    T value{};
    std::array<unsigned char, 16> after{};

    Guarded()
    {
        before.fill(0x36);
        after.fill(0xc9);
    }

    bool guards_intact() const
    {
        return std::all_of(before.begin(),
                   before.end(),
                   [](auto b)
                   {
                       return b == 0x36;
                   }) &&
               std::all_of(after.begin(),
                   after.end(),
                   [](auto b)
                   {
                       return b == 0xc9;
                   });
    }
};

struct RendererBinding
{
    Renderer* previous;

    explicit RendererBinding(Renderer& value)
        : previous(screen_request_renderer)
    {
        screen_request_renderer = &value;
    }

    ~RendererBinding()
    {
        screen_request_renderer = previous;
    }
};

struct Literal
{
    const char* name;
    unsigned int group;
    unsigned int width;
    unsigned int height;
    Rectangle destination;
    Rectangle source;
    Rectangle expected_destination;
    Rectangle expected_source;
    bool alias;
};

// Fixed results, not another clipping algorithm. Every add/subtract is
// representable in int, including the explicit signed-bound cases. Aliases
// name the same live Rectangle; no partial-overlap object or raw lifetime.
constexpr std::array<Literal, 13> literals{{
    {"inside",
        0,
        640,
        480,
        {10, 20, 30, 40},
        {2, 3, 22, 23},
        {10, 20, 30, 40},
        {2, 3, 22, 23},
        false},
    {"boundary-equality",
        0,
        640,
        480,
        {0, 0, 640, 480},
        {2, 3, 22, 23},
        {0, 0, 640, 480},
        {2, 3, 22, 23},
        false},
    {"left-edge",
        0,
        640,
        480,
        {-5, 20, 30, 40},
        {2, 3, 22, 23},
        {0, 20, 30, 40},
        {7, 3, 22, 23},
        false},
    {"right-edge",
        0,
        640,
        480,
        {10, 20, 645, 40},
        {2, 3, 22, 23},
        {10, 20, 640, 40},
        {2, 3, 17, 23},
        false},
    {"top-edge",
        0,
        640,
        480,
        {10, -3, 30, 40},
        {2, 3, 22, 23},
        {10, 0, 30, 40},
        {2, 6, 22, 23},
        false},
    {"bottom-edge",
        0,
        640,
        480,
        {10, 20, 30, 487},
        {2, 3, 22, 23},
        {10, 20, 30, 480},
        {2, 3, 22, 16},
        false},
    {"all-edges",
        0,
        640,
        480,
        {-5, -3, 650, 490},
        {0, 0, 32, 32},
        {0, 0, 640, 480},
        {5, 3, 22, 22},
        false},
    {"empty", 1, 640, 480, {10, 20, 10, 20}, {5, 7, 5, 7}, {10, 20, 10, 20}, {5, 7, 5, 7}, false},
    {"inverted",
        1,
        640,
        480,
        {650, 490, -5, -3},
        {41, 43, 11, 13},
        {650, 490, -5, -3},
        {41, 43, 11, 13},
        false},
    {"negative-signed-bounds",
        1,
        0xfffffff6U,
        0xffffffecU,
        {-5, -3, -2, -4},
        {0, 0, 32, 32},
        {0, 0, -10, -20},
        {5, 3, 24, 16},
        false},
    {"signed-extremes",
        1,
        0x7fffffffU,
        0x7fffffffU,
        {INT_MIN, INT_MIN, INT_MAX, INT_MAX},
        {INT_MIN, INT_MIN, 41, 43},
        {0, 0, INT_MAX, INT_MAX},
        {0, 0, 41, 43},
        false},
    {"same-object-all-edges",
        2,
        640,
        480,
        {-5, -3, 650, 490},
        {71, 73, 79, 83},
        {0, 0, 640, 480},
        {0, 0, 640, 480},
        true},
    {"same-object-inverted-bounds",
        2,
        0xfffffff6U,
        0xffffffecU,
        {-5, -3, -2, -4},
        {71, 73, 79, 83},
        {0, 0, -10, -20},
        {0, 0, -10, -20},
        true},
}};

void run(const Literal& literal)
{
    // Defined typed read model only: its actual host deque is constructed and
    // destroyed normally. This does not stand in for a retail Renderer ctor.
    Guarded<Renderer> renderer;
    renderer_queue_test::initialize(renderer.value, 37U);
    renderer.value.width = literal.width;
    renderer.value.height = literal.height;
    const Renderer before = renderer.value;
    RendererBinding binding(renderer.value);

    Guarded<Rectangle> destination;
    Guarded<Rectangle> separate_source;
    destination.value = literal.destination;
    separate_source.value = literal.source;
    Rectangle* source = literal.alias ? &destination.value : &separate_source.value;

    ClipScreenRectangles(&destination.value, source);
    ++counters.calls;
    counters.aliases += literal.alias ? 1U : 0U;
    counters.groups |= 1U << literal.group;

    require(renderer_queue_test::same_rectangle(destination.value, literal.expected_destination),
        literal.name,
        "destination rectangle differs");
    require(renderer_queue_test::same_rectangle(*source, literal.expected_source),
        literal.name,
        "source rectangle differs");
    if (literal.alias)
    {
        require(renderer_queue_test::same_rectangle(separate_source.value, literal.source),
            literal.name,
            "unselected rectangle changed");
    }
    require(destination.guards_intact(), literal.name, "destination outside guard changed");
    require(separate_source.guards_intact(), literal.name, "source outside guard changed");
    require(renderer.guards_intact(), literal.name, "renderer outside guard changed");
    require(renderer_queue_test::equal(renderer.value, before),
        literal.name,
        "unrelated renderer member changed");
    require(screen_request_renderer == &renderer.value, literal.name, "renderer global changed");
}
} // namespace

int main()
{
    try
    {
        Renderer* original = screen_request_renderer;
        for (const Literal& literal : literals)
        {
            run(literal);
            require(
                screen_request_renderer == original, literal.name, "renderer binding not restored");
        }
        std::printf("{\"groups\":%u,\"calls\":%zu,\"checks\":%zu,\"alias_calls\":%zu}\n",
            static_cast<unsigned int>(std::popcount(counters.groups)),
            counters.calls,
            counters.checks,
            counters.aliases);
        return 0;
    }
    catch (const Failure& error)
    {
        std::fprintf(stderr, "screen-clip: %s: %s\n", error.name, error.message);
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "screen-clip: host failure: %s\n", error.what());
        return 3;
    }
}
