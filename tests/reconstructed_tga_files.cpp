#include "data/folder_source.h"
#include "runtime/retail_files.h"
#include "tga_image.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <span>
#include <vector>

namespace
{
namespace data = beachlife::data;
namespace files = beachlife::runtime::retail_files;
using Bytes = std::vector<unsigned char>;
unsigned int cases = 0;
unsigned int calls = 0;
unsigned long long checks = 0;

[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "case %u: %s\n", cases, message);
    std::exit(2);
}

void require(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        fail(message);
    }
}

struct Allocation
{
    void* pointer = nullptr;
    std::uintptr_t address = 0;
    std::size_t bytes = 0;
    bool live = false;
};

// Only array allocation is observed. Scalar allocation remains the host's real
// operator new/delete, including all VFS bookkeeping and the TgaImage object.
// Every array obtains ordinary real storage from that same host scalar pair.
struct Ledger
{
    std::array<Allocation, 8> allocations{};
    std::array<std::size_t, 8> releases{};
    std::size_t allocated = 0;
    std::size_t released = 0;
    std::span<const unsigned char> file_bytes;
    bool input_checked = false;
};
thread_local Ledger* active = nullptr;

struct ArrayObservation
{
    explicit ArrayObservation(Ledger& ledger)
    {
        require(active == nullptr, "nested array observation");
        active = &ledger;
    }
    ~ArrayObservation()
    {
        active = nullptr;
    }
};

void* allocate_array(std::size_t bytes)
{
    void* pointer = ::operator new(bytes);
    if (active)
    {
        require(active->allocated < active->allocations.size(), "too many array allocations");
        active->allocations[active->allocated++] = {
            pointer, reinterpret_cast<std::uintptr_t>(pointer), bytes, true};
    }
    return pointer;
}

void release_array(void* pointer) noexcept
{
    if (!pointer)
    {
        return;
    }
    if (active)
    {
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        std::size_t index = 0;
        while (index < active->allocated && active->allocations[index].address != address)
        {
            ++index;
        }
        require(index != active->allocated, "array release has no allocation");
        Allocation& allocation = active->allocations[index];
        require(allocation.live, "array released twice");
        require(active->released < active->releases.size(), "too many array releases");
        if (index == 0)
        {
            require(
                allocation.bytes == active->file_bytes.size() + 1, "input array extent differs");
            const auto* input = static_cast<const unsigned char*>(pointer);
            for (std::size_t byte = 0; byte < active->file_bytes.size(); ++byte)
            {
                require(input[byte] == active->file_bytes[byte], "input file bytes changed");
            }
            require(input[active->file_bytes.size()] == '.', "input period terminator differs");
            active->input_checked = true;
        }
        active->releases[active->released++] = index;
        allocation.pointer = nullptr;
        allocation.live = false;
    }
    ::operator delete(pointer);
}

struct ImageCase
{
    const char* path;
    Bytes file;
    int width;
    int height;
    int depth;
    Bytes pixels;
    Bytes palette;
    std::array<std::size_t, 4> array_sizes;
    std::size_t array_count;
    std::array<std::size_t, 4> release_order;
    std::size_t released_before_return;
};

void write(const std::filesystem::path& path, const Bytes& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(bool(output), "cannot write generated file");
}

void compare_bytes(std::span<const unsigned char> actual, std::span<const unsigned char> expected)
{
    require(actual.size() == expected.size(), "byte extent differs");
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        require(actual[index] == expected[index], "decoded bytes differ");
    }
}

void image_case(const ImageCase& item, const char* alias = nullptr)
{
    ++cases;
    std::array<char, 64> filename{};
    const char* spelling = alias ? alias : item.path;
    require(std::strlen(spelling) < filename.size(), "filename fixture extent differs");
    std::strcpy(filename.data(), spelling);
    const auto before = filename;
    Ledger ledger;
    ledger.file_bytes = item.file;
    ArrayObservation observation(ledger);
    ++calls;
    TgaImage* image = LoadTgaFile(filename.data());
    require(filename == before, "caller filename changed");
    require(image != nullptr, "complete generated image was rejected");
    require(ledger.allocated == item.array_count, "array allocation count differs");
    for (std::size_t index = 0; index < item.array_count; ++index)
    {
        require(ledger.allocations[index].bytes == item.array_sizes[index],
            "literal array request differs");
    }
    require(
        ledger.input_checked && !ledger.allocations[0].live, "input array survived normal success");
    require(ledger.released == item.released_before_return, "early release count differs");
    require(image->width == item.width && image->height == item.height &&
                image->bits_per_pixel == item.depth && image->bytes_per_pixel == item.depth / 8,
        "image dimensions or depth differ");
    require(image->converted_palette == nullptr, "unexpected converted palette");
    require(reinterpret_cast<std::uintptr_t>(image->pixels) != ledger.allocations[0].address,
        "returned pixels alias released input");
    compare_bytes(std::span(image->pixels, item.pixels.size()), item.pixels);
    if (item.palette.empty())
    {
        require(image->palette == nullptr, "RGB image unexpectedly owns a palette");
    }
    else
    {
        require(image->palette != nullptr, "indexed palette missing");
        compare_bytes(std::span(image->palette, item.palette.size()), item.palette);
        for (std::size_t byte = item.palette.size(); byte < 768; ++byte)
        {
            require(image->palette[byte] == 0, "unused palette tail changed");
        }
    }
    // The actual image destructor owns its independent pixel/palette arrays.
    delete image;
    require(ledger.released == item.array_count, "image cleanup left an array live");
    for (std::size_t index = 0; index < item.array_count; ++index)
    {
        require(ledger.releases[index] == item.release_order[index], "array release order differs");
        require(!ledger.allocations[index].live, "array remains live");
    }
}

