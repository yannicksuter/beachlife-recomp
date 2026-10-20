#include "startup_gui_parser.h"
#include "startup_screen_item.h"
#include "startup_screen_renderer.h"
#if defined(BEACHLIFE_DECOMP_RENDERER_QUEUE)
#include "renderer_queue_test_state.h"
#endif

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <type_traits>

namespace
{
constexpr std::size_t guard_size = 16;
constexpr std::size_t input_capacity = 32;

struct Dimensions
{
    bool present;
    unsigned int width;
    unsigned int height;
};

struct Parent
{
    bool present;
    ScreenRectangle rectangle;
};

struct Expected
{
    std::array<std::uint32_t, 4> rectangle;
    unsigned int cursor;
    unsigned char next;
};

struct Literal
{
    const char* name;
    std::string_view input;
    unsigned int initial_cursor;
    Dimensions dimensions;
    Parent parent;
    Expected expected;
};

// Literal results are independent fixed expectations, not a second parser.
// The explicit lengths include the accessible NUL, including embedded-NUL cases.
constexpr std::array<Literal, 42> normal_cases{{
    {"center_even",
        {"\x43\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00", 16},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x0000015eu, 0x00000014u, 0x000001c2u, 0x000000dcu}, 14u, 41}},
    {"plain_null_renderer",
        {"\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00", 15},
        0u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0x0000000au, 0x00000014u, 0x0000006eu, 0x000000dcu}, 13u, 41}},
    {"top_left_only",
        {"\x54\x4c\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00", 17},
        0u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0x0000000au, 0x00000014u, 0x0000006eu, 0x000000dcu}, 15u, 41}},
    {"reflect_right",
        {"\x52\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00", 16},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x00000316u, 0x00000014u, 0x000002b2u, 0x000000dcu}, 14u, 41}},
    {"reflect_right_edge",
        {"\x53\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00", 16},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x0000000au, 0x00000014u, 0x000002b2u, 0x000000dcu}, 14u, 41}},
    {"reflect_bottom",
        {"\x42\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00", 16},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x0000000au, 0x00000244u, 0x0000006eu, 0x0000017cu}, 14u, 41}},
    {"right_then_edge",
        {"\x52\x53\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00", 17},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x00000316u, 0x00000014u, 0x0000006eu, 0x000000dcu}, 15u, 41}},
    {"edge_then_right_text",
        {"\x53\x52\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00", 17},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x00000316u, 0x00000014u, 0x0000006eu, 0x000000dcu}, 15u, 41}},
    {"repeat_center",
        {"\x43\x43\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00", 17},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x0000015eu, 0x00000014u, 0x000001c2u, 0x000000dcu}, 15u, 41}},
    {"odd_center_x",
        {"\x43\x31\x30\x2c\x32\x30\x2c\x31\x31\x31\x2c\x32\x32\x31\x29\x00", 16},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x0000015eu, 0x00000014u, 0x000001c3u, 0x000000ddu}, 14u, 41}},
    {"odd_center_y",
        {"\x59\x31\x30\x2c\x32\x30\x2c\x31\x31\x31\x2c\x32\x32\x31\x29\x00", 16},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x0000000au, 0x000000c8u, 0x0000006fu, 0x00000191u}, 14u, 41}},
    {"odd_center_xy",
        {"\x43\x59\x31\x30\x2c\x32\x30\x2c\x31\x31\x31\x2c\x32\x32\x31\x29\x00", 17},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x0000015eu, 0x000000c8u, 0x000001c3u, 0x00000191u}, 15u, 41}},
    {"edge_center",
        {"\x53\x43\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00", 17},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x0000003cu, 0x00000014u, 0x000002e4u, 0x000000dcu}, 15u, 41}},
    {"center_edge_text",
        {"\x43\x53\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00", 17},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x0000003cu, 0x00000014u, 0x000002e4u, 0x000000dcu}, 15u, 41}},
    {"right_center",
        {"\x52\x43\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00", 17},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x800001c2u, 0x00000014u, 0x8000015eu, 0x000000dcu}, 15u, 41}},
    {"center_right_text",
        {"\x43\x52\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00", 17},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x800001c2u, 0x00000014u, 0x8000015eu, 0x000000dcu}, 15u, 41}},
    {"high_bit_dimension",
        {"\x43\x31\x2c\x32\x2c\x34\x2c\x36\x29\x00", 10},
        0u,
        {true, 2147483648u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x3fffffffu, 0x00000002u, 0x40000002u, 0x00000006u}, 8u, 41}},
    {"all_flags",
        {"\x54\x42\x4c\x52\x53\x43\x59\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00",
            22},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x800002e4u, 0x80000190u, 0x8000003cu, 0x800000c8u}, 20u, 41}},
    {"all_flags_reverse",
        {"\x59\x43\x53\x52\x4c\x42\x54\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00",
            22},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x800002e4u, 0x80000190u, 0x8000003cu, 0x800000c8u}, 20u, 41}},
    {"leading_whitespace",
        {"\x20\x0d\x0a\x09\x31\x2c\x32\x2c\x33\x2c\x34\x29\x51\x00", 14},
        0u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u}, 11u, 41}},
    {"space_after_flag",
        {"\x43\x20\x31\x2c\x32\x2c\x33\x2c\x34\x29\x00", 11},
        0u,
        {true, 800u, 600u},
        {false, {0, 0, 0, 0}},
        {{0x0000018fu, 0x00000001u, 0x00000191u, 0x00000003u}, 8u, 52}},
    {"empty_second",
        {"\x31\x2c\x2c\x33\x2c\x34\x29\x00", 8},
        0u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0x00000001u, 0x00000000u, 0x00000003u, 0x00000004u}, 6u, 41}},
    {"empty_all",
        {"\x2c\x2c\x2c\x29\x00", 5},
        0u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 3u, 41}},
    {"close_immediate",
        {"\x29\x51\x00", 3},
        0u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0u, 41}},
    {"close_preserves_tail",
        {"\x31\x2c\x32\x2c\x33\x2c\x34\x29\x20\x09\x51\x00", 12},
        0u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u}, 7u, 41}},
    {"final_comma_no_skip",
        {"\x31\x2c\x32\x2c\x33\x2c\x34\x2c\x20\x09\x51\x00", 12},
        0u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u}, 8u, 32}},
    {"final_comma_once",
        {"\x31\x2c\x32\x2c\x33\x2c\x34\x2c\x2c\x51\x00", 11},
        0u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u}, 8u, 44}},
    {"space_delimiters",
        {"\x31\x20\x32\x20\x33\x20\x34\x20\x51\x00", 10},
        0u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u}, 8u, 81}},
    {"interior_tab",
        {"\x31\x09\x32\x2c\x33\x2c\x34\x2c\x35\x29\x00", 11},
        0u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0xfffffee0u, 0x00000003u, 0x00000004u, 0x00000005u}, 9u, 41}},
    {"minus_is_byte",
        {"\x2d\x31\x32\x2c\x33\x2c\x34\x2c\x35\x29\x00", 11},
        0u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0xfffffee0u, 0x00000003u, 0x00000004u, 0x00000005u}, 9u, 41}},
    {"interior_nul",
        {"\x31\x00\x2c\x32\x2c\x33\x2c\x34\x29\x00", 10},
        0u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0xffffffdau, 0x00000002u, 0x00000003u, 0x00000004u}, 8u, 41}},
    {"signed_high_byte",
        {"\x80\x2c\x32\x2c\x33\x2c\x34\x29\x00", 9},
        0u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0xffffff50u, 0x00000002u, 0x00000003u, 0x00000004u}, 7u, 41}},
    {"nonzero_cursor",
        {"\x58\x58\x31\x2c\x32\x2c\x33\x2c\x34\x29\x00", 11},
        2u,
        {false, 0u, 0u},
        {false, {0, 0, 0, 0}},
        {{0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u}, 9u, 41}},
    {"parent_translate",
        {"\x31\x2c\x32\x2c\x33\x2c\x34\x29\x00", 9},
        0u,
        {false, 0u, 0u},
        {true, {10, 20, 100, 200}},
        {{0x0000000bu, 0x00000016u, 0x0000000du, 0x00000018u}, 7u, 41}},
    {"parent_equal_upper",
        {"\x30\x2c\x30\x2c\x39\x30\x2c\x31\x38\x30\x29\x00", 12},
        0u,
        {false, 0u, 0u},
        {true, {10, 20, 100, 200}},
        {{0x0000000au, 0x00000014u, 0x00000064u, 0x000000c8u}, 10u, 41}},
    {"parent_no_lower_bound",
        {"\x2d\x31\x2c\x2d\x31\x2c\x33\x2c\x34\x29\x00", 11},
        0u,
        {false, 0u, 0u},
        {true, {10, 20, 100, 200}},
        {{0xffffffedu, 0xfffffff7u, 0x0000000du, 0x00000018u}, 9u, 41}},
    {"parent_zero_width",
        {"\x31\x2c\x32\x2c\x33\x2c\x34\x29\x00", 9},
        0u,
        {false, 0u, 0u},
        {true, {10, 20, 10, 200}},
        {{0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u}, 7u, 41}},
    {"parent_zero_height",
        {"\x31\x2c\x32\x2c\x33\x2c\x34\x29\x00", 9},
        0u,
        {false, 0u, 0u},
        {true, {10, 20, 100, 20}},
        {{0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u}, 7u, 41}},
    {"parent_zero_both",
        {"\x31\x2c\x32\x2c\x33\x2c\x34\x29\x00", 9},
        0u,
        {false, 0u, 0u},
        {true, {10, 20, 10, 20}},
        {{0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u}, 7u, 41}},
    {"parent_after_center",
        {"\x43\x31\x30\x2c\x32\x30\x2c\x31\x31\x30\x2c\x32\x32\x30\x29\x00", 16},
        0u,
        {true, 800u, 600u},
        {true, {5, 7, 1000, 1000}},
        {{0x00000163u, 0x0000001bu, 0x000001c7u, 0x000000e3u}, 14u, 41}},
    {"negative_parent_origin",
        {"\x31\x2c\x32\x2c\x33\x2c\x34\x29\x00", 9},
        0u,
        {false, 0u, 0u},
        {true, {-5, -7, 1000, 1000}},
        {{0xfffffffcu, 0xfffffffbu, 0xfffffffeu, 0xfffffffdu}, 7u, 41}},
    {"reversed_parent_can_return",
        {"\x2d\x31\x2c\x2d\x31\x2c\x2d\x31\x2c\x2d\x31\x29\x00", 13},
        0u,
        {false, 0u, 0u},
        {true, {10, 20, 5, 15}},
        {{0xffffffedu, 0xfffffff7u, 0xffffffedu, 0xfffffff7u}, 11u, 41}},
}};

