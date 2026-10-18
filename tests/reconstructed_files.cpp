#include "data/folder_source.h"
#include "runtime/retail_files.h"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

std::int32_t GetFileSize(const char* filename);
char* LoadFile(const char* filename);
char* LoadBleFile(const char* filename);

namespace
{
namespace data = beachlife::data;
namespace files = beachlife::runtime::retail_files;
using Bytes = std::vector<unsigned char>;
std::size_t checks = 0;

void expect(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

Bytes payload(std::size_t size)
{
    Bytes result(size);
    for (std::size_t index = 0; index < size; ++index)
    {
        result[index] = static_cast<unsigned char>(index * 73 + 127);
    }
    return result;
}

void write(const std::filesystem::path& path, const Bytes& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output)
    {
        throw std::runtime_error("Cannot write generated fixture.");
    }
}

struct Observations
{
    std::size_t opened = 0;
    std::size_t closed = 0;
    std::size_t reads = 0;
    std::size_t largest_read = 0;
    std::size_t chunk = std::numeric_limits<std::size_t>::max();
    std::uint64_t stop = std::numeric_limits<std::uint64_t>::max();
    bool fail_read = false;
    bool fail_seek = false;
    bool fail_position = false;
};

class ObservedFile final : public data::SourceFile
{
  public:
    ObservedFile(std::unique_ptr<data::SourceFile> file, std::shared_ptr<Observations> observations)
        : file_(std::move(file))
        , observations_(std::move(observations))
    {
        ++observations_->opened;
    }
    ~ObservedFile() override
    {
        ++observations_->closed;
    }
    std::uint64_t size() const override
    {
        return file_->size();
    }
    std::uint64_t tell() const override
    {
        if (sought_ && observations_->fail_position)
        {
            throw std::runtime_error("Generated position failure.");
        }
        return file_->tell();
    }
    void seek(std::uint64_t position) override
    {
        if (observations_->fail_seek)
        {
            throw std::runtime_error("Generated seek failure.");
        }
        file_->seek(position);
        sought_ = true;
    }
    std::size_t read(std::span<unsigned char> output) override
    {
        ++observations_->reads;
        observations_->largest_read = std::max(observations_->largest_read, output.size());
        const auto cursor = file_->tell();
        if (cursor >= observations_->stop)
        {
            if (observations_->fail_read)
            {
                throw std::runtime_error("Generated read failure.");
            }
            return 0;
        }
        const auto amount = static_cast<std::size_t>(std::min<std::uint64_t>(
            {output.size(), observations_->chunk, observations_->stop - cursor}));
        return file_->read(output.first(amount));
    }

  private:
    std::unique_ptr<data::SourceFile> file_;
    std::shared_ptr<Observations> observations_;
    bool sought_ = false;
};

class ObservedSource final : public data::FileSource
{
  public:
    ObservedSource(
        std::shared_ptr<const data::FileSource> source, std::shared_ptr<Observations> observations)
        : source_(std::move(source))
        , observations_(std::move(observations))
    {
    }
    std::unique_ptr<data::SourceFile> open(std::string_view name) const override
    {
        return std::make_unique<ObservedFile>(source_->open(name), observations_);
    }

  private:
    std::shared_ptr<const data::FileSource> source_;
    std::shared_ptr<Observations> observations_;
};

// Size-only fixtures qualify x86 truncation without creating or allocating
// multi-gigabyte data. No allocating retail helper sees one of these streams.
class SizeFile final : public data::SourceFile
{
  public:
    explicit SizeFile(std::uint64_t length)
        : length_(length)
    {
    }
    std::uint64_t size() const override
    {
        return length_;
    }
    std::uint64_t tell() const override
    {
        return cursor_;
    }
    void seek(std::uint64_t value) override
    {
        if (value > length_)
        {
            throw std::out_of_range("Generated size fixture seek.");
        }
        cursor_ = value;
    }
    std::size_t read(std::span<unsigned char>) override
    {
        throw std::runtime_error("A size-only fixture must never be read.");
    }

  private:
    std::uint64_t length_;
    std::uint64_t cursor_ = 0;
};

class SizeSource final : public data::FileSource
{
  public:
    explicit SizeSource(std::uint64_t size)
        : size_(size)
    {
    }
    std::unique_ptr<data::SourceFile> open(std::string_view) const override
    {
        return std::make_unique<SizeFile>(size_);
    }

