#include "data/folder_source.h"
#include "runtime/retail_files.h"
#include "tga_image.h"
#include "tiff_image_loader.h"
#include "tiff_read_context.h"

#include <array>
#include <bit>
#include <cfenv>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

namespace
{
static_assert(CHAR_BIT == 8 && sizeof(short) == 2 && sizeof(int) == 4);
static_assert(sizeof(unsigned int) == 4 && sizeof(void*) == 8);
static_assert(std::endian::native == std::endian::little);
static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == 8);
static_assert(std::is_standard_layout_v<TgaImage>);
static_assert(
    std::is_standard_layout_v<TiffReadContext> && std::is_trivially_copyable_v<TiffReadContext>);
static_assert(sizeof(TiffReadContext) == 216);
static_assert(
    std::is_same_v<decltype(TiffReadContext::file), beachlife::runtime::retail_files::File*>);
static_assert(offsetof(TiffReadContext, description) == 8);
static_assert(offsetof(TiffReadContext, bits_per_sample) == 88);
static_assert(offsetof(TiffReadContext, strip_count) == 128);
static_assert(offsetof(TiffReadContext, strip_offsets) == 144);
static_assert(offsetof(TiffReadContext, strip_byte_counts) == 152);
static_assert(offsetof(TiffReadContext, tile_offsets) == 176);
static_assert(offsetof(TiffReadContext, tile_byte_counts) == 184);
static_assert(offsetof(TiffReadContext, next_directory_offset) == 208);

namespace fs = std::filesystem;
namespace data = beachlife::data;
namespace retail = beachlife::runtime::retail_files;
using FileBytes = std::array<unsigned char, 4096>;
struct FileClose
{
    void operator()(FILE* stream) const
    {
        std::fclose(stream);
    }
};
using File = std::unique_ptr<FILE, FileClose>;

struct Failure
{
    const char* name;
    const char* reason;
    std::size_t index;
    unsigned actual;
    unsigned expected;
};

const char* current_case = "setup";
unsigned checks = 0;
unsigned calls = 0;
unsigned images = 0;
unsigned destroyed = 0;
unsigned pixel_bytes = 0;
unsigned palette_bytes = 0;

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        throw Failure{current_case, reason, 0, 0, 1};
    }
}

void host_require(bool condition, const char* reason)
{
    if (!condition)
    {
        throw std::runtime_error(reason);
    }
}

void equal_byte(unsigned actual, unsigned expected, const char* reason, std::size_t index)
{
    ++checks;
    if (actual != expected)
    {
        throw Failure{current_case, reason, index, actual, expected};
    }
}

void word(FileBytes& file, std::size_t offset, unsigned value)
{
    host_require(offset + 2 <= file.size() && value <= 0xffffu, "generated WORD bounds");
    file[offset] = static_cast<unsigned char>(value);
    file[offset + 1] = static_cast<unsigned char>(value >> 8);
}

void dword(FileBytes& file, std::size_t offset, unsigned value)
{
    host_require(offset + 4 <= file.size(), "generated DWORD bounds");
    for (unsigned byte = 0; byte < 4; ++byte)
    {
        file[offset + byte] = static_cast<unsigned char>(value >> (8 * byte));
    }
}

struct Directory
{
    FileBytes& file;
    unsigned entries = 0;
    std::size_t next = 10;

    explicit Directory(FileBytes& storage)
        : file(storage)
    {
        file.fill(0xa5);
        word(file, 0, 0x4949);
        word(file, 2, 42);
        dword(file, 4, 8);
    }

    void add(unsigned tag, unsigned type, unsigned count, unsigned value)
    {
        host_require(next + 16 < 320, "generated directory overlaps tables");
        word(file, next, tag);
        word(file, next + 2, type);
        dword(file, next + 4, count);
        dword(file, next + 8, value);
        ++entries;
        next += 12;
    }

    void finish()
    {
        word(file, 8, entries);
        dword(file, next, 0);
    }
};

void dimensions(Directory& directory,
    unsigned width,
    unsigned height,
    unsigned bits,
    unsigned compression,
    unsigned samples)
{
    directory.add(256, 4, 1, width);
    directory.add(257, 4, 1, height);
    directory.add(258, 3, 1, bits);
    directory.add(259, 3, 1, compression);
    directory.add(277, 3, 1, samples);
}

void strips(Directory& directory, unsigned count, unsigned rows, unsigned offset, unsigned bytes)
{
    directory.add(273, 4, count, offset);
    directory.add(278, 4, 1, rows);
    directory.add(279, 4, count, bytes);
}

template <std::size_t N>
void payload(FileBytes& file, std::size_t offset, const unsigned char (&bytes)[N])
{
    host_require(offset + N <= file.size(), "generated payload bounds");
    for (std::size_t i = 0; i < N; ++i)
    {
        file[offset + i] = bytes[i];
    }
}