constexpr std::array<Literal, 3> fatal_cases{{
    {"fatal_right",
        {"\x30\x2c\x30\x2c\x39\x31\x2c\x31\x38\x30\x29\x00", 12},
        0u,
        {false, 0u, 0u},
        {true, {10, 20, 100, 200}},
        {{0x0000000au, 0x00000014u, 0x00000065u, 0x000000c8u}, 10u, 41}},
    {"fatal_bottom",
        {"\x30\x2c\x30\x2c\x39\x30\x2c\x31\x38\x31\x29\x00", 12},
        0u,
        {false, 0u, 0u},
        {true, {10, 20, 100, 200}},
        {{0x0000000au, 0x00000014u, 0x00000064u, 0x000000c9u}, 10u, 41}},
    {"fatal_reversed",
        {"\x30\x2c\x30\x2c\x30\x2c\x30\x29\x00", 9},
        0u,
        {false, 0u, 0u},
        {true, {10, 20, 5, 15}},
        {{0x0000000au, 0x00000014u, 0x0000000au, 0x00000014u}, 7u, 41}},
}};

struct Counters
{
    std::uint64_t cases;
    std::uint64_t calls;
    std::uint64_t constructions;
    std::uint64_t destructions;
    std::uint64_t checks;
    std::uint64_t byte_checks;
};

