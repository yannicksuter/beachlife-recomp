#include "runtime/resource_files.h"
#include "data/folder_source.h"
#include "runtime/retail_files.h"

#include <array>
#include <bit>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;
namespace data = beachlife::data;
namespace resource = beachlife::runtime::resource_files;
namespace retail = beachlife::runtime::retail_files;
static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && sizeof(unsigned int) == 4);
static_assert(sizeof(std::uint64_t) == 8);

struct Failure
{
    const char* message;
};

unsigned checks = 0;

void check(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw Failure{message};
    }
}

void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

constexpr std::array<unsigned char, 11> metadata{10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
constexpr std::array<unsigned char, 4> palette{200, 201, 202, 203};
constexpr std::array<unsigned char, 12> raw{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr std::array<unsigned char, 4> failure_input{0xab, 0xcd, 0x12, 0x34};

class Generated
{
  public:
    explicit Generated(fs::path root)
        : root_(std::move(root))
    {
        require(!fs::exists(root_), "generated resource directory already exists");
        fs::create_directories(root_ / "Data/Sprites");
        write("Data/Sprites/BLSprite.hdr", metadata);
        write("Data/Sprites/PaletteModifiers.bin", palette);
        write("Data/Sprites/BLSprite.Dat", raw);
        write("Data/Sprites/ReadFailure.bin", failure_input);
    }
    ~Generated()
    {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }
    std::shared_ptr<const data::FileSource> mount() const
    {
        return std::make_shared<data::FolderSource>(root_, entries_);
    }
    void unchanged() const
    {
        equal("Data/Sprites/BLSprite.hdr", metadata);
        equal("Data/Sprites/PaletteModifiers.bin", palette);
        equal("Data/Sprites/BLSprite.Dat", raw);
        equal("Data/Sprites/ReadFailure.bin", failure_input);
    }
    void truncate_failure_input() const
    {
        // Real FolderSource/LocalFile detects the truncated file on its next
        // read. No generated replacement SourceFile or read body is involved.
        fs::resize_file(root_ / "Data/Sprites/ReadFailure.bin", 0);
    }
    void restore_failure_input() const
    {
        std::ofstream output(root_ / "Data/Sprites/ReadFailure.bin", std::ios::binary);
        output.write(reinterpret_cast<const char*>(failure_input.data()),
            static_cast<std::streamsize>(failure_input.size()));
        output.close();
        require(bool(output), "cannot restore generated failure input");
    }

  private:
    void write(const char* name, std::span<const unsigned char> bytes)
    {
        std::ofstream output(root_ / name, std::ios::binary);
        require(bool(output), "cannot create generated resource file");
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        output.close();
        require(bool(output), "cannot complete generated resource file");
        entries_.push_back({name, bytes.size()});
    }
    void equal(const char* name, std::span<const unsigned char> expected) const
    {
        check(fs::file_size(root_ / name) == expected.size(), "generated file size changed");
        std::vector<unsigned char> actual(expected.size());
        std::ifstream input(root_ / name, std::ios::binary);
        require(bool(input), "cannot verify generated resource file");
        input.read(
            reinterpret_cast<char*>(actual.data()), static_cast<std::streamsize>(actual.size()));
        require(input.gcount() == static_cast<std::streamsize>(actual.size()),
            "generated verification read failed");
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            check(actual[i] == expected[i], "generated file content changed");
        }
    }
    fs::path root_;
    std::vector<data::SourceEntry> entries_;
};

void metadata_close(const Generated& files)
{
    resource::Session session(files.mount());
    resource::Stream* stream = resource::open_stream("data\\sprites\\blsprite.HDR", "rb");
    check(stream != nullptr, "mounted metadata open");
    std::array<unsigned char, 20> bytes;
    bytes.fill(0x7d);
    check(resource::read_stream(bytes.data() + 3, 2, 2, stream) == 2, "two complete words");
    check(resource::read_stream(bytes.data() + 7, 3, 3, stream) == 2,
        "partial final element retains bytes");
    constexpr std::array<unsigned char, 20> expected{0x7d,
        0x7d,
        0x7d,
        10,
        11,
        12,
        13,
        14,
        15,
        16,
        17,
        18,
        19,
        20,
        0x7d,
        0x7d,
        0x7d,
        0x7d,
        0x7d,
        0x7d};
    check(bytes == expected, "metadata values and outside guards");
    check(resource::close_stream(stream) == 0, "first metadata close");

    resource::Stream* colours = resource::open_stream("DATA/SPRITES/PALETTEMODIFIERS.BIN", "rb");
    check(colours && colours != stream, "new open retains a distinct live token");
    check(resource::close_stream(stream) == -1, "closed metadata remains closed");
    check(resource::read_stream(bytes.data(), 1, 1, stream) == 0, "closed metadata read");
    check(bytes == expected, "closed read leaves all guards and bytes");
    std::array<unsigned char, 6> palette_bytes{0x91, 0, 0, 0, 0, 0x19};
    check(resource::read_stream(palette_bytes.data() + 1, 1, 4, colours) == 4,
        "retained metadata close does not close the new palette");
    check(palette_bytes == std::array<unsigned char, 6>{0x91, 200, 201, 202, 203, 0x19},
        "palette bytes and guards");
    check(resource::read_stream(nullptr, 0, 2, colours) == 0, "zero-size read");
    check(resource::close_stream(colours) == 0, "palette close");
    check(resource::open_stream("missing.bin", "rb") == nullptr, "missing mounted path");
    check(resource::open_stream("Data/Sprites/BLSprite.hdr", "wb") == nullptr,
        "read-only mode contract");
}

void raw_cursor(const Generated& files)
{
    resource::Session session(files.mount(), 0);
    const int descriptor = resource::open_descriptor("Data\\Sprites\\BLSprite.Dat", 0x8000);
    check(descriptor == 0, "zero is a valid resource descriptor");
    std::array<unsigned char, 8> bytes;
    bytes.fill(0xa6);
    check(resource::read_descriptor(descriptor, bytes.data() + 2, 3) == 3, "raw initial read");
    check(bytes == std::array<unsigned char, 8>{0xa6, 0xa6, 0, 1, 2, 0xa6, 0xa6, 0xa6},
        "raw values and guards");
    check(resource::seek_descriptor(descriptor, 0xffffffffU, 1) == 2,
        "signed minus-one relative seek");
    check(resource::read_descriptor(descriptor, bytes.data() + 2, 2) == 2, "overlapping raw read");
    check(bytes[2] == 2 && bytes[3] == 3, "relative seek bytes");
    check(
        resource::seek_descriptor(descriptor, 0xfffffffbU, 1) == -1, "negative position rejected");
    check(resource::read_descriptor(descriptor, bytes.data() + 2, 1) == 1 && bytes[2] == 4,
        "failed seek retains cursor");
    check(resource::seek_descriptor(descriptor, 0x80000000U, 1) == -1,
        "minimum signed displacement is not an unsigned forward seek");
    check(resource::seek_descriptor(descriptor, 4, 2) == 16, "logical position beyond EOF");
    check(resource::read_descriptor(descriptor, bytes.data() + 2, 1) == 0, "beyond EOF read");
    check(resource::seek_descriptor(descriptor, 0xfffffffbU, 1) == 11,
        "relative seek uses logical position");
    check(resource::read_descriptor(descriptor, bytes.data() + 2, 2) == 1 && bytes[2] == 11,
        "short raw read");
    check(resource::read_descriptor(descriptor, nullptr, 0) == 0, "zero raw read");
    check(resource::close_descriptor(descriptor) == 0, "explicit descriptor-zero close");
    const int second = resource::open_descriptor("Data/Sprites/BLSprite.Dat", 0);
    check(second == 1, "closed descriptor is not reused in this session");
    check(resource::close_descriptor(descriptor) == -1, "closed descriptor error");
    check(resource::read_descriptor(second, bytes.data() + 2, 1) == 1 && bytes[2] == 0,
        "old close leaves new descriptor live");
    check(resource::close_descriptor(second) == 0, "second descriptor close");
    const auto saved = bytes;
    check(resource::read_descriptor(-1, bytes.data(), 1) == -1, "negative descriptor error");
    check(bytes == saved, "invalid descriptor leaves buffer");
    check(bytes == std::array<unsigned char, 8>{0xa6, 0xa6, 0, 3, 2, 0xa6, 0xa6, 0xa6},
        "raw final values and outside guards");
    check(resource::open_descriptor("missing.bin", 0) == -1, "missing raw file");
    check(resource::open_descriptor("Data/Sprites/BLSprite.Dat", 1) == -1,
        "write descriptor flags rejected");

    auto* broken_stream = resource::open_stream("Data/Sprites/ReadFailure.bin", "rb");
    const int broken_descriptor = resource::open_descriptor("Data/Sprites/ReadFailure.bin", 0);
    require(broken_stream && broken_descriptor >= 0, "generated failure file open");
    files.truncate_failure_input();
    check(resource::read_stream(bytes.data(), 1, 4, broken_stream) == 0,
        "stdio error reports no completed elements");
    check(resource::read_descriptor(broken_descriptor, bytes.data(), 4) == -1,
        "raw I/O error is distinct from EOF");
    check(bytes == saved, "read failure before first byte preserves buffer");
    check(resource::close_stream(broken_stream) == 0, "failed stream close");
    check(resource::close_descriptor(broken_descriptor) == 0, "failed descriptor close");
    files.restore_failure_input();
}

void session_lifetime(const Generated& files)
{
    // This uses the existing adapter's real public API as a coexistence witness,
    // not a duplicate TIFF test or a substitute provider.
    retail::Session retail_session(files.mount());
    retail::File* retail_stream = retail::open("Data/Sprites/BLSprite.hdr", "rb");
    require(retail_stream != nullptr, "existing retail adapter open failed");
    struct RetailClose
    {
        void operator()(retail::File* stream) const
        {
            retail::close(stream);
        }
    };
    std::unique_ptr<retail::File, RetailClose> retail_owner(retail_stream);
    std::weak_ptr<const data::FileSource> resource_mount;
    {
        auto source = files.mount();
        resource_mount = source;
        resource::Session outer(source);
        source.reset();
        check(!resource_mount.expired(), "session retains the real mount");
        resource::Stream* retained = resource::open_stream("Data/Sprites/BLSprite.hdr", "rb");
        check(retained != nullptr, "outer stream");
        check(resource::open_descriptor("Data/Sprites/BLSprite.Dat", 0) == 3,
            "ordinary resource IDs reserve the stdio range");
        unsigned char byte = 0x75;
        {
            resource::Session inner(files.mount());
            check(resource::read_stream(&byte, 1, 1, retained) == 0 && byte == 0x75,
                "nested session rejects an outer token without reading it");
            check(
                resource::close_stream(retained) == -1, "nested close cannot release outer token");
            check(resource::open_stream("Data/Sprites/PaletteModifiers.bin", "rb") != nullptr,
                "inner outstanding stream");
        }
        check(resource::read_stream(&byte, 1, 1, retained) == 1 && byte == 10,
            "outer binding restored");
        // Leave the real stream and raw descriptor open. The Session is their
        // platform owner; no token is read after that owner ends.
    }
    check(resource_mount.expired(), "session releases its mount and stream owners");
    unsigned char byte = 0;
    check(retail::read(&byte, 1, 1, retail_owner.get()) == 1 && byte == 10,
        "resource sessions do not replace the existing retail binding");
    check(resource::open_stream("Data/Sprites/BLSprite.hdr", "rb") == nullptr,
        "no resource session has no fallback");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "provide one fresh generated-file directory");
        check(resource::open_stream("Data/Sprites/BLSprite.hdr", "rb") == nullptr,
            "no process-directory fallback");
        Generated files(argv[1]);
        metadata_close(files);
        raw_cursor(files);
        session_lifetime(files);
        files.unchanged();
        std::printf("resource-files: groups=3 checks=%u\n", checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "resource-files: %s\n", failure.message);
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "resource-files setup: %s\n", error.what());
        return 1;
    }
}
