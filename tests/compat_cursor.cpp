#include "platform/compat/cursor_provider.h"
#include "platform/compat/cursor_resource.h"
#include "platform/compat/user32_cursor.h"
#include "runtime/retail_cursor.h"
#include "runtime/retail_files.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
using Bytes = std::vector<unsigned char>;
using beachlife::compat::CursorImage;
using beachlife::compat::decode_cursor_resource;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void put16(Bytes& bytes, std::size_t at, unsigned value)
{
    bytes.at(at) = value;
    bytes.at(at + 1) = value >> 8;
}

void put32(Bytes& bytes, std::size_t at, std::uint32_t value)
{
    put16(bytes, at, value);
    put16(bytes, at + 2, value >> 16);
}

struct Frame
{
    unsigned id, width, height, depth;
    Bytes data;
};

// A 3x2 image also exercises packed pixels and DWORD scanline padding. Colour
// rows are red/green/transparent and blue/white/black; mono rows are
// white/black/transparent and black/white/black. The hotspot is (1,1).
Frame dib(unsigned id,
    unsigned depth,
    unsigned width = 3,
    unsigned height = 2,
    bool top_down = false,
    bool alpha = false,
    bool bitfields = false)
{
    const unsigned palette_size = depth <= 8 ? (1U << depth) * 4 : 0;
    const unsigned masks_size = bitfields ? 12 : 0;
    const unsigned xor_at = 4 + 40 + masks_size + palette_size;
    const unsigned xor_stride = (width * depth + 31) / 32 * 4;
    const unsigned and_stride = (width + 31) / 32 * 4;
    const unsigned and_at = xor_at + xor_stride * height;
    Frame result{id, width, height, depth, Bytes(and_at + and_stride * height)};
    auto& bytes = result.data;
    put16(bytes, 0, 1);
    put16(bytes, 2, 1);
    put32(bytes, 4, 40);
    put32(bytes, 8, width);
    put32(bytes, 12, top_down ? -std::int32_t(height * 2) : height * 2);
    put16(bytes, 16, 1);
    put16(bytes, 18, depth);
    put32(bytes, 20, bitfields ? 3 : 0);
    if (bitfields)
    {
        put32(bytes, 44, 0xf800);
        put32(bytes, 48, 0x07e0);
        put32(bytes, 52, 0x001f);
    }
    const std::array<std::uint32_t, 6> rgb{0xff0000, 0x00ff00, 0, 0x0000ff, 0xffffff, 0};
    if (depth == 1)
    {
        put32(bytes, 48, 0xffffff);
    }
    else if (depth <= 8)
    {
        for (unsigned i = 0; i < rgb.size(); ++i)
        {
            put32(bytes, 44 + i * 4, rgb[i]);
        }
    }
    for (unsigned y = 0; y < height; ++y)
    {
        const unsigned row = top_down ? y : height - y - 1;
        for (unsigned x = 0; x < width; ++x)
        {
            const unsigned index = y % 2 * 3 + x % 3;
            const auto colour = rgb[index];
            const auto at = xor_at + row * xor_stride + x * depth / 8;
            if (depth == 1)
            {
                bytes[at] |= (index == 0 || index == 4 ? 0x80 : 0) >> (x % 8);
            }
            else if (depth <= 8)
            {
                bytes[at] |= index << (8 - depth - (x * depth % 8));
            }
            else if (depth == 16)
            {
                put16(bytes,
                    at,
                    ((colour >> 19) << (bitfields ? 11 : 10)) |
                        (((colour >> (bitfields ? 10 : 11)) & (bitfields ? 63 : 31)) << 5) |
                        ((colour >> 3) & 31));
            }
            else
            {
                bytes[at] = colour;
                bytes[at + 1] = colour >> 8;
                bytes[at + 2] = colour >> 16;
                if (depth == 32)
                {
                    bytes[at + 3] = alpha ? (index == 2 ? 0 : index == 0 ? 128 : 255) : 0;
                }
            }
            if (index == 2 || alpha)
            {
                // An alpha image ignores the AND mask, including opaque pixels.
                bytes[and_at + row * and_stride + x / 8] |= 0x80 >> (x % 8);
            }
        }
    }
    return result;
}

struct PeFixture
{
    static constexpr unsigned raw = 0x400, root = raw + 0x40, rva = 0x2000;
    Bytes bytes;
    std::map<unsigned, std::size_t> cursor_offsets;
};

