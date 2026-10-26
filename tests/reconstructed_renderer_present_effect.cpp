#include "render_mask_setup.h"
#include "startup_renderer_present_effect.h"

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace
{
static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(USHRT_MAX == 0xffffU && UINT_MAX == 0xffffffffU);
static_assert(std::endian::native == std::endian::little);
static_assert(std::is_same_v<decltype(&ApplyPresentEffect),
    void (*)(unsigned short*, unsigned int, unsigned int, unsigned int)>);
static_assert(std::is_same_v<decltype(render_half_mask), unsigned short>);

const char* current_case = "start";
unsigned checks = 0;
unsigned calls = 0;
unsigned setup_calls = 0;
unsigned buffer_words_checked = 0;

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "present-effect failure: case=%s reason=%s\n", current_case, reason);
        std::exit(2);
    }
}

struct GlobalState
{
    unsigned int format;
    unsigned short quarter;
    unsigned short half;
    unsigned short* pixels;
    unsigned short full;
    unsigned short green;
    unsigned short sum_bias;
    unsigned short quarter_bias;
    unsigned short half_bias;
    unsigned char green_levels;
    unsigned short green_bits;
    unsigned short red_shift;
    unsigned short red;
    unsigned int width;
    unsigned short blue;

    bool operator==(const GlobalState&) const = default;
};

GlobalState globals()
{
    return {render_mask_format,
        render_quarter_mask,
        render_half_mask,
        render_mask_pixels,
        render_full_mask,
        render_green_mask,
        render_sum_bias,
        render_quarter_bias,
        render_half_bias,
        render_green_levels,
        render_green_bits,
        render_red_shift,
        render_red_mask,
        render_mask_width,
        render_blue_mask};
}

void setup(unsigned format)
{
    SetupRenderMasks(nullptr, 0, format);
    ++setup_calls;
}

template <std::size_t N>
void pixels(const char* name,
    unsigned width,
    unsigned height,
    unsigned mode,
    const std::array<unsigned short, N>& input,
    const std::array<unsigned short, N>& expected)
{
    current_case = name;
    std::array<unsigned short, N + 4> storage{};
    storage[0] = 0x1369;
    storage[1] = 0x24ac;
    storage[N + 2] = 0x9753;
    storage[N + 3] = 0xeca8;
    for (std::size_t i = 0; i < N; ++i)
    {
        storage[i + 2] = input[i];
    }
    auto expected_storage = storage;
    for (std::size_t i = 0; i < N; ++i)
    {
        expected_storage[i + 2] = expected[i];
    }
    const auto before = globals();
    ApplyPresentEffect(storage.data() + 2, width, height, mode);
    ++calls;
    for (std::size_t i = 0; i < storage.size(); ++i)
    {
        ++checks;
        ++buffer_words_checked;
        if (storage[i] != expected_storage[i])
        {
            std::fprintf(stderr,
                "present-effect failure: case=%s reason=pixel word=%zu actual=%04x expected=%04x\n",
                current_case,
                i,
                static_cast<unsigned>(storage[i]),
                static_cast<unsigned>(expected_storage[i]));
            std::exit(2);
        }
    }
    require(globals() == before, "global-state");
}

