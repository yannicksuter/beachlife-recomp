#include "data/source.h"
#include "data/tga.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

using namespace beachlife::data;
namespace {

using Bytes = std::vector<unsigned char>;
void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F fn, const char* message) {
    bool failed = false;
    try { fn(); } catch (const std::runtime_error&) { failed = true; }
    expect(failed, message);
}

class MemoryFile final : public SourceFile {
public:
    MemoryFile(const Bytes& bytes, std::size_t quantum, std::uint64_t reported_size,
               std::size_t& reads, std::size_t fail_at)
        : bytes_(bytes), quantum_(quantum), size_(reported_size), reads_(reads), fail_at_(fail_at) {}
    std::uint64_t size() const override { return size_; }
    std::uint64_t tell() const override { return position_; }
    void seek(std::uint64_t position) override {
        if (position > bytes_.size()) throw std::runtime_error("Bad test seek.");
        position_ = static_cast<std::size_t>(position);
    }
    std::size_t read(std::span<unsigned char> destination) override {
        if (++reads_ == fail_at_) throw std::runtime_error("Test source read cancelled.");
        expect(destination.size() <= 16 * 1024, "TGA read must remain bounded.");
        const auto count = std::min({destination.size(), quantum_, bytes_.size() - position_});
        std::copy_n(std::span(bytes_).subspan(position_).begin(), count, destination.begin());
        position_ += count;
        return count;
    }
private:
    const Bytes& bytes_;
    std::size_t quantum_, position_ = 0;
    std::uint64_t size_;
    std::size_t& reads_;
    std::size_t fail_at_;
};

class MemorySource final : public FileSource {
public:
    explicit MemorySource(Bytes bytes) : bytes(std::move(bytes)), reported_size(this->bytes.size()) {}
    std::unique_ptr<SourceFile> open(std::string_view path) const override {
        expect(path == "Data/Screens/test.tga", "Decoder must use the requested VFS path.");
        return std::make_unique<MemoryFile>(bytes, quantum, reported_size, reads, fail_at);
    }
    Bytes bytes;
    std::size_t quantum = std::numeric_limits<std::size_t>::max();
    std::uint64_t reported_size;
    mutable std::size_t reads = 0;
    std::size_t fail_at = 0;
};

TgaImage decode(const MemorySource& source) { return read_tga(source, "Data/Screens/test.tga"); }
TgaImage decode(Bytes bytes) { return decode(MemorySource(std::move(bytes))); }

void word(Bytes& bytes, std::size_t offset, unsigned value) {
    bytes[offset] = static_cast<unsigned char>(value);
    bytes[offset + 1] = static_cast<unsigned char>(value >> 8);
}
Bytes header(unsigned type, unsigned width, unsigned height, unsigned depth, unsigned descriptor) {
    Bytes bytes(18);
    bytes[2] = static_cast<unsigned char>(type);
    word(bytes, 12, width); word(bytes, 14, height);
    bytes[16] = static_cast<unsigned char>(depth);
    bytes[17] = static_cast<unsigned char>(descriptor);
    return bytes;
}
void color(Bytes& bytes, std::uint32_t pixel, unsigned depth = 24) {
    bytes.push_back(static_cast<unsigned char>(pixel >> 16));
    bytes.push_back(static_cast<unsigned char>(pixel >> 8));
    bytes.push_back(static_cast<unsigned char>(pixel));
    if (depth == 32) bytes.push_back(static_cast<unsigned char>(pixel >> 24));
}
void palette(Bytes& bytes, unsigned first, unsigned length, unsigned depth) {
    bytes[1] = 1;
    word(bytes, 3, first); word(bytes, 5, length);
    bytes[7] = static_cast<unsigned char>(depth);
}
void every_truncation(const Bytes& bytes) {
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        auto prefix = bytes;
        prefix.resize(size);
        rejects([&] { decode(prefix); }, "A truncated TGA must fail.");
    }
}

constexpr std::uint32_t red = 0xff0000ff, green = 0xff00ff00, blue = 0xffff0000, white = 0xffffffff;