Counters counters{};
const char* current_case = "setup";
bool normal_completed = false;

[[noreturn]] void fail(const char* reason)
{
    std::fprintf(stderr, "case %s: %s\n", current_case, reason);
    std::fflush(stderr);
    // Failure cannot run another exit callback or be mistaken for real exit(0).
    std::_Exit(2);
}

void require(bool condition, const char* reason)
{
    ++counters.checks;
    if (!condition)
    {
        fail(reason);
    }
}

struct GeneratedParent final : ScreenItem
{
    explicit GeneratedParent(ScreenRectangle initial)
        : ScreenItem(initial)
    {
        // Initialize only fields untouched by the actual base constructor.
        kind_28 = 0x13579;
        word_2c = 0x36363636U;
        std::memset(unknown_31, 0x69, sizeof(unknown_31));
        ++counters.constructions;
    }

    ~GeneratedParent() override
    {
        ++counters.destructions;
        // The compiler then invokes the actual reconstructed base destructor.
    }

#if defined(BEACHLIFE_DECOMP_COMPLETE_SCREEN_ITEM)
    void Unresolved04(float) override
#else
    void Unresolved04() override
#endif
    {
        fail("unresolved parent slot invoked");
    }
    void Unresolved08() override
    {
        fail("unresolved parent slot invoked");
    }
    void Unresolved0c() override
    {
        fail("unresolved parent slot invoked");
    }
    void Unresolved10(ScreenItem*) override
    {
        fail("unresolved parent slot invoked");
    }
    void Unresolved1c() override
    {
        fail("unresolved parent slot invoked");
    }
#if defined(BEACHLIFE_DECOMP_COMPLETE_SCREEN_ITEM)
    unsigned char Unresolved20() override
#else
    void Unresolved20() override
#endif
    {
        fail("unresolved parent slot invoked");
    }
#if defined(BEACHLIFE_DECOMP_SCREEN_ACTIVATION)
    void Unresolved24(unsigned char) override
#else
    void Unresolved24() override
#endif
    {
        fail("unresolved parent slot invoked");
    }
    void Unresolved28() override
    {
        fail("unresolved parent slot invoked");
    }
};

