#include "tga_image.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>

static_assert(CHAR_BIT == 8);
static_assert(sizeof(unsigned short) == 2);
static_assert(std::numeric_limits<unsigned short>::digits == 16);
static_assert(sizeof(int) == 4);
static_assert(std::is_standard_layout_v<TgaImage>);
static_assert(std::is_same_v<decltype(TgaImage::converted_palette), unsigned short*>);

namespace
{
constexpr std::size_t guard = alignof(std::max_align_t);
constexpr std::size_t pixel_bytes = 12;
constexpr std::size_t rgb_bytes = 768;
constexpr std::size_t color_count = 256;
using Rgb = std::array<unsigned char, rgb_bytes>;
using Words = std::array<unsigned short, color_count>;

struct Allocation
{
    unsigned char* raw = nullptr;
    void* pointer = nullptr;
    std::size_t bytes = 0;
    bool live = false;
};
std::array<Allocation, 8> allocations;
std::size_t allocation_count = 0;
std::size_t releases = 0;
bool tracking = false;
bool expecting = false;
bool throw_requested = false;
std::size_t expected_bytes = 0;
std::size_t attempts = 0;
TgaImage* observed_image = nullptr;
unsigned short* observed_previous = nullptr;
std::size_t cases = 0;
std::size_t calls = 0;
std::size_t converted_palettes = 0;
std::size_t exceptions = 0;
std::size_t overwritten_buffers = 0;
std::size_t checks = 0;

[[noreturn]] void fail(const char* reason, std::size_t index)
{
    std::fprintf(stderr, "case %zu: %s at %zu\n", cases, reason, index);
    std::exit(2);
}

void require(bool condition, const char* reason, std::size_t index = 0)
{
    ++checks;
    if (!condition)
    {
        fail(reason, index);
    }
}

Allocation& allocation(void* pointer)
{
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        if (allocations[i].pointer == pointer)
        {
            return allocations[i];
        }
    }
    fail("untracked array", 0);
}

void check_guards(const Allocation& value)
{
    require(value.live, "array lifetime ended early");
    for (std::size_t i = 0; i < guard; ++i)
    {
        require(value.raw[i] == 0xa5, "leading allocation guard changed", i);
        require(value.raw[guard + value.bytes + i] == 0x5a, "trailing allocation guard changed", i);
    }
}

void expect_allocation(std::size_t bytes, TgaImage* image = nullptr, bool should_throw = false)
{
    require(!expecting, "nested allocation expectation");
    expecting = true;
    expected_bytes = bytes;
    attempts = 0;
    observed_image = image;
    observed_previous = image ? image->converted_palette : nullptr;
    throw_requested = should_throw;
}

void finish_allocation()
{
    require(expecting && attempts == 1, "allocation attempt count differs");
    expecting = false;
    observed_image = nullptr;
}

// Generated host array allocation boundary. This is not the retail CRT's
// failure contract. All bookkeeping is fixed-size and allocation-free.
void* allocate(std::size_t bytes)
{
    if (!tracking)
    {
        void* result = std::malloc(bytes ? bytes : 1);
        if (!result)
        {
            throw std::bad_alloc();
        }
        return result;
    }
    require(expecting && attempts == 0, "unexpected array allocation");
    ++attempts;
    require(bytes == expected_bytes, "allocation size differs", bytes);
    if (observed_image)
    {
        require(observed_image->converted_palette == observed_previous,
            "palette published before allocation returned");
    }
    if (throw_requested)
    {
        throw std::bad_alloc();
    }
    require(allocation_count < allocations.size(), "allocation ledger exhausted");
    auto* raw = static_cast<unsigned char*>(std::malloc(bytes + 2 * guard));
    if (!raw)
    {
        throw std::bad_alloc();
    }
    std::memset(raw, 0xa5, guard);
    std::memset(raw + guard, 0x6b, bytes);
    std::memset(raw + guard + bytes, 0x5a, guard);
    allocations[allocation_count++] = {raw, raw + guard, bytes, true};
    return raw + guard;
}