void null_case(const char* filename, std::span<const unsigned char> file_bytes = {})
{
    ++cases;
    Ledger ledger;
    ledger.file_bytes = file_bytes;
    ArrayObservation observation(ledger);
    ++calls;
    require(LoadTgaFile(filename) == nullptr, "missing, empty or rejected file returned an image");
    if (file_bytes.empty())
    {
        require(ledger.allocated == 0 && ledger.released == 0,
            "missing or empty file allocated an input array");
        return;
    }
    require(ledger.allocated == 1 && ledger.allocations[0].bytes == 19,
        "complete rejected header did not allocate the literal input extent");
    const bool released_by_wrapper = !ledger.allocations[0].live;
    if (!released_by_wrapper)
    {
        // Reclaim the control's known byte array before reporting its defect.
        // This is fixture cleanup, not release credit for LoadTgaFile.
        delete[] static_cast<char*>(ledger.allocations[0].pointer);
    }
    require(released_by_wrapper, "input buffer not released on normal parser rejection");
    require(ledger.input_checked && ledger.released == 1 && ledger.releases[0] == 0,
        "normal rejection input release differs");
}
} // namespace

void* operator new[](std::size_t bytes)
{
    return allocate_array(bytes);
}
void operator delete[](void* pointer) noexcept
{
    release_array(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept
{
    release_array(pointer);
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 2;
    }
    try
    {
        // Literal headers, packets and decoded bytes are intentionally independent
        // of the parser/copy loops. All dimensions and allocation products are small.
        const ImageCase indexed{"GUI/indexed.tga",
            {2,
                1,
                1,
                0,
                0,
                3,
                0,
                24,
                0,
                0,
                0,
                0,
                2,
                0,
                2,
                0,
                8,
                0,
                0xaa,
                0x55,
                1,
                2,
                3,
                4,
                5,
                6,
                7,
                8,
                9,
                0,
                1,
                2,
                0},
            2,
            2,
            8,
            {2, 0, 0, 1},
            {3, 2, 1, 6, 5, 4, 9, 8, 7},
            {34, 4, 768, 0},
            3,
            {0, 1, 2, 0},
            1};
        const ImageCase rgb{"GUI/rgb.tga",
            {0,
                0,
                2,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                2,
                0,
                2,
                0,
                24,
                0,
                1,
                2,
                3,
                4,
                5,
                6,
                7,
                8,
                9,
                10,
                11,
                12},
            2,
            2,
            24,
            {9, 8, 7, 12, 11, 10, 3, 2, 1, 6, 5, 4},
            {},
            {31, 12, 0, 0},
            2,
            {0, 1, 0, 0},
            1};
        const ImageCase indexed_rle{"GUI/indexed-rle.tga",
            {0,
                1,
                9,
                0,
                0,
                3,
                0,
                24,
                0,
                0,
                0,
                0,
                3,
                0,
                2,
                0,
                8,
                0,
                1,
                2,
                3,
                4,
                5,
                6,
                7,
                8,
                9,
                0x82,
                1,
                2,
                0,
                2,
                1},
            3,
            2,
            8,
            {0, 2, 1, 1, 1, 1},
            {3, 2, 1, 6, 5, 4, 9, 8, 7},
            {34, 6, 768, 6},
            4,
            {3, 0, 1, 2},
            2};
        const ImageCase rgb_rle{"GUI/rgb-rle.tga",
            {0,
                0,
                10,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                2,
                0,
                2,
                0,
                24,
                0,
                0x81,
                1,
                2,
                3,
                1,
                4,
                5,
                6,
                7,
                8,
                9},
            2,
            2,
            24,
            {6, 5, 4, 9, 8, 7, 3, 2, 1, 3, 2, 1},
            {},
            {30, 12, 12, 0},
            3,
            {2, 0, 1, 0},
            2};
        const Bytes rejected{0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 16, 0};
        const std::filesystem::path root(argv[1]);
        std::filesystem::create_directories(root / "GUI");
        std::vector<data::SourceEntry> entries;
        for (const ImageCase* item : {&indexed, &rgb, &indexed_rle, &rgb_rle})
        {
            write(root / item->path, item->file);
            entries.push_back({item->path, item->file.size()});
        }
        write(root / "GUI/empty.tga", {});
        write(root / "GUI/rejected.tga", rejected);
        entries.push_back({"GUI/empty.tga", 0});
        entries.push_back({"GUI/rejected.tga", rejected.size()});
        auto source = std::make_shared<data::FolderSource>(root, entries);
        null_case("GUI/rgb.tga"); // An unbound thread cannot use process files.
        {
            files::Session session(source);
            null_case("GUI/missing.tga");
            null_case("GUI/empty.tga");
            null_case("GUI/rejected.tga", rejected);
            image_case(indexed);
            image_case(indexed, ".\\gUi\\InDeXeD.tGa");
            image_case(rgb);
            image_case(indexed_rle);
            image_case(rgb_rle);
        }
        require(active == nullptr, "array observation remained bound");
        require(cases == 9 && calls == 9, "literal case count differs");
        std::printf("Reconstructed TGA/VFS checks passed: %u cases, %u calls, %llu checks\n",
            cases,
            calls,
            checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "generated TGA/VFS fixture: %s\n", error.what());
        return 2;
    }
}
