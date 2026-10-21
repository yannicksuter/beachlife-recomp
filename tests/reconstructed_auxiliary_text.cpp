#include "font_owner_test_data.h"
#include "renderer_queue_test_state.h"
#include "startup_screen_font_data.h"
#include "startup_screen_manager.h"
#include "startup_screen_manager_auxiliary.h"
#include "startup_screen_text.h"
#include "text_lookup_owner_test_data.h"

#include <algorithm>
#include <array>
#include <climits>
#include <clocale>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <vector>

#if !defined(BEACHLIFE_DECOMP_AUXILIARY_TEXT)
#error This fixture requires the experimental auxiliary text declaration.
#endif

namespace
{
static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(sizeof(int) == 4 && INT_MAX == 2147483647);
static_assert(std::is_standard_layout_v<ScreenManager>);
static_assert(std::is_standard_layout_v<ScreenManagerAuxiliaryState>);
static_assert(std::is_trivially_destructible_v<ScreenFontRenderer>);
static_assert(std::is_same_v<decltype(&ScreenManagerAuxiliaryState::SetText),
    void (ScreenManagerAuxiliaryState::*)(const char*)>);
static_assert(std::extent_v<decltype(ScreenManager::fonts_2c)> == 10);
static_assert(sizeof(void*) != 4 || offsetof(ScreenManager, fonts_2c) == 0x2c);
static_assert(sizeof(void*) != 4 || offsetof(ScreenManager, unknown_54) == 0x54);
static_assert(sizeof(void*) != 4 || sizeof(ScreenManager) == 0x13c);
static_assert(sizeof(void*) != 8 || sizeof(ScreenManager) == 424);
static_assert(sizeof(void*) != 4 || offsetof(ScreenManagerAuxiliaryState, text_08) == 8);
static_assert(sizeof(void*) != 4 || offsetof(ScreenManagerAuxiliaryState, frame_24) == 0x24);
static_assert(sizeof(void*) != 4 || sizeof(ScreenManagerAuxiliaryState) == 0x28);
static_assert(sizeof(void*) != 8 || sizeof(ScreenManagerAuxiliaryState) == 48);

using Guard = std::array<unsigned char, 16>;
struct Failure
{
};
std::size_t case_index = 0;
const char* case_name = "setup";
unsigned int sessions = 0;
unsigned int constructors = 0;
unsigned int calls = 0;
unsigned int checks = 0;

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "auxiliary-text: case %zu %s: %s\n", case_index, case_name, reason);
        throw Failure{};
    }
}

Guard guard(unsigned int seed)
{
    Guard value{};
    for (std::size_t i = 0; i < value.size(); ++i)
    {
        value[i] = static_cast<unsigned char>(seed + 17U * i);
    }
    return value;
}

template <class T> struct Frame
{
    Guard before = guard(0x27);
    T value{};
    Guard after = guard(0xd3);
};

// A genuine owner with a scoped borrowed table view for these method cases.
template <> struct Frame<ScreenTextLookup>
{
    text_owner_test::Bootstrap bootstrap;
    Guard before = guard(0x27);
    ScreenTextLookup value{"owner.txt"};
    Guard after = guard(0xd3);
    text_owner_test::RestoreOwnedTable restore{value, bootstrap};
};

