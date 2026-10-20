#include "renderer_queue_test_state.h"
#include "startup_frame_clock.h"
#include "startup_input_motion.h"
#include "startup_screen_font_data.h"
#include "startup_screen_item.h"
#include "startup_screen_manager.h"
#include "startup_screen_manager_auxiliary.h"
#include "startup_screen_text.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>

#if !defined(BEACHLIFE_DECOMP_COMPLETE_SCREEN_ITEM)
#error This fixture requires the complete experimental ScreenItem declaration.
#endif

namespace
{
static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && sizeof(unsigned int) == 4);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(!std::is_abstract_v<ScreenItem> && std::has_virtual_destructor_v<ScreenItem>);
static_assert(std::is_same_v<decltype(&ScreenItem::Unresolved04), void (ScreenItem::*)(float)>);
static_assert(std::is_same_v<decltype(&ScreenItem::Unresolved20), unsigned char (ScreenItem::*)()>);

using Guard = std::array<unsigned char, 16>;
struct Failure
{
};
struct Counts
{
    unsigned int sessions = 0;
    unsigned int checks = 0;
    unsigned int constructions = 0;
    unsigned int destructions = 0;
    unsigned int font_constructions = 0;
    unsigned int text_calls = 0;
    unsigned int retained_text_releases = 0;
    unsigned int event_calls = 0;
    unsigned int coordinate_calls = 0;
    unsigned int callbacks = 0;
} counts;
const char* case_name = "setup";

void require(bool condition, const char* reason)
{
    ++counts.checks;
    if (!condition)
    {
        std::fprintf(stderr, "screen-item-complete: %s: %s\n", case_name, reason);
        throw Failure{};
    }
}

void begin(const char* name)
{
    case_name = name;
    ++counts.sessions;
}

Guard guard(unsigned int seed)
{
    Guard result{};
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        result[i] = static_cast<unsigned char>(seed + i * 19U);
    }
    return result;
}

template <class T> struct Frame
{
    Guard before = guard(0x23);
    T value{};
    Guard after = guard(0xc7);
};

template <class T> void verify_guards(const Frame<T>& f)
{
    require(f.before == guard(0x23) && f.after == guard(0xc7), "borrowed storage guards changed");
}

bool same_float(float a, float b)
{
    return std::bit_cast<unsigned int>(a) == std::bit_cast<unsigned int>(b);
}

struct ItemState
{
    ScreenRectangle rectangle;
    ScreenRectangle saved;
    float elapsed;
    int kind;
    unsigned int word;
    unsigned char state30;
    std::array<unsigned char, 3> opaque;
    int x;
    int y;
    char* text;
    unsigned char state40;
    unsigned char state41;
    unsigned char state42;
    unsigned char prepared;
};

ItemState state(const ScreenItem& v)
{
    return {v.rectangle,
        v.saved_rectangle,
        v.elapsed_24,
        v.kind_28,
        v.word_2c,
        v.state_30,
        {v.unknown_31[0], v.unknown_31[1], v.unknown_31[2]},
        v.last_x,
        v.last_y,
        v.owned_text,
        v.state_40,
        v.state_41,
        v.state_42,
        v.preparation_flag};
}

void verify_item(const ScreenItem& v, const ItemState& s)
{
    require(renderer_queue_test::same_rectangle(v.rectangle, s.rectangle) &&
                renderer_queue_test::same_rectangle(v.saved_rectangle, s.saved),
        "item rectangles differ");
    require(same_float(v.elapsed_24, s.elapsed), "item elapsed differs");
    require(v.kind_28 == s.kind && v.word_2c == s.word && v.state_30 == s.state30 &&
                std::equal(std::begin(v.unknown_31), std::end(v.unknown_31), s.opaque.begin()),
        "item retained prefix differs");
    require(v.last_x == s.x && v.last_y == s.y && v.owned_text == s.text,
        "item coordinates or text pointer differ");
    require(v.state_40 == s.state40 && v.state_41 == s.state41 && v.state_42 == s.state42 &&
                v.preparation_flag == s.prepared,
        "item state bytes differ");
}

