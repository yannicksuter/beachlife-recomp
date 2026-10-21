#include "renderer_queue_test_state.h"
#include "startup_screen_frame_draw.h"
#include "startup_screen_item.h"
#include "startup_screen_renderer.h"

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <vector>

static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(sizeof(int) == 4 && INT_MAX == 2147483647 && sizeof(SurfaceResult) == 4);
static_assert(std::endian::native == std::endian::little);
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Lock) == 25 * sizeof(void*));
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Unlock) == 32 * sizeof(void*));

namespace
{
struct RendererFixtureFailure
{
};

struct ColourCase
{
    const char* name;
    unsigned int format;
    int red, green, blue;
    unsigned int expected;
};
const std::array<ColourCase, 26> colours{{
    {"preserved red bit8", 0u, 256, 0, 0, 0x00010000u},
    {"zero", 0u, 0, 0, 0, 0x00000000u},
    {"red byte max", 0u, 255, 0, 0, 0x0000f800u},
    {"green byte max", 0u, 0, 255, 0, 0x000007e0u},
    {"blue byte max", 0u, 0, 0, 255, 0x0000001fu},
    {"all bytes max", 0u, 255, 255, 255, 0x0000ffffu},
    {"full red mask", 0u, 65535, 255, 255, 0x00ffffffu},
    {"negative channels", 0u, -1, -1, -1, 0x00ffffffu},
    {"red bit16 discarded", 0u, 65536, 0, 0, 0x00000000u},
    {"red high bits retained", 0u, 65280, 0, 0, 0x00ff0000u},
    {"low threshold", 0u, 7, 7, 7, 0x00000020u},
    {"eight threshold", 0u, 8, 8, 8, 0x00000841u},
    {"negative blue", 0u, 0, 0, -9, 0x0000001eu},
    {"blue bit8 ignored", 0u, 0, 0, 256, 0x00000000u},
    {"mixed full words", 0u, 305419896, -1985229329, -9, 0x00567f7eu},
    {"blue signed minimum", 0u, 0, 0, -2147483648, 0x00000000u},
    {"blue signed minimum plus8", 0u, 0, 0, -2147483640, 0x00000001u},
    {"555 max", 1u, 255, 255, 255, 0x00007fffu},
    {"555 red byte max", 1u, 255, 0, 0, 0x00007c00u},
    {"555 green byte max", 1u, 0, 255, 0, 0x000003e0u},
    {"555 low threshold", 1u, 7, 7, 7, 0x00000000u},
    {"555 eight threshold", 1u, 8, 8, 8, 0x00000421u},
    {"555 full words", 1u, 305419896, -1985229329, -9, 0x00003fbeu},
    {"every nonzero format2", 2u, 256, 255, 255, 0x000003ffu},
    {"every nonzero highbit", 2147483648u, 255, 0, 0, 0x00007c00u},
    {"every nonzero allbits", 4294967295u, -1, -1, -1, 0x00007fffu},
}};

// Literal final pixel regions; no reconstructed method or reference clipping is
// supplied by this fixture. Overlap order and out-of-bounds input are already
// resolved in these independent expected regions.
struct Band
{
    int left, top, right, bottom;
    unsigned short colour;
};
struct StyleCase
{
    const char* name;
    int style;
    ScreenRectangle input;
    unsigned int format;
    std::vector<Band> regions;
};
const std::array<StyleCase, 24> styles{{
    {"style2_normal_format0",
        2,
        {4, 6, 14, 16},
        0u,
        {{4, 6, 6, 16, 0x7cdfu},
            {12, 6, 14, 16, 0x7cdfu},
            {4, 14, 14, 16, 0x7cdfu},
            {6, 6, 12, 14, 0xef5fu}}},
    {"style3_normal_format0",
        3,
        {4, 6, 14, 16},
        0u,
        {{4, 6, 6, 16, 0x7cdfu},
            {12, 6, 14, 16, 0x7cdfu},
            {4, 4, 14, 6, 0x7cdfu},
            {6, 6, 12, 14, 0xef5fu}}},
    {"style7_normal_format0",
        7,
        {4, 6, 14, 16},
        0u,
        {{4, 6, 6, 16, 0x7cdfu},
            {12, 6, 14, 16, 0x7cdfu},
            {4, 14, 14, 16, 0x7cdfu},
            {4, 6, 14, 8, 0x7cdfu},
            {6, 8, 12, 14, 0xef5fu}}},
    {"style2_one_cell_format0",
        2,
        {4, 6, 5, 7},
        0u,
        {{3, 6, 6, 7, 0x7cdfu}, {4, 5, 5, 6, 0x7cdfu}}},
    {"style3_one_cell_format0",
        3,
        {4, 6, 5, 7},
        0u,
        {{3, 6, 6, 7, 0x7cdfu}, {4, 4, 5, 6, 0x7cdfu}}},
    {"style7_one_cell_format0",
        7,
        {4, 6, 5, 7},
        0u,
        {{3, 6, 6, 7, 0x7cdfu}, {4, 5, 5, 6, 0x7cdfu}, {4, 7, 5, 8, 0x7cdfu}}},
    {"style2_empty_format0", 2, {10, 10, 10, 10}, 0u, {}},
    {"style3_empty_format0", 3, {10, 10, 10, 10}, 0u, {}},
    {"style7_empty_format0", 7, {10, 10, 10, 10}, 0u, {}},
    {"style2_negative_coordinates_format0",
        2,
        {-3, -5, 5, 7},
        0u,
        {{3, 0, 5, 7, 0x7cdfu}, {0, 5, 3, 7, 0x7cdfu}, {0, 0, 3, 5, 0xef5fu}}},
    {"style3_negative_coordinates_format0",
        3,
        {-3, -5, 5, 7},
        0u,
        {{3, 0, 5, 7, 0x7cdfu}, {0, 0, 3, 5, 0xef5fu}}},
    {"style7_negative_coordinates_format0",
        7,
        {-3, -5, 5, 7},
        0u,
        {{3, 0, 5, 7, 0x7cdfu}, {0, 5, 3, 7, 0x7cdfu}, {0, 0, 3, 5, 0xef5fu}}},
    {"style2_inverted_format0", 2, {14, 16, 4, 6}, 0u, {}},
    {"style3_inverted_format0", 3, {14, 16, 4, 6}, 0u, {}},
    {"style7_inverted_format0", 7, {14, 16, 4, 6}, 0u, {}},
    {"style2_height_two_format0", 2, {4, 6, 14, 8}, 0u, {{4, 6, 14, 8, 0x7cdfu}}},
    {"style3_height_two_format0",
        3,
        {4, 6, 14, 8},
        0u,
        {{4, 6, 6, 8, 0x7cdfu}, {12, 6, 14, 8, 0x7cdfu}, {4, 4, 14, 6, 0x7cdfu}}},
    {"style7_height_two_format0", 7, {4, 6, 14, 8}, 0u, {{4, 6, 14, 8, 0x7cdfu}}},
    {"style2_normal_format1",
        2,
        {4, 6, 14, 16},
        1u,
        {{4, 6, 6, 16, 0x3e7fu},
            {12, 6, 14, 16, 0x3e7fu},
            {4, 14, 14, 16, 0x3e7fu},
            {6, 6, 12, 14, 0x77bfu}}},
    {"style3_normal_format1",
        3,
        {4, 6, 14, 16},
        1u,
        {{4, 6, 6, 16, 0x3e7fu},
            {12, 6, 14, 16, 0x3e7fu},
            {4, 4, 14, 6, 0x3e7fu},
            {6, 6, 12, 14, 0x77bfu}}},
    {"style7_normal_format1",
        7,
        {4, 6, 14, 16},
        1u,
        {{4, 6, 6, 16, 0x3e7fu},
            {12, 6, 14, 16, 0x3e7fu},
            {4, 14, 14, 16, 0x3e7fu},
            {4, 6, 14, 8, 0x3e7fu},
            {6, 8, 12, 14, 0x77bfu}}},
    {"style2_normal_format4294967295",
        2,
        {4, 6, 14, 16},
        4294967295u,
        {{4, 6, 6, 16, 0x3e7fu},
            {12, 6, 14, 16, 0x3e7fu},
            {4, 14, 14, 16, 0x3e7fu},
            {6, 6, 12, 14, 0x77bfu}}},
    {"style3_normal_format4294967295",
        3,
        {4, 6, 14, 16},
        4294967295u,
        {{4, 6, 6, 16, 0x3e7fu},
            {12, 6, 14, 16, 0x3e7fu},
            {4, 4, 14, 6, 0x3e7fu},
            {6, 6, 12, 14, 0x77bfu}}},
    {"style7_normal_format4294967295",
        7,
        {4, 6, 14, 16},
        4294967295u,
        {{4, 6, 6, 16, 0x3e7fu},
            {12, 6, 14, 16, 0x3e7fu},
            {4, 14, 14, 16, 0x3e7fu},
            {4, 6, 14, 8, 0x3e7fu},
            {6, 8, 12, 14, 0x77bfu}}},
}};

template <class T> struct Guarded
{
    std::array<unsigned char, 16> before{};
    T value{};
    std::array<unsigned char, 16> after{};
};

// Genuine renderer ownership; only method inputs are borrowed.
template <> struct Guarded<ScreenRequestRenderer>
{
    renderer_owner_test::Bootstrap renderer_bootstrap;
    std::array<unsigned char, 16> before{};
    ScreenRequestRenderer value{renderer_bootstrap.video(), 8, 4, 1};
    std::array<unsigned char, 16> after{};
    renderer_owner_test::RestoreOwnedState renderer_restore{value, renderer_bootstrap};
};
using DirectPixels = std::array<unsigned short, 800>;
using DensePixels = std::array<unsigned short, 400>;
using SurfacePointers = std::array<ScreenSurfacePosition*, 1>;
struct Fixture
{
    Guarded<ScreenRequestRenderer> renderer;
    Guarded<ScreenRequestRenderer> other_renderer;
    Guarded<ScreenRectangle> bounds;
    std::array<Guarded<ScreenSurfacePosition>, 2> entries;
    std::array<Guarded<DirectDrawSurface7Prefix>, 2> devices;
    Guarded<SurfacePointers> table;
    Guarded<SurfacePointers> other_table;
    Guarded<DirectPixels> direct;
    Guarded<DensePixels> dense;
    Guarded<DensePixels> other_pixels;
};
struct Snapshot
{
    renderer_queue_test::Snapshot renderer;
    renderer_queue_test::Snapshot other_renderer;
    ScreenRectangle bounds;
    std::array<ScreenSurfacePosition, 2> entries;
    DirectPixels direct;
    DensePixels dense;
    DensePixels other_pixels;
};
struct Context
{
    Fixture* fixture;
    ScreenRectangle* input;
    std::array<DensePixels, 5> expected_after;
    DensePixels initial;
    std::size_t fill_count;
    std::size_t locks = 0;
    std::size_t unlocks = 0;
};
Context* active;
const char* domain = "initial";
std::size_t current_case;
std::size_t checks;
std::size_t logical_bytes;
std::size_t colour_calls;
std::size_t style_calls;
std::size_t locks;
std::size_t unlocks;

void require(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        std::fprintf(stderr, "%s case %zu: %s\n", domain, current_case, message);
        throw RendererFixtureFailure{};
    }
}

