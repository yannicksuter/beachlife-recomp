#include "font_owner_test_data.h"
#include "renderer_queue_test_state.h"
#include "startup_screen_font_data.h"
#include "startup_screen_text.h"
#include "text_lookup_owner_test_data.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>

namespace
{
static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(sizeof(int) == 4 && INT_MAX == 2147483647);
static_assert(std::is_standard_layout_v<ScreenFontRenderer>);
static_assert(std::is_trivially_destructible_v<ScreenFontRenderer>);
static_assert(std::is_same_v<decltype(&ScreenFontRenderer::MeasureText),
    unsigned int (ScreenFontRenderer::*)(const unsigned short*)>);
static_assert(std::is_same_v<decltype(&ScreenFontRenderer::CountWrappedLines),
    unsigned int (ScreenFontRenderer::*)(const unsigned short*, unsigned int, unsigned int)>);
static_assert(std::is_same_v<decltype(&ScreenFontRenderer::MeasureWrappedText),
    unsigned int (ScreenFontRenderer::*)(const unsigned short*, unsigned int, unsigned int)>);

using Guard = std::array<unsigned char, 16>;
struct Failure
{
};

const char* category = "setup";
const char* case_name = "initial";
std::size_t case_index = 0;
unsigned int checks = 0;
unsigned int sessions = 0;
unsigned int constructors = 0;
unsigned int measurements_called = 0;

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr,
            "font-measurement: %s case %zu %s: %s\n",
            category,
            case_index,
            case_name,
            reason);
        throw Failure{};
    }
}

void expect(unsigned int actual, unsigned int expected, const char* method)
{
    ++checks;
    if (actual != expected)
    {
        std::fprintf(stderr,
            "font-measurement: %s case %zu %s: %s expected %u got %u\n",
            category,
            case_index,
            case_name,
            method,
            expected,
            actual);
        throw Failure{};
    }
}

Guard guard(unsigned int seed)
{
    Guard result{};
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        result[i] = static_cast<unsigned char>(seed + 17U * i);
    }
    return result;
}

struct Glyph
{
    unsigned int index;
    std::array<unsigned short, 4> words;
};
struct Kerning
{
    unsigned int index;
    int value;
};
struct Measurement
{
    const char* name;
    std::initializer_list<unsigned short> units;
    unsigned short repeated_unit;
    std::size_t repeat_count;
    unsigned int width;
    unsigned int spacing;
    std::array<unsigned int, 3> expected;
    std::initializer_list<Glyph> glyphs;
    std::initializer_list<Kerning> kerning;
    bool null_data;
    bool aliases_glyph_zero;
};
struct Construction
{
    const char* name;
    RenderRectangle rectangle;
    int data;
    int lookup;
    std::array<unsigned char, 3> opaque;
};