template <class T = ScreenItem> struct ItemBox
{
    Guard before = guard(0x47);
    std::optional<T> value;
    Guard after = guard(0x9b);

    ItemBox()
    {
        value.emplace(ScreenRectangle{2, 3, 7, 9});
        ++counts.constructions;
        // Only these fields are left untouched by the actual base constructor.
        value->kind_28 = 22;
        value->word_2c = 0x87654321U;
        renderer_queue_test::pattern(value->unknown_31, 0x59);
        require(value->owned_text == nullptr && value->elapsed_24 == 0.0f && value->state_30 == 0 &&
                    value->state_40 == 0 && value->state_41 == 0 && value->state_42 == 0 &&
                    value->preparation_flag == 1 && value->last_x == 0 && value->last_y == 0,
            "real base constructor state differs");
    }
    void close()
    {
        require(value.has_value(), "duplicate item destruction");
        value.reset(); // Actual base destructor; no dead typed member is read below.
        ++counts.destructions;
        require(before == guard(0x47) && after == guard(0x9b), "item lifetime guards changed");
    }
};

void text(ScreenItem& item, const char* input)
{
    item.SetOwnedText(input);
    ++counts.text_calls;
}

struct Environment
{
    Frame<ScreenRequestRenderer> renderer;
    Frame<ScreenManager> manager;
    Frame<ScreenManagerAuxiliaryState> auxiliary;
    Frame<FrameClock> clock;
    Frame<ScreenFontData> data;
    Frame<ScreenTextLookup> lookup;
    Frame<std::array<ScreenTextBucket, 2>> buckets;
    Frame<ScreenTextValue> entry;
    Frame<std::array<char, 2>> key;
    Frame<std::array<unsigned short, 2>> units;

    Environment()
    {
        renderer_queue_test::initialize(renderer.value, 0x31);
        auto& m = manager.value;
        m.renderer = &renderer.value;
        renderer_queue_test::pattern(m.unknown_04, 0x41);
        renderer_queue_test::pattern(m.unknown_54, 0x52);
        renderer_queue_test::pattern(m.unknown_90, 0x63);
        renderer_queue_test::pattern(m.unknown_c0, 0x74);
        m.mode = -11;
        m.previous_mode = 13;
        m.auxiliary_130 = &auxiliary.value;
        m.value_138 = 0.0f;
        auto& a = auxiliary.value;
        a.word_00 = 0;
        a.state_04 = 1;
        renderer_queue_test::pattern(a.unknown_05, 0x85);
        a.text_08 = screen_text_empty;
        a.text_width_0c = 101;
        a.text_height_10 = 102;
        a.width_14 = 103;
        a.height_18 = 104;
        a.x_1c = -321;
        a.y_20 = 654;
        a.frame_24 = nullptr; // Never accessed: Position's real early return only.
        clock.value = {37, 41, 43, 47, 2.0f, 1.5f, 0.25f, 4.0f};
        renderer_queue_test::pattern(data.value.unknown_00, 0x96);
        data.value.glyphs[65][2] = 80;
        data.value.unknown_10804[0] = 0x12345678U;
        data.value.unknown_10804[1] = 0xabcdef01U;
        key.value = {'A', 0};
        units.value = {65, 0};
        entry.value = {key.value.data(), units.value.data()};
        buckets.value[0] = {key.value.data(), &entry.value, 0, {0x37, 0x49, 0x5b}};
        buckets.value[1] = {nullptr, nullptr, 1, {0x69, 0x7b, 0x8d}};
        lookup.value.end_index = 1;
        lookup.value.buckets = buckets.value.data();
        lookup.value.bucket_count = 1;
        renderer_queue_test::pattern(lookup.value.unknown_04, 0xa7);
        renderer_queue_test::pattern(lookup.value.unknown_0c, 0xb8);
        renderer_queue_test::pattern(lookup.value.unknown_14, 0xc9);
    }
};