template <class T> void initialize_guard(Guarded<T>& x)
{
    x.before.fill(0x3c);
    x.after.fill(0xc3);
}

template <class T> void check_guard(const Guarded<T>& x)
{
    for (auto byte : x.before)
    {
        ++logical_bytes;
        require(byte == 0x3c, "leading guard differs");
    }
    for (auto byte : x.after)
    {
        ++logical_bytes;
        require(byte == 0xc3, "trailing guard differs");
    }
}

template <std::size_t N>
void check_pixels(const std::array<unsigned short, N>& actual,
    const std::array<unsigned short, N>& expected,
    const char* message)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        logical_bytes += sizeof(actual[i]);
        require(actual[i] == expected[i], message);
    }
}

void check_renderer(
    const ScreenRequestRenderer& actual, const renderer_queue_test::Snapshot& expected)
{
    logical_bytes += renderer_queue_test::compared_bytes(actual);
    require(renderer_queue_test::equal(actual, expected), "renderer members/queue differ");
}

bool equal_entry(const ScreenSurfacePosition& a, const ScreenSurfacePosition& b)
{
    return a.active == b.active && a.software == b.software &&
           renderer_queue_test::same_bytes(a.unknown_02, b.unknown_02) && a.pixels == b.pixels &&
           a.surface == b.surface && renderer_queue_test::same_entry_extension(a, b) &&
           a.width == b.width && a.height == b.height && a.pitch == b.pitch &&
           a.position.x == b.position.x && a.position.y == b.position.y && a.prepared == b.prepared;
}