// Literal D223 expectations copied mechanically; no scanner/oracle computes results.
const Measurement measurements[] = {

    {"empty", {0}, 0, 0, 0U, 99U, {0U, 1U, 0U}, {}, {}, false, false},

    {"empty_null_data", {0}, 0, 0, 0U, 99U, {0U, 1U, 0U}, {}, {}, true, false},

    {"unsplit_single", {65, 0}, 0, 0, 0U, 100U, {4U, 1U, 4U}, {}, {}, false, false},

    {"unsplit_word", {65, 66, 0}, 0, 0, 1U, 0U, {10U, 1U, 10U}, {}, {}, false, false},

    {"exact_width", {65, 32, 66, 0}, 0, 0, 12U, 0U, {12U, 1U, 12U}, {}, {}, false, false},

    {"width_below_total", {65, 32, 66, 0}, 0, 0, 11U, 0U, {12U, 2U, 6U}, {}, {}, false, false},

    {"spacing_causes_wrap", {65, 32, 66, 0}, 0, 0, 12U, 1U, {12U, 2U, 6U}, {}, {}, false, false},

    {"spacing_unsigned_wrap",
        {65, 32, 66, 0},
        0,
        0,
        4294967295U,
        4294967295U,
        {12U, 1U, 12U},
        {},
        {},
        false,
        false},

    {"spacing_wrap_then_break",
        {65, 32, 66, 0},
        0,
        0,
        10U,
        4294967295U,
        {12U, 2U, 6U},
        {},
        {},
        false,
        false},

    {"three_lines", {65, 32, 66, 32, 67, 0}, 0, 0, 6U, 0U, {17U, 3U, 6U}, {}, {}, false, false},

    {"break_on_separator",
        {65, 32, 66, 32, 67, 0},
        0,
        0,
        12U,
        0U,
        {17U, 2U, 11U},
        {},
        {},
        false,
        false},

    {"copied_line_wins", {65, 65, 32, 66, 0}, 0, 0, 15U, 0U, {16U, 2U, 8U}, {}, {}, false, false},

    {"partial_word_rewind",
        {65, 66, 32, 67, 68, 0},
        0,
        0,
        15U,
        0U,
        {20U, 2U, 10U},
        {},
        {},
        false,
        false},

    {"two_spaces_finite", {65, 32, 32, 66, 0}, 0, 0, 12U, 0U, {14U, 2U, 6U}, {}, {}, false, false},

    {"two_spaces_unwrapped",
        {65, 32, 32, 66, 0},
        0,
        0,
        14U,
        0U,
        {14U, 1U, 14U},
        {},
        {},
        false,
        false},

    {"leading_spaces", {32, 32, 65, 0}, 0, 0, 8U, 0U, {8U, 1U, 8U}, {}, {}, false, false},

    {"one_trailing_space", {65, 32, 0}, 0, 0, 0U, 0U, {6U, 1U, 6U}, {}, {}, false, false},

    {"tab_is_glyph", {65, 9, 66, 0}, 0, 0, 0U, 0U, {17U, 1U, 17U}, {}, {}, false, false},

    {"lf_is_glyph", {65, 10, 66, 0}, 0, 0, 0U, 0U, {15U, 1U, 15U}, {}, {}, false, false},

    {"cr_is_glyph", {65, 13, 66, 0}, 0, 0, 0U, 0U, {19U, 1U, 19U}, {}, {}, false, false},

    {"embedded_nul", {65, 0, 66, 0}, 0, 0, 0U, 0U, {4U, 1U, 4U}, {}, {}, false, false},

    {"high_unit_signed_kerning",
        {255, 65, 0},
        0,
        0,
        100U,
        0U,
        {7U, 1U, 7U},
        {},
        {{65090, -2}},
        false,
        false},

    {"terminal_kerning_not_read",
        {65, 0},
        0,
        0,
        100U,
        0U,
        {4U, 1U, 4U},
        {},
        {{16575, 127}},
        false,
        false},

    {"kerning_stride_control",
        {65, 66, 0},
        0,
        0,
        100U,
        0U,
        {8U, 1U, 8U},
        {},
        {{16641, -2}, {16706, 7}},
        false,
        false},

    {"positive_signed_kerning",
        {65, 66, 0},
        0,
        0,
        4294967295U,
        0U,
        {137U, 1U, 137U},
        {},
        {{16641, 127}},
        false,
        false},

    {"negative_signed_kerning_limit",
        {65, 66, 0},
        0,
        0,
        4294967295U,
        0U,
        {65418U, 1U, 65418U},
        {},
        {{16641, -128}},
        false,
        false},

    {"advance_underflow",
        {65, 66, 0},
        0,
        0,
        0U,
        0U,
        {65541U, 1U, 65541U},
        {{65, {10, 0, 10, 0}}},
        {{16641, -1}},
        false,
        false},

    {"metric_subtraction_wrap",
        {65, 66, 0},
        0,
        0,
        4294967295U,
        0U,
        {65537U, 1U, 65537U},
        {{65, {9, 0, 4, 0}}},
        {},
        false,
        false},

    {"advance_addition_wrap",
        {65, 66, 0},
        0,
        0,
        100U,
        0U,
        {6U, 1U, 6U},
        {{65, {0, 0, 65535, 0}}},
        {{16641, 1}},
        false,
        false},

    {"text_alias_in_glyph_array",
        {65, 66, 0},
        0,
        0,
        100U,
        0U,
        {10U, 1U, 10U},
        {{0, {65, 66, 0, 777}}},
        {},
        false,
        true},

    {"largest_terminated_scratch_copy",
        {32, 66, 0},
        65,
        255,
        1027U,
        0U,
        {1028U, 2U, 1020U},
        {},
        {},
        false,
        false},

    {"long_unsplit_word", {0}, 65, 300, 1U, 0U, {1200U, 1U, 1200U}, {}, {}, false, false},

    {"unsigned_accumulator_wrap",
        {0},
        65,
        65538,
        4294967295U,
        0U,
        {65534U, 1U, 65534U},
        {{65, {0, 0, 65535, 0}}},
        {},
        false,
        false},

};