struct Bindings
{
    ScreenManager* manager = screen_manager;
    ScreenTextLookup* lookup = screen_text_lookup;
    ScreenRequestRenderer* renderer = screen_request_renderer;
    FrameClock* clock = startup_frame_clock;
    InputState* input = startup_input_state;

    explicit Bindings(Environment& e)
    {
        screen_manager = &e.manager.value;
        screen_text_lookup = &e.lookup.value;
        screen_request_renderer = &e.renderer.value;
        startup_frame_clock = &e.clock.value;
    }
    ~Bindings()
    {
        startup_frame_clock = clock;
        screen_request_renderer = renderer;
        screen_text_lookup = lookup;
        screen_manager = manager;
    }
    void verify(const Environment& e, ScreenManager* expected_manager) const
    {
        require(screen_manager == expected_manager && screen_text_lookup == &e.lookup.value &&
                    screen_request_renderer == &e.renderer.value &&
                    startup_frame_clock == &e.clock.value && startup_input_state == input,
            "actual global owner bindings differ");
        require(screen_text_empty[0] == 0, "real empty text changed");
    }
};

struct FontBox
{
    Guard before = guard(0x18);
    ScreenFontRenderer value;
    Guard after = guard(0xe2);

    explicit FontBox(Environment& e)
        : value(&e.data.value, &e.lookup.value)
    {
        ++counts.font_constructions;
        renderer_queue_test::pattern(value.unknown_15, 0x72);
        require(value.data_1c == &e.data.value && value.lookup_20 == &e.lookup.value &&
                    value.word_10 == 0xffffffffU && value.word_18 == 0 && value.state_14 == 0,
            "real font constructor differs");
        for (auto& font : e.manager.value.fonts_2c)
        {
            font = &value;
        }
    }
    void verify(const ScreenFontRenderer& s) const
    {
        require(value.rectangle.x == s.rectangle.x && value.rectangle.y == s.rectangle.y &&
                    value.rectangle.width == s.rectangle.width &&
                    value.rectangle.height == s.rectangle.height && value.word_10 == s.word_10 &&
                    value.state_14 == s.state_14 && value.word_18 == s.word_18 &&
                    renderer_queue_test::same_bytes(value.unknown_15, s.unknown_15) &&
                    value.data_1c == s.data_1c && value.lookup_20 == s.lookup_20,
            "borrowed font fields changed");
        require(before == guard(0x18) && after == guard(0xe2), "font guards changed");
    }
    // This observed-prefix C++ lifetime does not implement unknown font ownership.
};

void verify_manager(const ScreenManager& a, const ScreenManager& b)
{
    require(a.renderer == b.renderer && a.registry == b.registry &&
                a.active.head == b.active.head && a.mode == b.mode &&
                a.previous_mode == b.previous_mode && a.auxiliary_130 == b.auxiliary_130 &&
                a.item_134 == b.item_134 && same_float(a.value_138, b.value_138) &&
                std::equal(std::begin(a.fonts_2c), std::end(a.fonts_2c), std::begin(b.fonts_2c)) &&
                renderer_queue_test::same_bytes(a.unknown_04, b.unknown_04) &&
                renderer_queue_test::same_bytes(a.unknown_54, b.unknown_54) &&
                renderer_queue_test::same_bytes(a.unknown_90, b.unknown_90) &&
                renderer_queue_test::same_bytes(a.unknown_c0, b.unknown_c0),
        "manager fields differ");
}