// Only literal, already clipped expected regions reach this data painter.
// It never calls a game method, derives a border, or performs reference clipping.
template <std::size_t N>
void paint(std::array<unsigned short, N>& pixels,
    const std::vector<Band>& regions,
    std::size_t words_per_pixel)
{
    for (const Band& b : regions)
    {
        require(b.left >= 0 && b.top >= 0 && b.right <= 20 && b.bottom <= 20 && b.left < b.right &&
                    b.top < b.bottom,
            "invalid literal pixel region");
        for (int y = b.top; y < b.bottom; ++y)
        {
            for (int x = b.left; x < b.right; ++x)
            {
                pixels.at((static_cast<std::size_t>(y) * 20U + static_cast<std::size_t>(x)) *
                          words_per_pixel) = b.colour;
            }
        }
    }
}

SurfaceResult SURFACE_STDCALL lock_surface(DirectDrawSurface7Prefix* receiver,
    SurfaceApiRectangle* rectangle,
    SurfaceDescription* description,
    unsigned int flags,
    void* event)
{
    require(active != nullptr, "unexpected Lock");
    auto& c = *active;
    auto& f = *c.fixture;
    auto& r = f.renderer.value;
    require(c.locks == c.unlocks && c.locks < c.fill_count, "Lock order differs");
    require(
        receiver == &f.devices[0].value && rectangle == nullptr && flags == 0 && event == nullptr,
        "captured renderer/Lock ABI differs");
    require(r.pixels_locked == (c.locks == 0 ? 0x7b : 0), "locked state before Lock differs");
    require(f.entries[0].value.prepared == 0, "prepared store must precede Lock");
    check_pixels(f.dense.value,
        c.locks == 0 ? c.initial : c.expected_after[c.locks - 1],
        "writes before Lock differ");
    require(screen_request_renderer == (c.locks == 0 ? &f.renderer.value : &f.other_renderer.value),
        "global mutation/order differs");
    std::array<unsigned char, sizeof(SurfaceDescription)> expected{};
    const unsigned int size = sizeof(SurfaceDescription);
    std::memcpy(expected.data(), &size, sizeof(size));
    require(description != nullptr &&
                std::memcmp(
                    static_cast<const void*>(description), expected.data(), expected.size()) == 0,
        "Lock descriptor differs");
    description->pixels = f.dense.value.data();
    description->pitch = 1234; // Real Fill must consume the selected entry's 40-byte pitch.
    if (c.locks == 0)
    {
        screen_request_renderer = &f.other_renderer.value;
        r.format_44 = 1;
    }
    constexpr std::array<ScreenRectangle, 4> mutations{
        {{2, 3, 17, 19}, {1, 4, 18, 18}, {3, 2, 16, 15}, {5, 5, 15, 14}}};
    if (c.locks < mutations.size())
    {
        *c.input = mutations[c.locks];
    }
    ++c.locks;
    ++locks;
    return 0;
}

