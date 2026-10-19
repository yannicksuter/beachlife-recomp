#include "tga_image.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>
#include <vector>

static_assert(sizeof(unsigned short) == 2);
static_assert(sizeof(int) == 4);
static_assert(sizeof(unsigned int) == 4);
static_assert(std::is_standard_layout_v<TgaImage>);
static_assert(offsetof(TgaImage, width) == 0);
static_assert(offsetof(TgaImage, height) == sizeof(int));
static_assert(offsetof(TgaImage, bits_per_pixel) == 2 * sizeof(int));
static_assert(offsetof(TgaImage, bytes_per_pixel) == 3 * sizeof(int));
static_assert(offsetof(TgaImage, pixels) == 4 * sizeof(int));
static_assert(offsetof(TgaImage, palette) == offsetof(TgaImage, pixels) + sizeof(TgaImage::pixels));
static_assert(offsetof(TgaImage, converted_palette) ==
              offsetof(TgaImage, palette) + sizeof(TgaImage::palette));
static_assert(offsetof(TgaImage, unknown_1c) ==
              offsetof(TgaImage, converted_palette) + sizeof(TgaImage::converted_palette));
static_assert(offsetof(TgaImage, unknown_1c) + sizeof(TgaImage::unknown_1c) <= sizeof(TgaImage));

namespace
{
constexpr std::size_t guard = alignof(std::max_align_t);
struct Allocation
{
    unsigned char* raw = nullptr;
    void* pointer = nullptr;
    std::size_t bytes = 0;
    bool array = false;
    bool live = false;
};
std::array<Allocation, 8> allocations;
std::array<std::size_t, 8> releases;
std::size_t allocation_count = 0;
std::size_t release_count = 0;
std::size_t fail_at = 0;
std::size_t attempts = 0;
bool tracing = false;
unsigned int cases = 0;
unsigned long long checks = 0;
unsigned int leak_cases = 0;
unsigned int thrown_cases = 0;

[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr,
        "case %u: %s (allocations=%zu releases=%zu attempts=%zu)\n",
        cases,
        message,
        allocation_count,
        release_count,
        attempts);
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        fail(message);
    }
}

void check_guards(const Allocation& allocation)
{
    for (std::size_t byte = 0; byte < guard; ++byte)
    {
        require(
            allocation.raw[byte] == 0xa5 && allocation.raw[guard + allocation.bytes + byte] == 0xa5,
            "allocation guard changed");
    }
}

void* allocate(std::size_t bytes, bool array)
{
    if (!tracing)
    {
        void* pointer = std::malloc(bytes ? bytes : 1);
        if (!pointer)
        {
            throw std::bad_alloc();
        }
        return pointer;
    }
    ++attempts;
    if (fail_at && attempts == fail_at)
    {
        throw std::bad_alloc();
    }
    require(allocation_count < allocations.size(), "too many allocations");
    require(bytes <= 2 * 1024 * 1024, "unexpected allocation extent");
    auto* raw = static_cast<unsigned char*>(std::malloc(bytes + 2 * guard));
    if (!raw)
    {
        throw std::bad_alloc();
    }
    std::memset(raw, 0xa5, bytes + 2 * guard);
    std::memset(raw + guard, 0x5a, bytes);
    Allocation& allocation = allocations[allocation_count++];
    allocation = {raw, raw + guard, bytes, array, true};
    return allocation.pointer;
}

void release(void* pointer, bool array) noexcept
{
    if (!pointer)
    {
        return;
    }
    for (std::size_t index = 0; index < allocation_count; ++index)
    {
        Allocation& allocation = allocations[index];
        if (allocation.pointer == pointer)
        {
            require(allocation.live, "double release");
            require(allocation.array == array, "allocation/deletion kind mismatch");
            check_guards(allocation);
            require(release_count < releases.size(), "too many releases");
            releases[release_count++] = index;
            allocation.live = false;
            std::free(allocation.raw);
            allocation.raw = nullptr;
            return;
        }
    }
    require(!tracing, "release of unowned pointer");
    std::free(pointer);
}

struct Case
{
    unsigned int type = 2;
    unsigned int depth = 24;
    unsigned int map = 0;
    unsigned int palette_depth = 24;
    unsigned int first = 0;
    unsigned int count = 0;
    unsigned int descriptor = 0;
    unsigned int id = 0;
    unsigned int width = 3;
    unsigned int height = 2;
    unsigned int packet_mode = 0;
};

void word(std::vector<unsigned char>& bytes, std::size_t offset, unsigned int value)
{
    bytes[offset] = static_cast<unsigned char>(value);
    bytes[offset + 1] = static_cast<unsigned char>(value >> 8);
}

