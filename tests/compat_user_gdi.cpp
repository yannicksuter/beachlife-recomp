#include "platform/compat/ansi_text.h"
#include "platform/compat/gdi_dib.h"
#include "runtime/retail_files.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
void expect(bool condition)
{
    if (!condition)
    {
        throw std::runtime_error("USER32/GDI conversion check failed");
    }
}
BITMAPINFO info(int width, int height, unsigned depth)
{
    BITMAPINFO result{};
    result.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    result.bmiHeader.biWidth = width;
    result.bmiHeader.biHeight = height;
    result.bmiHeader.biPlanes = 1;
    result.bmiHeader.biBitCount = depth;
    return result;
}

void check_ansi()
{
    using namespace beachlife::compat;
    std::string bytes;
    for (unsigned i = 0; i < 256; ++i)
    {
        bytes += static_cast<char>(i);
    }
    const auto utf8 = ansi_to_utf8(bytes);
    std::string restored;
    expect(utf8_to_ansi(utf8, restored) && restored == bytes);
    expect(ansi_to_utf8("\x80\x93\xe9") == "\xe2\x82\xac\xe2\x80\x9c\xc3\xa9");
    for (const std::string_view invalid :
        {"\xc0\xaf", "\xe0\x80\xaf", "\xc3", "\xe2\x82", "\xed\xa0\x80", "\xf0\x9f\x8c\xb4"})
    {
        restored = "unchanged";
        expect(!utf8_to_ansi(invalid, restored) && restored == "unchanged");
    }
}

void check_dib_layout()
{
    using beachlife::compat::gdi::describe_dib;
    using beachlife::compat::gdi::dib_pixel;
    using beachlife::compat::gdi::DibLayout;
    auto header = info(1, 2, 24);
    DibLayout layout;
    expect(describe_dib(&header, layout) && layout.stride == 4 && layout.bytes == 8);
    const std::array<BYTE, 8> rows{255, 0, 0, 0xe7, 0, 0, 255, 0xe7};
    expect(dib_pixel(layout, rows.data(), 0, 0) == 0xff0000ff); // top red
    expect(dib_pixel(layout, rows.data(), 0, 1) == 0xffff0000); // bottom blue
    header.bmiHeader.biHeight = -2;
    expect(describe_dib(&header, layout));
    expect(dib_pixel(layout, rows.data(), 0, 0) == 0xffff0000);
    header = info(2, 1, 16);
    const std::array<BYTE, 4> rgb555{0x00, 0x7c, 0xe0, 0x03};
    expect(describe_dib(&header, layout));
    expect(dib_pixel(layout, rgb555.data(), 0, 0) == 0xff0000ff);
    expect(dib_pixel(layout, rgb555.data(), 1, 0) == 0xff00ff00);
    const std::array<BYTE, 4> midtones{0x63, 0x0c, 0, 0}; // 3 in every RGB555 channel
    expect(dib_pixel(layout, midtones.data(), 0, 0) == 0xff181818);
    struct MaskInfo
    {
        BITMAPINFOHEADER header;
        std::array<DWORD, 3> masks;
    } masks{};
    masks.header = header.bmiHeader;
    masks.header.biCompression = 3;
    masks.masks = {0xf800, 0x07e0, 0x001f};
    const std::array<BYTE, 4> rgb565{0, 0xf8, 0xe0, 7};
    expect(describe_dib(reinterpret_cast<BITMAPINFO*>(&masks), layout));
    expect(dib_pixel(layout, rgb565.data(), 0, 0) == 0xff0000ff);
    expect(dib_pixel(layout, rgb565.data(), 1, 0) == 0xff00ff00);
    masks.masks[1] = 0x07e1;
    expect(!describe_dib(reinterpret_cast<BITMAPINFO*>(&masks), layout));
    masks.masks = {0xf800, 0xf800, 0x1f};
    expect(!describe_dib(reinterpret_cast<BITMAPINFO*>(&masks), layout));
    struct PaletteInfo
    {
        BITMAPINFOHEADER header;
        std::array<RGBQUAD, 2> colours;
    } palette{};
    palette.header = info(3, 1, 1).bmiHeader;
    palette.colours = {RGBQUAD{0, 0, 0, 0}, RGBQUAD{0, 0, 255, 0}};
    const std::array<BYTE, 4> indices{0xa0, 0xff, 0xff, 0xff};
    expect(describe_dib(reinterpret_cast<BITMAPINFO*>(&palette), layout));
    expect(layout.stride == 4 && dib_pixel(layout, indices.data(), 0, 0) == 0xff0000ff);
    expect(dib_pixel(layout, indices.data(), 1, 0) == 0xff000000);
    expect(dib_pixel(layout, indices.data(), 2, 0) == 0xff0000ff);
    for (auto invalid :
        {info(0, 1, 32), info(-1, 1, 32), info(1, 0, 32), info(1, INT32_MIN, 32), info(1, 1, 12)})
    {
        expect(!describe_dib(&invalid, layout));
    }
    header = info(1, 1, 32);
    header.bmiHeader.biPlanes = 2;
    expect(!describe_dib(&header, layout));
}