SurfaceResult SURFACE_STDCALL unlock_surface(
    DirectDrawSurface7Prefix* receiver, SurfaceApiRectangle* rectangle)
{
    require(active != nullptr, "unexpected Unlock");
    auto& c = *active;
    auto& f = *c.fixture;
    require(c.locks == c.unlocks + 1 && receiver == &f.devices[0].value && rectangle == nullptr,
        "Unlock receiver/order differs");
    check_pixels(f.dense.value, c.expected_after[c.unlocks], "pixels before Unlock differ");
    require(f.renderer.value.pixels_locked == 1, "locked byte cleared before Unlock");
    f.renderer.value.pixels_locked = 0xa5;
    ++c.unlocks;
    ++unlocks;
    return -37; // Actual Fill ignores the result and clears pixels_locked afterwards.
}

const DirectDrawSurface7VtablePrefix surface_table = []
{
    DirectDrawSurface7VtablePrefix table{};
    table.Lock = lock_surface;
    table.Unlock = unlock_surface;
    return table;
}();

void initialize(Fixture& f)
{
    initialize_guard(f.renderer);
    initialize_guard(f.other_renderer);
    initialize_guard(f.bounds);
    initialize_guard(f.table);
    initialize_guard(f.other_table);
    initialize_guard(f.direct);
    initialize_guard(f.dense);
    initialize_guard(f.other_pixels);
    renderer_queue_test::initialize(f.renderer.value, 23);
    renderer_queue_test::initialize(f.other_renderer.value, 71);
    for (std::size_t i = 0; i < f.direct.value.size(); ++i)
    {
        f.direct.value[i] = (i & 1U) ? static_cast<unsigned short>(0xa500U + (i % 256U)) : 0x1234;
    }
    f.dense.value.fill(0x3456);
    f.other_pixels.value.fill(0x789a);
    f.table.value = {&f.entries[0].value};
    f.other_table.value = {&f.entries[1].value};
    for (std::size_t i = 0; i < f.entries.size(); ++i)
    {
        initialize_guard(f.entries[i]);
        initialize_guard(f.devices[i]);
        auto& e = f.entries[i].value;
        e.active = static_cast<unsigned char>(0x41 + i);
        e.software = static_cast<unsigned char>(0x51 + i);
        renderer_queue_test::pattern(e.unknown_02, static_cast<unsigned int>(71 + i));
        e.pixels = i == 0 ? f.dense.value.data() : f.other_pixels.value.data();
        e.surface = &f.devices[i].value;
        renderer_queue_test::initialize_entry_extension(e, static_cast<unsigned int>(91 + i));
        e.width = 20;
        e.height = 20;
        e.pitch = 40;
        e.position = {0, 0};
        e.prepared = static_cast<unsigned char>(0x31 + i);
        f.devices[i].value.vtable = &surface_table;
    }
    for (auto* r : {&f.renderer.value, &f.other_renderer.value})
    {
        r->software_mode = 0;
        r->width = 20;
        r->height = 20;
        r->format_44 = 0;
        r->unscaled_clip_5c = 0;
        r->current_surface = 0;
        r->pixels_locked = 0x7b;
    }
    f.renderer.value.pixels_1c = f.direct.value.data();
    f.renderer.value.surface_storage.assign(f.table.value.begin(), f.table.value.end());
    f.other_renderer.value.pixels_1c = f.other_pixels.value.data();
    f.other_renderer.value.surface_storage.assign(
        f.other_table.value.begin(), f.other_table.value.end());
    // A mistaken global reload must still reach a valid typed renderer/COM object.
    f.other_renderer.value.unscaled_clip_5c = 1;
}