std::vector<unsigned char> input(const Case& item, std::vector<unsigned char>& decoded)
{
    const unsigned int bpp = item.depth / 8;
    const unsigned int pixels = item.width * item.height;
    const unsigned int palette_bytes = item.map ? item.count * 3 : 0;
    std::vector<unsigned char> bytes(18 + item.id + palette_bytes, 0x97);
    bytes[0] = static_cast<unsigned char>(item.id);
    bytes[1] = static_cast<unsigned char>(item.map);
    bytes[2] = static_cast<unsigned char>(item.type);
    word(bytes, 3, item.first);
    word(bytes, 5, item.count);
    bytes[7] = static_cast<unsigned char>(item.palette_depth);
    word(bytes, 8, 0x8b21); // Both origins are ignored, even when nonzero.
    word(bytes, 10, 0xe75a);
    word(bytes, 12, item.width);
    word(bytes, 14, item.height);
    bytes[16] = static_cast<unsigned char>(item.depth);
    bytes[17] = static_cast<unsigned char>(item.descriptor);
    for (unsigned int byte = 0; byte < palette_bytes; ++byte)
    {
        bytes[18 + item.id + byte] = static_cast<unsigned char>(byte * 29 + 7);
    }
    decoded.resize(pixels * bpp);
    for (unsigned int pixel = 0; pixel < pixels; ++pixel)
    {
        for (unsigned int channel = 0; channel < bpp; ++channel)
        {
            // Repeating three-pixel groups independently enable run packets.
            const unsigned int group = item.packet_mode == 1   ? 0
                                       : item.packet_mode == 2 ? pixel / 3
                                                               : pixel;
            decoded[pixel * bpp + channel] =
                static_cast<unsigned char>(group * 17 + channel * 41 + 11);
        }
    }
    if (item.type != 9 && item.type != 10)
    {
        bytes.insert(bytes.end(), decoded.begin(), decoded.end());
    }
    else
    {
        unsigned int pixel = 0;
        while (pixel < pixels)
        {
            const bool run = item.packet_mode == 1 || (item.packet_mode == 2 && pixel % 6 == 0);
            unsigned int count = pixels - pixel;
            if (count > 128)
            {
                count = 128;
            }
            if (item.packet_mode == 2 && count > 3)
            {
                count = 3;
            }
            bytes.push_back(static_cast<unsigned char>((count - 1) | (run ? 0x80 : 0)));
            const unsigned int copied_pixels = run ? 1 : count;
            bytes.insert(bytes.end(),
                decoded.begin() + pixel * bpp,
                decoded.begin() + (pixel + copied_pixels) * bpp);
            pixel += count;
        }
    }
    bytes.insert(bytes.begin(), guard, 0x83);
    bytes.insert(bytes.end(), guard, 0x46);
    return bytes;
}