void release(void* pointer) noexcept
{
    if (!pointer)
    {
        return;
    }
    if (!tracking)
    {
        std::free(pointer);
        return;
    }
    Allocation& value = allocation(pointer);
    check_guards(value);
    value.live = false;
    ++releases;
    std::free(value.raw);
    value.raw = nullptr;
}

struct ImageStorage
{
    std::array<unsigned char, guard> before;
    alignas(TgaImage) std::array<unsigned char, sizeof(TgaImage)> bytes;
    std::array<unsigned char, guard> after;
    TgaImage* image = nullptr;

    ImageStorage()
    {
        before.fill(0x39);
        after.fill(0x93);
        // Keep pre-lifetime representation setup observable; compare snapshots
        // after the actual constructor, not an assumed padding value.
        volatile unsigned char* raw = bytes.data();
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            raw[i] = 0xc5;
        }
        image = ::new (static_cast<void*>(bytes.data())) TgaImage(3, 2);
        for (std::size_t i = 0; i < sizeof(image->unknown_1c); ++i)
        {
            image->unknown_1c[i] = static_cast<unsigned char>(0x41 + i);
        }
        require(image->width == 3 && image->height == 2 && image->bits_per_pixel == 16 &&
                    image->bytes_per_pixel == 2 && image->pixels == nullptr &&
                    image->palette == nullptr && image->converted_palette == nullptr,
            "actual constructor state differs");
        expect_allocation(pixel_bytes);
        image->pixels = new unsigned char[pixel_bytes];
        finish_allocation();
        for (std::size_t i = 0; i < pixel_bytes; ++i)
        {
            image->pixels[i] = static_cast<unsigned char>(i * 17 + 3);
        }
        expect_allocation(rgb_bytes);
        image->palette = new unsigned char[rgb_bytes];
        finish_allocation();
    }

    void check() const
    {
        for (std::size_t i = 0; i < guard; ++i)
        {
            require(before[i] == 0x39 && after[i] == 0x93, "image storage guard changed", i);
        }
    }

    void destroy()
    {
        require(image != nullptr, "image destroyed twice");
        image->~TgaImage();
        image = nullptr;
        check();
    }

    ~ImageStorage()
    {
        if (image)
        {
            destroy();
        }
    }
};
static_assert(offsetof(ImageStorage, bytes) == guard);
static_assert(offsetof(ImageStorage, after) == guard + sizeof(TgaImage));

struct Snapshot
{
    std::array<unsigned char, sizeof(TgaImage)> object;
    std::array<unsigned char, pixel_bytes> pixels;
    Rgb rgb;
    unsigned short* previous;
    Words previous_words;
};

Snapshot snapshot(const TgaImage& image)
{
    Snapshot saved{};
    std::memcpy(saved.object.data(), &image, sizeof(image));
    std::memcpy(saved.pixels.data(), image.pixels, pixel_bytes);
    std::memcpy(saved.rgb.data(), image.palette, rgb_bytes);
    saved.previous = image.converted_palette;
    if (saved.previous)
    {
        require(allocation(saved.previous).live, "old converted array is dead");
        std::memcpy(saved.previous_words.data(), saved.previous, 512);
    }
    return saved;
}