void verify_environment(const Environment& a, const Environment& b)
{
    verify_manager(a.manager.value, b.manager.value);
    const auto& x = a.auxiliary.value;
    const auto& y = b.auxiliary.value;
    require(x.word_00 == y.word_00 && x.state_04 == y.state_04 && x.text_08 == y.text_08 &&
                x.text_width_0c == y.text_width_0c && x.text_height_10 == y.text_height_10 &&
                x.width_14 == y.width_14 && x.height_18 == y.height_18 && x.x_1c == y.x_1c &&
                x.y_20 == y.y_20 && x.frame_24 == y.frame_24 &&
                renderer_queue_test::same_bytes(x.unknown_05, y.unknown_05),
        "auxiliary text, metrics or retained position differ");
    require(renderer_queue_test::equal(a.renderer.value, b.renderer.value), "renderer changed");
    const auto& c = a.clock.value;
    const auto& d = b.clock.value;
    require(c.start_ticks == d.start_ticks && c.sampled_frames == d.sampled_frames &&
                c.frames == d.frames && c.previous_frames == d.previous_frames &&
                same_float(c.seconds, d.seconds) &&
                same_float(c.previous_seconds, d.previous_seconds) &&
                same_float(c.delta_seconds, d.delta_seconds) &&
                same_float(c.sample_seconds, d.sample_seconds),
        "clock fields differ");
    const auto& f = a.data.value;
    const auto& g = b.data.value;
    require(renderer_queue_test::same_bytes(f.unknown_00, g.unknown_00) &&
                std::equal(std::begin(f.unknown_10804),
                    std::end(f.unknown_10804),
                    std::begin(g.unknown_10804)) &&
                std::equal(std::begin(f.kerning), std::end(f.kerning), std::begin(g.kerning)),
        "font data changed");
    for (std::size_t i = 0; i < 256; ++i)
    {
        require(std::equal(std::begin(f.glyphs[i]), std::end(f.glyphs[i]), std::begin(g.glyphs[i])),
            "borrowed glyph changed");
    }
    const auto& l = a.lookup.value;
    const auto& m = b.lookup.value;
    require(l.end_index == m.end_index && l.buckets == m.buckets &&
                l.bucket_count == m.bucket_count &&
                renderer_queue_test::same_bytes(l.unknown_04, m.unknown_04) &&
                renderer_queue_test::same_bytes(l.unknown_0c, m.unknown_0c) &&
                renderer_queue_test::same_bytes(l.unknown_14, m.unknown_14),
        "lookup changed");
    for (std::size_t i = 0; i < a.buckets.value.size(); ++i)
    {
        const auto& p = a.buckets.value[i];
        const auto& q = b.buckets.value[i];
        require(p.key == q.key && p.value == q.value && p.unavailable == q.unavailable &&
                    renderer_queue_test::same_bytes(p.unknown_09, q.unknown_09),
            "bucket changed");
    }
    require(a.entry.value.key == b.entry.value.key && a.entry.value.text == b.entry.value.text &&
                a.key.value == b.key.value && a.units.value == b.units.value,
        "borrowed text changed");
    verify_guards(a.renderer);
    verify_guards(a.manager);
    verify_guards(a.auxiliary);
    verify_guards(a.clock);
    verify_guards(a.data);
    verify_guards(a.lookup);
    verify_guards(a.buckets);
    verify_guards(a.entry);
    verify_guards(a.key);
    verify_guards(a.units);
}

void expect_clear(Environment& expected)
{
    expected.manager.value.item_134 = nullptr;
    expected.manager.value.value_138 = 2.5f;
    expected.auxiliary.value.state_04 = 0;
}

void expect_notification(
    Environment& expected, const unsigned short* actual_units, ScreenItem* receiver)
{
    expected.manager.value.item_134 = receiver;
    auto& a = expected.auxiliary.value;
    a.text_08 = actual_units;
    a.text_width_0c = 80;
    a.text_height_10 = 15;
    a.width_14 = 90;
    a.height_18 = 21;
}