// A genuine owner; snapshots below copy values into a different non-owning type.
template <> struct Frame<ScreenFontData>
{
    Guard before = guard(0x27);
    ScreenFontData value{"Fixture"};
    Guard after = guard(0xd3);

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

enum class Route
{
    Hit,
    Miss,
    Null,
    CaseFold,
    Duplicate,
    EndIsHit
};
struct Glyph
{
    unsigned int unit;
    unsigned short left;
    unsigned short right;
};
struct Kerning
{
    unsigned int index;
    int value;
};
struct Literal
{
    const char* name;
    Route route;
    unsigned int font_index;
    std::initializer_list<unsigned short> units;
    unsigned short repeat_unit;
    std::size_t repeat_count;
    std::initializer_list<Glyph> glyphs;
    std::initializer_list<Kerning> kerning;
    bool alias_glyph_zero;
    std::array<unsigned int, 4> expected;
};

// Explicit independent arithmetic/boundary literals; no measurement oracle runs here.
const Literal literals[] = {
    {"exact_160_control", Route::Hit, 3, {65, 65, 0}, 0, 0, {}, {}, false, {160, 15, 170, 21}},
    {"below_159", Route::Hit, 9, {65, 66, 0}, 0, 0, {{66, 0, 79}}, {}, false, {159, 15, 169, 21}},
    {"above_161_unsplit",
        Route::Hit,
        0,
        {65, 66, 0},
        0,
        0,
        {{66, 0, 81}},
        {},
        false,
        {171, 12, 171, 18}},
    {"empty_hit", Route::Hit, 3, {0}, 0, 0, {}, {}, false, {0, 15, 10, 21}},
    {"missing_uses_global_empty", Route::Miss, 9, {65, 0}, 0, 0, {}, {}, false, {0, 15, 10, 21}},
    {"null_key_uses_global_empty", Route::Null, 0, {65, 0}, 0, 0, {}, {}, false, {0, 15, 10, 21}},
    {"casefold_hit", Route::CaseFold, 3, {65, 0}, 0, 0, {}, {}, false, {80, 15, 90, 21}},
    {"first_duplicate_pointer",
        Route::Duplicate,
        9,
        {65, 0},
        0,
        0,
        {},
        {},
        false,
        {80, 15, 90, 21}},
    {"two_words", Route::Hit, 0, {65, 32, 66, 0}, 0, 0, {}, {}, false, {100, 24, 100, 30}},
    {"three_words",
        Route::Hit,
        3,
        {65, 32, 66, 32, 67, 0},
        0,
        0,
        {},
        {},
        false,
        {100, 36, 100, 42}},
    {"count_uses_published_width",
        Route::Hit,
        9,
        {65, 32, 66, 32, 67, 0},
        0,
        0,
        {{65, 0, 155}, {66, 0, 155}, {67, 0, 1}},
        {},
        false,
        {165, 24, 165, 30}},
    {"spacing_break_on_separator",
        Route::Hit,
        0,
        {65, 32, 66, 32, 67, 0},
        0,
        0,
        {{65, 0, 75}, {66, 0, 78}, {67, 0, 10}},
        {},
        false,
        {100, 24, 100, 30}},
    {"partial_word_rewind",
        Route::Hit,
        3,
        {65, 32, 66, 67, 68, 0},
        0,
        0,
        {{66, 0, 40}, {67, 0, 35}},
        {},
        false,
        {115, 24, 115, 30}},
    {"ushort_advance_wrap",
        Route::Hit,
        9,
        {65, 0},
        0,
        0,
        {{65, 2, 1}},
        {},
        false,
        {65545, 12, 65545, 18}},
    {"signed_kerning_short",
        Route::Hit,
        0,
        {65, 66, 0},
        0,
        0,
        {},
        {{16641, -11}},
        false,
        {159, 15, 169, 21}},
    {"tab_is_not_word_separator",
        Route::Hit,
        3,
        {65, 9, 66, 0},
        0,
        0,
        {},
        {},
        false,
        {187, 12, 187, 18}},
    {"embedded_terminator", Route::Hit, 9, {65, 0, 66, 0}, 0, 0, {}, {}, false, {80, 15, 90, 21}},
    {"uint_width_addition_wrap",
        Route::Hit,
        0,
        {0},
        65,
        65537,
        {{65, 0, 65535}},
        {},
        false,
        {9, 12, 9, 18}},
    {"found_bucket_equals_end", Route::EndIsHit, 3, {65, 0}, 0, 0, {}, {}, false, {0, 15, 10, 21}},
    {"glyph_array_alias", Route::Hit, 9, {65, 0}, 0, 0, {}, {}, true, {80, 15, 90, 21}},
};

struct Environment
{
    Frame<ScreenRequestRenderer> renderer;
    Frame<ScreenManager> manager;
    Frame<ScreenManagerAuxiliaryState> auxiliary;
    std::array<Frame<ScreenFontData>, 2> data;
    Frame<ScreenTextLookup> lookup;
    Frame<ScreenTextLookup> font_lookup;
    Frame<std::array<ScreenTextBucket, 3>> buckets;
    Frame<std::array<ScreenTextValue, 2>> values;
    Frame<std::array<char, 4>> query;
    std::array<Frame<std::array<char, 4>>, 2> keys;
    Frame<std::array<unsigned short, 3>> decoy_text;
    Frame<CircularListNode> sentinel;
};

struct EnvironmentSnapshot
{
    decltype(Environment::renderer) renderer;
    decltype(Environment::manager) manager;
    decltype(Environment::auxiliary) auxiliary;
    std::array<FontFrameSnapshot, 2> data;
    text_owner_test::FrameSnapshot<Guard> lookup;
    text_owner_test::FrameSnapshot<Guard> font_lookup;
    decltype(Environment::buckets) buckets;
    decltype(Environment::values) values;
    decltype(Environment::query) query;
    decltype(Environment::keys) keys;
    decltype(Environment::decoy_text) decoy_text;
    decltype(Environment::sentinel) sentinel;

    explicit EnvironmentSnapshot(const Environment& env)
        : renderer(env.renderer)
        , manager(env.manager)
        , auxiliary(env.auxiliary)
        , data{FontFrameSnapshot(env.data[0]), FontFrameSnapshot(env.data[1])}
        , lookup(env.lookup)
        , font_lookup(env.font_lookup)
        , buckets(env.buckets)
        , values(env.values)
        , query(env.query)
        , keys(env.keys)
        , decoy_text(env.decoy_text)
        , sentinel(env.sentinel)
    {
    }
};

void initialize_data(ScreenFontData& data, bool selected)
{
    font_owner_test::clear_metrics(data);
    data.unknown_10804[0] = 0x12345678U;
    data.unknown_10804[1] = 0x87654321U;
    constexpr Glyph defaults[] = {{9, 0, 7},
        {10, 0, 5},
        {13, 0, 9},
        {32, 0, 2},
        {65, 0, 80},
        {66, 0, 90},
        {67, 0, 70},
        {68, 0, 30}};
    for (const auto& entry : defaults)
    {
        data.glyphs[entry.unit][0] = entry.left;
        data.glyphs[entry.unit][2] = selected ? entry.right : 1;
    }
}

void initialize(Environment& e, const Literal& literal, const unsigned short* input)
{
    renderer_queue_test::initialize(e.renderer.value, 0x51);
    initialize_data(e.data[0].value, true);
    initialize_data(e.data[1].value, false);
    for (const auto& entry : literal.glyphs)
    {
        e.data[0].value.glyphs[entry.unit][0] = entry.left;
        e.data[0].value.glyphs[entry.unit][2] = entry.right;
    }
    for (const auto& entry : literal.kerning)
    {
        e.data[0].value.kerning[entry.index] = static_cast<signed char>(entry.value);
    }
    if (literal.alias_glyph_zero)
    {
        auto& glyph = e.data[0].value.glyphs[0];
        glyph[0] = 65;
        glyph[1] = glyph[2] = glyph[3] = 0;
        input = glyph;
    }

    e.query.value = {literal.route == Route::Miss       ? 'Z'
                     : literal.route == Route::CaseFold ? 'a'
                                                        : 'A',
        0,
        'q',
        'r'};
    e.keys[0].value = {'A', 0, 's', 't'};
    e.keys[1].value = {'a', 0, 'u', 'v'};
    e.decoy_text.value = {66, 0, 67};
    e.values.value[0] = {e.keys[0].value.data(), input};
    e.values.value[1] = {e.keys[1].value.data(), e.decoy_text.value.data()};
    for (auto& bucket : e.buckets.value)
    {
        bucket = text_owner_test::bucket(e.keys[0].value.data(), &e.values.value[0], 1);
    }
    e.buckets.value[0].unavailable = 0;
    auto& lookup = e.lookup.value;
    lookup.end_index = literal.route == Route::EndIsHit ? 0 : 1;
    lookup.bucket_count = 1;
    lookup.buckets = e.buckets.value.data();
    lookup.capacity_04 = 3;
    renderer_queue_test::pattern(lookup.unknown_0c, 0x23);
    lookup.used_14 = 0x34U;
    if (literal.route == Route::Duplicate)
    {
        lookup.end_index = 3;
        lookup.bucket_count = 3;
        e.buckets.value[0].key = e.keys[1].value.data();
        e.buckets.value[0].value = &e.values.value[1];
        e.buckets.value[2].unavailable = 0;
    }
    // Font constructors receive a distinct genuine owner with the same borrowed
    // view. SetText still resolves through the global rather than the font field.
    text_owner_test::copy_view(e.font_lookup.value, lookup);
    e.font_lookup.value.end_index = 0;

    auto& manager = e.manager.value;
    manager.renderer = &e.renderer.value;
    std::fill(std::begin(manager.fonts_04), std::end(manager.fonts_04), nullptr);
    renderer_queue_test::pattern(manager.unknown_54, 0x56);
    manager.item_90 = nullptr;
    renderer_queue_test::pattern(manager.unknown_94, 0x67);
    renderer_queue_test::pattern(manager.unknown_c0, 0x78);
    manager.registry = nullptr;
    e.sentinel.value = {&e.sentinel.value, &e.sentinel.value};
    manager.active.head = &e.sentinel.value;
    manager.mode = -17;
    manager.previous_mode = 23;
    manager.auxiliary_130 = &e.auxiliary.value;
    manager.item_134 = nullptr;
    manager.value_138 = -2.25f;

    auto& auxiliary = e.auxiliary.value;
    auxiliary.word_00 = literal.font_index;
    auxiliary.state_04 = static_cast<unsigned char>(0x80U + case_index);
    renderer_queue_test::pattern(auxiliary.unknown_05, 0x89);
    auxiliary.text_08 = e.decoy_text.value.data();
    auxiliary.text_width_0c = 0x10203040U;
    auxiliary.text_height_10 = 0x50607080U;
    auxiliary.width_14 = 0x90a0b0c0U;
    auxiliary.height_18 = 0xd0e0f001U;
    auxiliary.x_1c = -321;
    auxiliary.y_20 = 654;
    // The real SetText body never accesses this borrowed pointer. No Frame
    // object, virtual body or retail constructor is fabricated by this fixture.
    auxiliary.frame_24 = nullptr;
}

struct Bindings
{
    ScreenManager* manager = screen_manager;
    ScreenTextLookup* lookup = screen_text_lookup;
    ScreenRequestRenderer* renderer = screen_request_renderer;
    explicit Bindings(Environment& e)
    {
        screen_manager = &e.manager.value;
        screen_text_lookup = &e.lookup.value;
        screen_request_renderer = &e.renderer.value;
    }
    ~Bindings()
    {
        screen_request_renderer = renderer;
        screen_text_lookup = lookup;
        screen_manager = manager;
    }
};

struct FontFrame
{
    Guard before = guard(0x36);
    ScreenFontRenderer value;
    Guard after = guard(0xc8);
    FontFrame(ScreenFontData* data, ScreenTextLookup* lookup)
        : value(data, lookup)
    {
        ++constructors;
        require(value.data_1c == data && value.lookup_20 == lookup,
            "real font constructor borrowed pointers differ");
        require(value.word_10 == 0xffffffffU && value.word_18 == 0 && value.state_14 == 0,
            "real font constructor scalar state differs");
        // Initialize the actual opaque members after construction. Their
        // pre-lifetime preservation already belongs to the separate font test.
        renderer_queue_test::pattern(value.unknown_15, 0x9a);
    }
    // Only the generated observed-prefix C++ lifetime ends here. This does not
    // implement or claim the unknown retail font/data destruction or ownership.
};

bool same_rectangle(const RenderRectangle& a, const RenderRectangle& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

struct FontState
{
    RenderRectangle rectangle;
    unsigned int word10;
    unsigned char state;
    std::array<unsigned char, 3> opaque;
    unsigned int word18;
    ScreenFontData* data;
    ScreenTextLookup* lookup;
};

FontState state(const ScreenFontRenderer& f)
{
    return {f.rectangle,
        f.word_10,
        f.state_14,
        {f.unknown_15[0], f.unknown_15[1], f.unknown_15[2]},
        f.word_18,
        f.data_1c,
        f.lookup_20};
}

void verify_font(const FontFrame& frame, const FontState& before)
{
    const auto& f = frame.value;
    require(same_rectangle(f.rectangle, before.rectangle) && f.word_10 == before.word10 &&
                f.state_14 == before.state && f.word_18 == before.word18 &&
                f.data_1c == before.data && f.lookup_20 == before.lookup &&
                std::equal(std::begin(f.unknown_15), std::end(f.unknown_15), before.opaque.begin()),
        "measurement changed font receiver");
    require(frame.before == guard(0x36) && frame.after == guard(0xc8), "font guards changed");
}

template <class A, class B> void verify_guards(const A& a, const B& b)
{
    require(a.before == b.before && a.after == b.after, "borrowed object guards changed");
}

void verify_lookup(const ScreenTextLookup& a, const text_owner_test::Snapshot& b)
{
    require(text_owner_test::equal(a, b), "lookup owner or borrowed view changed");
}

void verify_environment(const Environment& a, const EnvironmentSnapshot& b)
{
    require(renderer_queue_test::equal(a.renderer.value, b.renderer.value),
        "renderer fields or actual queue changed");
    verify_guards(a.renderer, b.renderer);
    const auto& m = a.manager.value;
    const auto& n = b.manager.value;
    require(m.renderer == n.renderer && m.registry == n.registry &&
                m.active.head == n.active.head && m.mode == n.mode &&
                m.previous_mode == n.previous_mode && m.auxiliary_130 == n.auxiliary_130 &&
                m.item_134 == n.item_134 && m.value_138 == n.value_138 &&
                std::equal(std::begin(m.fonts_2c), std::end(m.fonts_2c), std::begin(n.fonts_2c)) &&
                std::equal(std::begin(m.fonts_04), std::end(m.fonts_04), std::begin(n.fonts_04)) &&
                renderer_queue_test::same_bytes(m.unknown_54, n.unknown_54) &&
                m.item_90 == n.item_90 &&
                renderer_queue_test::same_bytes(m.unknown_94, n.unknown_94) &&
                renderer_queue_test::same_bytes(m.unknown_c0, n.unknown_c0),
        "manager prefix changed");
    verify_guards(a.manager, b.manager);
    const auto& x = a.auxiliary.value;
    const auto& y = b.auxiliary.value;
    require(x.word_00 == y.word_00 && x.state_04 == y.state_04 &&
                renderer_queue_test::same_bytes(x.unknown_05, y.unknown_05) && x.x_1c == y.x_1c &&
                x.y_20 == y.y_20 && x.frame_24 == y.frame_24,
        "auxiliary retained fields changed");
    verify_guards(a.auxiliary, b.auxiliary);
    for (std::size_t i = 0; i < a.data.size(); ++i)
    {
        const auto& p = a.data[i].value;
        const auto& q = b.data[i].value;
        require(p.image == q.image &&
                    std::equal(std::begin(p.unknown_10804),
                        std::end(p.unknown_10804),
                        std::begin(q.unknown_10804)) &&
                    std::equal(std::begin(p.kerning), std::end(p.kerning), std::begin(q.kerning)),
            "font data or kerning changed");
        for (std::size_t glyph = 0; glyph < 256; ++glyph)
        {
            require(std::equal(std::begin(p.glyphs[glyph]),
                        std::end(p.glyphs[glyph]),
                        std::begin(q.glyphs[glyph])),
                "borrowed glyph changed");
        }
        verify_guards(a.data[i], b.data[i]);
    }
    verify_lookup(a.lookup.value, b.lookup.value);
    verify_lookup(a.font_lookup.value, b.font_lookup.value);
    verify_guards(a.lookup, b.lookup);
    verify_guards(a.font_lookup, b.font_lookup);
    for (std::size_t i = 0; i < a.buckets.value.size(); ++i)
    {
        const auto& p = a.buckets.value[i];
        const auto& q = b.buckets.value[i];
        require(p.key == q.key && p.value == q.value && p.unavailable == q.unavailable,
            "lookup bucket changed");
    }
    for (std::size_t i = 0; i < a.values.value.size(); ++i)
    {
        require(a.values.value[i].key == b.values.value[i].key &&
                    a.values.value[i].text == b.values.value[i].text,
            "lookup value changed");
        require(a.keys[i].value == b.keys[i].value, "borrowed key changed");
        verify_guards(a.keys[i], b.keys[i]);
    }
    verify_guards(a.buckets, b.buckets);
    verify_guards(a.values, b.values);
    require(a.query.value == b.query.value && a.decoy_text.value == b.decoy_text.value,
        "query or alternate text changed");
    verify_guards(a.query, b.query);
    verify_guards(a.decoy_text, b.decoy_text);
    require(a.sentinel.value.next == b.sentinel.value.next &&
                a.sentinel.value.previous == b.sentinel.value.previous,
        "unread manager list changed");
    verify_guards(a.sentinel, b.sentinel);
}

void run_case(const Literal& literal)
{
    std::vector<unsigned short> text(8, 0x5a3c);
    text.insert(text.end(), literal.repeat_count, literal.repeat_unit);
    text.insert(text.end(), literal.units.begin(), literal.units.end());
    text.insert(text.end(), 8, 0xc35a);
    const auto text_before = text;
    auto env = std::make_unique<Environment>();
    initialize(*env, literal, text.data() + 8);
    ScreenManager* const prior_manager = screen_manager;
    ScreenTextLookup* const prior_lookup = screen_text_lookup;
    ScreenRequestRenderer* const prior_renderer = screen_request_renderer;
    {
        Bindings bindings(*env);
        FontFrame selected(&env->data[0].value, &env->font_lookup.value);
        FontFrame decoy(&env->data[1].value, &env->font_lookup.value);
        for (auto& entry : env->manager.value.fonts_2c)
        {
            entry = &decoy.value;
        }
        env->manager.value.fonts_2c[literal.font_index] = &selected.value;
        const auto selected_before = state(selected.value);
        const auto decoy_before = state(decoy.value);
        auto before = std::make_unique<EnvironmentSnapshot>(*env);
        const char* query = literal.route == Route::Null ? nullptr : env->query.value.data();
        const bool fallback = literal.route == Route::Miss || literal.route == Route::Null ||
                              literal.route == Route::EndIsHit;
        const unsigned short* expected_text =
            fallback ? screen_text_empty : env->values.value[0].text;
        env->auxiliary.value.SetText(query);
        ++calls;
        const auto& a = env->auxiliary.value;
        require(a.text_08 == expected_text, "borrowed text publication differs");
        require(a.text_width_0c == literal.expected[0], "text width differs");
        require(a.text_height_10 == literal.expected[1], "text height differs");
        require(a.width_14 == literal.expected[2], "outer width differs");
        require(a.height_18 == literal.expected[3], "outer height differs");
        require(screen_manager == &env->manager.value && screen_text_lookup == &env->lookup.value &&
                    screen_request_renderer == &env->renderer.value,
            "actual global binding changed");
        require(screen_text_empty[0] == 0, "real empty global changed");
        verify_font(selected, selected_before);
        verify_font(decoy, decoy_before);
        verify_environment(*env, *before);
        require(text == text_before, "borrowed text or enclosing guards changed");
    }
    require(screen_manager == prior_manager && screen_text_lookup == prior_lookup &&
                screen_request_renderer == prior_renderer,
        "fixture did not restore the actual globals");
    ++sessions;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        font_owner_test::host_require(argc == 2, "expected a fresh generated-font directory");
        font_owner_test::GeneratedFonts generated(argv[1]);
        generated.add("Fixture", font_owner_test::Input{});
        font_owner_test::files::Session files(generated.mount());
        require(std::setlocale(LC_CTYPE, "C") != nullptr, "C locale unavailable");
        require(screen_manager == nullptr && screen_text_lookup == nullptr &&
                    screen_request_renderer == nullptr && screen_text_empty[0] == 0,
            "actual global initial state differs");
        for (case_index = 0; case_index < std::size(literals); ++case_index)
        {
            case_name = literals[case_index].name;
            run_case(literals[case_index]);
        }
        require(sessions == 20 && calls == 20 && constructors == 40, "literal call totals differ");
        require(screen_manager == nullptr && screen_text_lookup == nullptr &&
                    screen_request_renderer == nullptr,
            "final global state differs");
        std::printf("auxiliary-text: %u sessions, %u SetText calls, %u font constructors, "
                    "%u checks; manager %zu, auxiliary %zu\n",
            sessions,
            calls,
            constructors,
            checks,
            sizeof(ScreenManager),
            sizeof(ScreenManagerAuxiliaryState));
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