const Construction constructions[] = {

    {"distinct-components-live-pointers", {-7, 11, 101, 203}, 0, 0, {90, 165, 60}},

    {"null-borrowed-pointers", {0, 0, 0, 0}, -1, -1, {255, 0, 17}},

    {"signed-extrema-no-coordinate-arithmetic", {INT_MIN, 2147483647, -1, 1}, 1, -1, {1, 2, 3}},

    {"lookup-only-and-reversed-signs", {800, -600, -120, 77}, -1, 1, {128, 127, 254}},

};

template <class T> struct Frame
{
    Guard before = guard(0x17);
    T value{};
    Guard after = guard(0xe3);
};

// A genuine owner with a scoped borrowed table view for these method cases.
template <> struct Frame<ScreenTextLookup>
{
    text_owner_test::Bootstrap bootstrap;
    Guard before = guard(0x17);
    ScreenTextLookup value{"owner.txt"};
    Guard after = guard(0xe3);
    text_owner_test::RestoreOwnedTable restore{value, bootstrap};
};

// A genuine owner; snapshots below copy values into a different non-owning type.
template <> struct Frame<ScreenFontData>
{
    Guard before = guard(0x17);
    ScreenFontData value{"Fixture"};
    Guard after = guard(0xe3);

    Frame() = default;
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
};

struct FontFrameSnapshot
{
    Guard before;
    font_owner_test::Snapshot value;
    Guard after;

    explicit FontFrameSnapshot(const Frame<ScreenFontData>& frame)
        : before(frame.before)
        , value(frame.value)
        , after(frame.after)
    {
    }
};

struct Environment
{
    Frame<ScreenRequestRenderer> renderer;
    std::array<Frame<ScreenFontData>, 2> data;
    std::array<Frame<ScreenTextLookup>, 2> lookup;
    std::array<ScreenTextBucket, 2> buckets{};
    std::array<ScreenTextValue, 2> values{};
    std::array<unsigned short, 3> lookup_text{65, 66, 0};
};

struct EnvironmentSnapshot
{
    decltype(Environment::renderer) renderer;
    std::array<FontFrameSnapshot, 2> data;
    std::array<text_owner_test::FrameSnapshot<Guard>, 2> lookup;
    decltype(Environment::buckets) buckets;
    decltype(Environment::values) values;
    decltype(Environment::lookup_text) lookup_text;

    explicit EnvironmentSnapshot(const Environment& env)
        : renderer(env.renderer)
        , data{FontFrameSnapshot(env.data[0]), FontFrameSnapshot(env.data[1])}
        , lookup{text_owner_test::FrameSnapshot<Guard>(env.lookup[0]),
              text_owner_test::FrameSnapshot<Guard>(env.lookup[1])}
        , buckets(env.buckets)
        , values(env.values)
        , lookup_text(env.lookup_text)
    {
    }
};