void verify(const Case& item, std::size_t injected_failure = 0)
{
    ++cases;
    std::vector<unsigned char> decoded;
    std::vector<unsigned char> bytes = input(item, decoded);
    const std::vector<unsigned char> before = bytes;
    allocations.fill({});
    releases.fill(0);
    allocation_count = release_count = attempts = 0;
    fail_at = injected_failure;

    const bool depth_supported = item.depth == 8 || item.depth == 24 || item.depth == 32;
    const bool palette_compatible =
        item.depth == 8 ? !item.map || item.palette_depth == 24 : !item.map;
    const bool accepted_header =
        depth_supported && palette_compatible && !item.first && !(item.descriptor & 0x30);
    const bool has_palette = item.depth == 8 && item.map;
    const bool leak_type = item.type == 0 || item.type == 3 || item.type == 11;
    const bool copies_pixels =
        item.type == 1 || item.type == 2 || item.type == 9 || item.type == 10;
    const bool copies_palette = item.type == 1 || item.type == 9;
    const bool rle = item.type == 9 || item.type == 10;
    const std::size_t pixel_bytes = decoded.size();
    const std::size_t usual_allocations = 2 + (has_palette ? 1 : 0) + (rle ? 1 : 0);
    const bool should_throw = accepted_header && injected_failure &&
                              injected_failure <= usual_allocations &&
                              (!leak_type || injected_failure <= 2 + (has_palette ? 1 : 0));
    TgaImage* image = nullptr;
    bool threw = false;
    tracing = true;
    try
    {
        image = ParseTgaBuffer(bytes.data() + guard);
    }
    catch (const std::bad_alloc&)
    {
        threw = true;
    }
    tracing = false;
    require(bytes == before, "input or input guards changed");
    require(threw == should_throw, "allocation failure propagation differs");
    if (!accepted_header)
    {
        require(image == nullptr && allocation_count == 0 && attempts == 0,
            "invalid header allocated or returned an image");
    }
    else if (threw)
    {
        ++thrown_cases;
        require(image == nullptr && allocation_count == injected_failure - 1 &&
                    attempts == injected_failure,
            "failure allocation prefix differs");
        const bool failed_in_constructor =
            injected_failure == 2 || (has_palette && injected_failure == 3);
        require(
            release_count == (failed_in_constructor ? 1u : 0u), "failure release count differs");
        if (failed_in_constructor)
        {
            require(releases[0] == 0 && !allocations[0].live,
                "constructor failure did not release raw object");
        }
        for (std::size_t index = 1; index < allocation_count; ++index)
        {
            require(allocations[index].live, "existing field allocation was silently repaired");
        }
    }
    else
    {
        const std::size_t wanted_allocations =
            2 + (has_palette ? 1 : 0) + (rle && !leak_type ? 1 : 0);
        require(allocation_count == wanted_allocations && attempts == wanted_allocations,
            "allocation count differs");
        require(!allocations[0].array && allocations[0].bytes == sizeof(TgaImage),
            "raw object allocation differs");
        require(allocations[1].array && allocations[1].bytes == pixel_bytes,
            "pixel allocation differs");
        if (has_palette)
        {
            require(
                allocations[2].array && allocations[2].bytes == 768, "palette allocation differs");
        }
        if (rle && !leak_type)
        {
            const std::size_t temporary = wanted_allocations - 1;
            require(allocations[temporary].array && allocations[temporary].bytes == pixel_bytes &&
                        !allocations[temporary].live && release_count == 1 &&
                        releases[0] == temporary,
                "RLE temporary lifecycle differs");
        }
        else
        {
            require(release_count == 0, "unexpected parser release");
        }
        if (leak_type)
        {
            ++leak_cases;
            require(image == nullptr, "rejected image type returned an image");
            for (std::size_t index = 0; index < allocation_count; ++index)
            {
                require(allocations[index].live, "rejected type leak was silently repaired");
            }
        }
        else
        {
            require(image == allocations[0].pointer, "returned pointer differs from allocation");
            require(image->width == static_cast<int>(item.width) &&
                        image->height == static_cast<int>(item.height) &&
                        image->bits_per_pixel == static_cast<int>(item.depth) &&
                        image->bytes_per_pixel == static_cast<int>(item.depth / 8),
                "image dimensions or depth differ");
            require(image->pixels == allocations[1].pointer &&
                        image->converted_palette == nullptr &&
                        image->palette == (has_palette ? allocations[2].pointer : nullptr),
                "image ownership fields differ");
            std::array<unsigned char, sizeof(image->unknown_1c)> tail;
            std::memcpy(tail.data(), &image->unknown_1c, tail.size());
            for (unsigned char byte : tail)
            {
                require(byte == (item.depth == 8 ? 0 : 0x5a),
                    "tail word representation changed unexpectedly");
            }
            const unsigned int bpp = item.depth / 8;
            for (unsigned int y = 0; y < item.height; ++y)
            {
                for (unsigned int x = 0; x < item.width; ++x)
                {
                    for (unsigned int channel = 0; channel < bpp; ++channel)
                    {
                        const unsigned int source_channel =
                            bpp >= 3 && channel < 3 ? 2 - channel : channel;
                        const std::size_t source =
                            ((item.height - 1 - y) * item.width + x) * bpp + source_channel;
                        const unsigned char expected = copies_pixels ? decoded[source] : 0;
                        require(image->pixels[(y * item.width + x) * bpp + channel] == expected,
                            "pixel result differs");
                    }
                }
            }
            if (has_palette)
            {
                for (unsigned int index = 0; index < 768; ++index)
                {
                    const unsigned int swapped = index / 3 * 3 + 2 - index % 3;
                    const unsigned char expected = copies_palette && index < item.count * 3
                                                       ? before[guard + 18 + item.id + swapped]
                                                       : 0;
                    require(image->palette[index] == expected, "palette result differs");
                }
            }
            const std::size_t prior_releases = release_count;
            tracing = true;
            delete image;
            tracing = false;
            require(release_count == prior_releases + 2 + (has_palette ? 1 : 0),
                "image destruction release count differs");
            require(releases[prior_releases] == 1 && releases[release_count - 1] == 0,
                "pixel/object release order differs");
            if (has_palette)
            {
                require(releases[prior_releases + 1] == 2, "palette release order differs");
            }
            for (std::size_t index = 0; index < allocation_count; ++index)
            {
                require(!allocations[index].live, "successful image lifecycle leaked");
            }
        }
    }
    // Leaks are observed above before the fixture reclaims its own backing storage.
    // This cleanup is not a change to ParseTgaBuffer or its child source.
    for (Allocation& allocation : allocations)
    {
        if (allocation.live)
        {
            check_guards(allocation);
            std::free(allocation.raw);
        }
        allocation = {};
    }
    allocation_count = release_count = attempts = fail_at = 0;
}
} // namespace