template <typename T> struct Guarded
{
    std::array<unsigned char, guard_size> before{};
    T value{};
    std::array<unsigned char, guard_size> after{};
};

struct Input
{
    std::array<unsigned char, guard_size> before{};
    std::array<char, input_capacity> bytes{};
    std::array<unsigned char, guard_size> after{};
};

struct ParentStorage
{
    std::array<unsigned char, guard_size> before{};
    alignas(GeneratedParent) unsigned char bytes[sizeof(GeneratedParent)]{};
    std::array<unsigned char, guard_size> after{};
};

// The parent is constructed explicitly in byte storage. In the experimental
// queue profile the observation owner is initialized before exit callbacks are
// registered, so its real deque members remain alive during their checks.
struct Observation
{
    const Literal* literal{};
    Input input{};
    Guarded<GuiParserState> parser{};
    Guarded<ScreenRectangle> output{};
    Guarded<ScreenRequestRenderer> renderer{};
    ParentStorage parent_storage{};
    GeneratedParent* parent{};
    ScreenRequestRenderer* saved_renderer{};
    ScreenRequestRenderer* expected_renderer{};
    std::array<char, input_capacity> input_before{};
    std::array<unsigned char, sizeof(GuiParserState)> parser_before{};
#if defined(BEACHLIFE_DECOMP_RENDERER_QUEUE)
    ScreenRequestRenderer renderer_before{};
#else
    std::array<unsigned char, sizeof(ScreenRequestRenderer)> renderer_before{};
#endif
    std::array<unsigned char, sizeof(GeneratedParent)> parent_before{};
    bool parent_live{};
    bool fatal_armed{};
    bool reader_returned{};
};

#if defined(BEACHLIFE_DECOMP_RENDERER_QUEUE)
Observation* observed_pointer{};

Observation& observation_owner()
{
    static Observation value{};
    return value;
}

Observation& observation()
{
    return *observed_pointer;
}
#else
Observation observed{};

Observation& observation()
{
    return observed;
}
#endif

static_assert(CHAR_BIT == 8);
static_assert(sizeof(int) == 4 && sizeof(unsigned int) == 4);
static_assert(std::numeric_limits<int>::digits == 31);
static_assert(std::numeric_limits<unsigned int>::digits == 32);
static_assert(sizeof(ScreenRectangle) == 16);
static_assert(std::is_standard_layout_v<GuiParserState>);
static_assert(std::is_trivially_copyable_v<GuiParserState>);
#if !defined(BEACHLIFE_DECOMP_RENDERER_QUEUE)
static_assert(std::is_trivially_copyable_v<ScreenRequestRenderer>);
static_assert(std::is_trivially_destructible_v<Observation>);
#endif
static_assert(std::is_trivially_destructible_v<Literal>);