PeFixture executable(const std::map<unsigned, std::vector<Frame>>& groups)
{
    using Resources = std::map<unsigned, std::map<unsigned, Bytes>>;
    Resources resources;
    for (const auto& [id, frames] : groups)
    {
        Bytes group(6 + frames.size() * 14);
        put16(group, 2, 2);
        put16(group, 4, frames.size());
        for (std::size_t i = 0; i < frames.size(); ++i)
        {
            const auto& entry = frames[i];
            const auto at = 6 + i * 14;
            put16(group, at, entry.width);
            put16(group, at + 2, entry.height * 2);
            put16(group, at + 4, 1);
            put16(group, at + 6, entry.depth);
            put32(group, at + 8, entry.data.size());
            put16(group, at + 12, entry.id);
            resources[1][entry.id] = entry.data;
        }
        resources[12][id] = std::move(group);
    }
    Bytes tree;
    const auto allocate = [&](std::size_t count)
    {
        const auto at = tree.size();
        tree.resize(at + count);
        return at;
    };
    const auto directory = [&](std::size_t count)
    {
        const auto at = allocate(16 + count * 8);
        put16(tree, at + 14, count);
        return at;
    };
    directory(resources.size());
    std::map<unsigned, std::size_t> offsets;
    std::size_t type_index = 0;
    for (const auto& [type, names] : resources)
    {
        const auto names_at = directory(names.size());
        put32(tree, 16 + type_index * 8, type);
        put32(tree, 20 + type_index++ * 8, 0x80000000 | names_at);
        std::size_t name_index = 0;
        for (const auto& [id, data] : names)
        {
            const auto language_at = directory(1);
            put32(tree, names_at + 16 + name_index * 8, id);
            put32(tree, names_at + 20 + name_index++ * 8, 0x80000000 | language_at);
            const auto leaf = allocate(16);
            put32(tree, language_at + 16, 0x409);
            put32(tree, language_at + 20, leaf);
            const auto payload = allocate((data.size() + 3) & ~std::size_t(3));
            put32(tree, leaf, PeFixture::rva + 0x40 + payload);
            put32(tree, leaf + 4, data.size());
            std::copy(data.begin(), data.end(), tree.begin() + payload);
            if (type == 1)
            {
                offsets[id] = PeFixture::root + payload;
            }
        }
    }
    Bytes pe(PeFixture::root + tree.size());
    put16(pe, 0, 0x5a4d);
    put32(pe, 0x3c, 0x80);
    put32(pe, 0x80, 0x4550);
    put16(pe, 0x84, 0x14c);
    put16(pe, 0x86, 1);
    put16(pe, 0x94, 224);
    put16(pe, 0x98, 0x10b);
    put32(pe, 0x98 + 56, PeFixture::rva + 0x40 + tree.size());
    put32(pe, 0x98 + 60, PeFixture::raw);
    put32(pe, 0x98 + 92, 16);
    put32(pe, 0x98 + 112, PeFixture::rva + 0x40);
    put32(pe, 0x98 + 116, tree.size());
    const unsigned section = 0x98 + 224;
    put32(pe, section + 8, 0x40 + tree.size());
    put32(pe, section + 12, PeFixture::rva);
    put32(pe, section + 16, 0x40 + tree.size());
    put32(pe, section + 20, PeFixture::raw);
    std::copy(tree.begin(), tree.end(), pe.begin() + PeFixture::root);
    return {std::move(pe), std::move(offsets)};
}

void pixel(const CursorImage& image, int x, int y, std::array<unsigned char, 4> expected)
{
    const auto at = (std::size_t(y) * image.width + x) * 4;
    require(
        std::equal(expected.begin(), expected.end(), image.rgba.begin() + at), "cursor RGBA pixel");
}