void initialize(Environment& env)
{
    renderer_queue_test::initialize(env.renderer.value, 0x31);
    env.values[0] = {"literal-A", env.lookup_text.data()};
    env.values[1] = {"literal-B", env.lookup_text.data() + 1};
    for (std::size_t i = 0; i < env.buckets.size(); ++i)
    {
        auto& bucket = env.buckets[i];
        bucket.key = env.values[i].key;
        bucket.value = &env.values[i];
        bucket.unavailable = static_cast<unsigned char>(i);
        auto& lookup = env.lookup[i].value;
        lookup.end_index = 1;
        lookup.buckets = env.buckets.data();
        lookup.bucket_count = 2;
        lookup.capacity_04 = 2;
        renderer_queue_test::pattern(lookup.unknown_0c, 0x61U + static_cast<unsigned int>(i));
        lookup.used_14 = 0x71U + static_cast<unsigned int>(i);
    }
}

void initialize_data(ScreenFontData& data, unsigned int)
{
    font_owner_test::clear_metrics(data);
    data.unknown_10804[0] = 0x81a2b3c4U;
    data.unknown_10804[1] = 0x1938576aU;
    constexpr Glyph defaults[] = {{9, {0, 0, 7, 0}},
        {10, {0, 0, 5, 0}},
        {13, {0, 0, 9, 0}},
        {32, {0, 0, 2, 0}},
        {65, {0, 0, 4, 0}},
        {66, {0, 0, 6, 0}},
        {67, {0, 0, 3, 0}},
        {68, {0, 0, 5, 0}},
        {255, {0, 0, 5, 0}}};
    for (const auto& entry : defaults)
    {
        std::copy(entry.words.begin(), entry.words.end(), std::begin(data.glyphs[entry.index]));
    }
}