void preserved(const ImageStorage& storage, const Snapshot& saved, bool published)
{
    const TgaImage& image = *storage.image;
    std::array<unsigned char, sizeof(TgaImage)> actual{};
    std::memcpy(actual.data(), &image, sizeof(image));
    constexpr auto first = offsetof(TgaImage, converted_palette);
    constexpr auto last = first + sizeof(TgaImage::converted_palette);
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        if (!published || i < first || i >= last)
        {
            require(actual[i] == saved.object[i], "unrelated image representation changed", i);
        }
    }
    for (std::size_t i = 0; i < pixel_bytes; ++i)
    {
        require(image.pixels[i] == saved.pixels[i], "source pixels changed", i);
    }
    for (std::size_t i = 0; i < rgb_bytes; ++i)
    {
        require(image.palette[i] == saved.rgb[i], "source RGB bytes changed", i);
    }
    if (saved.previous)
    {
        require(allocation(saved.previous).live, "overwritten array was released");
        for (std::size_t i = 0; i < color_count; ++i)
        {
            require(saved.previous[i] == saved.previous_words[i], "old converted words changed", i);
        }
    }
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        if (allocations[i].live)
        {
            check_guards(allocations[i]);
        }
    }
    storage.check();
}

void convert(ImageStorage& storage, unsigned char flag, const Words& expected)
{
    TgaImage& image = *storage.image;
    const auto saved = snapshot(image);
    const auto old_count = allocation_count;
    const auto old_releases = releases;
    expect_allocation(512, &image);
    ++calls;
    image.ConvertPalette(flag);
    finish_allocation();
    require(allocation_count == old_count + 1 && releases == old_releases,
        "conversion allocation or release count differs");
    require(image.converted_palette == allocations[old_count].pointer,
        "new converted array not published");
    preserved(storage, saved, true);
    for (std::size_t i = 0; i < color_count; ++i)
    {
        require(image.converted_palette[i] == expected[i], "converted palette differs", i);
    }
    ++converted_palettes;
}

void begin_case()
{
    require(!tracking && !expecting, "unfinished fixture state");
    allocations = {};
    allocation_count = 0;
    releases = 0;
    tracking = true;
}

void end_case()
{
    require(!expecting && releases == allocation_count, "owned arrays not released");
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        require(!allocations[i].live, "live array remains", i);
    }
    tracking = false;
    ++cases;
}

Words expected_words(const Rgb& rgb, bool rgb555)
{
    Words expected{};
    for (std::size_t i = 0; i < color_count; ++i)
    {
        const unsigned int red = rgb[3 * i] / 8;
        const unsigned int green = rgb[3 * i + 1] / (rgb555 ? 8 : 4);
        const unsigned int blue = rgb[3 * i + 2] / 8;
        expected[i] = static_cast<unsigned short>(red * (rgb555 ? 1024 : 2048) + green * 32 + blue);
    }
    return expected;
}

Rgb generated_rgb(unsigned int family)
{
    Rgb rgb{};
    for (std::size_t i = 0; i < color_count; ++i)
    {
        if (family < 3)
        {
            rgb[3 * i + family] = static_cast<unsigned char>(i);
        }
        else if (family == 3)
        {
            rgb[3 * i] = static_cast<unsigned char>(i);
            rgb[3 * i + 1] = static_cast<unsigned char>(255 - i);
            rgb[3 * i + 2] = static_cast<unsigned char>((73 * i + 41) & 255);
        }
        else
        {
            rgb[3 * i] = static_cast<unsigned char>((11 * i + 7) & 255);
            rgb[3 * i + 1] = static_cast<unsigned char>((5 * i + 3) & 255);
            rgb[3 * i + 2] = static_cast<unsigned char>((17 * i + 13) & 255);
        }
    }
    return rgb;
}

void one_palette(const Rgb& rgb, unsigned char flag, const Words& expected)
{
    begin_case();
    {
        ImageStorage storage;
        std::memcpy(storage.image->palette, rgb.data(), rgb.size());
        convert(storage, flag, expected);
    }
    end_case();
}