  private:
    std::uint64_t size_;
};

void reconstructed_loaders(
    const std::shared_ptr<const data::FileSource>& source, const std::vector<std::size_t>& sizes)
{
    files::Session session(source);
    for (std::size_t index = 0; index < sizes.size(); ++index)
    {
        const auto name = ".\\dAtA\\FILE" + std::to_string(index) + ".BIN";
        const auto input = payload(sizes[index]);
        expect(GetFileSize(name.c_str()) == static_cast<std::int32_t>(input.size()),
            "File size or path alias differs.");
        std::unique_ptr<char[]> plain(LoadFile(name.c_str()));
        std::unique_ptr<char[]> ble(LoadBleFile(name.c_str()));
        if (input.empty())
        {
            expect(!plain && !ble, "Empty input must return null.");
            continue;
        }
        expect(plain && ble && plain.get() != ble.get(), "Loaders must return independent arrays.");
        for (std::size_t byte = 0; byte < input.size(); ++byte)
        {
            expect(static_cast<unsigned char>(plain[byte]) == input[byte], "Plain bytes differ.");
            expect(static_cast<unsigned char>(ble[byte]) == 255u - input[byte],
                "BLE inversion differs.");
        }
        expect(plain[input.size()] == '.' && ble[input.size()] == '.',
            "The retail terminator must be a period.");
    }
    for (const char* name : {"Data/missing.bin",
             "unlisted.bin",
             "../unlisted.bin",
             "/Data/file1.bin",
             "CMakeLists.txt"})
    {
        expect(GetFileSize(name) == 0 && LoadFile(name) == nullptr && LoadBleFile(name) == nullptr,
            "Missing or disallowed path escaped the VFS.");
    }
}

void facade_reads(const std::shared_ptr<const data::FileSource>& source)
{
    auto observations = std::make_shared<Observations>();
    auto observed = std::make_shared<ObservedSource>(source, observations);
    files::Session session(observed);
    expect(files::open("Data/file3.bin", "r") == nullptr &&
               files::open("Data/file3.bin", "wb") == nullptr,
        "Unsupported modes must fail.");
    auto* file = files::open("Data/file3.bin", "rb");
    expect(file != nullptr, "Cannot open facade fixture.");
    auto* independent = files::open("Data/file3.bin", "rb");
    expect(independent != nullptr, "Cannot open independent handle.");
    std::array<unsigned char, 12> bytes;
    bytes.fill(0xa5);
    observations->chunk = 2;
    expect(files::read(bytes.data(), 3, 3, file) == 3, "Partial VFS reads were not accumulated.");
    const auto input = payload(255);
    expect(std::equal(bytes.begin(), bytes.begin() + 9, input.begin()) && bytes[9] == 0xa5,
        "Bounded read bytes differ.");
    files::Position cursor = 999;
    expect(files::position(file, &cursor) == 0 && cursor == 9, "Read position differs.");
    expect(files::position(independent, &cursor) == 0 && cursor == 0,
        "Handles must have independent cursors.");
    expect(files::read(nullptr, 0, 1, file) == 0 && files::read(nullptr, 1, 0, file) == 0,
        "Zero-sized reads must do nothing.");
    expect(files::read(bytes.data(), 2, std::numeric_limits<std::size_t>::max(), file) == 0,
        "Overflowing read count was accepted.");
    expect(files::seek(file, -4, files::SeekOrigin::current) == 0 &&
               files::position(file, &cursor) == 0 && cursor == 5,
        "Relative seek differs.");
    expect(files::seek(file, -6, files::SeekOrigin::current) == -1 &&
               files::position(file, &cursor) == 0 && cursor == 5,
        "Invalid seek changed the cursor.");
    expect(
        files::seek(file, std::numeric_limits<std::int32_t>::min(), files::SeekOrigin::begin) == -1,
        "Minimum signed offset was accepted before the start.");
    expect(files::seek(file, -2, files::SeekOrigin::end) == 0, "End-relative seek failed.");
    bytes.fill(0xa5);
    expect(files::read(bytes.data(), 3, 2, file) == 0 && bytes[0] == input[253] &&
               bytes[1] == input[254] && bytes[2] == 0xa5,
        "Partial element bytes or completed-element result differ.");
    expect(files::read(bytes.data(), 1, 1, file) == 0, "EOF must return zero.");
    expect(files::seek(file, 7, files::SeekOrigin::end) == 0 &&
               files::position(file, &cursor) == 0 && cursor == 262 &&
               files::read(bytes.data(), 1, 1, file) == 0,
        "Logical seek beyond EOF differs.");
    expect(files::seek(file, -10, files::SeekOrigin::current) == 0 &&
               files::read(bytes.data(), 1, 1, file) == 1 && bytes[0] == input[252],
        "Read after seeking back from EOF differs.");
    expect(files::close(file) == 0 && files::close(independent) == 0,
        "Cannot release facade handles.");
    expect(observations->opened == observations->closed, "Facade stream leaked.");

    observations->chunk = std::numeric_limits<std::size_t>::max();
    observations->largest_read = 0;
    std::unique_ptr<char[]> large(LoadFile("Data/file7.bin"));
    expect(large && observations->largest_read <= 256 * 1024,
        "The bridge must bound individual VFS reads.");
    expect(observations->opened == observations->closed, "Loader stream leaked.");
}

void failure_paths(const std::shared_ptr<const data::FileSource>& source)
{
    for (bool fail_read : {false, true})
    {
        auto observations = std::make_shared<Observations>();
        observations->chunk = 2;
        observations->stop = 3;
        observations->fail_read = fail_read;
        files::Session session(std::make_shared<ObservedSource>(source, observations));
        auto* file = files::open("Data/file3.bin", "rb");
        expect(file != nullptr, "Cannot open generated short-read fixture.");
        std::array<unsigned char, 8> bytes;
        bytes.fill(0xa5);
        expect(files::read(bytes.data(), 2, 3, file) == 1,
            "Short/error read must return completed elements only.");
        const auto input = payload(255);
        expect(std::equal(bytes.begin(), bytes.begin() + 3, input.begin()) && bytes[3] == 0xa5,
            "Short read modified bytes beyond its known prefix.");
        expect(files::close(file) == 0, "Cannot close short-read fixture.");

        // LoadFile ignores short reads, as the original does. Only initialized
        // prefix bytes and the explicit terminator are observed here. BLE's
        // uninitialized-tail inversion is outside the safe fixture domain.
        std::unique_ptr<char[]> plain(LoadFile("Data/file3.bin"));
        expect(plain && plain[255] == '.', "Short LoadFile result/terminator differs.");
        for (std::size_t index = 0; index < 3; ++index)
        {
            expect(static_cast<unsigned char>(plain[index]) == input[index],
                "Short LoadFile prefix differs.");
        }
        expect(observations->opened == observations->closed, "Error path leaked a stream.");
    }
    for (bool fail_seek : {false, true})
    {
        auto observations = std::make_shared<Observations>();
        observations->fail_seek = fail_seek;
        observations->fail_position = !fail_seek;
        files::Session session(std::make_shared<ObservedSource>(source, observations));
        expect(GetFileSize("Data/file3.bin") == 0, "Size-query failure must return zero.");
        expect(observations->opened == 1 && observations->closed == 1,
            "Size-query failure must close its stream.");
    }
}

void size_width()
{
    struct Case
    {
        std::uint64_t size;
        std::int32_t result;
    };
    for (const Case item : {Case{0, 0},
             Case{0x7fffffff, 0x7fffffff},
             Case{0x80000000, std::numeric_limits<std::int32_t>::min()},
             Case{0xffffffff, -1},
             Case{0x100000000, 0},
             Case{0x100000001, 1},
             Case{0x180000000, std::numeric_limits<std::int32_t>::min()},
             Case{0x7fffffffffffffff, -1}})
    {
        files::Session session(std::make_shared<SizeSource>(item.size));
        expect(GetFileSize("size-only") == item.result, "Signed low32 position differs.");
    }
    files::Session session(std::make_shared<SizeSource>(std::numeric_limits<std::uint64_t>::max()));
    auto* file = files::open("overflow-only", "rb");
    expect(
        file && files::seek(file, 1, files::SeekOrigin::end) == -1, "Position overflow must fail.");
    expect(files::close(file) == 0, "Size-only handle leaked.");
}

void sessions(const std::filesystem::path& root,
    const std::shared_ptr<const data::FileSource>& first,
    const std::shared_ptr<const data::FileSource>& second)
{
    expect(files::open("Data/file1.bin", "rb") == nullptr,
        "An unbound thread must not open process files.");
    bool rejected = false;
    try
    {
        files::Session invalid(nullptr);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    expect(rejected, "Null VFS session was accepted.");
    for (int iteration = 0; iteration < 16; ++iteration)
    {
        files::Session outer(first);
        expect(GetFileSize("Data/file1.bin") == 1, "Outer session differs.");
        {
            files::Session inner(second);
            expect(GetFileSize("Data/file1.bin") == 17, "Nested session differs.");
        }
        expect(GetFileSize("Data/file1.bin") == 1, "Previous session was not restored.");
    }
    expect(files::open("Data/file1.bin", "rb") == nullptr, "Session binding leaked.");

    std::weak_ptr<const data::FileSource> lifetime;
    files::File* surviving = nullptr;
    {
        auto source = std::make_shared<data::FolderSource>(
            root, std::vector<data::SourceEntry>{{"Data/file1.bin", 1}});
        lifetime = source;
        files::Session session(source);
        source.reset();
        surviving = files::open("Data/file1.bin", "rb");
        expect(surviving != nullptr && !lifetime.expired(), "Session did not retain the VFS.");
    }
    expect(!lifetime.expired(), "Open handle did not retain its VFS.");
    unsigned char byte = 0;
    expect(files::read(&byte, 1, 1, surviving) == 1 && byte == payload(1)[0],
        "Handle cannot read after the binding ends.");
    expect(files::close(surviving) == 0 && lifetime.expired(), "Handle ownership leaked.");

    files::Session outer(first);
    auto* file = files::open("Data/file1.bin", "rb");
    expect(file != nullptr, "Cannot open thread fixture.");
    std::exception_ptr failure;
    std::thread worker(
        [&]
        {
            try
            {
                expect(files::open("Data/file1.bin", "rb") == nullptr,
                    "Thread inherited another thread's session.");
                unsigned char output = 0xa5;
                files::Position position = 99;
                expect(files::read(&output, 1, 1, file) == 0 && output == 0xa5 &&
                           files::seek(file, 0, files::SeekOrigin::begin) == -1 &&
                           files::position(file, &position) == -1 && position == 99 &&
                           files::close(file) == -1,
                    "A file handle crossed thread affinity.");
                files::Session own(second);
                expect(GetFileSize("Data/file1.bin") == 17, "Worker's own session failed.");
            }
            catch (...)
            {
                failure = std::current_exception();
            }
        });
    worker.join();
    if (failure)
    {
        std::rethrow_exception(failure);
    }
    expect(GetFileSize("Data/file1.bin") == 1 && files::read(&byte, 1, 1, file) == 1,
        "Worker disturbed the parent thread's binding or cursor.");
    expect(files::close(file) == 0, "Thread-affine handle leaked.");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 2;
    }
    try
    {
        const std::filesystem::path root(argv[1]);
        std::filesystem::create_directories(root / "Data");
        std::filesystem::create_directories(root / "other/Data");
        const std::vector<std::size_t> sizes{0, 1, 2, 255, 256, 257, 5003, 1024 * 1024 + 17};
        std::vector<data::SourceEntry> entries;
        for (std::size_t index = 0; index < sizes.size(); ++index)
        {
            const auto name = "Data/file" + std::to_string(index) + ".bin";
            write(root / name, payload(sizes[index]));
            entries.push_back({name, sizes[index]});
        }
        write(root / "unlisted.bin", payload(3));
        write(root / "other/Data/file1.bin", payload(17));
        auto first = std::make_shared<data::FolderSource>(root, entries);
        auto second = std::make_shared<data::FolderSource>(
            root / "other", std::vector<data::SourceEntry>{{"Data/file1.bin", 17}});
        reconstructed_loaders(first, sizes);
        facade_reads(first);
        failure_paths(first);
        size_width();
        sessions(root, first, second);
        std::cout << "Reconstructed file/VFS checks passed: " << checks << "\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