bool same_rectangle(const RenderRectangle& a, const RenderRectangle& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool same_data(const ScreenFontData& a, const font_owner_test::Snapshot& b)
{
    if (a.image != b.image || a.unknown_10804[0] != b.unknown_10804[0] ||
        a.unknown_10804[1] != b.unknown_10804[1] ||
        !std::equal(std::begin(a.kerning), std::end(a.kerning), std::begin(b.kerning)))
    {
        return false;
    }
    for (std::size_t i = 0; i < 256; ++i)
    {
        if (!std::equal(std::begin(a.glyphs[i]), std::end(a.glyphs[i]), std::begin(b.glyphs[i])))
        {
            return false;
        }
    }
    return true;
}

bool same_lookup(const ScreenTextLookup& a, const text_owner_test::Snapshot& b)
{
    return text_owner_test::equal(a, b);
}

void verify_environment(const Environment& env, const EnvironmentSnapshot& before)
{
    require(screen_request_renderer == &env.renderer.value, "renderer global changed");
    require(renderer_queue_test::equal(env.renderer.value, before.renderer.value),
        "renderer fields or queue changed");
    require(env.renderer.before == before.renderer.before, "renderer leading guard changed");
    require(env.renderer.after == before.renderer.after, "renderer trailing guard changed");
    for (std::size_t i = 0; i < env.data.size(); ++i)
    {
        require(same_data(env.data[i].value, before.data[i].value), "borrowed font data changed");
        require(env.data[i].before == before.data[i].before, "data leading guard changed");
        require(env.data[i].after == before.data[i].after, "data trailing guard changed");
        require(
            same_lookup(env.lookup[i].value, before.lookup[i].value), "borrowed lookup changed");
        require(env.lookup[i].before == before.lookup[i].before, "lookup leading guard changed");
        require(env.lookup[i].after == before.lookup[i].after, "lookup trailing guard changed");
        const auto& a = env.buckets[i];
        const auto& b = before.buckets[i];
        require(a.key == b.key && a.value == b.value && a.unavailable == b.unavailable,
            "borrowed bucket changed");
        require(env.values[i].key == before.values[i].key &&
                    env.values[i].text == before.values[i].text,
            "borrowed text value changed");
    }
    require(env.lookup_text == before.lookup_text, "borrowed lookup text changed");
}

struct FontSlot
{
    Guard before = guard(0x29);
    alignas(ScreenFontRenderer) unsigned char storage[sizeof(ScreenFontRenderer)];
    Guard after = guard(0xd7);
    ScreenFontRenderer* object = nullptr;

    FontSlot(const std::array<unsigned char, 3>& opaque)
    {
        // Observable byte stores precede the actual constructor. Assertions
        // concern its explicit opaque members, never ABI padding or a vptr.
        volatile unsigned char* observable = storage;
        for (std::size_t i = 0; i < sizeof(storage); ++i)
        {
            observable[i] = 0xc3;
        }
        for (std::size_t i = 0; i < opaque.size(); ++i)
        {
            observable[offsetof(ScreenFontRenderer, unknown_15) + i] = opaque[i];
        }
    }

    FontSlot(const FontSlot&) = delete;
    FontSlot& operator=(const FontSlot&) = delete;

    void construct(ScreenFontData* data, ScreenTextLookup* lookup)
    {
        object = ::new (static_cast<void*>(storage)) ScreenFontRenderer(data, lookup);
        ++constructors;
    }

    void end()
    {
        // End only this generated observed-prefix object's C++ lifetime. This
        // trivial implicit destructor is not a recovered retail font release.
        if (object != nullptr)
        {
            std::destroy_at(object);
            object = nullptr;
        }
    }

    ~FontSlot()
    {
        end();
    }

    void verify_guards() const
    {
        require(before == guard(0x29), "font leading guard changed");
        require(after == guard(0xd7), "font trailing guard changed");
    }
};

struct FontState
{
    RenderRectangle rectangle;
    unsigned int word_10;
    unsigned char state_14;
    std::array<unsigned char, 3> opaque;
    unsigned int word_18;
    ScreenFontData* data;
    ScreenTextLookup* lookup;
};

FontState state(const ScreenFontRenderer& font)
{
    return {font.rectangle,
        font.word_10,
        font.state_14,
        {font.unknown_15[0], font.unknown_15[1], font.unknown_15[2]},
        font.word_18,
        font.data_1c,
        font.lookup_20};
}

bool same_font(const ScreenFontRenderer& font, const FontState& before)
{
    return same_rectangle(font.rectangle, before.rectangle) && font.word_10 == before.word_10 &&
           font.state_14 == before.state_14 && font.word_18 == before.word_18 &&
           std::equal(
               std::begin(font.unknown_15), std::end(font.unknown_15), before.opaque.begin()) &&
           font.data_1c == before.data && font.lookup_20 == before.lookup;
}

void verify_construction(const FontSlot& slot,
    const RenderRectangle& rectangle,
    const std::array<unsigned char, 3>& opaque,
    ScreenFontData* data,
    ScreenTextLookup* lookup)
{
    const auto& font = *slot.object;
    require(same_rectangle(font.rectangle, rectangle), "constructor rectangle differs");
    require(font.word_10 == 0xffffffffU, "word_10 differs");
    require(font.word_18 == 0, "word_18 differs");
    require(font.state_14 == 0, "state_14 differs");
    require(std::equal(std::begin(font.unknown_15), std::end(font.unknown_15), opaque.begin()),
        "constructor opaque bytes changed");
    require(font.data_1c == data, "data pointer differs");
    require(font.lookup_20 == lookup, "lookup pointer differs");
    slot.verify_guards();
}

void construction_case(Environment& env, const Construction& literal)
{
    ++sessions;
    env.renderer.value.clip_rectangle = literal.rectangle;
    auto snapshot = std::make_unique<EnvironmentSnapshot>(env);
    auto* data =
        literal.data < 0 ? nullptr : &env.data[static_cast<std::size_t>(literal.data)].value;
    auto* lookup =
        literal.lookup < 0 ? nullptr : &env.lookup[static_cast<std::size_t>(literal.lookup)].value;
    FontSlot slot(literal.opaque);
    slot.construct(data, lookup);
    verify_construction(slot, literal.rectangle, literal.opaque, data, lookup);
    verify_environment(env, *snapshot);
    slot.end();
    slot.verify_guards();
    verify_environment(env, *snapshot);
}

void measurement_case(Environment& env, const Measurement& literal)
{
    ++sessions;
    initialize_data(env.data[0].value, 0x91);
    for (const auto& entry : literal.glyphs)
    {
        std::copy(entry.words.begin(),
            entry.words.end(),
            std::begin(env.data[0].value.glyphs[entry.index]));
    }
    for (const auto& entry : literal.kerning)
    {
        env.data[0].value.kerning[entry.index] = static_cast<signed char>(entry.value);
    }
    // Guards and text form one real ushort array. The special alias uses only
    // glyphs[0]'s existing four-element array, not a cross-member array view.
    std::vector<unsigned short> text(8, 0x5a3c);
    text.insert(text.end(), literal.repeat_count, literal.repeated_unit);
    text.insert(text.end(), literal.units.begin(), literal.units.end());
    text.insert(text.end(), 8, 0xc35a);
    const auto text_before = text;
    const unsigned short* input =
        literal.aliases_glyph_zero ? env.data[0].value.glyphs[0] : text.data() + 8;
    auto* data = literal.null_data ? nullptr : &env.data[0].value;
    auto* lookup = &env.lookup[0].value;
    const std::array<unsigned char, 3> opaque{0x5a, 0xa5, 0x3c};
    env.renderer.value.clip_rectangle = {-7, 11, 101, 203};
    auto snapshot = std::make_unique<EnvironmentSnapshot>(env);
    FontSlot slot(opaque);
    slot.construct(data, lookup);
    verify_construction(slot, env.renderer.value.clip_rectangle, opaque, data, lookup);
    verify_environment(env, *snapshot);
    const FontState before = state(*slot.object);

    const auto verify_after = [&]()
    {
        require(same_font(*slot.object, before), "measurement changed receiver");
        slot.verify_guards();
        verify_environment(env, *snapshot);
        require(text == text_before, "text or enclosing guards changed");
    };
    ++measurements_called;
    expect(slot.object->MeasureText(input), literal.expected[0], "MeasureText");
    verify_after();
    ++measurements_called;
    expect(slot.object->CountWrappedLines(input, literal.width, literal.spacing),
        literal.expected[1],
        "CountWrappedLines");
    verify_after();
    ++measurements_called;
    expect(slot.object->MeasureWrappedText(input, literal.width, literal.spacing),
        literal.expected[2],
        "MeasureWrappedText");
    verify_after();
    slot.end();
    slot.verify_guards();
    verify_environment(env, *snapshot);
    require(text == text_before, "text changed after prefix lifetime ended");
}

struct RendererBinding
{
    ScreenRequestRenderer* previous;

    explicit RendererBinding(ScreenRequestRenderer& renderer)
        : previous(screen_request_renderer)
    {
        screen_request_renderer = &renderer;
    }

    ~RendererBinding()
    {
        screen_request_renderer = previous;
    }
};
} // namespace