void check_decode()
{
    const auto mono = dib(1, 1);
    auto fixture = executable({{101, {mono}},
        {102, {dib(2, 4)}},
        {103, {dib(3, 8)}},
        {107, {dib(4, 16)}},
        {108, {dib(5, 24, 3, 2, true)}},
        {111, {dib(6, 32, 3, 2, false, true)}},
        {112, {dib(7, 16, 3, 2, false, false, true)}},
        {113, {dib(8, 32)}}});
    const auto image = decode_cursor_resource(fixture.bytes, 101, 0, 0);
    require(image.width == 3 && image.height == 2 && image.hotspot_x == 1 && image.hotspot_y == 1,
        "intrinsic size and hotspot");
    pixel(image, 0, 0, {255, 255, 255, 255});
    pixel(image, 1, 0, {0, 0, 0, 255});
    pixel(image, 2, 0, {0, 0, 0, 0});
    pixel(image, 0, 1, {0, 0, 0, 255});
    pixel(image, 1, 1, {255, 255, 255, 255});
    for (const auto id : {102, 103, 107, 108, 112, 113})
    {
        const auto colour = decode_cursor_resource(fixture.bytes, id, 6, 4);
        require(colour.width == 6 && colour.height == 4 && colour.hotspot_x == 2 &&
                    colour.hotspot_y == 2,
            "scaled dimensions and hotspot");
        pixel(colour, 0, 0, {255, 0, 0, 255});
        pixel(colour, 3, 0, {0, 255, 0, 255});
        pixel(colour, 4, 0, {0, 0, 0, 0});
        pixel(colour, 0, 3, {0, 0, 255, 255});
        pixel(colour, 3, 3, {255, 255, 255, 255});
    }
    const auto alpha = decode_cursor_resource(fixture.bytes, 111, 0, 0);
    pixel(alpha, 0, 0, {255, 0, 0, 128});
    pixel(alpha, 1, 0, {0, 255, 0, 255});
    pixel(alpha, 2, 0, {0, 0, 0, 0});

    auto midtones = dib(9, 16);
    put16(midtones.data, 52, 0x1084); // four in each RGB555 channel, top-left
    const auto midtone_pe = executable({{114, {midtones}}});
    pixel(decode_cursor_resource(midtone_pe.bytes, 114, 0, 0), 0, 0, {33, 33, 33, 255});

    auto low_colour = dib(11, 8, 32, 32);
    put32(low_colour.data, 44, 0x0000ff);
    const auto multiple = executable(
        {{106, {dib(10, 1, 16, 16), low_colour, dib(12, 24, 32, 32), dib(13, 24, 48, 48)}}});
    const auto exact = decode_cursor_resource(multiple.bytes, 106, 32, 32);
    pixel(exact, 0, 0, {255, 0, 0, 255});
    require(exact.hotspot_x == 1, "exact size precedes a larger frame");
    const auto smaller = decode_cursor_resource(multiple.bytes, 106, 24, 24);
    pixel(smaller, 0, 0, {255, 255, 255, 255});
    const auto smallest = decode_cursor_resource(multiple.bytes, 106, 8, 8);
    pixel(smallest, 0, 0, {255, 255, 255, 255});
    require(smallest.hotspot_x == 0 && smaller.hotspot_x == 1, "hotspot down/up scaling");
    require(decode_cursor_resource(multiple.bytes, 106, 0, 0).width == 16,
        "zero size uses first image");

    auto inversion = executable({{106, {mono}}});
    // Top-left white XOR now has AND=1: it would invert the desktop.
    inversion.bytes[inversion.cursor_offsets.at(1) + 4 + 40 + 8 + 8 + 4] |= 0x80;
    bool unsupported = false;
    try
    {
        decode_cursor_resource(inversion.bytes, 106, 0, 0);
    }
    catch (const beachlife::compat::UnsupportedCursor&)
    {
        unsupported = true;
    }
    require(unsupported, "inverted pixels must not silently change appearance");
}

