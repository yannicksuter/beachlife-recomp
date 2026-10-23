#include "renderer_queue_test_state.h"
#include "startup_screen_text.h"

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <type_traits>

namespace
{
static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(sizeof(int) == 4 && INT_MAX == 2147483647);
static_assert(std::endian::native == std::endian::little);
static_assert(!std::is_aggregate_v<ScreenRequestRenderer>);
static_assert(!std::is_copy_constructible_v<ScreenRequestRenderer>);
static_assert(!std::is_polymorphic_v<ScreenRequestRenderer>);
static_assert(std::is_trivially_copyable_v<ScreenFontRenderer>);
static_assert(std::is_trivially_destructible_v<ScreenFontRenderer>);

using Guard = std::array<unsigned char, 16>;
using Words = std::array<unsigned short, 96>;
enum class Kind
{
    direct,
    surface
};

const char* current_case = "setup";
unsigned int calls = 0;
unsigned int checks = 0;
unsigned int word_checks = 0;
unsigned int source_byte_checks = 0;

struct Failure
{
    const char* reason;
};

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        throw Failure{reason};
    }
}

template <typename T, std::size_t N> struct Buffer
{
    Guard before{};
    std::array<T, N> values{};
    Guard after{};

    Buffer()
    {
        before.fill(0xa6);
        after.fill(0x6a);
    }

    void check_guards() const
    {
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            require(before[i] == 0xa6 && after[i] == 0x6a, "buffer guard changed");
        }
    }
};
using Source = Buffer<unsigned char, 32>;
using Output = Buffer<unsigned short, 96>;

struct Environment
{
    renderer_owner_test::Bootstrap renderer_bootstrap;
    Guard before{};
    ScreenRequestRenderer renderer{renderer_bootstrap.video(), 8, 4, 1};
    Guard after{};
    renderer_owner_test::RestoreOwnedState renderer_restore{renderer, renderer_bootstrap};
    ScreenRequestRenderer* previous;

    Environment()
        : previous(screen_request_renderer)
    {
        before.fill(0xa3);
        after.fill(0x3a);
        renderer_queue_test::initialize(renderer, 31);
        renderer.clip_rectangle = {0, 0, 64, 64};
        screen_request_renderer = &renderer;
    }

    ~Environment()
    {
        screen_request_renderer = previous;
    }
};

struct FontFrame
{
    Guard before{};
    ScreenFontRenderer font;
    Guard after{};

    FontFrame()
        : font(nullptr, nullptr)
    {
        before.fill(0xb4);
        after.fill(0x4b);
        font.unknown_15[0] = 0x37;
        font.unknown_15[1] = 0x59;
        font.unknown_15[2] = 0x7b;
    }
};

struct Geometry
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

void invoke(Environment& environment,
    Kind kind,
    const unsigned char* source,
    unsigned short* destination,
    Geometry geometry,
    unsigned int format,
    unsigned char state)
{
    environment.renderer.format_44 = format;
    const renderer_queue_test::Snapshot renderer_before = environment.renderer;
    FontFrame frame;
    auto& font = frame.font;
    require(font.rectangle.x == 0 && font.rectangle.y == 0 && font.rectangle.width == 64 &&
                font.rectangle.height == 64 && font.word_10 == 0xffffffffU && font.word_18 == 0 &&
                font.state_14 == 0 && font.data_1c == nullptr && font.lookup_20 == nullptr,
        "real constructor state differs");
    font.word_10 = 0xab2f8040U;
    font.word_18 = 0x8a72c419U;
    font.state_14 = state;
    const ScreenFontRenderer before = font;

    if (kind == Kind::direct)
    {
        font.DrawGlyphDirect(source,
            geometry.source_stride,
            destination,
            geometry.destination_stride,
            geometry.source_x,
            geometry.source_y,
            geometry.destination_x,
            geometry.destination_y,
            geometry.width,
            geometry.height,
            nullptr);
    }
    else
    {
        font.DrawGlyphSurface(source,
            geometry.source_stride,
            destination,
            geometry.destination_stride,
            geometry.source_x,
            geometry.source_y,
            geometry.destination_x,
            geometry.destination_y,
            geometry.width,
            geometry.height,
            nullptr);
    }
    ++calls;

    require(font.rectangle.x == before.rectangle.x && font.rectangle.y == before.rectangle.y &&
                font.rectangle.width == before.rectangle.width &&
                font.rectangle.height == before.rectangle.height &&
                font.word_10 == before.word_10 && font.state_14 == before.state_14 &&
                font.word_18 == before.word_18 && font.data_1c == before.data_1c &&
                font.lookup_20 == before.lookup_20,
        "font member changed");
    for (std::size_t i = 0; i < sizeof(font.unknown_15); ++i)
    {
        require(font.unknown_15[i] == before.unknown_15[i], "font opaque byte changed");
    }
    require(renderer_queue_test::equal(environment.renderer, renderer_before), "renderer changed");
    require(screen_request_renderer == &environment.renderer, "renderer global changed");
    for (std::size_t i = 0; i < frame.before.size(); ++i)
    {
        require(frame.before[i] == 0xb4 && frame.after[i] == 0x4b, "font guard changed");
        require(environment.before[i] == 0xa3 && environment.after[i] == 0x3a,
            "renderer guard changed");
    }
}

void initialize_output(Output& output, Kind kind)
{
    output.values.fill(0x7777);
    if (kind == Kind::direct)
    {
        for (std::size_t i = 1; i < output.values.size(); i += 2)
        {
            output.values[i] = 0xaaaa;
        }
    }
}

void check_output(const Output& output, const Words& expected)
{
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        ++word_checks;
        require(output.values[i] == expected[i], "output word differs");
    }
    output.check_guards();
}