void* operator new(std::size_t bytes)
{
    return allocate(bytes, false);
}
void* operator new[](std::size_t bytes)
{
    return allocate(bytes, true);
}
void operator delete(void* pointer) noexcept
{
    release(pointer, false);
}
void operator delete[](void* pointer) noexcept
{
    release(pointer, true);
}
void operator delete(void* pointer, std::size_t) noexcept
{
    release(pointer, false);
}
void operator delete[](void* pointer, std::size_t) noexcept
{
    release(pointer, true);
}

void generated_cases()
{
    for (unsigned int type = 0; type < 256; ++type)
    {
        for (unsigned int depth : {8u, 24u, 32u})
        {
            Case item;
            item.type = type;
            item.depth = depth;
            item.descriptor = 0xcf;
            item.id = type % 3 == 0 ? 0 : type % 3 == 1 ? 1 : 255;
            verify(item);
            if (depth == 8)
            {
                item.map = type % 3 == 0 ? 1 : type % 3 == 1 ? 128 : 255;
                item.count = type % 3 == 0 ? 0 : type % 3 == 1 ? 7 : 256;
                verify(item);
            }
        }
    }
    for (unsigned int depth = 0; depth < 256; ++depth)
    {
        for (unsigned int map : {0u, 1u, 255u})
        {
            Case item;
            item.type = 2;
            item.depth = depth;
            item.map = map;
            item.count = 1;
            item.width = item.height = 0;
            verify(item);
        }
    }
    for (unsigned int descriptor = 0; descriptor < 256; ++descriptor)
    {
        Case item;
        item.descriptor = descriptor;
        verify(item);
    }
    for (unsigned int depth : {8u, 24u, 32u})
    {
        for (unsigned int first : {1u, 255u, 256u, 65535u})
        {
            Case item;
            item.depth = depth;
            item.first = first;
            verify(item);
        }
    }
    for (unsigned int palette_depth = 0; palette_depth < 256; ++palette_depth)
    {
        Case item;
        item.depth = 8;
        item.map = 2;
        item.palette_depth = palette_depth;
        item.count = 1;
        verify(item);
    }
    for (unsigned int type : {1u, 2u, 9u, 10u})
    {
        for (unsigned int depth : {8u, 24u, 32u})
        {
            for (unsigned int mode = 0; mode < 3; ++mode)
            {
                for (unsigned int width : {0u, 1u, 2u, 3u, 127u, 128u, 129u})
                {
                    for (unsigned int height : {0u, 1u, 2u})
                    {
                        Case item;
                        item.type = type;
                        item.depth = depth;
                        item.map = depth == 8 ? 255 : 0;
                        item.count = depth == 8 ? 256 : 0;
                        item.width = width;
                        item.height = height;
                        item.packet_mode = mode;
                        verify(item);
                    }
                }
            }
        }
    }
    for (unsigned int count = 1; count <= 128; ++count)
    {
        for (unsigned int mode : {0u, 1u})
        {
            Case item;
            item.type = 10;
            item.width = count;
            item.height = 1;
            item.packet_mode = mode;
            verify(item);
        }
    }
    for (unsigned int type : {0u, 1u, 2u, 3u, 4u, 9u, 10u, 11u, 255u})
    {
        for (unsigned int depth : {8u, 24u, 32u})
        {
            for (unsigned int attempt = 1; attempt <= 5; ++attempt)
            {
                Case item;
                item.type = type;
                item.depth = depth;
                item.map = depth == 8 ? 1 : 0;
                item.count = depth == 8 ? 7 : 0;
                verify(item, attempt);
            }
        }
    }
}