void check_malformed()
{
    const auto fixture = executable({{106, {dib(1, 1)}}});
    const auto rejects = [](std::span<const unsigned char> bytes, unsigned id = 106, int width = 32)
    {
        bool rejected = false;
        try
        {
            decode_cursor_resource(bytes, id, width, 32);
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        require(rejected, "malformed/missing cursor must fail");
    };
    for (std::size_t size = 0; size < fixture.bytes.size(); ++size)
    {
        rejects(std::span(fixture.bytes).first(size));
    }
    rejects(fixture.bytes, 999);
    rejects(fixture.bytes, 106, -1);
    rejects(fixture.bytes, 106, 0x7fffffff);
    for (const auto [offset, value] :
        std::array<std::pair<std::size_t, unsigned>, 7>{{{0x3c, 0xfffffff0},
            {0x98 + 112, 0xfffffff0},
            {PeFixture::root + 20, 0x80000000},
            {PeFixture::root + 28, 0xfffffff0},
            {fixture.cursor_offsets.at(1), 0xffff},
            {fixture.cursor_offsets.at(1) + 12, 3},
            {fixture.cursor_offsets.at(1) + 36, 256}}})
    {
        auto corrupt = fixture.bytes;
        put32(corrupt, offset, value);
        rejects(corrupt);
    }
}

class GeneratedFile final : public beachlife::data::SourceFile
{
  public:
    explicit GeneratedFile(const Bytes& bytes)
        : bytes_(bytes)
    {
    }
    std::uint64_t size() const override
    {
        return bytes_.size();
    }
    std::uint64_t tell() const override
    {
        return position_;
    }
    void seek(std::uint64_t position) override
    {
        position_ = position;
    }
    std::size_t read(std::span<unsigned char> destination) override
    {
        const auto count =
            std::min<std::size_t>({destination.size(), 7, bytes_.size() - position_});
        std::copy_n(bytes_.begin() + position_, count, destination.begin());
        position_ += count;
        return count;
    }

  private:
    const Bytes& bytes_;
    std::size_t position_ = 0;
};

class GeneratedSource final : public beachlife::data::FileSource
{
  public:
    Bytes bytes = executable({{106, {dib(1, 24, 32, 32)}}}).bytes;
    mutable unsigned opens = 0;
    std::unique_ptr<beachlife::data::SourceFile> open(std::string_view name) const override
    {
        require(name == "BeachLife.exe", "cursor reads only the VFS executable data");
        ++opens;
        return std::make_unique<GeneratedFile>(bytes);
    }
};

void check_sdl()
{
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    {
        auto source = std::make_shared<GeneratedSource>();
        beachlife::runtime::retail_files::Session files(source);
        const auto before = SDL_GetCursor();
        {
            beachlife::compat::ResourceCursorSession cursors;
            beachlife::runtime::retail_cursor::Session provider(
                beachlife::compat::retail_cursor_provider());
            const auto name = reinterpret_cast<LPCSTR>(106);
            const auto a =
                beachlife::runtime::retail_cursor::load_image(nullptr, 106, 2, 32, 32, 0x8000);
            require(a, SDL_GetError());
            require(a == LoadImageA(GetModuleHandleA(nullptr), name, 2, 32, 32, 0x8000),
                "module and null share a cursor");
            require(a == LoadImageA(nullptr, name, 2, 0, 0, 0x8040),
                "default size shares 32x32 cursor");
            const auto b = LoadImageA(nullptr, name, 2, 32, 32, 0);
            const auto c = LoadImageA(nullptr, name, 2, 32, 32, 0);
            const auto small = LoadImageA(nullptr, name, 2, 16, 16, 0x8000);
            require(b && c && small && a != b && b != c && a != small,
                "sharing is opt-in and size-specific");
            require(source->opens == 1, "one lazy executable snapshot per session");
            require(!LoadImageA(nullptr, reinterpret_cast<LPCSTR>(999), 2, 32, 32, 0x8000),
                "missing resource fails");
            require(!LoadImageA(nullptr, name, 2, -1, 32, 0), "negative size fails");
            require(GetLastError() == ERROR_INVALID_PARAMETER, "invalid size error");
            require(beachlife::runtime::retail_cursor::set_cursor(a) == before,
                "SetCursor returns previous cursor");
            {
                beachlife::compat::ResourceCursorSession nested;
                const auto inner = LoadImageA(nullptr, name, 2, 32, 32, 0x8000);
                require(inner && inner != a, "nested cache owns independent handles");
                SetCursor(reinterpret_cast<HCURSOR>(inner));
            }
            require(SDL_GetCursor() == reinterpret_cast<SDL_Cursor*>(a),
                "nested session restores selected cursor");
            require(LoadImageA(nullptr, name, 2, 32, 32, 0x8000) == a, "outer cache restored");
        }
        require(SDL_GetCursor() == before, "cursor restored before resource destruction");
    }
    SDL_Quit();
}
} // namespace

int main(int argc, char** argv)
{
    check_decode();
    check_malformed();
    if (argc == 2 && std::string_view(argv[1]) == "--sdl")
    {
        check_sdl();
        std::cout << "Generated PE cursors: VFS, SDL handles, sharing and lifetime passed.\n";
    }
    std::cout
        << "Generated PE cursors: selection, masks, colours, alpha, hotspots and bounds passed.\n";
}