void check_source(const Source& source, const std::array<unsigned char, 32>& expected)
{
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        ++source_byte_checks;
        require(source.values[i] == expected[i], "source byte changed");
    }
    source.check_guards();
}

void colour_rows(Environment& environment)
{
    struct Literal
    {
        const char* name;
        Kind kind;
        unsigned int format;
        unsigned short colour;
        std::array<std::size_t, 3> words;
    };
    constexpr std::array<Literal, 4> cases{{
        {"colour-direct-565", Kind::direct, 0, 0x2c08, {22, 26, 14}},
        {"colour-direct-555", Kind::direct, 1, 0x1608, {22, 26, 14}},
        {"colour-surface-565", Kind::surface, 0, 0x2c08, {11, 13, 7}},
        {"colour-surface-555", Kind::surface, 1, 0x1608, {11, 13, 7}},
    }};
    for (const Literal& literal : cases)
    {
        current_case = literal.name;
        Source source;
        source.values.fill(0xa0);
        source.values[6] = 0;
        source.values[7] = 1;
        source.values[8] = 2;
        source.values[11] = 3;
        source.values[12] = 4;
        source.values[13] = 5;
        const auto source_before = source.values;
        Output output;
        initialize_output(output, literal.kind);
        Words expected = output.values;
        for (std::size_t index : literal.words)
        {
            expected[index] = literal.colour;
        }
        invoke(environment,
            literal.kind,
            source.values.data(),
            output.values.data(),
            {5, 5, 1, 1, 1, 1, 3, 2},
            literal.format,
            0);
        check_output(output, expected);
        check_source(source, source_before);
    }
}

void mask_rows(Environment& environment)
{
    struct Literal
    {
        const char* name;
        Kind kind;
        unsigned int format;
        unsigned char state;
        std::array<std::size_t, 6> words;
    };
    constexpr std::array<Literal, 4> cases{{
        {"mask-direct-format1", Kind::direct, 1, 1, {30, 32, 34, 38, 40, 42}},
        {"mask-direct-format0", Kind::direct, 0, 255, {30, 32, 34, 38, 40, 42}},
        {"mask-surface-format1", Kind::surface, 1, 1, {15, 16, 17, 19, 20, 21}},
        {"mask-surface-format0", Kind::surface, 0, 255, {15, 16, 17, 19, 20, 21}},
    }};
    constexpr std::array<unsigned char, 8> bytes{0, 1, 2, 3, 128, 129, 130, 131};
    constexpr std::array<unsigned short, 6> values{0xffff, 0, 0, 0xffff, 0, 0};
    for (const Literal& literal : cases)
    {
        current_case = literal.name;
        Source source;
        source.values.fill(0xa0);
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            source.values[11 + i] = bytes[i];
        }
        const auto source_before = source.values;
        Output output;
        initialize_output(output, literal.kind);
        Words expected = output.values;
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            expected[literal.words[i]] = values[i];
        }
        invoke(environment,
            literal.kind,
            source.values.data(),
            output.values.data(),
            {10, 12, 1, 1, 2, 1, 8, 1},
            literal.format,
            literal.state);
        check_output(output, expected);
        check_source(source, source_before);
    }
}

void aliases_and_empty(Environment& environment)
{
    struct Alias
    {
        const char* name;
        Kind kind;
        int source_stride;
        std::size_t word;
    };
    constexpr std::array<Alias, 2> aliases{{
        {"alias-direct", Kind::direct, 8, 8},
        {"alias-surface", Kind::surface, 4, 4},
    }};
    for (const Alias& literal : aliases)
    {
        current_case = literal.name;
        Output output;
        initialize_output(output, literal.kind);
        output.values[literal.word] = 0x5501;
        Words expected = output.values;
        expected[literal.word] = 0;
        const auto* bytes = reinterpret_cast<const unsigned char*>(output.values.data());
        invoke(environment,
            literal.kind,
            bytes,
            output.values.data(),
            {literal.source_stride, 2, 0, 2, 0, 2, 1, 1},
            1,
            1);
        check_output(output, expected);
    }

    struct Empty
    {
        const char* name;
        Kind kind;
        int width;
        int height;
    };
    constexpr std::array<Empty, 4> empty{{
        {"height0-direct", Kind::direct, 3, 0},
        {"width0-direct", Kind::direct, 0, 2},
        {"height0-surface", Kind::surface, 3, 0},
        {"width0-surface", Kind::surface, 0, 2},
    }};
    for (const Empty& literal : empty)
    {
        current_case = literal.name;
        Source source;
        source.values.fill(0xff);
        const auto source_before = source.values;
        Output output;
        initialize_output(output, literal.kind);
        const Words expected = output.values;
        invoke(environment,
            literal.kind,
            source.values.data(),
            output.values.data(),
            {5, 5, 1, 1, 1, 1, literal.width, literal.height},
            0,
            1);
        check_output(output, expected);
        check_source(source, source_before);
    }
}
} // namespace

int main()
{
    try
    {
        ScreenRequestRenderer* previous = screen_request_renderer;
        {
            Environment environment;
            colour_rows(environment);
            mask_rows(environment);
            aliases_and_empty(environment);
        }
        require(screen_request_renderer == previous, "renderer global restoration differs");
        require(calls == 14, "literal call count differs");
        std::printf(
            "font-glyphs groups=3 calls=%u checks=%u word-checks=%u source-byte-checks=%u pass\n",
            calls,
            checks,
            word_checks,
            source_byte_checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "font-glyphs %s: %s\n", current_case, failure.reason);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "font-glyphs host failure: %s\n", failure.what());
        return 3;
    }
}