void unchanged(const void* actual, const void* expected, std::size_t size, const char* reason)
{
    const auto* actual_bytes = static_cast<const unsigned char*>(actual);
    const auto* expected_bytes = static_cast<const unsigned char*>(expected);
    bool same = true;
    for (std::size_t i = 0; i < size; ++i)
    {
        ++counters.byte_checks;
        same = (actual_bytes[i] == expected_bytes[i]) && same;
    }
    require(same, reason);
}

void guard(
    const std::array<unsigned char, guard_size>& bytes, unsigned char expected, const char* reason)
{
    bool same = true;
    for (unsigned char byte : bytes)
    {
        ++counters.byte_checks;
        same = (byte == expected) && same;
    }
    require(same, reason);
}

template <typename T> void initialize_guards(T& value)
{
    value.before.fill(0xa5);
    value.after.fill(0x5a);
}

void parent_guards()
{
    auto& observed = observation();
    guard(observed.parent_storage.before, 0xa5, "parent leading guard changed");
    guard(observed.parent_storage.after, 0x5a, "parent trailing guard changed");
}

void prepare(const Literal& literal)
{
    auto& observed = observation();
    current_case = literal.name;
    require(!observed.parent_live, "previous parent is still live");
    require(
        !literal.input.empty() && literal.input.size() <= input_capacity, "input extent invalid");
    require(literal.initial_cursor < literal.input.size(), "initial cursor outside input");
    require(literal.expected.cursor < literal.input.size(), "expected cursor outside input");
    observed.literal = &literal;
    observed.fatal_armed = false;
    observed.reader_returned = false;
    observed.parent = nullptr;
    observed.saved_renderer = screen_request_renderer;

    initialize_guards(observed.input);
    initialize_guards(observed.parser);
    initialize_guards(observed.output);
    initialize_guards(observed.renderer);
    initialize_guards(observed.parent_storage);
    observed.input.bytes.fill('Z');
    std::memcpy(observed.input.bytes.data(), literal.input.data(), literal.input.size());
    observed.parser.value.manager = nullptr;
    observed.parser.value.cursor = literal.initial_cursor;
    observed.parser.value.buffer = observed.input.bytes.data();
    // Valid untouched prefix state; no context or registry method is executed.
    observed.parser.value.current_context = nullptr;
    observed.parser.value.alternate_context_10 = nullptr;
    observed.parser.value.alternate_context_14 = nullptr;
    observed.parser.value.context_head = nullptr;
    observed.parser.value.previous_context = nullptr;
    observed.parser.value.last_registered_item = nullptr;
    std::memset(observed.parser.value.unknown_24, 0x5a, sizeof(observed.parser.value.unknown_24));
    observed.parser.value.registry_head = nullptr;
    observed.parser.value.items_head = nullptr;
    observed.parser.value.items_tail = nullptr;
    observed.parser.value.registry_tail = nullptr;
    observed.parser.value.word_38 = 0xa5a55a5aU;

    observed.output.value = {-1111, 2222, -3333, 4444};
#if defined(BEACHLIFE_DECOMP_RENDERER_QUEUE)
    renderer_queue_test::initialize(observed.renderer.value, 0x34);
#else
    observed.renderer.value = {};
#endif
    observed.renderer.value.width = literal.dimensions.width;
    observed.renderer.value.height = literal.dimensions.height;
    observed.renderer.value.current_surface = -37;
    std::memset(
        observed.renderer.value.unknown_01, 0x12, sizeof(observed.renderer.value.unknown_01));
#if !defined(BEACHLIFE_DECOMP_RENDERER_QUEUE)
    std::memset(
        observed.renderer.value.unknown_0c, 0x34, sizeof(observed.renderer.value.unknown_0c));
#endif
    std::memset(
        observed.renderer.value.unknown_6c, 0x56, sizeof(observed.renderer.value.unknown_6c));
    observed.expected_renderer = literal.dimensions.present ? &observed.renderer.value : nullptr;
    screen_request_renderer = observed.expected_renderer;

    // Pattern only live byte storage, never a constructed vptr or pointer field.
    std::memset(observed.parent_storage.bytes, 0xd7, sizeof(observed.parent_storage.bytes));
    if (literal.parent.present)
    {
        observed.parent = ::new (static_cast<void*>(observed.parent_storage.bytes))
            GeneratedParent(literal.parent.rectangle);
        observed.parent_live = true;
        std::memcpy(observed.parent_before.data(),
            static_cast<const void*>(observed.parent),
            observed.parent_before.size());
    }

    observed.input_before = observed.input.bytes;
    std::memcpy(
        observed.parser_before.data(), &observed.parser.value, observed.parser_before.size());
#if defined(BEACHLIFE_DECOMP_RENDERER_QUEUE)
    observed.renderer_before = observed.renderer.value;
#else
    std::memcpy(
        observed.renderer_before.data(), &observed.renderer.value, observed.renderer_before.size());
#endif
    ++counters.cases;
}