int main(int argc, char** argv)
{
    try
    {
        font_owner_test::host_require(argc == 2, "expected a fresh generated-font directory");
        font_owner_test::GeneratedFonts generated(argv[1]);
        generated.add("Fixture", font_owner_test::Input{});
        font_owner_test::files::Session files(generated.mount());
        auto env = std::make_unique<Environment>();
        initialize(*env);
        initialize_data(env->data[0].value, 0x91);
        initialize_data(env->data[1].value, 0x27);
        RendererBinding binding(env->renderer.value);
        category = "constructor";
        for (case_index = 0; case_index < std::size(constructions); ++case_index)
        {
            case_name = constructions[case_index].name;
            construction_case(*env, constructions[case_index]);
        }
        category = "measurement";
        for (case_index = 0; case_index < std::size(measurements); ++case_index)
        {
            case_name = measurements[case_index].name;
            measurement_case(*env, measurements[case_index]);
        }
        require(sessions == 37, "case count differs");
        require(constructors == 37, "constructor count differs");
        require(measurements_called == 99, "measurement call count differs");
        std::printf("font-measurement: %u sessions, %u constructors, %u measurements, %u checks\n",
            sessions,
            constructors,
            measurements_called,
            checks);
        return 0;
    }
    catch (const Failure&)
    {
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "font-owner setup: %s\n", error.what());
        return 3;
    }
}
