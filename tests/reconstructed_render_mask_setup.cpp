#include "render_mask_setup.h"

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
static_assert(std::is_same_v<decltype(&SetupRenderMasks),
    void (*)(unsigned short*, unsigned int, unsigned int)>);
static_assert(std::is_same_v<decltype(render_mask_pixels), unsigned short*>);
static_assert(std::is_same_v<decltype(render_green_levels), unsigned char>);

const char* current_case = "initial-storage";
unsigned checks = 0;
unsigned calls = 0;
unsigned buffer_words_checked = 0;

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "render-mask failure: case=%s reason=%s\n", current_case, reason);
        std::exit(2);
    }
}

struct Masks
{
    unsigned short quarter;
    unsigned short half;
    unsigned short full;
    unsigned short green;
    unsigned short sum_bias;
    unsigned short quarter_bias;
    unsigned short half_bias;
    unsigned char green_levels;
    unsigned short green_bits;
    unsigned short red_shift;
    unsigned short red;
    unsigned short blue;
};

constexpr Masks rgb565{
    0xe79c, 0xf7de, 0xffff, 0x07e0, 0xb5d6, 0x39e7, 0x7bef, 0x3f, 6, 11, 0xf800, 0x001f};
constexpr Masks rgb555{
    0x739c, 0x7bde, 0x7fff, 0x03e0, 0x5ad6, 0x1ce7, 0x3def, 0x1f, 5, 10, 0x7c00, 0x001f};

void check_state(const Masks& masks, unsigned short* pixels, unsigned width, unsigned format)
{
    require(render_mask_format == format, "format");
    require(render_quarter_mask == masks.quarter, "quarter-mask");
    require(render_half_mask == masks.half, "half-mask");
    require(render_mask_pixels == pixels, "pixels");
    require(render_full_mask == masks.full, "full-mask");
    require(render_green_mask == masks.green, "green-mask");
    require(render_sum_bias == masks.sum_bias, "sum-bias");
    require(render_quarter_bias == masks.quarter_bias, "quarter-bias");
    require(render_half_bias == masks.half_bias, "half-bias");
    require(render_green_levels == masks.green_levels, "green-levels");
    require(render_green_bits == masks.green_bits, "green-bits");
    require(render_red_shift == masks.red_shift, "red-shift");
    require(render_red_mask == masks.red, "red-mask");
    require(render_mask_width == width, "width");
    require(render_blue_mask == masks.blue, "blue-mask");
}

template <std::size_t N>
void check_storage(
    const std::array<unsigned short, N>& actual, const std::array<unsigned short, N>& before)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        ++buffer_words_checked;
        require(actual[i] == before[i], "borrowed-storage");
    }
}

void exercise()
{
    // These are the actual TU's static objects, not fixture definitions.
    check_state(Masks{}, nullptr, 0, 0);

    std::array<unsigned short, 32> first{};
    std::array<unsigned short, 24> second{};
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        first[i] = static_cast<unsigned short>(0x1400U + i * 37U);
    }
    for (std::size_t i = 0; i < second.size(); ++i)
    {
        second[i] = static_cast<unsigned short>(0xa600U + i * 19U);
    }
    const auto first_before = first;
    const auto second_before = second;
    auto after = [&](const Masks& masks, unsigned short* pixels, unsigned width, unsigned format)
    {
        check_state(masks, pixels, width, format);
        check_storage(first, first_before);
        check_storage(second, second_before);
    };

    // Group 1: zero selects 565; a zero width neither sizes nor touches storage.
    current_case = "zero-format";
    SetupRenderMasks(first.data() + 4, 0, 0);
    ++calls;
    after(rgb565, first.data() + 4, 0, 0);

    // Group 2: all nonzero selector bits and all width bits survive unchanged.
    current_case = "high-bit-format";
    SetupRenderMasks(second.data() + 9, 0x80000001U, 0x80000000U);
    ++calls;
    after(rgb555, second.data() + 9, 0x80000001U, 0x80000000U);

    current_case = "full-width-null";
    SetupRenderMasks(nullptr, 0xffffffffU, 0xffffffffU);
    ++calls;
    after(rgb555, nullptr, 0xffffffffU, 0xffffffffU);

    current_case = "one-past-borrowed";
    SetupRenderMasks(first.data() + first.size(), 0x12345678U, 1);
    ++calls;
    after(rgb555, first.data() + first.size(), 0x12345678U, 1);

    // Group 3: real typed global aliases and values captured before publication.
    current_case = "typed-global-alias";
    SetupRenderMasks(&render_half_mask, 0xffffffffU, 0);
    ++calls;
    after(rgb565, &render_half_mask, 0xffffffffU, 0);

    current_case = "captured-global-arguments";
    SetupRenderMasks(&render_quarter_bias, render_half_mask, render_mask_width);
    ++calls;
    after(rgb555, &render_quarter_bias, 0x0000f7deU, 0xffffffffU);
    // The final stored pointer names static storage; no dangling buffer is left.
}
} // namespace

int main()
{
    exercise();
    std::printf("{\"groups\":3,\"calls\":%u,\"checks\":%u,\"buffer_words_checked\":%u,"
                "\"pointer_bytes\":%zu}\n",
        calls,
        checks,
        buffer_words_checked,
        sizeof(void*));
    return 0;
}