namespace
{
using Bytes = std::vector<unsigned char>;

// The following literal cases are separate from the retained broad generator.
// Their complete expected pixel/palette streams are specified by the fixture,
// not obtained by decoding the input with another implementation.
enum class Outcome
{
    image,
    header_rejection,
    leaked_rejection,
    blank_image
};

std::size_t literal_cases = 0;
std::size_t constructor_cases = 0;

void reset_trace()
{
    allocations.fill({});
    releases.fill(0);
    allocation_count = release_count = attempts = fail_at = 0;
}

void reclaim_observed_leaks()
{
    for (auto& allocation : allocations)
    {
        if (allocation.live)
        {
            check_guards(allocation);
            // Source leaks are asserted before this fixture-only reclamation.
            std::free(allocation.raw);
        }
        allocation = {};
    }
    allocation_count = release_count = attempts = fail_at = 0;
}

template <class T>
void write_expected(
    std::array<unsigned char, sizeof(TgaImage)>& expected, std::size_t offset, const T& value)
{
    require(offset <= expected.size() && sizeof(value) <= expected.size() - offset,
        "expected field is outside the observed native prefix");
    std::memcpy(expected.data() + offset, &value, sizeof(value));
}

void check_object(const TgaImage& image,
    int width,
    int height,
    int depth,
    unsigned char* pixels,
    unsigned char* palette)
{
    std::array<unsigned char, sizeof(TgaImage)> expected;
    expected.fill(0x5a);
    const int bpp = depth / 8;
    unsigned short* converted = nullptr;
    write_expected(expected, offsetof(TgaImage, width), width);
    write_expected(expected, offsetof(TgaImage, height), height);
    write_expected(expected, offsetof(TgaImage, bits_per_pixel), depth);
    write_expected(expected, offsetof(TgaImage, bytes_per_pixel), bpp);
    write_expected(expected, offsetof(TgaImage, pixels), pixels);
    write_expected(expected, offsetof(TgaImage, palette), palette);
    write_expected(expected, offsetof(TgaImage, converted_palette), converted);
    if (depth == 1 || depth == 8)
    {
        const std::array<unsigned char, 4> tail{
            static_cast<unsigned char>(depth == 1 ? 2 : 0), 0, 0, 0};
        write_expected(expected, offsetof(TgaImage, unknown_1c), tail);
    }
    // Check every member byte, including the opaque tail, but not ABI tail padding.
    constexpr std::size_t member_bytes =
        offsetof(TgaImage, unknown_1c) + sizeof(TgaImage::unknown_1c);
    std::array<unsigned char, sizeof(TgaImage)> observed;
    std::memcpy(observed.data(), &image, member_bytes);
    for (std::size_t byte = 0; byte < member_bytes; ++byte)
    {
        require(observed[byte] == expected[byte], "native image member bytes differ");
    }
}

void direct_constructors()
{
    const std::array<std::array<int, 2>, 4> dimensions{{{0, 0}, {1, 1}, {3, 2}, {129, 2}}};
    for (const auto& dimension : dimensions)
    {
        for (const int depth : {1, 8, 16, 24, 32})
        {
            for (const unsigned char map : {0, 255})
            {
                ++cases;
                ++constructor_cases;
                alignas(TgaImage) std::array<unsigned char, sizeof(TgaImage) + 2 * guard> storage;
                storage.fill(0xa5);
                // These setup stores must survive the following lifetime transition.
                volatile unsigned char* initial_bytes = storage.data() + guard;
                for (std::size_t byte = 0; byte < sizeof(TgaImage); ++byte)
                {
                    initial_bytes[byte] = 0x5a;
                }
                reset_trace();
                tracing = true;
                TgaImage* image =
                    new (storage.data() + guard) TgaImage(dimension[0], dimension[1], depth, map);
                tracing = false;
                const std::size_t pixels =
                    static_cast<std::size_t>(dimension[0] * dimension[1] * (depth / 8));
                require(allocation_count == (map ? 2u : 1u) && release_count == 0,
                    "direct constructor allocation count differs");
                require(allocations[0].array && allocations[0].bytes == pixels,
                    "direct constructor pixel allocation differs");
                if (map)
                {
                    require(allocations[1].array && allocations[1].bytes == 768,
                        "direct constructor palette allocation differs");
                }
                check_object(*image,
                    dimension[0],
                    dimension[1],
                    depth,
                    static_cast<unsigned char*>(allocations[0].pointer),
                    map ? static_cast<unsigned char*>(allocations[1].pointer) : nullptr);
                for (std::size_t byte = 0; byte < pixels; ++byte)
                {
                    require(image->pixels[byte] == 0, "direct constructor did not zero pixels");
                }
                if (map)
                {
                    for (unsigned byte = 0; byte < 768; ++byte)
                    {
                        require(
                            image->palette[byte] == 0, "direct constructor did not zero palette");
                    }
                }
                for (std::size_t byte = 0; byte < guard; ++byte)
                {
                    require(
                        storage[byte] == 0xa5 && storage[guard + sizeof(TgaImage) + byte] == 0xa5,
                        "direct constructor object guard changed");
                }
                tracing = true;
                image->~TgaImage();
                tracing = false;
                require(release_count == allocation_count && releases[0] == 0 &&
                            (!map || releases[1] == 1),
                    "direct constructor ownership release differs");
                for (std::size_t index = 0; index < allocation_count; ++index)
                {
                    require(!allocations[index].live, "direct constructor lifecycle leaked");
                }
                reclaim_observed_leaks();
            }
        }
    }
}

Bytes literal_header(unsigned type,
    unsigned width,
    unsigned height,
    unsigned depth,
    unsigned map = 0,
    unsigned count = 0,
    unsigned descriptor = 0,
    unsigned id = 0)
{
    Bytes result(18 + id, 0x97);
    result[0] = static_cast<unsigned char>(id);
    result[1] = static_cast<unsigned char>(map);
    result[2] = static_cast<unsigned char>(type);
    word(result, 3, 0);
    word(result, 5, count);
    result[7] = 24;
    word(result, 8, 0x8b21);
    word(result, 10, 0xe75a);
    word(result, 12, width);
    word(result, 14, height);
    result[16] = static_cast<unsigned char>(depth);
    result[17] = static_cast<unsigned char>(descriptor);
    return result;
}

void append(Bytes& bytes, const Bytes& tail)
{
    bytes.insert(bytes.end(), tail.begin(), tail.end());
}

void literal(const char* name,
    const Bytes& input_bytes,
    const Bytes& expected_pixels,
    const Bytes& expected_palette_prefix = {},
    Outcome outcome = Outcome::image)
{
    ++cases;
    ++literal_cases;
    const std::size_t prefix = guard + 1; // Force all original word loads off natural alignment.
    Bytes guarded(input_bytes.size() + prefix + guard, 0x83);
    std::memcpy(guarded.data() + prefix, input_bytes.data(), input_bytes.size());
    const Bytes before = guarded;
    const auto field = [&](std::size_t offset)
    {
        return unsigned(input_bytes[offset]) | (unsigned(input_bytes[offset + 1]) << 8);
    };
    const unsigned width = field(12), height = field(14), depth = input_bytes[16];
    const bool has_palette = depth == 8 && input_bytes[1] != 0;
    const bool rle = outcome == Outcome::image && (input_bytes[2] == 9 || input_bytes[2] == 10);
    reset_trace();
    tracing = true;
    TgaImage* image = ParseTgaBuffer(guarded.data() + prefix);
    tracing = false;
    if (guarded != before)
    {
        std::fprintf(stderr, "literal %s: ", name);
        fail("input or guard bytes changed");
    }
    require(expected_palette_prefix.size() <= 768, "invalid literal palette oracle");
    if (outcome == Outcome::header_rejection)
    {
        require(
            !image && allocation_count == 0 && attempts == 0, "literal header rejection differs");
        return;
    }
    const std::size_t pixel_bytes = width * height * (depth / 8);
    const std::size_t wanted_allocations = 2 + (has_palette ? 1 : 0) + (rle ? 1 : 0);
    require(allocation_count == wanted_allocations && attempts == wanted_allocations,
        "literal allocation count differs");
    require(!allocations[0].array && allocations[0].bytes == sizeof(TgaImage),
        "literal object allocation differs");
    require(allocations[1].array && allocations[1].bytes == pixel_bytes,
        "literal pixel allocation differs");
    if (has_palette)
    {
        require(allocations[2].array && allocations[2].bytes == 768,
            "literal palette allocation differs");
    }
    if (outcome == Outcome::leaked_rejection)
    {
        ++leak_cases;
        require(!image && release_count == 0, "literal unsupported type was repaired");
        for (std::size_t index = 0; index < allocation_count; ++index)
        {
            require(allocations[index].live, "literal unsupported-type allocation was released");
        }
        reclaim_observed_leaks();
        return;
    }
    require(image == allocations[0].pointer, "literal result pointer differs");
    check_object(*image,
        static_cast<int>(width),
        static_cast<int>(height),
        static_cast<int>(depth),
        static_cast<unsigned char*>(allocations[1].pointer),
        has_palette ? static_cast<unsigned char*>(allocations[2].pointer) : nullptr);
    require(expected_pixels.size() == pixel_bytes, "literal pixel oracle extent differs");
    for (std::size_t byte = 0; byte < pixel_bytes; ++byte)
    {
        if (image->pixels[byte] != expected_pixels[byte])
        {
            std::fprintf(stderr, "literal %s byte %zu: ", name, byte);
            fail("literal pixel stream differs");
        }
        ++checks;
    }
    if (has_palette)
    {
        for (std::size_t byte = 0; byte < 768; ++byte)
        {
            const unsigned char expected =
                byte < expected_palette_prefix.size() ? expected_palette_prefix[byte] : 0;
            require(image->palette[byte] == expected,
                "literal palette or untouched palette tail differs");
        }
    }
    if (rle)
    {
        const auto temporary = wanted_allocations - 1;
        require(allocations[temporary].array && allocations[temporary].bytes == pixel_bytes &&
                    !allocations[temporary].live && release_count == 1 && releases[0] == temporary,
            "literal RLE temporary lifecycle differs");
    }
    else
    {
        require(release_count == 0, "literal parser released an unexpected object");
    }
    const auto prior_releases = release_count;
    tracing = true;
    delete image;
    tracing = false;
    require(release_count == prior_releases + 2 + (has_palette ? 1 : 0) &&
                releases[prior_releases] == 1 && releases[release_count - 1] == 0 &&
                (!has_palette || releases[prior_releases + 1] == 2),
        "literal release order differs");
    for (std::size_t index = 0; index < allocation_count; ++index)
    {
        require(!allocations[index].live, "literal success lifecycle leaked");
    }
    reclaim_observed_leaks();
}

void literal_images()
{
    const Bytes bgr_palette{0x30, 0x20, 0x10, 0x60, 0x50, 0x40, 0x90, 0x80, 0x70, 0xc0, 0xb0, 0xa0};
    const Bytes rgb_palette{0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0};
    auto bytes = literal_header(1, 2, 2, 8, 1, 4);
    append(bytes, bgr_palette);
    append(bytes, {0, 1, 2, 3});
    literal("indexed rows", bytes, {2, 3, 0, 1}, rgb_palette);

    bytes = literal_header(2, 2, 2, 24, 0, 0, 0xcf, 3);
    append(bytes, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    literal("truecolor rows and ignored descriptor bits",
        bytes,
        {9, 8, 7, 12, 11, 10, 3, 2, 1, 6, 5, 4});

    bytes = literal_header(2, 1, 2, 32, 0, 0, 0x0f);
    append(bytes, {0x10, 0x20, 0x30, 0, 0x40, 0x50, 0x60, 255});
    literal("32-bit alpha and rows", bytes, {0x60, 0x50, 0x40, 255, 0x30, 0x20, 0x10, 0});

    bytes = literal_header(9, 3, 2, 8, 255, 2, 0, 1);
    append(bytes, {3, 2, 1, 6, 5, 4, 1, 0, 1, 0x83, 0});
    literal("indexed mixed packet crosses row", bytes, {0, 0, 0, 0, 1, 0}, {1, 2, 3, 4, 5, 6});

    bytes = literal_header(10, 3, 2, 24);
    append(bytes, {1, 1, 2, 3, 4, 5, 6, 0x83, 7, 8, 9});
    literal("24-bit mixed packet crosses row",
        bytes,
        {9, 8, 7, 9, 8, 7, 9, 8, 7, 3, 2, 1, 6, 5, 4, 9, 8, 7});

    bytes = literal_header(10, 2, 2, 32);
    append(bytes, {0x81, 1, 2, 3, 4, 1, 5, 6, 7, 8, 9, 10, 11, 12});
    literal("32-bit mixed packets", bytes, {7, 6, 5, 8, 11, 10, 9, 12, 3, 2, 1, 4, 3, 2, 1, 4});

    bytes = literal_header(2, 4, 1, 8);
    append(bytes, {0, 127, 128, 255});
    literal("8-bit type2 without palette", bytes, {0, 127, 128, 255});

    bytes = literal_header(2, 2, 1, 8, 128, 2);
    append(bytes, {9, 8, 7, 6, 5, 4, 255, 2});
    literal("type2 skips but does not copy palette", bytes, {255, 2});

    bytes = literal_header(10, 2, 1, 8, 2, 2);
    append(bytes, {9, 8, 7, 6, 5, 4, 0x81, 255});
    literal("type10 skips but does not copy palette", bytes, {255, 255});

    bytes = literal_header(1, 1, 1, 24);
    append(bytes, {1, 2, 3});
    literal("type1 empty palette truecolor", bytes, {3, 2, 1});

    bytes = literal_header(9, 1, 1, 32);
    append(bytes, {0x80, 1, 2, 3, 4});
    literal("type9 empty palette truecolor", bytes, {3, 2, 1, 4});

    bytes = literal_header(1, 4, 1, 8, 1, 256, 0, 255);
    Bytes palette(768, 0), expected_palette(768, 0);
    palette[0] = 0x30;
    palette[1] = 0x20;
    palette[2] = 0x10;
    palette[765] = 0xc0;
    palette[766] = 0xb0;
    palette[767] = 0xa0;
    expected_palette[0] = 0x10;
    expected_palette[1] = 0x20;
    expected_palette[2] = 0x30;
    expected_palette[765] = 0xa0;
    expected_palette[766] = 0xb0;
    expected_palette[767] = 0xc0;
    append(bytes, palette);
    append(bytes, {0, 255, 1, 254});
    literal("full palette and extreme indices", bytes, {0, 255, 1, 254}, expected_palette);

    bytes = literal_header(9, 1, 1, 8, 1, 0);
    append(bytes, {0x80, 255});
    literal("empty allocated palette accepts index255", bytes, {255});

    bytes = literal_header(2, 3, 0, 24);
    literal("zero height", bytes, {});
    bytes = literal_header(10, 0, 2, 32);
    literal("zero width RLE", bytes, {});

    // Every rejection buffer still contains a complete safe pixel payload. The
    // orientation control can take the accepted branch without an unsafe read.
    for (const unsigned descriptor : {0x10u, 0x20u, 0x30u})
    {
        bytes = literal_header(2, 1, 1, 24, 0, 0, descriptor);
        append(bytes, {1, 2, 3});
        literal("orientation rejection", bytes, {}, {}, Outcome::header_rejection);
    }
    bytes = literal_header(2, 1, 1, 24);
    word(bytes, 3, 1);
    append(bytes, {1, 2, 3});
    literal("palette first rejection", bytes, {}, {}, Outcome::header_rejection);
    bytes = literal_header(2, 1, 1, 16);
    append(bytes, {1, 2, 3, 4});
    literal("unsupported depth rejection", bytes, {}, {}, Outcome::header_rejection);
    bytes = literal_header(1, 1, 1, 8, 1, 1);
    bytes[7] = 32;
    append(bytes, {1, 2, 3, 4, 0});
    literal("palette depth rejection", bytes, {}, {}, Outcome::header_rejection);
    bytes = literal_header(2, 1, 1, 24, 1, 1);
    append(bytes, {1, 2, 3, 4, 5, 6});
    literal("truecolor map rejection", bytes, {}, {}, Outcome::header_rejection);

    for (const unsigned type : {0u, 3u, 11u})
    {
        bytes = literal_header(type, 2, 1, 8, 1, 1);
        literal("unsupported type leaks constructed allocations",
            bytes,
            {},
            {},
            Outcome::leaked_rejection);
    }
    for (const unsigned type : {4u, 255u})
    {
        bytes = literal_header(type, 2, 1, 8, 1, 1);
        literal(
            "unknown type returns allocated blank image", bytes, {0, 0}, {}, Outcome::blank_image);
    }

    // Generate the real first-screen dimensions without using retail assets.
    // Each source row is one known palette index split into run packets. The
    // independent expected output row y is the literal formula (599-y)%256.
    bytes = literal_header(9, 800, 600, 8, 1, 256);
    palette.assign(768, 0);
    expected_palette.assign(768, 0);
    for (unsigned color = 0; color < 256; ++color)
    {
        palette[3 * color] = static_cast<unsigned char>(255 - color);
        palette[3 * color + 1] = 17;
        palette[3 * color + 2] = static_cast<unsigned char>(color);
        expected_palette[3 * color] = static_cast<unsigned char>(color);
        expected_palette[3 * color + 1] = 17;
        expected_palette[3 * color + 2] = static_cast<unsigned char>(255 - color);
    }
    append(bytes, palette);
    for (unsigned row = 0; row < 600; ++row)
    {
        for (unsigned packet = 0; packet < 6; ++packet)
        {
            append(bytes, {0xff, static_cast<unsigned char>(row % 256)});
        }
        append(bytes, {0x9f, static_cast<unsigned char>(row % 256)});
    }
    Bytes expected(800 * 600);
    for (unsigned row = 0; row < 600; ++row)
    {
        for (unsigned column = 0; column < 800; ++column)
        {
            expected[row * 800 + column] = static_cast<unsigned char>((599 - row) % 256);
        }
    }
    literal("generated 800x600 indexed screen", bytes, expected, expected_palette);
}
} // namespace

int main()
{
    direct_constructors();
    literal_images();
    generated_cases();
    require(constructor_cases == 40 && literal_cases == 28 && cases == 3531,
        "generated case count changed");
    require(leak_cases == 39 && thrown_cases == 69, "preserved leak/exception case count changed");
    std::printf("%u parser/allocation cases (%zu direct constructors, %zu literal images); %llu "
                "checks; %u observed rejected-type leaks; %u injected host allocation exceptions\n",
        cases,
        constructor_cases,
        literal_cases,
        checks,
        leak_cases,
        thrown_cases);
}