void orientations() {
    const std::array<std::array<std::uint32_t, 4>, 4> streams{{
        {blue, white, red, green}, {white, blue, green, red},
        {red, green, blue, white}, {green, red, white, blue}}};
    for (unsigned origin = 0; origin < streams.size(); ++origin) {
        auto bytes = header(2, 2, 2, 24, origin << 4);
        for (auto pixel : streams[origin]) color(bytes, pixel);
        const auto image = decode(bytes);
        expect(image.width == 2 && image.height == 2, "Image dimensions differ.");
        expect(image.pixels == std::vector<std::uint32_t>{red, green, blue, white},
               "A TGA origin was not normalized to top-left.");
        every_truncation(bytes);
    }
}

void indexed_images() {
    auto bytes = header(9, 3, 2, 8, 0);
    palette(bytes, 5, 2, 24);
    color(bytes, red); color(bytes, green);
    // Raw two-pixel packet, followed by a four-pixel repeated packet crossing a row.
    bytes.insert(bytes.end(), {1, 5, 6, 0x83, 6});
    expect(decode(bytes).pixels == std::vector<std::uint32_t>{green, green, green, red, green, green},
           "Indexed RLE or palette origin differs.");
    every_truncation(bytes);
    auto bad = bytes; bad.at(bad.size() - 1) = 4;
    rejects([&] { decode(bad); }, "Palette underflow must fail.");
    bad.at(bad.size() - 1) = 7;
    rejects([&] { decode(bad); }, "Palette overflow must fail.");
    bad = bytes; bad[bytes.size() - 2] = 0x84;
    rejects([&] { decode(bad); }, "RLE output overflow must fail.");

    bytes = header(1, 2, 1, 16, 0x28);
    palette(bytes, 300, 2, 32);
    color(bytes, 0x800000ff, 32); color(bytes, 0x4000ff00, 32);
    bytes.insert(bytes.end(), {0x2d, 1, 0x2c, 1});
    expect(decode(bytes).pixels == std::vector<std::uint32_t>{0x4000ff00, 0x800000ff},
           "16-bit palette indices or declared alpha differ.");
    every_truncation(bytes);

    bytes = header(1, 1, 1, 8, 0x20);
    palette(bytes, 0, 1, 32); color(bytes, 0x000000ff, 32); bytes.push_back(0);
    expect(decode(bytes).pixels[0] == red, "Undeclared palette alpha must be opaque.");
}

void truecolor_and_streams() {
    auto bytes = header(10, 5, 1, 32, 0x28);
    bytes.push_back(1); color(bytes, 0x800000ff, 32); color(bytes, 0x4000ff00, 32);
    bytes.push_back(0x82); color(bytes, 0xffff0000, 32);
    const auto expected = std::vector<std::uint32_t>{0x800000ff, 0x4000ff00, blue, blue, blue};
    expect(decode(bytes).pixels == expected, "Truecolor RLE or alpha differs.");
    every_truncation(bytes);
    MemorySource short_reads(bytes); short_reads.quantum = 1;
    expect(decode(short_reads).pixels == expected && short_reads.reads == bytes.size(),
           "Short VFS reads must be handled.");
    MemorySource interrupted(bytes); interrupted.quantum = 1; interrupted.fail_at = 20;
    bool propagated = false;
    try { decode(interrupted); }
    catch (const std::runtime_error& error) { propagated = std::string(error.what()) == "Test source read cancelled."; }
    expect(propagated, "The decoder must propagate VFS cancellation/read errors.");
    MemorySource stale(bytes); stale.reported_size += 100; stale.bytes.resize(stale.bytes.size() - 1);
    rejects([&] { decode(stale); }, "Premature VFS EOF must fail.");

    bytes = header(2, 1, 1, 32, 0x20);
    bytes[0] = 3; bytes.insert(bytes.end(), {'i', 'd', '!'});
    word(bytes, 8, 15); word(bytes, 10, 23);
    color(bytes, 0x000000ff, 32);
    every_truncation(bytes);
    bytes.insert(bytes.end(), {'t', 'a', 'i', 'l'});
    expect(decode(bytes).pixels == std::vector<std::uint32_t>{red},
           "ID, canvas coordinates, trailing metadata or opaque alpha differs.");

    bytes = header(10, 128, 1, 24, 0x20); bytes.push_back(0xff); color(bytes, red);
    expect(decode(bytes).pixels == std::vector<std::uint32_t>(128, red), "Maximum RLE packet differs.");
    bytes[12] = 127;
    rejects([&] { decode(bytes); }, "A 128-pixel RLE packet must not overrun a shorter image.");

    bytes = header(2, 128, 128, 24, 0x20);
    for (unsigned i = 0; i < 128 * 128; ++i) color(bytes, green);
    MemorySource buffered(bytes);
    expect(decode(buffered).pixels == std::vector<std::uint32_t>(128 * 128, green) && buffered.reads > 1,
           "Large reads must use multiple bounded VFS requests.");
}