Snapshot snapshot(const Fixture& f)
{
    return {f.renderer.value,
        f.other_renderer.value,
        f.bounds.value,
        {f.entries[0].value, f.entries[1].value},
        f.direct.value,
        f.dense.value,
        f.other_pixels.value};
}

void check(const Fixture& f, const Snapshot& expected)
{
    check_pixels(f.direct.value, expected.direct, "direct pixels differ");
    check_pixels(f.dense.value, expected.dense, "dense pixels differ");
    check_pixels(f.other_pixels.value, expected.other_pixels, "unselected pixels differ");
    check_renderer(f.renderer.value, expected.renderer);
    check_renderer(f.other_renderer.value, expected.other_renderer);
    require(renderer_queue_test::same_rectangle(f.bounds.value, expected.bounds), "input differs");
    require(
        f.table.value[0] == &f.entries[0].value && f.other_table.value[0] == &f.entries[1].value,
        "entry pointer table differs");
    for (std::size_t i = 0; i < f.entries.size(); ++i)
    {
        require(equal_entry(f.entries[i].value, expected.entries[i]), "entry members differ");
        require(f.devices[i].value.vtable == &surface_table, "COM table pointer differs");
        check_guard(f.entries[i]);
        check_guard(f.devices[i]);
    }
    check_guard(f.renderer);
    check_guard(f.other_renderer);
    check_guard(f.bounds);
    check_guard(f.table);
    check_guard(f.other_table);
    check_guard(f.direct);
    check_guard(f.dense);
    check_guard(f.other_pixels);
}

void invoke(int style, const ScreenRectangle& bounds)
{
    switch (style)
    {
    case 2:
        DrawFrameStyle2(bounds);
        break;
    case 3:
        DrawFrameStyle3(bounds);
        break;
    case 7:
        DrawFrameStyle7(bounds);
        break;
    default:
        require(false, "invalid fixture style");
    }
    ++style_calls;
}