void mode_group()
{
    constexpr std::array<unsigned short, 4> input{0x0000, 0x0001, 0x7bef, 0xffff};
    constexpr std::array<std::array<unsigned short, 4>, 5> rgb565{{
        {0x0000, 0x0001, 0x7bef, 0xffff},
        {0x39e7, 0x39e7, 0x8cb1, 0xefbd},
        {0x7bef, 0x7bef, 0xb5d6, 0xf7de},
        {0xb5d6, 0xb5d6, 0xceb9, 0xefbd},
        {0xffff, 0xffff, 0xffff, 0xffff},
    }};
    constexpr std::array<std::array<unsigned short, 4>, 5> rgb555{{
        {0x0000, 0x0001, 0x7bef, 0xffff},
        {0x1ce7, 0x1ce7, 0x77b1, 0x77bd},
        {0x3def, 0x3def, 0x7bd6, 0x7bde},
        {0x5ad6, 0x5ad6, 0x77b9, 0x77bd},
        {0xffff, 0xffff, 0xffff, 0xffff},
    }};
    constexpr std::array<unsigned, 5> modes{0x100U, 0x101U, 2U, 3U, 0xffffff04U};
    constexpr std::array<unsigned, 5> order{4, 0, 1, 2, 3};
    constexpr std::array<const char*, 5> names565{
        "565-mode0", "565-mode1", "565-mode2", "565-mode3", "565-mode4"};
    constexpr std::array<const char*, 5> names555{
        "555-mode0", "555-mode1", "555-mode2", "555-mode3", "555-mode4"};
    setup(0);
    for (unsigned mode : order)
    {
        pixels(names565[mode], 2, 2, modes[mode], input, rgb565[mode]);
    }
    setup(0xffffffffU);
    for (unsigned mode : order)
    {
        pixels(names555[mode], 2, 2, modes[mode], input, rgb555[mode]);
    }
}

void count_group()
{
    constexpr std::array<unsigned short, 6> input{0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666};
    constexpr std::array<unsigned short, 6> carried{3, 3, 3, 3, 3, 3};
    pixels("default5", 2, 3, 5, input, carried);
    pixels("default105", 2, 3, 0x105U, input, carried);
    pixels("default-ff", 2, 3, 0xffffffffU, input, carried);

    struct NoAccess
    {
        const char* name;
        unsigned width;
        unsigned height;
    };
    constexpr std::array<NoAccess, 4> gates{{
        {"zero-height", 1, 0},
        {"negative-height", 1, 0x80000000U},
        {"zero-width", 0, 2},
        {"negative-width", 0x80000000U, 2},
    }};
    for (const auto& gate : gates)
    {
        current_case = gate.name;
        const auto before = globals();
        ApplyPresentEffect(nullptr, gate.width, gate.height, 1);
        ++calls;
        require(globals() == before, "no-access-global-state");
    }

    constexpr std::array<unsigned short, 6> contiguous{
        0x0001, 0x0102, 0x0203, 0x0304, 0x0405, 0x0506};
    pixels("contiguous-words", 3, 2, 0, contiguous, contiguous);
}

void alias_group()
{
    setup(0);
    render_half_mask = 0xfffe;
    render_half_bias = 1;
    current_case = "actual-half-mask-alias";
    auto expected = globals();
    expected.half = 0x8000;
    ApplyPresentEffect(&render_half_mask, 1, 1, 2);
    ++calls;
    require(globals() == expected, "aliased-global-state");
    pixels<1>("later-global-read", 1, 1, 2, {0xffff}, {0x4001});

    setup(0);
    render_quarter_bias = 0xffff;
    render_half_bias = 0x0123;
    pixels<1>("quarter-bias-low16-a", 1, 1, 1, {0xffff}, {0xb5d5});
    render_half_bias = 0xfedc;
    pixels<1>("quarter-bias-low16-b", 1, 1, 1, {0xffff}, {0xb5d5});
    render_half_bias = 0xffff;
    pixels<1>("half-bias-word-wrap", 1, 1, 2, {0xffff}, {0x7bee});
    render_sum_bias = 0xffff;
    pixels<1>("sum-bias-word-wrap", 1, 1, 3, {0xffff}, {0x39e6});
    setup(0);
}
} // namespace

int main()
{
    mode_group();
    count_group();
    alias_group();
    std::printf("{\"groups\":3,\"calls\":%u,\"setup_calls\":%u,\"checks\":%u,"
                "\"buffer_words_checked\":%u,\"pointer_bytes\":%zu}\n",
        calls,
        setup_calls,
        checks,
        buffer_words_checked,
        sizeof(void*));
    return 0;
}