void defaults_and_bytes()
{
    begin("defaults");
    ItemBox item;
    ItemBox other;
    ScreenItem& v = *item.value;
    const auto before = state(v);
    v.Unresolved04(-0.25f);
    v.Unresolved08();
    v.Unresolved0c();
    v.Unresolved10(nullptr);
    v.Unresolved10(&*other.value);
    v.Unresolved1c();
    v.Unresolved28();
    v.Unresolved4c(0x80000001U);
    require(!v.HandleInput38(-9, 17, 3), "constant-false input changed");
    verify_item(v, before);
    item.close();
    other.close();
    constexpr unsigned char bytes[] = {0, 1, 128, 255};
    for (unsigned char byte : bytes)
    {
        begin("raw_state_bytes");
        ItemBox box;
        auto& i = *box.value;
        i.state_30 = i.state_40 = i.state_41 = i.state_42 = byte;
        const auto s = state(i);
        require(i.Unresolved20() == byte && i.ReadState40() == byte && i.ReadState41() == byte &&
                    i.ReadState42() == byte,
            "raw state byte normalized");
        verify_item(i, s);
        box.close();
    }
}

struct HitCase
{
    const char* name;
    int x;
    int y;
    bool hit;
    unsigned char state;
};
constexpr HitCase hit_cases[] = {{"hit_right_control", 7, 5, true, 1},
    {"hit_top_left", 2, 3, true, 1},
    {"hit_bottom_right", 7, 9, true, 1},
    {"miss_left", 1, 5, false, 109},
    {"miss_right", 8, 5, false, 109},
    {"miss_top", 4, 2, false, 109},
    {"miss_bottom", 4, 10, false, 109}};

void hits()
{
    for (const auto& c : hit_cases)
    {
        begin(c.name);
        ItemBox box;
        box.value->state_40 = 109;
        auto expected = state(*box.value);
        expected.state40 = c.state;
        require(box.value->TestRectangle(c.x, c.y) == c.hit, "hit result differs");
        verify_item(*box.value, expected);
        box.close();
    }
    begin("hit_then_sticky_miss");
    ItemBox sticky;
    sticky.value->state_40 = 109;
    auto expected = state(*sticky.value);
    expected.state40 = 1;
    require(sticky.value->TestRectangle(7, 5), "first hit differs");
    require(!sticky.value->TestRectangle(8, 5), "following miss differs");
    verify_item(*sticky.value, expected);
    sticky.close();
    begin("inverted_rectangle");
    ItemBox inverted;
    inverted.value->rectangle = {7, 3, 2, 9};
    inverted.value->state_40 = 109;
    expected = state(*inverted.value);
    require(!inverted.value->TestRectangle(4, 5), "inverted hit differs");
    verify_item(*inverted.value, expected);
    inverted.close();
}

void geometry_and_activation()
{
    begin("geometry_and_activation");
    auto env = std::make_unique<Environment>();
    Bindings bind(*env);
    ItemBox box;
    auto& i = *box.value;
    env->manager.value.item_134 = &i;
    auto expected_env = std::make_unique<Environment>(*env);
    auto expected = state(i);
    i.preparation_flag = 0;
    i.MarkPrepared();
    i.ApplyPosition(-2, 4);
    expected.rectangle = {0, 7, 5, 13};
    verify_item(i, expected);
    i.MoveRectangle(3, -2);
    expected.rectangle = {3, 5, 8, 11};
    require(renderer_queue_test::same_rectangle(i.GetRectangle(), expected.rectangle),
        "real getter rectangle differs");
    i.Unresolved24(128);
    expected.state30 = 128;
    verify_item(i, expected);
    i.elapsed_24 = 2.0f;
    i.Unresolved24(0);
    expected.state30 = 0;
    expect_clear(*expected_env);
    verify_item(i, expected);
    verify_environment(*env, *expected_env);
    bind.verify(*env, &env->manager.value);
    box.close();
}

struct RawReceipt
{
    char* value = nullptr;
    ~RawReceipt()
    {
        ::operator delete(value);
    }
};