void run_colour(const ColourCase& s)
{
    Fixture f{};
    initialize(f);
    f.renderer.value.format_44 = s.format;
    const auto expected = snapshot(f);
    screen_request_renderer = &f.other_renderer.value;
    const auto result = f.renderer.value.ConvertColour(s.red, s.green, s.blue);
    ++colour_calls;
    require(result == s.expected, "full result differs");
    require(screen_request_renderer == &f.other_renderer.value, "converter changed global");
    check(f, expected);
    screen_request_renderer = nullptr;
}

void run_style(const StyleCase& s)
{
    Fixture f{};
    initialize(f);
    f.renderer.value.format_44 = s.format;
    f.bounds.value = s.input;
    auto expected = snapshot(f);
    paint(expected.direct, s.regions, 2);
    screen_request_renderer = &f.renderer.value;
    invoke(s.style, f.bounds.value);
    check(f, expected);
    require(screen_request_renderer == &f.renderer.value, "style changed global");
    screen_request_renderer = nullptr;
}

void run_live(int style, bool queue_alias)
{
    Fixture f{};
    initialize(f);
    f.renderer.value.unscaled_clip_5c = 1;
    f.bounds.value = {4, 6, 14, 16};
    ScreenRectangle* input = &f.bounds.value;
    if (queue_alias)
    {
        f.renderer.value.dirty_rectangles.values.front() = f.bounds.value;
        input = &f.renderer.value.dirty_rectangles.values.front();
    }
    auto expected = snapshot(f);
    const std::size_t count = style == 7 ? 5 : 4;
    std::array<std::vector<Band>, 5> regions{
        {{{4, 6, 6, 16, 0x7cdf}}, {{15, 6, 17, 16, 0x7cdf}}, {}, {}, {}}};
    if (style == 3)
    {
        regions[2] = {{1, 2, 18, 4, 0x7cdf}};
    }
    if (style == 7)
    {
        regions[3] = {{1, 2, 18, 4, 0x7cdf}};
        regions[4] = {{7, 7, 13, 12, 0x77bf}};
    }
    else
    {
        regions[3] = {{5, 2, 14, 13, 0x77bf}};
    }
    Context context{&f, input, {}, f.dense.value, count};
    for (std::size_t i = 0; i < count; ++i)
    {
        context.expected_after[i] = i == 0 ? f.dense.value : context.expected_after[i - 1];
        paint(context.expected_after[i], regions[i], 1);
    }
    expected.dense = context.expected_after[count - 1];
    expected.renderer.format_44 = 1;
    expected.renderer.pixels_locked = 0;
    expected.entries[0].prepared = 0;
    if (queue_alias)
    {
        expected.renderer.dirty_rectangles.values.front() = {5, 5, 15, 14};
    }
    else
    {
        expected.bounds = {5, 5, 15, 14};
    }
    screen_request_renderer = &f.renderer.value;
    active = &context;
    invoke(style, *input);
    active = nullptr;
    require(context.locks == count && context.unlocks == count, "COM call counts differ");
    require(screen_request_renderer == &f.other_renderer.value, "global callback mutation lost");
    check(f, expected);
    screen_request_renderer = nullptr;
}
} // namespace

int main()
{
    try
    {
        require(screen_request_renderer == nullptr, "actual renderer global not initially null");
        domain = "colour";
        for (current_case = 0; current_case < colours.size(); ++current_case)
        {
            run_colour(colours[current_case]);
        }
        domain = "style";
        for (current_case = 0; current_case < styles.size(); ++current_case)
        {
            run_style(styles[current_case]);
        }
        domain = "COM";
        current_case = 0;
        for (bool alias : {false, true})
        {
            for (int style : {2, 3, 7})
            {
                run_live(style, alias);
                ++current_case;
            }
        }
        require(colour_calls == 26 && style_calls == 30 && locks == 26 && unlocks == 26,
            "final call counts differ");
        require(screen_request_renderer == nullptr && active == nullptr,
            "fixture globals not restored");
        std::printf("frame fill styles: 26 colour cases, 24 literal styles, 6 COM styles, "
                    "%zu style calls, %zu locks, %zu unlocks, %zu checks, %zu logical bytes\n",
            style_calls,
            locks,
            unlocks,
            checks,
            logical_bytes);
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