// Entirely generated bitmap font. FreeType reads the same bytes the VFS
// exposes; there are no installed-font or game-data dependencies.
constexpr std::string_view font_data = R"(STARTFONT 2.1
FONT -fixture-fixture-medium-r-normal--8-80-75-75-c-80-iso10646-1
SIZE 8 75 75
FONTBOUNDINGBOX 8 8 0 -2
STARTPROPERTIES 5
FONT_ASCENT 6
FONT_DESCENT 2
FAMILY_NAME "Fixture"
CHARSET_REGISTRY "ISO10646"
CHARSET_ENCODING "1"
ENDPROPERTIES
CHARS 2
STARTCHAR space
ENCODING 32
SWIDTH 500 0
DWIDTH 4 0
BBX 0 0 0 0
BITMAP
ENDCHAR
STARTCHAR A
ENCODING 65
SWIDTH 1000 0
DWIDTH 8 0
BBX 8 8 0 -2
BITMAP
18
24
42
42
7E
42
42
00
ENDCHAR
ENDFONT
)";
class GeneratedFile final : public beachlife::data::SourceFile
{
  public:
    std::uint64_t size() const override
    {
        return font_data.size();
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
        const auto count = std::min<std::uint64_t>(
            destination.size(), position_ < size() ? size() - position_ : 0);
        std::memcpy(destination.data(), font_data.data() + std::min(position_, size()), count);
        position_ += count;
        return count;
    }

  private:
    std::uint64_t position_ = 0;
};
class GeneratedSource final : public beachlife::data::FileSource
{
  public:
    std::unique_ptr<beachlife::data::SourceFile> open(std::string_view path) const override
    {
        if (path == "Data\\Fixture.bdf" || path == "data/fixture.bdf")
        {
            return std::make_unique<GeneratedFile>();
        }
        return nullptr;
    }
};

void check_font_and_stretch()
{
    beachlife::runtime::retail_files::Session files(std::make_shared<GeneratedSource>());
    expect(AddFontResourceA("missing.bdf") == 0);
    expect(AddFontResourceA("Data\\Fixture.bdf") == 1);
    expect(AddFontResourceA("data/fixture.bdf") == 1);
    HDC dc = CreateCompatibleDC(nullptr);
    expect(dc && GetDeviceCaps(dc, LOGPIXELSY) == 96);
    auto header = info(32, 16, 32);
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &header, DIB_RGB_COLORS, &pixels, nullptr, 0);
    expect(bitmap && pixels);
    const auto old_bitmap = SelectObject(dc, bitmap);
    expect(old_bitmap != nullptr);
    HFONT font = CreateFontA(
        -8, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, VARIABLE_PITCH, "Fixture");
    expect(font != nullptr);
    const auto old_font = SelectObject(dc, font);
    expect(old_font != nullptr);
    expect(RemoveFontResourceA("DATA/FIXTURE.BDF"));
    expect(RemoveFontResourceA("data/fixture.bdf"));
    expect(!RemoveFontResourceA("data/fixture.bdf"));
    SIZE size{};
    expect(GetTextExtentPoint32A(dc, "A A", 3, &size) && size.cx == 20 && size.cy == 8);
    expect(GetKerningPairsA(dc, 0, nullptr) == 0);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkColor(dc, 0);
    expect(ExtTextOutA(dc, 2, 2, ETO_OPAQUE, nullptr, "A", 1, nullptr));
    const auto* bytes = static_cast<BYTE*>(pixels);
    const auto channel = [&](int x, int y)
    {
        return bytes[(15 - y) * 128 + x * 4];
    };
    expect(channel(5, 2) == 255 && channel(6, 2) == 255 && channel(4, 2) == 0);
    expect(channel(5, 9) == 0);
    expect(ExtTextOutA(dc, 0, 0, ETO_OPAQUE, nullptr, "A A", 3, nullptr));
    expect(!DeleteObject(bitmap) && !DeleteObject(font));
    expect(SelectObject(dc, old_font) == font && DeleteObject(font));

    const std::array<BYTE, 8> source{0x1f, 0, 0xff, 0x7f, 0, 0x7c, 0xe0, 3};
    const auto source_info = info(2, 2, 16);
    expect(
        StretchDIBits(dc, 0, 0, 4, 4, 0, 0, 2, 2, source.data(), &source_info, 0, 0x00cc0020) == 2);
    // Top red/green, bottom blue/white, with nearest-neighbour scaling.
    expect(bytes[15 * 128 + 2] == 255 && bytes[15 * 128] == 0);
    expect(bytes[15 * 128 + 3 * 4 + 1] == 255);
    expect(bytes[12 * 128] == 255 && bytes[12 * 128 + 2] == 0);
    expect(bytes[12 * 128 + 3 * 4] == 255 && bytes[12 * 128 + 3 * 4 + 2] == 255);
    expect(SelectObject(dc, old_bitmap) == bitmap && DeleteObject(bitmap));
    expect(DeleteDC(dc));
}
} // namespace

int main()
{
    check_ansi();
    check_dib_layout();
    check_font_and_stretch();
    std::cout << "DIB layout, RGB/palette conversion, VFS fonts, glyph orientation and stretch "
                 "checks passed.\n";
}