void owned_text()
{
    for (const char* input : {"", "alpha"})
    {
        begin(*input ? "owned_alpha" : "owned_empty");
        ItemBox box;
        auto expected = state(*box.value);
        text(*box.value, input);
        require(box.value->owned_text != nullptr && std::strcmp(box.value->owned_text, input) == 0,
            "real text copy differs");
        expected.text = box.value->owned_text;
        verify_item(*box.value, expected);
        box.close();
    }
    begin("repeated_owned_alias");
    RawReceipt retained;
    ItemBox box;
    text(*box.value, "alpha");
    auto expected = state(*box.value);
    char* const old = box.value->owned_text;
    text(*box.value, old + 2);
    // Ownership of the leaked allocation is accepted only after successful
    // replacement. If allocation throws, the real item still owns the old one.
    retained.value = old;
    require(box.value->owned_text != old && std::strcmp(box.value->owned_text, "pha") == 0 &&
                std::strcmp(old, "alpha") == 0,
        "repeat allocation or retained old bytes differ");
    expected.text = box.value->owned_text;
    verify_item(*box.value, expected);
    box.close();
    require(std::strcmp(retained.value, "alpha") == 0,
        "old leak did not remain live after item cleanup");
    ::operator delete(retained.value);
    retained.value = nullptr;
    ++counts.retained_text_releases;
}

void thresholds()
{
    constexpr std::array<float, 4> values{1.0f, 0.0f, -0.0f, -1.0f};
    constexpr std::array<float, 4> expected{0.05f, 1.5f, 1.5f, 1.5f};
    for (std::size_t n = 0; n < values.size(); ++n)
    {
        begin("finite_threshold");
        auto env = std::make_unique<Environment>();
        env->manager.value.value_138 = values[n];
        auto before = std::make_unique<Environment>(*env);
        require(
            same_float(env->manager.value.ReadItemThreshold(), expected[n]), "threshold differs");
        verify_environment(*env, *before);
    }
}

struct EventCase
{
    const char* name;
    const char* text;
    unsigned char state40;
    float elapsed;
    int event;
    bool other_item;
    float expected_elapsed;
    bool clear;
};
constexpr EventCase event_cases[] = {{"event_null_text", nullptr, 1, 1.5f, 2, false, 1.5f, false},
    {"event_state_zero", "A", 0, 1.5f, 2, false, 1.5f, false},
    {"event_zero", "A", 1, 1.5f, 0, false, 1.5f, false},
    {"event_below", "A", 1, 1.25f, 1, false, 1.25f, false},
    {"event_two_control", "A", 1, 1.5f, 2, false, 7.0f, true},
    {"event_three", "A", 1, 1.5f, 3, false, 1.5f, false},
    {"event_raw_byte", "A", 128, 1.75f, 1, false, 7.0f, true},
    {"event_allocated_empty", "", 1, 1.5f, 2, false, 7.0f, true},
    {"event_other_item", "A", 1, 1.5f, 2, true, 7.0f, false}};

void events()
{
    for (const auto& c : event_cases)
    {
        begin(c.name);
        auto env = std::make_unique<Environment>();
        Bindings bind(*env);
        ItemBox box;
        ItemBox other;
        auto& i = *box.value;
        if (c.text)
        {
            text(i, c.text);
        }
        i.state_40 = c.state40;
        i.elapsed_24 = c.elapsed;
        env->manager.value.item_134 = c.other_item ? &*other.value : &i;
        env->auxiliary.value.state_04 = 90;
        auto expected_env = std::make_unique<Environment>(*env);
        auto expected = state(i);
        const auto other_before = state(*other.value);
        expected.elapsed = c.expected_elapsed;
        if (c.clear)
        {
            expect_clear(*expected_env);
        }
        require(!i.HandleTimedEvent(-9, 17, c.event), "event return differs");
        ++counts.event_calls;
        verify_item(i, expected);
        verify_item(*other.value, other_before);
        verify_environment(*env, *expected_env);
        bind.verify(*env, &env->manager.value);
        box.close();
        other.close();
    }
}

