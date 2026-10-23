#include "startup_jpeg_export.h"
#include "startup_screen_item.h"
#include "tga_image.h"

#include "runtime/jpeg_export.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace
{
std::size_t checks = 0;

void require(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

std::vector<unsigned char> read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "encoded file cannot be opened");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

unsigned int be16(const std::vector<unsigned char>& bytes, std::size_t offset)
{
    require(offset + 1 < bytes.size(), "truncated JPEG word");
    return (static_cast<unsigned int>(bytes[offset]) << 8) | bytes[offset + 1];
}

void check_jpeg_header(const std::vector<unsigned char>& bytes, int width, int height)
{
    require(bytes.size() > 4 && bytes[0] == 0xff && bytes[1] == 0xd8 &&
                bytes[bytes.size() - 2] == 0xff && bytes.back() == 0xd9,
        "JPEG start/end markers differ");
    bool frame = false;
    unsigned int quantization_tables = 0;
    std::size_t position = 2;
    while (position + 4 <= bytes.size())
    {
        require(bytes[position] == 0xff, "JPEG marker boundary differs");
        const unsigned char marker = bytes[position + 1];
        const unsigned int length = be16(bytes, position + 2);
        require(length >= 2 && position + 2 + length <= bytes.size(), "JPEG segment is truncated");
        const std::size_t body = position + 4;
        const std::size_t end = position + 2 + length;
        if (marker == 0xc0)
        {
            require(length == 17 && bytes[body] == 8 && bytes[body + 5] == 3,
                "JPEG baseline RGB component contract differs");
            require(be16(bytes, body + 1) == static_cast<unsigned int>(height) &&
                        be16(bytes, body + 3) == static_cast<unsigned int>(width),
                "JPEG dimensions differ");
            require(bytes[body + 6] == 1 && bytes[body + 7] == 0x22 && bytes[body + 8] == 0 &&
                        bytes[body + 9] == 2 && bytes[body + 10] == 0x11 && bytes[body + 11] == 1 &&
                        bytes[body + 12] == 3 && bytes[body + 13] == 0x11 && bytes[body + 14] == 1,
                "JPEG 2x2/1x1/1x1 sampling differs");
            frame = true;
        }
        else if (marker == 0xdb)
        {
            std::size_t entry = body;
            while (entry < end)
            {
                require(entry + 65 <= end && (bytes[entry] & 0xf0) == 0,
                    "JPEG quantization table shape differs");
                require(bytes[entry] == 0 || bytes[entry] == 1, "unexpected quantization table");
                quantization_tables |= 1U << bytes[entry];
                for (std::size_t index = 1; index <= 64; ++index)
                {
                    require(bytes[entry + index] == 1, "JPEG quality-100 quantization differs");
                }
                entry += 65;
            }
        }
        if (marker == 0xda)
        {
            break;
        }
        position = end;
    }
    require(frame && quantization_tables == 3, "JPEG frame/quantization tables missing");
}

void check_color(
    const unsigned char* pixels, int width, int x, int y, std::array<unsigned char, 3> expected)
{
    for (int channel = 0; channel < 3; ++channel)
    {
        const int actual = pixels[(y * width + x) * 3 + channel];
        require(std::abs(actual - expected[channel]) <= 8, "decoded RGB/top-down pixel differs");
    }
}

void check_state(const ScreenRectangle& area, unsigned char* pixels, const char* path)
{
    const auto& state = jpeg_export_properties;
    require(state.dib_width == static_cast<unsigned int>(area.right - area.left) &&
                state.dib_height == area.bottom - area.top && state.dib_bytes == pixels &&
                state.dib_pad_bytes == 0 && state.dib_channels == 3 && state.dib_color == 1 &&
                state.jpg_file == path && state.jpg_width == state.dib_width &&
                state.jpg_height == static_cast<unsigned int>(state.dib_height) &&
                state.jpg_channels == 3 && state.jpg_color == 3 && state.jpg_subsampling == 1 &&
                state.quality == 100,
        "game JPEG settings publication differs");
}

void generated_image(const std::filesystem::path& folder)
{
    constexpr int width = 33;
    constexpr int height = 35;
    TgaImage image(width, height, 24, 0);
    require(image.width == width && image.height == height && image.bits_per_pixel == 24 &&
                image.bytes_per_pixel == 3 && image.pixels && !image.palette &&
                !image.converted_palette,
        "genuine TgaImage allocation differs");
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::array<unsigned char, 3> color =
                x < 8 ? std::array<unsigned char, 3>{24, 208, 32}
                      : (y < 17 ? std::array<unsigned char, 3>{224, 32, 16}
                                : std::array<unsigned char, 3>{16, 48, 224});
            std::copy(color.begin(), color.end(), image.pixels + (y * width + x) * 3);
        }
    }
    const std::vector<unsigned char> before(image.pixels, image.pixels + width * height * 3);
    const ScreenRectangle area{7, 11, 40, 46};
    const std::string path = (folder / "generated.jpg").string();
    jpeg_export_result = 123;
    ExportJpegImage(&area, image.pixels, path.c_str(), 100);
    require(jpeg_export_result == 0, "game JPEG export failed");
    const auto bytes = read_file(path);
    check_jpeg_header(bytes, width, height);
    check_state(area, image.pixels, path.c_str());

    int decoded_width = 0;
    int decoded_height = 0;
    int channels = 0;
    require(bytes.size() <= INT_MAX, "encoded fixture unexpectedly large");
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> decoded(
        stbi_load_from_memory(bytes.data(),
            static_cast<int>(bytes.size()),
            &decoded_width,
            &decoded_height,
            &channels,
            3),
        stbi_image_free);
    require(decoded && decoded_width == width && decoded_height == height && channels == 3,
        "real JPEG decoder dimensions differ");
    check_color(decoded.get(), width, 20, 3, {224, 32, 16});
    check_color(decoded.get(), width, 20, 30, {16, 48, 224});
    check_color(decoded.get(), width, 2, 16, {24, 208, 32});
    require(std::equal(before.begin(), before.end(), image.pixels), "borrowed RGB bytes changed");

    const std::string missing = (folder / "missing-parent" / "image.jpg").string();
    require(
        !std::filesystem::exists(folder / "missing-parent"), "failure parent unexpectedly exists");
    ExportJpegImage(&area, image.pixels, missing.c_str(), 100);
    require(jpeg_export_result == -10, "game JPEG open-failure result differs");
    check_state(area, image.pixels, missing.c_str());
    require(std::equal(before.begin(), before.end(), image.pixels),
        "failure changed borrowed RGB bytes");
    // The original globals retain borrowed pointer values. Do not dereference
    // them after these local inputs and the genuine image finish their lives.
}