// These three WORDs are away from precision-sensitive integer boundaries.
// The independent literal RGB oracle below is not produced by TiffPaletteByte.
void palette(FileBytes& file)
{
    constexpr unsigned values[] = {0x0000, 0x0200, 0xff00};
    for (unsigned channel = 0; channel < 3; ++channel)
    {
        for (unsigned index = 0; index < 256; ++index)
        {
            word(file, 1024 + 2 * (channel * 256 + index), values[(index + channel) % 3]);
        }
    }
}

struct GeneratedFiles
{
    fs::path root;

    explicit GeneratedFiles(const char* path)
        : root(path)
    {
        host_require(!fs::exists(root), "generated directory must be fresh");
        host_require(fs::create_directories(root), "create generated directory");
        host_require(
            fs::create_directories(root / "Data" / "Fonts"), "create generated asset path");
    }

    ~GeneratedFiles()
    {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }

    fs::path write(const char* name, const FileBytes& bytes) const
    {
        fs::path path = root / "Data" / "Fonts" / name;
        const std::string native = path.string();
        File stream(std::fopen(native.c_str(), "wb"));
        host_require(stream != nullptr, "open generated TIFF");
        host_require(std::fwrite(bytes.data(), 1, bytes.size(), stream.get()) == bytes.size(),
            "write generated TIFF");
        host_require(std::fclose(stream.release()) == 0, "close generated TIFF");
        return path;
    }
};

void file_unchanged(const fs::path& path, const FileBytes& before)
{
    const std::string native = path.string();
    File stream(std::fopen(native.c_str(), "rb"));
    host_require(stream != nullptr, "reopen generated TIFF");
    FileBytes after;
    host_require(std::fread(after.data(), 1, after.size(), stream.get()) == after.size(),
        "reread generated TIFF");
    require(std::fgetc(stream.get()) == EOF && !std::ferror(stream.get()), "file-extent");
    for (std::size_t i = 0; i < before.size(); ++i)
    {
        equal_byte(after[i], before[i], "file-bytes", i);
    }
    host_require(std::fclose(stream.release()) == 0, "close reread TIFF");
}

template <std::size_t N>
void load_case(const GeneratedFiles& files,
    const char* name,
    const FileBytes& bytes,
    int width,
    int height,
    int depth,
    const unsigned char (&pixels)[N],
    int palette_entries)
{
    current_case = name;
    const fs::path path = files.write(name, bytes);
    const std::string logical = std::string("Data/Fonts/") + name;
    const std::string alias = std::string("DATA\\FONTS\\") + name;
    auto source = std::make_shared<data::FolderSource>(
        files.root, std::vector<data::SourceEntry>{{logical, bytes.size()}});
    retail::Session session(source);
    ++calls;
    std::unique_ptr<TgaImage> image(LoadTiffFile(alias.c_str()));
    require(image != nullptr, "image-owner");
    ++images;
    require(image->width == width && image->height == height, "dimensions");
    require(image->bits_per_pixel == depth && image->bytes_per_pixel == depth / 8, "depth");
    require(image->pixels != nullptr && image->converted_palette == nullptr, "pixel-ownership");
    require(N == static_cast<std::size_t>(width * height * (depth / 8)), "literal-output-extent");
    for (std::size_t i = 0; i < N; ++i)
    {
        equal_byte(image->pixels[i], pixels[i], "pixels", i);
    }
    pixel_bytes += N;

    if (palette_entries == 0)
    {
        require(image->palette == nullptr, "no-palette");
    }
    else
    {
        require(image->palette != nullptr, "palette-owner");
        constexpr unsigned char triples[][3] = {{0, 1, 254}, {1, 254, 0}, {254, 0, 1}};
        for (unsigned entry = 0; entry < 256; ++entry)
        {
            for (unsigned channel = 0; channel < 3; ++channel)
            {
                const unsigned expected = entry < static_cast<unsigned>(palette_entries)
                                              ? triples[entry % 3][channel]
                                              : 0;
                equal_byte(
                    image->palette[entry * 3 + channel], expected, "palette", entry * 3 + channel);
            }
        }
        palette_bytes += 768;
    }
    // The genuine depth-8 constructor initializes this member. Depth-24 leaves
    // it untouched, so no uninitialized or ABI-padding bytes are inspected.
    if (depth == 8)
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            equal_byte(image->unknown_1c[i], 0, "depth-eight-opaque", i);
        }
    }

    image.reset(); // Actual TgaImage destructor and scalar/array CRT ownership.
    ++destroyed;
    file_unchanged(path, bytes);
}