struct ManagerSwitchItem final : ScreenItem
{
    using ScreenItem::ScreenItem;
    ScreenManager* next = nullptr;
    unsigned char ReadState40() override
    {
        ++counts.callbacks;
        screen_manager = next;
        return 128;
    }
};

void event_callback()
{
    begin("event_post_callback_manager");
    auto env = std::make_unique<Environment>();
    Bindings bind(*env);
    ItemBox<ManagerSwitchItem> box;
    Frame<ScreenManager> alternate;
    alternate.value = env->manager.value;
    box.value->next = &alternate.value;
    box.value->elapsed_24 = 1.0f;
    text(*box.value, "A");
    env->manager.value.value_138 = 1.0f;
    env->manager.value.item_134 = &*box.value;
    alternate.value.item_134 = &*box.value;
    const auto alternate_before = alternate.value;
    auto before = std::make_unique<Environment>(*env);
    const auto expected = state(*box.value);
    require(!box.value->HandleTimedEvent(-9, 17, 2), "event callback return differs");
    ++counts.event_calls;
    verify_item(*box.value, expected);
    verify_environment(*env, *before);
    verify_manager(alternate.value, alternate_before);
    verify_guards(alternate);
    bind.verify(*env, &alternate.value);
    // Remove the callback publication while its borrowed receiver is still alive.
    screen_manager = &env->manager.value;
    box.close();
}

struct CoordinateCase
{
    const char* name;
    bool has_text;
    int x;
    int y;
    float elapsed;
    float delta;
    int event;
    float expected_elapsed;
    unsigned char expected_state;
    bool notify;
    bool clear;
};
constexpr CoordinateCase coordinate_cases[] = {
    {"coordinates_null", false, -3, 12, .25f, .25f, 0, .25f, 109, false, false},
    {"coordinates_miss", true, 8, 5, .25f, .25f, 0, 0, 109, false, true},
    {"coordinates_same_control", true, 7, 5, .25f, .25f, 0, .5f, 1, false, false},
    {"coordinates_moved", true, 6, 5, .25f, .25f, 0, 0, 1, false, false},
    {"coordinates_double_increment", true, 7, 5, 1.25f, .25f, 0, 1.75f, 1, true, false},
    {"coordinates_at_threshold", true, 7, 5, 1.5f, .25f, 0, 1.75f, 1, true, false},
    {"coordinates_event_cross", true, 7, 5, 1.25f, .25f, 2, 7, 1, false, true},
    {"coordinates_already_seven", true, 7, 5, 7, .25f, 0, 7, 1, false, true},
    {"coordinates_negative_delta", true, 7, 5, .25f, -.125f, 0, .125f, 1, false, false}};

void coordinates()
{
    for (const auto& c : coordinate_cases)
    {
        begin(c.name);
        auto env = std::make_unique<Environment>();
        Bindings bind(*env);
        FontBox font(*env);
        const auto font_before = font.value;
        ItemBox box;
        auto& i = *box.value;
        if (c.has_text)
        {
            text(i, "A");
        }
        i.state_40 = 109;
        i.elapsed_24 = c.elapsed;
        i.last_x = 7;
        i.last_y = 5;
        env->manager.value.item_134 = &i;
        env->clock.value.delta_seconds = c.delta;
        auto expected_env = std::make_unique<Environment>(*env);
        auto expected = state(i);
        expected.elapsed = c.expected_elapsed;
        expected.state40 = c.expected_state;
        expected.x = c.x;
        expected.y = c.y;
        if (c.clear)
        {
            expect_clear(*expected_env);
        }
        if (c.notify)
        {
            expect_notification(*expected_env, env->units.value.data(), &i);
        }
        require(!i.HandleTimedCoordinates(c.x, c.y, c.event), "coordinate return differs");
        ++counts.coordinate_calls;
        verify_item(i, expected);
        verify_environment(*env, *expected_env);
        font.verify(font_before);
        bind.verify(*env, &env->manager.value);
        box.close();
    }
}