void unsupported_and_limits() {
    const auto valid = [] { auto bytes = header(2, 1, 1, 24, 0x20); color(bytes, red); return bytes; }();
    auto bad = valid;
    for (unsigned type : {0u, 3u, 11u, 32u}) {
        bad = valid; bad[2] = static_cast<unsigned char>(type);
        rejects([&] { decode(bad); }, "Unsupported image types must fail.");
    }
    for (unsigned depth : {0u, 8u, 15u, 16u, 31u}) {
        bad = valid; bad[16] = static_cast<unsigned char>(depth);
        rejects([&] { decode(bad); }, "Unsupported color depths must fail.");
    }
    for (unsigned descriptor : {0x21u, 0x28u, 0x60u, 0xa0u, 0xe0u}) {
        bad = valid; bad[17] = static_cast<unsigned char>(descriptor);
        rejects([&] { decode(bad); }, "Unsupported alpha/interleaving must fail.");
    }
    for (unsigned offset : {12u, 14u}) {
        bad = valid; word(bad, offset, 0);
        rejects([&] { decode(bad); }, "Zero image dimensions must fail.");
    }
    bad = valid; word(bad, 12, 65535); word(bad, 14, 65535);
    rejects([&] { decode(bad); }, "Excessive pixel allocations must fail.");
    MemorySource oversized(valid); oversized.reported_size = 128u * 1024u * 1024u + 1;
    rejects([&] { decode(oversized); }, "Excessive input sizes must fail.");
    expect(oversized.reads == 0, "Excessive input size must fail before reading.");
    bad = valid; bad[1] = 1;
    rejects([&] { decode(bad); }, "Inconsistent palette/image types must fail.");
    bad = valid; bad[5] = 1;
    rejects([&] { decode(bad); }, "Unexpected color-map metadata must fail.");

    bad = header(1, 1, 1, 8, 0x20); palette(bad, 255, 2, 24);
    rejects([&] { decode(bad); }, "Palette must fit the index range.");
    bad = header(1, 1, 1, 16, 0x20); palette(bad, 65535, 2, 24);
    rejects([&] { decode(bad); }, "Palette arithmetic must not wrap.");
    bad = header(1, 1, 1, 8, 0x20); palette(bad, 0, 0, 24);
    rejects([&] { decode(bad); }, "An indexed image must have a palette.");
    bad = header(1, 1, 1, 8, 0x20); palette(bad, 0, 1, 16);
    rejects([&] { decode(bad); }, "Unsupported palette color depth must fail.");
    bad = header(1, 1, 1, 24, 0x20); palette(bad, 0, 1, 24);
    rejects([&] { decode(bad); }, "Unsupported palette index depth must fail.");
}

} // namespace

int main() {
    orientations(); indexed_images(); truecolor_and_streams(); unsupported_and_limits();
    std::cout << "TGA VFS decoding, palette/truecolor RLE, origins, alpha, bounded reads and rejection checks passed.\n";
}
