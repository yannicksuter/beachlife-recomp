#pragma once

#include "data/folder_source.h"
#include "runtime/retail_files.h"
#include "startup_screen_font_data.h"
#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace font_owner_test
{
static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(std::endian::native == std::endian::little);
namespace fs = std::filesystem;
namespace files = beachlife::runtime::retail_files;

inline void host_require(bool condition, const char* reason)
{
    if (!condition)
    {
        throw std::runtime_error(reason);
    }
}

// A value snapshot has no ownership or ScreenFontData lifetime. In particular,
// its image pointer is only an identity to compare while the real owner lives.
struct Snapshot
{
    TgaImage* image;
    unsigned short glyphs[256][4];
    signed char kerning[65536];
    unsigned int unknown_10804[2];

    explicit Snapshot(const ScreenFontData& value)
        : image(value.image)
    {
        for (std::size_t i = 0; i < 256; ++i)
        {
            std::copy(std::begin(value.glyphs[i]), std::end(value.glyphs[i]), glyphs[i]);
        }
        std::copy(std::begin(value.kerning), std::end(value.kerning), kerning);
        std::copy(std::begin(value.unknown_10804), std::end(value.unknown_10804), unknown_10804);
    }
};

// The old metric literals deliberately replace metric values after genuine
// construction. The image and its real owning lifetime are never replaced.
inline void clear_metrics(ScreenFontData& value)
{
    for (auto& glyph : value.glyphs)
    {
        std::fill(std::begin(glyph), std::end(glyph), 0);
    }
    std::fill(std::begin(value.kerning), std::end(value.kerning), 0);
    std::fill(std::begin(value.unknown_10804), std::end(value.unknown_10804), 0);
}

inline void word(std::span<unsigned char> bytes, std::size_t offset, unsigned value)
{
    host_require(offset + 2 <= bytes.size() && value <= 0xffffU, "generated WORD bounds");
    bytes[offset] = static_cast<unsigned char>(value);
    bytes[offset + 1] = static_cast<unsigned char>(value >> 8);
}

inline void dword(std::span<unsigned char> bytes, std::size_t offset, unsigned value)
{
    host_require(offset + 4 <= bytes.size(), "generated DWORD bounds");
    for (unsigned i = 0; i < 4; ++i)
    {
        bytes[offset + i] = static_cast<unsigned char>(value >> (i * 8));
    }
}

struct Input
{
    std::array<unsigned char, 2056> coordinates{};
    std::array<unsigned char, 65536> kerning{};
    std::array<unsigned char, 516> tiff{};

    Input()
    {
        dword(coordinates, 0, 5);
        dword(coordinates, 4, 9);
        // A complete little-endian 2x2, one-strip, eight-bit TIFF.
        word(tiff, 0, 0x4949);
        word(tiff, 2, 42);
        dword(tiff, 4, 8);
        constexpr unsigned tags[][4] = {{256, 4, 1, 2},
            {257, 4, 1, 2},
            {258, 3, 1, 8},
            {259, 3, 1, 1},
            {277, 3, 1, 1},
            {273, 4, 1, 512},
            {278, 4, 1, 2},
            {279, 4, 1, 4}};
        word(tiff, 8, static_cast<unsigned>(std::size(tags)));
        std::size_t offset = 10;
        for (const auto& tag : tags)
        {
            word(tiff, offset, tag[0]);
            word(tiff, offset + 2, tag[1]);
            dword(tiff, offset + 4, tag[2]);
            dword(tiff, offset + 8, tag[3]);
            offset += 12;
        }
        dword(tiff, offset, 0);
        tiff[512] = 1;
        tiff[513] = 2;
        tiff[514] = 3;
        tiff[515] = 4;
    }
};

// Only generated files are written. FolderSource receives an explicit manifest;
// the real constructor and TIFF loader read through the real Session.
class GeneratedFonts
{
  public:
    explicit GeneratedFonts(const fs::path& root)
        : root_(root)
    {
        host_require(!fs::exists(root_), "generated font directory already exists");
        fs::create_directories(root_ / "Data/Fonts");
    }
    ~GeneratedFonts()
    {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }
    GeneratedFonts(const GeneratedFonts&) = delete;
    GeneratedFonts& operator=(const GeneratedFonts&) = delete;

    void add(const std::string& name, const Input& input, bool image = true)
    {
        host_require(!name.empty() && name.size() < 24 &&
                         name.find_first_not_of(
                             "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") ==
                             std::string::npos,
            "generated font name outside bounded domain");
        write("Data/Fonts/" + name + "TexCoord.Dat", input.coordinates);
        if (image)
        {
            write("Data/Fonts/" + name + ".tiff", input.tiff);
        }
        write("Data/Fonts/" + name + ".Dat", input.kerning);
    }
    std::shared_ptr<const beachlife::data::FileSource> mount() const
    {
        return std::make_shared<beachlife::data::FolderSource>(root_, entries_);
    }

  private:
    struct Close
    {
        void operator()(FILE* file) const
        {
            std::fclose(file);
        }
    };
    void write(const std::string& name, std::span<const unsigned char> bytes)
    {
        const auto path = root_ / name;
        host_require(!fs::exists(path), "duplicate generated font file");
        std::unique_ptr<FILE, Close> stream(std::fopen(path.string().c_str(), "wb"));
        host_require(stream != nullptr, "cannot create generated font file");
        host_require(std::fwrite(bytes.data(), 1, bytes.size(), stream.get()) == bytes.size(),
            "generated font write failed");
        host_require(std::fflush(stream.get()) == 0, "generated font flush failed");
        entries_.push_back({name, bytes.size()});
    }

    fs::path root_;
    std::vector<beachlife::data::SourceEntry> entries_;
};
} // namespace font_owner_test