struct ClockMutationItem final : ScreenItem
{
    using ScreenItem::ScreenItem;
    bool TestRectangle(int x, int y) override
    {
        const bool result = ScreenItem::TestRectangle(x, y);
        ++counts.callbacks;
        startup_frame_clock->delta_seconds = 4.0f;
        return result;
    }
};

void coordinate_callback()
{
    begin("coordinate_captured_delta");
    auto env = std::make_unique<Environment>();
    Bindings bind(*env);
    FontBox font(*env);
    const auto font_before = font.value;
    ItemBox<ClockMutationItem> box;
    auto& i = *box.value;
    text(i, "A");
    i.elapsed_24 = .25f;
    i.last_x = 7;
    i.last_y = 5;
    i.state_40 = 109;
    env->manager.value.item_134 = &i;
    auto expected_env = std::make_unique<Environment>(*env);
    expected_env->clock.value.delta_seconds = 4.0f;
    auto expected = state(i);
    expected.elapsed = .5f;
    expected.state40 = 1;
    require(!i.HandleTimedCoordinates(7, 5, 0), "captured-delta return differs");
    ++counts.coordinate_calls;
    verify_item(i, expected);
    verify_environment(*env, *expected_env);
    font.verify(font_before);
    bind.verify(*env, &env->manager.value);
    box.close();
}

void direct_notification()
{
    begin("direct_show_inactive_position");
    auto env = std::make_unique<Environment>();
    Bindings bind(*env);
    FontBox font(*env);
    const auto font_before = font.value;
    ItemBox box;
    auto expected_env = std::make_unique<Environment>(*env);
    const auto before = state(*box.value);
    expect_notification(*expected_env, env->units.value.data(), &*box.value);
    env->manager.value.ShowItemText(&*box.value, 100, -200, "A");
    verify_item(*box.value, before);
    verify_environment(*env, *expected_env);
    font.verify(font_before);
    bind.verify(*env, &env->manager.value);
    box.close();
}
} // namespace

int main()
{
    if (!std::setlocale(LC_ALL, "C"))
    {
        return 2;
    }
    ScreenManager* const manager = screen_manager;
    ScreenTextLookup* const lookup = screen_text_lookup;
    ScreenRequestRenderer* const renderer = screen_request_renderer;
    FrameClock* const clock = startup_frame_clock;
    InputState* const input = startup_input_state;
    try
    {
        defaults_and_bytes();
        hits();
        geometry_and_activation();
        owned_text();
        thresholds();
        events();
        event_callback();
        coordinates();
        coordinate_callback();
        direct_notification();
        case_name = "completion";
        require(counts.sessions == 43 && counts.constructions == 49 && counts.destructions == 49 &&
                    counts.font_constructions == 11 && counts.text_calls == 22 &&
                    counts.retained_text_releases == 1 && counts.event_calls == 10 &&
                    counts.coordinate_calls == 10 && counts.callbacks == 2,
            "case or actual item lifetime count differs");
        require(screen_manager == manager && screen_text_lookup == lookup &&
                    screen_request_renderer == renderer && startup_frame_clock == clock &&
                    startup_input_state == input,
            "global restoration differs");
        std::printf("screen-item-complete: %u sessions, %u item lifetimes, %u font constructions, "
                    "%u text calls, %u retained releases, %u event calls, %u coordinate calls, "
                    "%u callbacks, %u checks\n",
            counts.sessions,
            counts.constructions,
            counts.font_constructions,
            counts.text_calls,
            counts.retained_text_releases,
            counts.event_calls,
            counts.coordinate_calls,
            counts.callbacks,
            counts.checks);
        return 0;
    }
    catch (const Failure&)
    {
        return 2;
    }
}
