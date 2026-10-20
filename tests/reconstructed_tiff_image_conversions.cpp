#include "tga_image.h"
#include "tiff_read_context.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <iterator>
#include <memory>
#include <type_traits>

namespace
{
static_assert(sizeof(int) == 4 && sizeof(unsigned char) == 1);
static_assert(std::is_standard_layout_v<TgaImage>);
static_assert(offsetof(TgaImage, height) == offsetof(TgaImage, width) + sizeof(int));
static_assert(offsetof(TgaImage, bits_per_pixel) == offsetof(TgaImage, height) + sizeof(int));
static_assert(
    offsetof(TgaImage, bytes_per_pixel) == offsetof(TgaImage, bits_per_pixel) + sizeof(int));

struct Failure
{
    const char* group;
    const char* reason;
    std::size_t index;
    unsigned actual;
    unsigned expected;
};

const char* group = "start";
unsigned checks;
unsigned calls;

void require(bool value, const char* reason)
{
    ++checks;
    if (!value)
    {
        throw Failure{group, reason, 0, 0, 1};
    }
}

void bytes(const unsigned char* actual,
    const unsigned char* expected,
    std::size_t count,
    const char* reason)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        ++checks;
        if (actual[i] != expected[i])
        {
            throw Failure{group, reason, i, actual[i], expected[i]};
        }
    }
}

void initialize_opaque(TgaImage& image)
{
    image.unknown_1c[0] = 0x31;
    image.unknown_1c[1] = 0x42;
    image.unknown_1c[2] = 0x53;
    image.unknown_1c[3] = 0x64;
}

void preserved_input(const TgaImage& image,
    int width,
    int height,
    int depth,
    const unsigned char* pixels,
    const unsigned char* palette)
{
    require(image.width == width && image.height == height && image.bits_per_pixel == depth &&
                image.bytes_per_pixel == depth / 8,
        "input-members");
    require(
        image.pixels == pixels && image.palette == palette && image.converted_palette == nullptr,
        "input-ownership");
    constexpr unsigned char opaque[] = {0x31, 0x42, 0x53, 0x64};
    bytes(image.unknown_1c, opaque, sizeof(opaque), "input-opaque");
}

void rgba_group()
{
    group = "rgba";
    TgaImage input(2, 2, 32, 0);
    initialize_opaque(input);
    constexpr unsigned char source[] = {0x10,
        0x20,
        0x30,
        0xaa,
        0x40,
        0x50,
        0x60,
        0xbb,
        0x70,
        0x80,
        0x90,
        0xcc,
        0xa0,
        0xb0,
        0xc0,
        0xdd};
    constexpr unsigned char expected[] = {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0};
    std::copy(std::begin(source), std::end(source), input.pixels);
    const auto* old_pixels = input.pixels;
    std::unique_ptr<TgaImage> result(TiffConvertFourSamples(&input));
    ++calls;
    require(result && result.get() != &input, "result-owner");
    require(result->width == 2 && result->height == 2 && result->bits_per_pixel == 24 &&
                result->bytes_per_pixel == 3,
        "result-members");
    require(result->pixels != old_pixels && result->palette == nullptr &&
                result->converted_palette == nullptr,
        "result-buffers");
    bytes(result->pixels, expected, sizeof(expected), "pixels");
    preserved_input(input, 2, 2, 32, old_pixels, nullptr);
    bytes(input.pixels, source, sizeof(source), "input-pixels");
    // Both objects destroy their own real arrays when this scope exits, including
    // a changed-source failure. No test allocator or replacement game body.
}

constexpr std::array<unsigned char, 24> palette_prefix = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};

void prepare_palette(TgaImage& image)
{
    std::fill(image.palette, image.palette + 768, 0xee);
    std::copy(palette_prefix.begin(), palette_prefix.end(), image.palette);
}

void check_palette(const TgaImage& result)
{
    require(result.palette != nullptr && result.converted_palette == nullptr, "palette-owner");
    bytes(result.palette, palette_prefix.data(), palette_prefix.size(), "eight-palette-entries");
    for (std::size_t i = palette_prefix.size(); i < 768; ++i)
    {
        require(result.palette[i] == 0, "remaining-palette-zero");
    }
}

void packed_case(int width,
    int height,
    const unsigned char* source,
    const unsigned char* expected,
    std::size_t count)
{
    TgaImage input(width, height, 8, 1);
    initialize_opaque(input);
    prepare_palette(input);
    std::copy(source, source + count, input.pixels);
    const auto* old_pixels = input.pixels;
    const auto* old_palette = input.palette;
    std::array<unsigned char, 768> palette_before;
    std::copy(input.palette, input.palette + 768, palette_before.begin());
    std::unique_ptr<TgaImage> result(TiffExpandFourBits(&input));
    ++calls;
    require(result && result.get() != &input, "result-owner");
    require(result->width == width && result->height == height && result->bits_per_pixel == 8 &&
                result->bytes_per_pixel == 1,
        "result-members");
    require(result->pixels != old_pixels && result->palette != old_palette, "distinct-buffers");
    bytes(result->pixels, expected, count, "nibbles");
    check_palette(*result);
    preserved_input(input, width, height, 8, old_pixels, old_palette);
    bytes(input.pixels, source, count, "input-pixels");
    bytes(input.palette, palette_before.data(), palette_before.size(), "input-palette");
}

void packed_group()
{
    group = "odd-rows";
    constexpr unsigned char first_source[] = {0xab, 0xc1, 0x23, 0xd4, 0xe5, 0xf6};
    constexpr unsigned char first_expected[] = {10, 11, 12, 1, 2, 3};
    packed_case(3, 2, first_source, first_expected, sizeof(first_expected));
    group = "odd-count";
    constexpr unsigned char second_source[] = {0xab, 0xc0, 0xff};
    constexpr unsigned char second_expected[] = {10, 11, 12};
    packed_case(3, 1, second_source, second_expected, sizeof(second_expected));
}

void empty_group()
{
    group = "empty-rgba";
    TgaImage input(0, 0, 32, 0);
    initialize_opaque(input);
    const auto* pixels = input.pixels;
    std::unique_ptr<TgaImage> result(TiffConvertFourSamples(&input));
    ++calls;
    require(result && result->width == 0 && result->height == 0 && result->bits_per_pixel == 24 &&
                result->bytes_per_pixel == 3,
        "empty-members");
    require(result->palette == nullptr && result->converted_palette == nullptr, "empty-owner");
    preserved_input(input, 0, 0, 32, pixels, nullptr);
    group = "empty-packed";
    constexpr unsigned char unused[] = {0};
    packed_case(0, 0, unused, unused, 0);
}
} // namespace

int main()
{
    try
    {
        rgba_group();
        packed_group();
        empty_group();
        std::printf("{\"groups\":3,\"calls\":%u,\"checks\":%u}\n", calls, checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr,
            "tiff-conversion case=%s reason=%s index=%zu actual=%02x expected=%02x\n",
            failure.group,
            failure.reason,
            failure.index,
            failure.actual,
            failure.expected);
        return 2;
    }
    catch (const std::exception& exception)
    {
        std::fprintf(stderr, "tiff-conversion unexpected: %s\n", exception.what());
        return 3;
    }
}
