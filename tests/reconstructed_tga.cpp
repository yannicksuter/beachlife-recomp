#include "tga_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <span>
#include <stdexcept>
#include <vector>

unsigned char* GetTgaPaletteData(unsigned char*, unsigned char, unsigned char);
unsigned char* GetTgaPixelData(unsigned char*, unsigned char, unsigned short, unsigned char);
void CopyTgaPalette(const unsigned char*, TgaImage*, int);
void CopyTgaPixels(const unsigned char*, TgaImage*);
void ExpandTgaRle(const unsigned char*, TgaImage*);

namespace allocations
{
constexpr std::size_t guard = alignof(std::max_align_t);
struct Record
{
    unsigned char* base = nullptr;
    std::size_t bytes = 0;
    bool released = false;
};
std::array<Record, 8> records;
std::size_t count = 0;
bool active = false;

[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "Allocation check failed: %s\n", message);
    std::abort();
}

void begin()
{
    if (active)
    {
        fail("nested allocation fixture");
    }
    records.fill({});
    count = 0;
    active = true;
}

void finish()
{
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!records[index].released)
        {
            fail("owned allocation was not released");
        }
    }
    active = false;
}
} // namespace allocations

// Test-only array allocation instrumentation still allocates and frees through
// the host CRT. The reconstructed library contains no replacement operators.
void* operator new[](std::size_t bytes)
{
    if (!allocations::active)
    {
        if (void* result = std::malloc(bytes ? bytes : 1))
        {
            return result;
        }
        throw std::bad_alloc();
    }
    if (allocations::count == allocations::records.size() || bytes > 1024 * 1024)
    {
        allocations::fail("unexpected array allocation");
    }
    auto* base = static_cast<unsigned char*>(std::malloc(bytes + 2 * allocations::guard));
    if (!base)
    {
        throw std::bad_alloc();
    }
    std::memset(base, 0xa5, bytes + 2 * allocations::guard);
    allocations::records[allocations::count++] = {base, bytes, false};
    return base + allocations::guard;
}

void operator delete[](void* memory) noexcept
{
    if (!memory)
    {
        return;
    }
    if (allocations::active)
    {
        for (std::size_t index = 0; index < allocations::count; ++index)
        {
            auto& record = allocations::records[index];
            if (memory != record.base + allocations::guard)
            {
                continue;
            }
            if (record.released)
            {
                allocations::fail("array released twice");
            }
            for (std::size_t byte = 0; byte < allocations::guard; ++byte)
            {
                if (record.base[byte] != 0xa5 ||
                    record.base[allocations::guard + record.bytes + byte] != 0xa5)
                {
                    allocations::fail("array guard changed");
                }
            }
            record.released = true;
            std::free(record.base);
            return;
        }
    }
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    ::operator delete[](memory);
}