void verify_observation()
{
    auto& observed = observation();
    const auto& literal = *observed.literal;
    const auto& value = observed.output.value;
    require(static_cast<std::uint32_t>(value.left) == literal.expected.rectangle[0],
        "rectangle.left differs");
    require(static_cast<std::uint32_t>(value.top) == literal.expected.rectangle[1],
        "rectangle.top differs");
    require(static_cast<std::uint32_t>(value.right) == literal.expected.rectangle[2],
        "rectangle.right differs");
    require(static_cast<std::uint32_t>(value.bottom) == literal.expected.rectangle[3],
        "rectangle.bottom differs");
    require(observed.parser.value.cursor == literal.expected.cursor, "cursor differs");
    require(observed.parser.value.cursor < literal.input.size(), "cursor escaped input");
    require(static_cast<unsigned char>(observed.input.bytes[observed.parser.value.cursor]) ==
                literal.expected.next,
        "next byte differs");
    require(observed.parser.value.manager == nullptr, "parser manager changed");
    require(observed.parser.value.buffer == observed.input.bytes.data(), "parser buffer changed");

    const auto* parser_bytes = reinterpret_cast<const unsigned char*>(&observed.parser.value);
    bool same = true;
    for (std::size_t i = 0; i < sizeof(GuiParserState); ++i)
    {
        if (i >= offsetof(GuiParserState, cursor) &&
            i < offsetof(GuiParserState, cursor) + sizeof(observed.parser.value.cursor))
        {
            continue;
        }
        ++counters.byte_checks;
        same = (parser_bytes[i] == observed.parser_before[i]) && same;
    }
    require(same, "parser changed outside cursor");
#if defined(BEACHLIFE_DECOMP_RENDERER_QUEUE)
    counters.byte_checks += renderer_queue_test::compared_bytes(observed.renderer_before);
    require(renderer_queue_test::equal(observed.renderer.value, observed.renderer_before),
        "renderer changed");
#else
    unchanged(&observed.renderer.value,
        observed.renderer_before.data(),
        observed.renderer_before.size(),
        "renderer changed");
#endif
    require(screen_request_renderer == observed.expected_renderer, "renderer global changed");
    unchanged(observed.input.bytes.data(),
        observed.input_before.data(),
        input_capacity,
        "input bytes changed");
    guard(observed.input.before, 0xa5, "input leading guard changed");
    guard(observed.input.after, 0x5a, "input trailing guard changed");
    guard(observed.parser.before, 0xa5, "parser leading guard changed");
    guard(observed.parser.after, 0x5a, "parser trailing guard changed");
    guard(observed.output.before, 0xa5, "output leading guard changed");
    guard(observed.output.after, 0x5a, "output trailing guard changed");
    guard(observed.renderer.before, 0xa5, "renderer leading guard changed");
    guard(observed.renderer.after, 0x5a, "renderer trailing guard changed");
    parent_guards();
    if (literal.parent.present)
    {
        require(observed.parent_live && observed.parent != nullptr, "parent lifetime missing");
        require(observed.parent->owned_text == nullptr, "parent unexpectedly owns text");
        unchanged(observed.parent,
            observed.parent_before.data(),
            observed.parent_before.size(),
            "parent changed");
    }
    else
    {
        require(!observed.parent_live && observed.parent == nullptr, "unexpected parent lifetime");
    }
}

