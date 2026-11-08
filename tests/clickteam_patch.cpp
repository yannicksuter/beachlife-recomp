#include "data/clickteam_patch.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace beachlife::data;
using Bytes = std::vector<unsigned char>;

namespace
{
void expect(bool value, const char* what)
{
    if (!value)
    {
        throw std::runtime_error(std::string("Clickteam patch check failed: ") + what);
    }
}

template <class F> bool throws(F fn)
{
    try
    {
        fn();
    }
    catch (const std::exception&)
    {
        return true;
    }
    return false;
}

Bytes bytes(std::string_view text)
{
    return {text.begin(), text.end()};
}

void put16(Bytes& out, std::uint32_t value)
{
    out.push_back(static_cast<unsigned char>(value));
    out.push_back(static_cast<unsigned char>(value >> 8));
}

void put32(Bytes& out, std::uint32_t value)
{
    put16(out, value & 0xffff);
    put16(out, value >> 16);
}

void set32(Bytes& out, std::size_t at, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
    {
        out[at + i] = static_cast<unsigned char>(value >> (8 * i));
    }
}

// One final stored block: type 7 and the final bit, then a 16-bit length.
Bytes stored(const Bytes& data)
{
    Bytes out{0x0f};
    put16(out, static_cast<std::uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

Bytes entry(std::string_view name,
    bool added,
    const Bytes& result,
    std::uint32_t original_size,
    std::uint32_t offset,
    std::uint32_t length)
{
    Bytes out(0x36, 0);
    out[8] = added ? 2 : 0;
    set32(out, 0x0a, static_cast<std::uint32_t>(result.size()));
    set32(out, 0x0e, clickteam_checksum(result));
    out.insert(out.end(), name.begin(), name.end());
    out.push_back(0);
    const auto table = out.size();
    out[0x12] = 1;
    out[0x16] = static_cast<unsigned char>(table);
    put32(out, 0);
    put32(out, original_size);
    put32(out, offset);
    put32(out, length);
    set32(out, 0, static_cast<std::uint32_t>(out.size()));
    return out;
}

class BitWriter
{
  public:
    void bits(std::uint32_t value, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            bit((value >> i) & 1);
        }
    }
    // Huffman codes are sent from their most significant bit.
    void code(std::uint32_t value, int count)
    {
        for (int i = count - 1; i >= 0; --i)
        {
            bit((value >> i) & 1);
        }
    }
    Bytes out;

  private:
    void bit(unsigned value)
    {
        if (used_ % 8 == 0)
        {
            out.push_back(0);
        }
        out.back() |= static_cast<unsigned char>(value << (used_++ % 8));
    }
    int used_ = 0;
};
} // namespace

int main()
{
    // Words are little-endian; each step adds the value to the rotated sum.
    expect(clickteam_checksum(Bytes{1, 0, 0, 0, 2}) == 4, "checksum tail bytes");
    expect(clickteam_checksum(Bytes{0, 0, 0, 0x80, 0}) == 1, "checksum rotation");

    // Fixed Huffman: a literal 'A', then length 3 at distance 1, then the end.
    BitWriter fixed;
    fixed.bits(5, 3);
    fixed.bits(1, 1);
    fixed.code(0x30 + 'A', 8);
    fixed.code(1, 7); // length code 257: 3 bytes
    fixed.code(0, 5); // distance code 0: 1 byte back
    fixed.code(0, 7); // end of block
    expect(clickteam_inflate(fixed.out, 4) == bytes("AAAA"), "fixed block");
    expect(throws(
               [&]
               {
                   clickteam_inflate(fixed.out, 5);
               }),
        "inflated size");

    const auto original = bytes("ABCDEFGHIJKLMNOP");
    const auto result = bytes(std::string("ABCDxyzEFGHIJKLMNOP!AB\0\0\0FG", 27));
    // Runs: copy 4 at +0; literals "xyz"; copy 12 at +0; literal "!" with a
    // copy of 2 at -16 (6-bit offset); copy 3 at +0x20 (15-bit offset) past
    // the original, which reads zeros; copy 2 at an exact 32-bit offset of -32.
    const Bytes runs{0x03,
        0x80,
        0x82,
        0x0b,
        0x80,
        0xa0,
        0xb0,
        0x02,
        0x00,
        0x00,
        0x01,
        0xd0,
        0x00,
        0x00,
        0xff,
        0xff,
        0xff,
        0xe0};
    const auto literals = stored(bytes("xyz!"));
    Bytes data{0x00};
    put16(data, static_cast<std::uint32_t>(runs.size()));
    data.insert(data.end(), runs.begin(), runs.end());
    data.push_back(0x01); // compressed, 16-bit sizes
    put16(data, 4);
    put16(data, static_cast<std::uint32_t>(literals.size()));
    data.insert(data.end(), literals.begin(), literals.end());
    const auto patch_length = static_cast<std::uint32_t>(data.size());
    const auto added = bytes("a new file");
    const auto added_data = stored(added);
    data.insert(data.end(), added_data.begin(), added_data.end());

    Bytes list;
    put32(list, 2);
    const auto changed = entry("Data\\text_uk.ble", false, result, 16, 0, patch_length);
    const auto created = entry("Data\\new.ble",
        true,
        added,
        0,
        patch_length,
        static_cast<std::uint32_t>(added_data.size()));
    list.insert(list.end(), changed.begin(), changed.end());
    list.insert(list.end(), created.begin(), created.end());
    Bytes packed_list;
    put32(packed_list, static_cast<std::uint32_t>(list.size()));
    const auto list_stream = stored(list);
    packed_list.insert(packed_list.end(), list_stream.begin(), list_stream.end());

    Bytes image(0x100, 0);
    image[0] = 'M';
    image[1] = 'Z';
    const unsigned char marker[] = {0x77, 0x77, 0x67, 0x54, 0x29, 0x48};
    std::copy(std::begin(marker), std::end(marker), image.begin() + 0x40);
    set32(image, 0x46, 0x100);
    put16(image, 0x1234); // an unrelated stored tag
    put16(image, 0);
    put32(image, 2);
    put16(image, 0);
    put16(image, 0x1242);
    put16(image, 1);
    put32(image, static_cast<std::uint32_t>(packed_list.size()));
    image.insert(image.end(), packed_list.begin(), packed_list.end());
    put16(image, 0x7f7f);
    put16(image, 0);
    put32(image, static_cast<std::uint32_t>(data.size()));
    put32(image, static_cast<std::uint32_t>(data.size()));
    image.insert(image.end(), data.begin(), data.end());

    const auto path = std::filesystem::temp_directory_path() /
                      ("beachlife-clickteam-" + std::to_string(std::rand()) + ".exe");
    const auto write = [&](const Bytes& contents)
    {
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(contents.data()),
                static_cast<std::streamsize>(contents.size()));
    };
    write(image);
    const ClickteamPatch patch(path);
    expect(patch.entries().size() == 2 && patch.entries()[0].path == "Data\\text_uk.ble" &&
               !patch.entries()[0].added && patch.entries()[1].added && patch.sha256().size() == 64,
        "entries");
    const auto& text = patch.entries()[0];
    expect(patch.apply(text, original) == result, "patched file");
    expect(patch.apply(text, result) == result, "already patched file");
    expect(!patch.apply(text, bytes("other")), "unknown original");
    auto damaged = original;
    damaged[1] = 'b';
    expect(throws(
               [&]
               {
                   patch.apply(text, damaged);
               }),
        "result checksum");
    expect(patch.added(patch.entries()[1]) == added, "added file");

    image[image.size() - 3] ^= 0xff; // inside the added file's stored data
    write(image);
    const ClickteamPatch corrupt(path);
    expect(throws(
               [&]
               {
                   corrupt.added(corrupt.entries()[1]);
               }),
        "added checksum");
    write(bytes("MZ" + std::string(100, '.')));
    expect(throws(
               [&]
               {
                   ClickteamPatch none(path);
               }),
        "not a patch");
    std::filesystem::remove(path);
    std::cout << "Clickteam patch checks passed.\n";
    return 0;
}