namespace
{
using Bytes = std::vector<unsigned char>;
using Pixel = std::array<unsigned char, 4>;
std::size_t checks = 0;
std::size_t image_cases = 0;

void expect(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

void pointer_helpers()
{
    for (unsigned id : {0u, 1u, 255u})
    {
        for (unsigned palette : {0u, 1u, 256u, 65535u})
        {
            Bytes file(18 + id + palette * 3 + 1);
            for (unsigned flag : {0u, 1u, 255u})
            {
                auto* colors = GetTgaPaletteData(
                    file.data(), static_cast<unsigned char>(id), static_cast<unsigned char>(flag));
                auto* pixels = GetTgaPixelData(file.data(),
                    static_cast<unsigned char>(id),
                    static_cast<unsigned short>(palette),
                    static_cast<unsigned char>(flag));
                expect(
                    colors == (flag ? file.data() + 18 + id : nullptr), "Palette offset differs");
                expect(pixels == file.data() + 18 + id + (flag ? palette * 3 : 0),
                    "Pixel offset differs");
            }
        }
    }
}

void constructor_and_ownership()
{
    for (unsigned mask = 0; mask < 8; ++mask)
    {
        allocations::begin();
        std::size_t expected_allocations = 0;
        {
            TgaImage image(7, 3);
            expect(image.width == 7 && image.height == 3 && image.bits_per_pixel == 16 &&
                       image.bytes_per_pixel == 2,
                "Constructor format differs");
            expect(!image.pixels && !image.palette && !image.converted_palette,
                "Nonallocating constructor owns a buffer");
            if (mask & 1)
            {
                image.pixels = new unsigned char[42];
                ++expected_allocations;
            }
            if (mask & 2)
            {
                image.palette = new unsigned char[768];
                ++expected_allocations;
            }
            if (mask & 4)
            {
                image.converted_palette = new unsigned short[256];
                ++expected_allocations;
            }
        }
        expect(allocations::count == expected_allocations, "Unexpected owned allocation count");
        allocations::finish();
    }
}

void palettes()
{
    const Bytes bgr{37, 23, 11, 71, 59, 43, 127, 113, 97};
    const Bytes rgb{11, 23, 37, 43, 59, 71, 97, 113, 127};
    for (int count : {-3, 0, 1, 3})
    {
        allocations::begin();
        {
            TgaImage image(0, 0);
            image.palette = new unsigned char[rgb.size()];
            CopyTgaPalette(bgr.data(), &image, count);
            for (std::size_t byte = 0; byte < rgb.size(); ++byte)
            {
                const auto expected =
                    count > 0 && byte < static_cast<std::size_t>(count) * 3 ? rgb[byte] : 0xa5;
                expect(image.palette[byte] == expected,
                    "Palette conversion or untouched tail differs");
            }
        }
        allocations::finish();
    }
    allocations::begin();
    {
        TgaImage image(0, 0);
        image.palette = new unsigned char[3]{11, 23, 37};
        CopyTgaPalette(image.palette, &image, 1);
        expect(image.palette[0] == 37 && image.palette[1] == 23 && image.palette[2] == 37,
            "Sequential in-place palette writes changed");
    }
    allocations::finish();
}

std::vector<Pixel> picture(int width, int height)
{
    std::vector<Pixel> pixels;
    for (int index = 0; index < width * height; ++index)
    {
        pixels.push_back({static_cast<unsigned char>(index * 53 + 17),
            static_cast<unsigned char>(index * 29 + 71),
            static_cast<unsigned char>(index * 11 + 137),
            static_cast<unsigned char>(index * 7 + 211)});
    }
    return pixels;
}

Bytes bottom_rows(const std::vector<Pixel>& picture, int width, int height, int bytes_per_pixel)
{
    Bytes encoded;
    for (int row = height - 1; row >= 0; --row)
    {
        for (int column = 0; column < width; ++column)
        {
            const Pixel& pixel = picture[static_cast<std::size_t>(row * width + column)];
            if (bytes_per_pixel == 1)
            {
                encoded.push_back(pixel[0]);
            }
            else
            {
                encoded.insert(encoded.end(), {pixel[2], pixel[1], pixel[0]});
                if (bytes_per_pixel == 4)
                {
                    encoded.push_back(pixel[3]);
                }
            }
        }
    }
    return encoded;
}

struct Packet
{
    unsigned count;
    bool run;
};

Bytes packets(const Bytes& raw, int bytes_per_pixel, std::span<const Packet> plan)
{
    Bytes encoded;
    std::size_t position = 0;
    for (const auto packet : plan)
    {
        expect(packet.count >= 1 && packet.count <= 128, "Invalid generated packet length");
        const std::size_t bytes = packet.count * static_cast<std::size_t>(bytes_per_pixel);
        expect(bytes <= raw.size() - position, "Generated packet exceeds the logical picture");
        if (packet.run)
        {
            for (std::size_t byte = 0; byte < bytes; ++byte)
            {
                expect(raw[position + byte] == raw[position + byte % bytes_per_pixel],
                    "Generated run contains different pixels");
            }
        }
        encoded.push_back(static_cast<unsigned char>((packet.run ? 128 : 0) | (packet.count - 1)));
        const auto length = packet.run ? static_cast<std::size_t>(bytes_per_pixel) : bytes;
        encoded.insert(encoded.end(),
            raw.begin() + static_cast<std::ptrdiff_t>(position),
            raw.begin() + static_cast<std::ptrdiff_t>(position + length));
        position += bytes;
    }
    expect(position == raw.size(), "Generated packet plan is incomplete");
    return encoded;
}

void check_picture(int width,
    int height,
    int bytes_per_pixel,
    const std::vector<Pixel>& logical,
    std::span<const Packet> plan = {})
{
    const auto raw = bottom_rows(logical, width, height, bytes_per_pixel);
    const auto encoded = plan.empty() ? raw : packets(raw, bytes_per_pixel, plan);
    const auto saved = encoded;
    allocations::begin();
    {
        TgaImage image(width, height);
        image.bits_per_pixel = bytes_per_pixel * 8;
        image.bytes_per_pixel = bytes_per_pixel;
        image.pixels = new unsigned char[raw.size()];
        if (plan.empty())
        {
            CopyTgaPixels(encoded.data(), &image);
            expect(allocations::count == 1, "Raw pixel copy allocated a buffer");
        }
        else
        {
            ExpandTgaRle(encoded.data(), &image);
            expect(allocations::count == 2 && allocations::records[1].bytes == raw.size() &&
                       allocations::records[1].released,
                "RLE temporary allocation/release differs");
        }
        for (std::size_t index = 0; index < logical.size(); ++index)
        {
            for (int channel = 0; channel < bytes_per_pixel; ++channel)
            {
                expect(image.pixels[index * bytes_per_pixel + channel] == logical[index][channel],
                    "Pixel channel, row or packet output differs");
            }
        }
        expect(encoded == saved, "Source pixels changed");
    }
    allocations::finish();
    ++image_cases;
}

void pixel_and_packet_cases()
{
    const Pixel a{1, 17, 33, 49}, b{2, 18, 34, 50}, c{3, 19, 35, 51};
    const Pixel d{4, 20, 36, 52}, e{5, 21, 37, 53}, f{6, 22, 38, 54};
    const Pixel g{7, 23, 39, 55}, h{8, 24, 40, 56};
    const std::vector<Pixel> mixed{f, g, h, c, d, e, c, c, c, a, b, c};
    const std::array<Packet, 3> mixed_plan{{{2, false}, {5, true}, {5, false}}};
    for (int bytes_per_pixel : {1, 3, 4})
    {
        check_picture(2, 2, bytes_per_pixel, {a, b, c, d});
        check_picture(7, 3, bytes_per_pixel, picture(7, 3));
        check_picture(3, 4, bytes_per_pixel, mixed, mixed_plan);
        for (unsigned count = 1; count <= 128; ++count)
        {
            const std::array<Packet, 1> raw{{{count, false}}}, run{{{count, true}}};
            check_picture(static_cast<int>(count),
                1,
                bytes_per_pixel,
                picture(static_cast<int>(count), 1),
                raw);
            check_picture(
                static_cast<int>(count), 1, bytes_per_pixel, std::vector<Pixel>(count, a), run);
        }
        const std::array<Packet, 1> maximum_raw{{{128, false}}}, maximum_run{{{128, true}}};
        check_picture(16, 8, bytes_per_pixel, picture(16, 8), maximum_raw);
        check_picture(16, 8, bytes_per_pixel, std::vector<Pixel>(128, a), maximum_run);
        const std::array<Packet, 2> split{{{128, false}, {1, false}}};
        check_picture(3, 43, bytes_per_pixel, picture(3, 43), split);
    }
}

void empty_and_overlap_cases()
{
    for (const auto dimensions : {std::array{0, 0}, std::array{0, 3}, std::array{3, 0}})
    {
        allocations::begin();
        {
            TgaImage image(dimensions[0], dimensions[1]);
            image.bits_per_pixel = 8;
            image.bytes_per_pixel = 1;
            ExpandTgaRle(nullptr, &image);
            expect(allocations::count == 1 && allocations::records[0].bytes == 0 &&
                       allocations::records[0].released,
                "Empty image temporary differs");
        }
        allocations::finish();
    }
    for (const auto dimensions : {std::array{-1, 2}, std::array{2, -1}, std::array{0, 2}})
    {
        allocations::begin();
        {
            TgaImage image(dimensions[0], dimensions[1]);
            image.bits_per_pixel = 24;
            CopyTgaPixels(nullptr, &image);
            expect(allocations::count == 0, "Empty raw copy allocated memory");
        }
        allocations::finish();
    }
    allocations::begin();
    {
        TgaImage image(2, 2);
        image.bits_per_pixel = 8;
        image.pixels = new unsigned char[4]{1, 2, 3, 4};
        CopyTgaPixels(image.pixels, &image);
        expect(image.pixels[0] == 1 && image.pixels[1] == 2 && image.pixels[2] == 1 &&
                   image.pixels[3] == 2,
            "Sequential in-place row writes changed");
    }
    allocations::finish();
}
} // namespace

int main()
{
    try
    {
        pointer_helpers();
        constructor_and_ownership();
        palettes();
        pixel_and_packet_cases();
        empty_and_overlap_cases();
        std::cout << checks << " reconstructed TGA checks passed across " << image_cases
                  << " generated image cases\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