void raw_group(const GeneratedFiles& files)
{
    FileBytes file;
    Directory directory(file);
    dimensions(directory, 2, 2, 8, 1, 4);
    strips(directory, 2, 1, 320, 336);
    directory.add(270, 2, 6, 384); // Exercise description position save/restore.
    directory.finish();
    constexpr unsigned char description[] = {'h', 'e', 'l', 'l', 'o', 0};
    payload(file, 384, description);
    dword(file, 320, 512);
    dword(file, 324, 544);
    dword(file, 336, 8);
    dword(file, 340, 8);
    constexpr unsigned char row0[] = {0x10, 0x20, 0x30, 0xaa, 0x40, 0x50, 0x60, 0xbb};
    constexpr unsigned char row1[] = {0x70, 0x80, 0x90, 0xcc, 0xa0, 0xb0, 0xc0, 0xdd};
    constexpr unsigned char expected[] = {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0};
    payload(file, 512, row0);
    payload(file, 544, row1);
    load_case(files, "raw-rgba", file, 2, 2, 24, expected, 0);

    current_case = "unbound";
    const std::string existing = (files.root / "Data" / "Fonts" / "raw-rgba").string();
    ++calls;
    std::unique_ptr<TgaImage> unbound(LoadTiffFile(existing.c_str()));
    require(unbound == nullptr, "no-host-path-fallback");

    current_case = "missing";
    auto source = std::make_shared<data::FolderSource>(
        files.root, std::vector<data::SourceEntry>{{"Data/Fonts/raw-rgba", file.size()}});
    retail::Session session(source);
    const std::string missing = "DATA\\FONTS\\not-created";
    require(
        !fs::exists(files.root / "Data" / "Fonts" / "not-created"), "missing-path-precondition");
    ++calls;
    std::unique_ptr<TgaImage> absent(LoadTiffFile(missing.c_str()));
    require(absent == nullptr, "missing-path-result");
}

void tile_group(const GeneratedFiles& files)
{
    FileBytes file;
    Directory directory(file);
    dimensions(directory, 5, 3, 8, 1, 1);
    directory.add(322, 4, 1, 2);
    directory.add(323, 4, 1, 2);
    directory.add(324, 4, 2, 320);
    directory.add(325, 4, 2, 336);
    directory.finish();
    dword(file, 320, 512);
    dword(file, 324, 544);
    // These byte counts are deliberately not the tile footprint. The actual
    // tile reader uses geometry and ignores its separately allocated counts.
    dword(file, 336, 1);
    dword(file, 340, 99);
    constexpr unsigned char first[] = {1, 2, 3, 4};
    constexpr unsigned char second[] = {5, 6, 7, 8};
    constexpr unsigned char expected[] = {1, 2, 5, 6, 0, 3, 4, 7, 8, 0, 0, 0, 0, 0, 0};
    payload(file, 512, first);
    payload(file, 544, second);
    load_case(files, "floor-tiles", file, 5, 3, 8, expected, 0);
}

void palette_group(const GeneratedFiles& files)
{
    {
        FileBytes file;
        Directory directory(file);
        dimensions(directory, 3, 3, 8, 0x8005, 1);
        strips(directory, 2, 2, 320, 336);
        directory.add(320, 3, 768, 1024);
        directory.finish();
        dword(file, 320, 512);
        dword(file, 324, 544);
        dword(file, 336, 7);
        dword(file, 340, 4);
        constexpr unsigned char first[] = {0x80, 2, 0x12, 0x34, 0x56, 0xfe, 0x78};
        constexpr unsigned char second[] = {2, 0x9a, 0xbc, 0xde};
        constexpr unsigned char expected[] = {0x12, 0x34, 0x56, 0x78, 0x78, 0x78, 0x9a, 0xbc, 0xde};
        payload(file, 512, first);
        payload(file, 544, second);
        palette(file);
        load_case(files, "packbits-eight", file, 3, 3, 8, expected, 256);
    }
    {
        FileBytes file;
        Directory directory(file);
        dimensions(directory, 3, 3, 4, 1, 1);
        strips(directory, 1, 3, 512, 5);
        directory.add(320, 3, 768, 1024);
        directory.finish();
        constexpr unsigned char packed[] = {0x12, 0x34, 0x56, 0x78, 0x90};
        constexpr unsigned char expected[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        payload(file, 512, packed);
        palette(file);
        load_case(files, "indexed-four", file, 3, 3, 8, expected, 8);
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        host_require(argc == 2, "usage: reconstructed_tiff_image_loader_tests FRESH_DIRECTORY");
        require(std::fegetround() == FE_TONEAREST, "round-to-nearest-domain");
        GeneratedFiles files(argv[1]);
        raw_group(files);
        tile_group(files);
        palette_group(files);
        std::printf("{\"groups\":3,\"calls\":%u,\"images\":%u,\"destroyed\":%u,"
                    "\"pixel_bytes\":%u,\"palette_bytes\":%u,\"checks\":%u}\n",
            calls,
            images,
            destroyed,
            pixel_bytes,
            palette_bytes,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr,
            "tiff-loader case=%s reason=%s index=%zu actual=%02x expected=%02x\n",
            failure.name,
            failure.reason,
            failure.index,
            failure.actual,
            failure.expected);
        return 2;
    }
    catch (const std::exception& exception)
    {
        std::fprintf(stderr, "tiff-loader host failure: %s\n", exception.what());
        return 3;
    }
}