void bounded_edges(const std::filesystem::path& folder)
{
    std::array<unsigned char, 19> guarded{};
    guarded.fill(0xa5);
    guarded[8] = 128;
    guarded[9] = 64;
    guarded[10] = 192;
    const auto before = guarded;
    const ScreenRectangle area{-3, 4, -2, 5};
    const std::string path = (folder / "one-pixel.jpg").string();
    ExportJpegImage(&area, guarded.data() + 8, path.c_str(), 100);
    require(jpeg_export_result == 0, "one-pixel export failed");
    check_jpeg_header(read_file(path), 1, 1);
    require(guarded == before, "one-pixel input/guards changed");
    check_state(area, guarded.data() + 8, path.c_str());

    using beachlife::runtime::jpeg::Result;
    using beachlife::runtime::jpeg::write_rgb420_file;
    const std::string invalid = (folder / "invalid.jpg").string();
    constexpr std::array<std::array<std::uint32_t, 2>, 3> invalid_dimensions{
        {{0, 1}, {65536, 1}, {65535, 65535}}};
    for (const auto& dimensions : invalid_dimensions)
    {
        require(write_rgb420_file(
                    invalid.c_str(), guarded.data() + 8, dimensions[0], dimensions[1], 100) ==
                    Result::invalid_properties,
            "native dimension/indexing boundary differs");
    }
    require(write_rgb420_file(invalid.c_str(), nullptr, 1, 1, 100) == Result::invalid_properties,
        "null native input accepted");
    require(!std::filesystem::exists(invalid) && guarded == before,
        "invalid native request performed IO or changed input");
}

void real_write_failure(const char* full_device)
{
    std::array<unsigned char, 3> pixel{20, 40, 60};
    const auto before = pixel;
    const ScreenRectangle area{0, 0, 1, 1};
    ExportJpegImage(&area, pixel.data(), full_device, 100);
    require(jpeg_export_result == -23, "game JPEG real write-failure result differs");
    check_state(area, pixel.data(), full_device);
    require(pixel == before, "real write failure changed borrowed input");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        require(argc == 3, "expected an output directory and real failing device");
        const std::filesystem::path folder(argv[1]);
        std::filesystem::create_directories(folder);
        generated_image(folder);
        bounded_edges(folder);
        real_write_failure(argv[2]);
        std::cout << "{\"groups\":3,\"image_lifetimes\":1,\"game_calls\":4,"
                     "\"host_calls\":8,\"encoded_images\":2,\"checks\":"
                  << checks << "}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