void finish_normal_case()
{
    auto& observed = observation();
    const auto destructions = counters.destructions;
    if (observed.parent_live)
    {
        observed.parent->~GeneratedParent();
        observed.parent_live = false;
        observed.parent = nullptr;
        require(counters.destructions == destructions + 1, "parent destructor count differs");
        // External byte guards survive; never inspect the destroyed object.
        parent_guards();
    }
    else
    {
        require(counters.destructions == destructions, "unexpected parent destruction");
    }
    screen_request_renderer = observed.saved_renderer;
}

void normal_completion_guard()
{
    if (!normal_completed)
    {
        fail("normal reader exited before completion");
    }
}

void fatal_observer()
{
    auto& observed = observation();
    // An unexpected exit during construction must not inspect an incomplete object.
    if (!observed.fatal_armed || observed.literal == nullptr)
    {
        fail("fatal observer was not armed");
    }
    require(!observed.reader_returned, "fatal reader returned");
    require(counters.cases == 1, "fatal case count differs");
    require(counters.calls == 1, "fatal call count differs");
    require(counters.constructions == 1, "fatal parent construction count differs");
    require(counters.destructions == 0, "fatal parent was destroyed");
    verify_observation();
    if (std::printf("GUI rectangle fatal %s: %llu case, %llu call, %llu parent construction, "
                    "%llu parent destructions, %llu checks, %llu byte checks\n",
            current_case,
            static_cast<unsigned long long>(counters.cases),
            static_cast<unsigned long long>(counters.calls),
            static_cast<unsigned long long>(counters.constructions),
            static_cast<unsigned long long>(counters.destructions),
            static_cast<unsigned long long>(counters.checks),
            static_cast<unsigned long long>(counters.byte_checks)) < 0 ||
        std::fflush(stdout) != 0)
    {
        fail("fatal summary write failed");
    }
    // Return to the real exit(0), allowing ordinary CRT/sanitizer termination.
}

int run_normal()
{
    auto& observed = observation();
    if (std::atexit(normal_completion_guard) != 0)
    {
        fail("normal observer registration failed");
    }
    for (const auto& literal : normal_cases)
    {
        prepare(literal);
        ++counters.calls;
        observed.parser.value.ReadRectangle(observed.output.value, observed.parent);
        verify_observation();
        finish_normal_case();
    }
    require(counters.cases == 42, "normal case count differs");
    require(counters.calls == 42, "normal call count differs");
    require(counters.constructions == 9, "normal parent construction count differs");
    require(counters.destructions == 9, "normal parent destruction count differs");
    require(!observed.parent_live, "last normal parent still live");
    if (std::printf("GUI rectangle: %llu cases, %llu calls, %llu parent constructions, "
                    "%llu parent destructions, %llu checks, %llu byte checks\n",
            static_cast<unsigned long long>(counters.cases),
            static_cast<unsigned long long>(counters.calls),
            static_cast<unsigned long long>(counters.constructions),
            static_cast<unsigned long long>(counters.destructions),
            static_cast<unsigned long long>(counters.checks),
            static_cast<unsigned long long>(counters.byte_checks)) < 0 ||
        std::fflush(stdout) != 0)
    {
        fail("normal summary write failed");
    }
    normal_completed = true;
    return 0;
}

[[noreturn]] void run_fatal(const Literal& literal)
{
    auto& observed = observation();
    current_case = literal.name;
    if (std::atexit(fatal_observer) != 0)
    {
        fail("fatal observer registration failed");
    }
    prepare(literal);
    observed.fatal_armed = true;
    ++counters.calls;
    observed.parser.value.ReadRectangle(observed.output.value, observed.parent);
    observed.reader_returned = true;
    fail("expected fatal reader returned");
}
} // namespace

int main(int argc, char** argv)
{
#if defined(BEACHLIFE_DECOMP_RENDERER_QUEUE)
    // Construction and static destruction registration finish before either
    // later atexit callback is registered. Both see live deque members.
    observed_pointer = &observation_owner();
#endif
    if (argc == 1)
    {
        return run_normal();
    }
    if (argc == 3 && std::strcmp(argv[1], "--fatal") == 0)
    {
        for (const auto& literal : fatal_cases)
        {
            if (std::strcmp(argv[2], literal.name) == 0)
            {
                run_fatal(literal);
            }
        }
    }
    fail("usage: reconstructed_gui_rectangle_tests [--fatal case]");
}