void literal_cases()
{
    constexpr std::array<unsigned char, 36> first = {0,
        4,
        0,
        255,
        0,
        0,
        0,
        255,
        0,
        0,
        0,
        255,
        255,
        255,
        255,
        7,
        7,
        7,
        8,
        8,
        8,
        248,
        252,
        248,
        128,
        64,
        32,
        1,
        2,
        3,
        254,
        253,
        252,
        31,
        63,
        127};
    Rgb rgb{};
    std::memcpy(rgb.data(), first.data(), first.size());
    Words rgb565{};
    Words rgb555{};
    constexpr std::array<unsigned short, 12> packed565 = {0x0020,
        0xf800,
        0x07e0,
        0x001f,
        0xffff,
        0x0020,
        0x0841,
        0xffff,
        0x8204,
        0x0000,
        0xffff,
        0x19ef};
    constexpr std::array<unsigned short, 12> packed555 = {0x0000,
        0x7c00,
        0x03e0,
        0x001f,
        0x7fff,
        0x0000,
        0x0421,
        0x7fff,
        0x4104,
        0x0000,
        0x7fff,
        0x0cef};
    for (std::size_t i = 0; i < packed565.size(); ++i)
    {
        rgb565[i] = packed565[i];
        rgb555[i] = packed555[i];
    }
    one_palette(rgb, 0, rgb565);
    one_palette(rgb, 1, rgb555);
}

void repeated_conversion()
{
    begin_case();
    ImageStorage storage;
    const Rgb first = generated_rgb(3);
    const Words first_words = expected_words(first, true);
    std::memcpy(storage.image->palette, first.data(), first.size());
    convert(storage, 1, first_words);
    unsigned short* overwritten = storage.image->converted_palette;
    const Rgb second = generated_rgb(4);
    std::memcpy(storage.image->palette, second.data(), second.size());
    convert(storage, 0, expected_words(second, false));
    require(storage.image->converted_palette != overwritten,
        "converted allocation reused live storage");
    storage.destroy();
    require(allocation(overwritten).live && releases == 3, "destructor released overwritten array");
    for (std::size_t i = 0; i < color_count; ++i)
    {
        require(overwritten[i] == first_words[i], "destructor changed overwritten array", i);
    }
    ++overwritten_buffers;
    // Observe the faithful leak, then retire only this test-owned old array.
    delete[] overwritten;
    end_case();
}

void allocation_exception(bool existing)
{
    begin_case();
    {
        ImageStorage storage;
        const Rgb rgb = generated_rgb(4);
        std::memcpy(storage.image->palette, rgb.data(), rgb.size());
        if (existing)
        {
            convert(storage, 0x80, expected_words(rgb, true));
        }
        const auto saved = snapshot(*storage.image);
        const auto old_count = allocation_count;
        const auto old_releases = releases;
        expect_allocation(512, storage.image, true);
        bool caught = false;
        ++calls;
        try
        {
            storage.image->ConvertPalette(0);
        }
        catch (const std::bad_alloc&)
        {
            caught = true;
        }
        finish_allocation();
        require(caught, "host array exception did not propagate");
        require(allocation_count == old_count && releases == old_releases,
            "failed conversion changed allocation ownership");
        preserved(storage, saved, false);
        ++exceptions;
    }
    end_case();
}
} // namespace

void* operator new[](std::size_t bytes)
{
    return allocate(bytes);
}

void operator delete[](void* pointer) noexcept
{
    release(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept
{
    release(pointer);
}

int main()
{
    literal_cases();
    constexpr std::array<unsigned char, 5> flags = {0, 1, 2, 0x80, 0xff};
    for (unsigned int family = 0; family < 5; ++family)
    {
        const Rgb rgb = generated_rgb(family);
        for (unsigned char flag : flags)
        {
            one_palette(rgb, flag, expected_words(rgb, flag != 0));
        }
    }
    repeated_conversion();
    allocation_exception(false);
    allocation_exception(true);
    std::printf("tga palette conversion: %zu cases, %zu calls, %zu converted palettes, "
                "%zu host exceptions, %zu overwritten buffers, %zu checks\n",
        cases,
        calls,
        converted_palettes,
        exceptions,
        overwritten_buffers,
        checks);
    return 0;
}
