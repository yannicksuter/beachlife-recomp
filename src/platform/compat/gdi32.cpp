#include "ansi_text.h"
#include "compat_host.h"
#include "data/game_path.h"
#include "gdi_dib.h"
#include "platform/video.h"
#include "runtime/retail_files.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace
{
using beachlife::compat::unsupported;
using beachlife::compat::gdi::describe_dib;
using beachlife::compat::gdi::dib_pixel;
using beachlife::compat::gdi::DibLayout;

// These resources outlive their registration when an HFONT still uses them.
struct FontData
{
    std::shared_ptr<std::vector<BYTE>> bytes;
    FT_Long index = 0;
    std::string family;
    FT_Long style = 0;
};
struct FontResource
{
    std::string path;
    unsigned references = 1;
    std::vector<FontData> faces;
};
FT_Library library = nullptr;
std::vector<FontResource> resources;

// Windows system fonts the game names without a file (sDEBUGFONT_NAME is
// "Small Fonts", a Windows bitmap font) are not game data. Like the Windows
// font mapper substituting a missing face, the host substitutes the UI font it
// ships next to the executable.
const FontData* system_substitute()
{
    static FontResource substitute;
    if (substitute.faces.empty())
    {
        const char* base = SDL_GetBasePath();
        const std::string path =
            std::string(base ? base : "") + "assets/launcher/Roboto-Medium.ttf";
        SDL_IOStream* stream = SDL_IOFromFile(path.c_str(), "rb");
        if (!stream)
        {
            return nullptr;
        }
        const Sint64 size = SDL_GetIOSize(stream);
        auto bytes =
            std::make_shared<std::vector<BYTE>>(size > 0 ? static_cast<std::size_t>(size) : 0);
        const bool complete =
            size > 0 && SDL_ReadIO(stream, bytes->data(), bytes->size()) == bytes->size();
        SDL_CloseIO(stream);
        FT_Face face = nullptr;
        if (!complete || FT_New_Memory_Face(library, bytes->data(), bytes->size(), 0, &face))
        {
            return nullptr;
        }
        FontData data{bytes, 0, face->family_name ? face->family_name : "", face->style_flags};
        FT_Done_Face(face);
        substitute.path = path;
        substitute.faces.push_back(std::move(data));
    }
    return &substitute.faces.front();
}

bool init_freetype()
{
    return library || FT_Init_FreeType(&library) == 0;
}
std::string folded(std::string_view text)
{
    std::string result(text);
    for (char& c : result)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c += 'a' - 'A';
        }
        if (c == '\\')
        {
            c = '/';
        }
    }
    while (result.starts_with("./"))
    {
        result.erase(0, 2);
    }
    return result;
}

FT_ULong unicode(BYTE c)
{
    return beachlife::compat::ansi_codepoint(c);
}
int pixels(FT_Pos value)
{
    return static_cast<int>(std::floor(double(value) / 64 + 0.5));
}

enum class Kind
{
    bitmap,
    font,
    brush
};
struct Object
{
    explicit Object(Kind type, bool permanent = false)
        : kind(type)
        , stock(permanent)
    {
    }
    virtual ~Object() = default;
    Kind kind;
    bool stock;
    unsigned selections = 0;
};
struct Bitmap final : Object
{
    explicit Bitmap(bool permanent = false)
        : Object(Kind::bitmap, permanent)
    {
    }
    DibLayout layout;
    std::vector<RGBQUAD> palette;
    std::vector<BYTE> bytes;
};
struct Font final : Object
{
    explicit Font(bool permanent = false)
        : Object(Kind::font, permanent)
    {
    }
    ~Font() override
    {
        if (face)
        {
            FT_Done_Face(face);
        }
    }
    std::shared_ptr<std::vector<BYTE>> bytes;
    FT_Face face = nullptr;
    bool bold = false, italic = false, underline = false, strikeout = false;
    int height = 0, ascent = 0;
};
struct Brush final : Object
{
    explicit Brush(COLORREF colour)
        : Object(Kind::brush, true)
        , colour(colour)
    {
    }
    COLORREF colour;
};
Bitmap stock_bitmap(true);
Font stock_font(true);
Brush black_brush(0), white_brush(0xffffff);
std::unordered_map<HGDIOBJ, std::unique_ptr<Object>> objects;
struct Context
{
    HWND window = nullptr;
    Bitmap* bitmap = &stock_bitmap;
    Font* font = &stock_font;
    Brush* brush = &white_brush;
    COLORREF text = 0, background = 0xffffff;
    UINT align = 0;
};
std::unordered_map<HDC, std::unique_ptr<Context>> contexts;

Object* object(HGDIOBJ handle)
{
    if (handle == &stock_bitmap)
    {
        return &stock_bitmap;
    }
    if (handle == &stock_font)
    {
        return &stock_font;
    }
    if (handle == &black_brush)
    {
        return &black_brush;
    }
    if (handle == &white_brush)
    {
        return &white_brush;
    }
    const auto it = objects.find(handle);
    return it == objects.end() ? nullptr : it->second.get();
}
Context* context(HDC handle)
{
    const auto it = contexts.find(handle);
    return it == contexts.end() ? nullptr : it->second.get();
}
template <class T> HGDIOBJ retain(std::unique_ptr<T> value)
{
    HGDIOBJ handle = value.get();
    objects.emplace(handle, std::move(value));
    return handle;
}
HDC make_context(HWND window)
{
    auto value = std::make_unique<Context>();
    value->window = window;
    const auto handle = reinterpret_cast<HDC>(value.get());
    contexts.emplace(handle, std::move(value));
    return handle;
}
void deselect(Context& dc)
{
    if (!dc.bitmap->stock)
    {
        --dc.bitmap->selections;
    }
    if (!dc.font->stock)
    {
        --dc.font->selections;
    }
    if (!dc.brush->stock)
    {
        --dc.brush->selections;
    }
}
Font* selected_font(Context& dc, const char* api)
{
    if (!dc.font->face)
    {
        unsupported(api, "select a font created from AddFontResourceA first");
    }
    return dc.font;
}
bool glyph(Font& font, BYTE character, bool render)
{
    if (FT_Load_Char(font.face, unicode(character), FT_LOAD_DEFAULT))
    {
        return false;
    }
    if (font.bold)
    {
        FT_GlyphSlot_Embolden(font.face->glyph);
    }
    if (font.italic)
    {
        FT_GlyphSlot_Oblique(font.face->glyph);
    }
    return !render || FT_Render_Glyph(font.face->glyph, FT_RENDER_MODE_NORMAL) == 0;
}
bool measure(Font& font, LPCSTR text, unsigned length, std::int64_t& width)
{
    width = 0;
    for (unsigned i = 0; i < length; ++i)
    {
        if (!glyph(font, static_cast<BYTE>(text[i]), false))
        {
            return false;
        }
        width += pixels(font.face->glyph->advance.x);
        if (width > std::numeric_limits<LONG>::max())
        {
            return false;
        }
    }
    return true;
}
void write_pixel(Bitmap& bitmap, int x, int y, COLORREF colour, unsigned coverage = 255)
{
    if (x < 0 || y < 0 || x >= bitmap.layout.width || y >= bitmap.layout.height)
    {
        return;
    }
    const auto row = bitmap.layout.top_down ? y : bitmap.layout.height - 1 - y;
    BYTE* pixel =
        bitmap.bytes.data() + std::size_t(row) * bitmap.layout.stride + std::size_t(x) * 4;
    const std::array<unsigned, 3> rgb{(colour >> 16) & 255, (colour >> 8) & 255, colour & 255};
    for (unsigned i = 0; i < 3; ++i)
    {
        pixel[i] = (rgb[i] * coverage + pixel[i] * (255 - coverage) + 127) / 255;
    }
}
void fill(Bitmap& bitmap, RECT rectangle, COLORREF colour)
{
    const int left = std::max<LONG>(0, rectangle.left), top = std::max<LONG>(0, rectangle.top);
    const int right = std::min<LONG>(bitmap.layout.width, rectangle.right),
              bottom = std::min<LONG>(bitmap.layout.height, rectangle.bottom);
    for (int y = top; y < bottom; ++y)
    {
        for (int x = left; x < right; ++x)
        {
            write_pixel(bitmap, x, y, colour);
        }
    }
}
void validate_format(const BITMAPINFO* info, const char* api)
{
    if (info && info->bmiHeader.biCompression != BI_RGB && info->bmiHeader.biCompression != 3)
    {
        unsupported(api, "compressed DIB data");
    }
}
} // namespace

extern "C"
{
    HDC WINAPI GetDC(HWND window)
    {
        if (!window)
        {
            unsupported("GetDC", "desktop drawing context");
        }
        if (!SDL_IsMainThread() || !beachlife::compat::video(window)->window())
        {
            return nullptr;
        }
        // Also establishes the host HWND for subsequent cursor/window operations.
        RECT rectangle;
        if (!GetClientRect(window, &rectangle))
        {
            return nullptr;
        }
        try
        {
            return make_context(window);
        }
        catch (...)
        {
            return nullptr;
        }
    }
    int WINAPI ReleaseDC(HWND window, HDC handle)
    {
        auto* dc = context(handle);
        if (!dc || !window || dc->window != window)
        {
            return 0;
        }
        deselect(*dc);
        contexts.erase(handle);
        return 1;
    }
    HDC WINAPI CreateCompatibleDC(HDC handle)
    {
        if (handle && !context(handle))
        {
            return nullptr;
        }
        try
        {
            return make_context(nullptr);
        }
        catch (...)
        {
            return nullptr;
        }
    }
    BOOL WINAPI DeleteDC(HDC handle)
    {
        auto* dc = context(handle);
        if (!dc || dc->window)
        {
            return FALSE;
        }
        deselect(*dc);
        contexts.erase(handle);
        return TRUE;
    }
    HBITMAP WINAPI CreateDIBSection(
        HDC handle, const BITMAPINFO* info, UINT usage, void** bits, HANDLE section, DWORD offset)
    {
        if (bits)
        {
            *bits = nullptr;
        }
        if (!bits || (handle && !context(handle)))
        {
            return nullptr;
        }
        if (section || offset)
        {
            unsupported("CreateDIBSection", "file-mapped DIB sections");
        }
        if (usage != DIB_RGB_COLORS)
        {
            unsupported("CreateDIBSection", "logical palette colour indices");
        }
        validate_format(info, "CreateDIBSection");
        try
        {
            auto bitmap = std::make_unique<Bitmap>();
            if (!describe_dib(info, bitmap->layout))
            {
                return nullptr;
            }
            // DIB sections retain their palette, rather than a caller's temporary BITMAPINFO.
            if (bitmap->layout.depth <= 8)
            {
                bitmap->palette.assign(
                    bitmap->layout.palette, bitmap->layout.palette + bitmap->layout.colours);
                bitmap->layout.palette = bitmap->palette.data();
            }
            bitmap->bytes.resize(bitmap->layout.bytes);
            BYTE* storage = bitmap->bytes.data();
            const auto result = reinterpret_cast<HBITMAP>(retain(std::move(bitmap)));
            *bits = storage;
            return result;
        }
        catch (...)
        {
            return nullptr;
        }
    }
    HGDIOBJ WINAPI GetStockObject(int index)
    {
        if (index == BLACK_BRUSH)
        {
            return &black_brush;
        }
        if (index == 0)
        {
            return &white_brush;
        }
        if (index == 13 || index == 17)
        {
            return &stock_font;
        }
        unsupported("GetStockObject", "unsupported stock pen, brush or font");
    }
    HGDIOBJ WINAPI SelectObject(HDC handle, HGDIOBJ selected)
    {
        auto* dc = context(handle);
        auto* value = object(selected);
        if (!dc || !value)
        {
            return nullptr;
        }
        Object* previous;
        switch (value->kind)
        {
        case Kind::bitmap:
            if (dc->window || (!value->stock && value->selections && value != dc->bitmap))
            {
                return nullptr;
            }
            previous = dc->bitmap;
            dc->bitmap = static_cast<Bitmap*>(value);
            break;
        case Kind::font:
            previous = dc->font;
            dc->font = static_cast<Font*>(value);
            break;
        case Kind::brush:
            previous = dc->brush;
            dc->brush = static_cast<Brush*>(value);
            break;
        }
        if (!previous->stock)
        {
            --previous->selections;
        }
        if (!value->stock)
        {
            ++value->selections;
        }
        return previous;
    }
    BOOL WINAPI DeleteObject(HGDIOBJ handle)
    {
        auto* value = object(handle);
        if (!value)
        {
            return FALSE;
        }
        if (value->stock)
        {
            return TRUE;
        }
        if (value->selections)
        {
            return FALSE;
        }
        return objects.erase(handle) != 0;
    }
    int WINAPI SetMapMode(HDC handle, int mode)
    {
        if (!context(handle))
        {
            return 0;
        }
        if (mode != MM_TEXT)
        {
            unsupported("SetMapMode", "only MM_TEXT mapping is available");
        }
        return MM_TEXT;
    }
    int WINAPI GetDeviceCaps(HDC handle, int index)
    {
        if (!context(handle))
        {
            return 0;
        }
        // A memory DC uses Windows' conventional logical DPI, independent of monitor scaling.
        if (index == LOGPIXELSY || index == 88)
        {
            return 96;
        }
        if (index == 12)
        {
            return 32; // BITSPIXEL
        }
        if (index == 14)
        {
            return 1; // PLANES
        }
        unsupported("GetDeviceCaps", "unsupported device capability");
    }
    COLORREF WINAPI SetTextColor(HDC handle, COLORREF colour)
    {
        auto* dc = context(handle);
        if (!dc)
        {
            return 0xffffffff;
        }
        const auto previous = dc->text;
        dc->text = colour;
        return previous;
    }
    COLORREF WINAPI SetBkColor(HDC handle, COLORREF colour)
    {
        auto* dc = context(handle);
        if (!dc)
        {
            return 0xffffffff;
        }
        const auto previous = dc->background;
        dc->background = colour;
        return previous;
    }
    UINT WINAPI SetTextAlign(HDC handle, UINT align)
    {
        auto* dc = context(handle);
        if (!dc)
        {
            return 0xffffffff;
        }
        if (align & ~UINT(0x1e))
        {
            unsupported("SetTextAlign", "updating the current position or RTL text");
        }
        if ((align & 6) == 4 || (align & 0x18) == 0x10)
        {
            return 0xffffffff;
        }
        const auto previous = dc->align;
        dc->align = align;
        return previous;
    }
    int WINAPI AddFontResourceA(LPCSTR file)
    {
        if (!file || !*file)
        {
            return 0;
        }
        try
        {
            const auto path = beachlife::data::normalize_game_path(file).key;
            for (auto& resource : resources)
            {
                if (resource.path == path)
                {
                    ++resource.references;
                    return static_cast<int>(resource.faces.size());
                }
            }
            if (!init_freetype())
            {
                return 0;
            }
            namespace files = beachlife::runtime::retail_files;
            std::unique_ptr<files::File, decltype(&files::close)> stream(
                files::open(file, "rb"), files::close);
            if (!stream || files::seek(stream.get(), 0, files::SeekOrigin::end))
            {
                return 0;
            }
            files::Position size;
            if (files::position(stream.get(), &size) || !size ||
                size > std::numeric_limits<std::int32_t>::max() ||
                files::seek(stream.get(), 0, files::SeekOrigin::begin))
            {
                return 0;
            }
            auto bytes = std::make_shared<std::vector<BYTE>>(static_cast<std::size_t>(size));
            if (files::read(bytes->data(), 1, bytes->size(), stream.get()) != bytes->size())
            {
                return 0;
            }
            FontResource resource;
            resource.path = path;
            FT_Face probe = nullptr;
            if (FT_New_Memory_Face(library, bytes->data(), bytes->size(), -1, &probe))
            {
                return 0;
            }
            const FT_Long count = probe->num_faces;
            FT_Done_Face(probe);
            for (FT_Long index = 0; index < count; ++index)
            {
                FT_Face face = nullptr;
                if (FT_New_Memory_Face(library, bytes->data(), bytes->size(), index, &face))
                {
                    return 0;
                }
                // Copy the metadata before any allocation that might throw.
                const auto cleanup =
                    std::unique_ptr<std::remove_pointer_t<FT_Face>, decltype(&FT_Done_Face)>(
                        face, FT_Done_Face);
                resource.faces.push_back({bytes,
                    index,
                    folded(face->family_name ? face->family_name : ""),
                    face->style_flags});
            }
            if (resource.faces.empty())
            {
                return 0;
            }
            resources.push_back(std::move(resource));
            return static_cast<int>(count);
        }
        catch (...)
        {
            return 0;
        }
    }
    BOOL WINAPI RemoveFontResourceA(LPCSTR file)
    {
        if (!file)
        {
            return FALSE;
        }
        try
        {
            const auto path = beachlife::data::normalize_game_path(file).key;
            for (auto it = resources.begin(); it != resources.end(); ++it)
            {
                if (it->path == path)
                {
                    if (--it->references == 0)
                    {
                        resources.erase(it);
                    }
                    return TRUE;
                }
            }
        }
        catch (...)
        {
            return FALSE;
        }
        return FALSE;
    }
    HFONT WINAPI CreateFontA(int height,
        int width,
        int escapement,
        int orientation,
        int weight,
        DWORD italic,
        DWORD underline,
        DWORD strikeout,
        DWORD charset,
        DWORD output_precision,
        DWORD clip_precision,
        DWORD quality,
        DWORD pitch,
        LPCSTR family)
    {
        if (escapement || orientation || width || (charset != 0 && charset != DEFAULT_CHARSET) ||
            output_precision || clip_precision || quality || (pitch & ~DWORD(0x32)))
        {
            unsupported("CreateFontA",
                "transformed fonts, non-ANSI charsets or nondefault precision/quality");
        }
        if (height == std::numeric_limits<int>::min() || weight < 0 || weight > 1000)
        {
            return nullptr;
        }
        try
        {
            if (!init_freetype())
            {
                return nullptr;
            }
            const auto wanted = folded(family ? family : "");
            const FontData* best = nullptr;
            int score = std::numeric_limits<int>::min();
            for (const auto& resource : resources)
            {
                for (const auto& candidate : resource.faces)
                {
                    const int match =
                        (candidate.family == wanted ? 8 : 0) +
                        (bool(candidate.style & FT_STYLE_FLAG_BOLD) == (weight >= 600) ? 2 : 0) +
                        (bool(candidate.style & FT_STYLE_FLAG_ITALIC) == bool(italic) ? 1 : 0);
                    if (match > score)
                    {
                        score = match;
                        best = &candidate;
                    }
                }
            }
            if (!best)
            {
                best = system_substitute();
            }
            if (!best)
            {
                unsupported(
                    "CreateFontA", "no registered font and the host substitute font is missing");
            }
            auto font = std::make_unique<Font>();
            font->bytes = best->bytes;
            if (FT_New_Memory_Face(
                    library, font->bytes->data(), font->bytes->size(), best->index, &font->face))
            {
                return nullptr;
            }
            if (FT_Select_Charmap(font->face, FT_ENCODING_UNICODE))
            {
                return nullptr;
            }
            unsigned size = height ? static_cast<unsigned>(height < 0 ? -height : height) : 16;
            if (height > 0)
            {
                if (font->face->height <= 0 || !font->face->units_per_EM)
                {
                    return nullptr;
                }
                size = std::max(1U,
                    static_cast<unsigned>(
                        (std::uint64_t(size) * font->face->units_per_EM) / font->face->height));
            }
            if (FT_Set_Pixel_Sizes(font->face, 0, size))
            {
                return nullptr;
            }
            font->ascent = pixels(font->face->size->metrics.ascender);
            font->height = std::max(1, font->ascent - pixels(font->face->size->metrics.descender));
            font->bold = weight >= 600 && !(best->style & FT_STYLE_FLAG_BOLD);
            font->italic = italic && !(best->style & FT_STYLE_FLAG_ITALIC);
            font->underline = underline;
            font->strikeout = strikeout;
            return reinterpret_cast<HFONT>(retain(std::move(font)));
        }
        catch (...)
        {
            return nullptr;
        }
    }
    BOOL WINAPI GetTextExtentPoint32A(HDC handle, LPCSTR text, int length, LPSIZE result)
    {
        auto* dc = context(handle);
        if (!dc || !result || length < 0 || (!text && length))
        {
            return FALSE;
        }
        auto* font = selected_font(*dc, "GetTextExtentPoint32A");
        std::int64_t width;
        if (!measure(*font, text, length, width))
        {
            return FALSE;
        }
        *result = {static_cast<LONG>(width), font->height};
        return TRUE;
    }
    BOOL WINAPI ExtTextOutA(HDC handle,
        int x,
        int y,
        UINT options,
        const RECT* rectangle,
        LPCSTR text,
        UINT length,
        const INT* spacing)
    {
        auto* dc = context(handle);
        if (!dc || (!text && length))
        {
            return FALSE;
        }
        if (options & ~UINT(6))
        {
            unsupported("ExtTextOutA", "glyph indices, RTL or non-ANSI text");
        }
        if (dc->window)
        {
            unsupported(
                "ExtTextOutA", "text directly on a GPU window; select a DIB in a memory DC");
        }
        auto& bitmap = *dc->bitmap;
        if (bitmap.stock)
        {
            return FALSE;
        }
        if (bitmap.layout.depth != 32 || bitmap.layout.red != 0xff0000 ||
            bitmap.layout.green != 0xff00 || bitmap.layout.blue != 0xff)
        {
            unsupported("ExtTextOutA", "text rasterization needs a 32-bit BGR DIB");
        }
        auto* font = selected_font(*dc, "ExtTextOutA");
        if (length > 8192)
        {
            return FALSE;
        }
        std::int64_t width;
        if (!measure(*font, text, length, width))
        {
            return FALSE;
        }
        if (spacing)
        {
            width = 0;
            for (unsigned i = 0; i < length; ++i)
            {
                width += spacing[i];
            }
        }
        std::int64_t left = x, top = y;
        if ((dc->align & 6) == 2)
        {
            left -= width;
        }
        else if ((dc->align & 6) == 6)
        {
            left -= width / 2;
        }
        if ((dc->align & 0x18) == 8)
        {
            top -= font->height;
        }
        else if ((dc->align & 0x18) == 0x18)
        {
            top -= font->ascent;
        }
        if (left < std::numeric_limits<LONG>::min() || left > std::numeric_limits<LONG>::max() ||
            left + width < std::numeric_limits<LONG>::min() ||
            left + width > std::numeric_limits<LONG>::max() ||
            top < std::numeric_limits<LONG>::min() ||
            top + font->height > std::numeric_limits<LONG>::max())
        {
            return FALSE;
        }
        RECT clip{0, 0, bitmap.layout.width, bitmap.layout.height};
        if ((options & 4) && rectangle)
        {
            clip.left = std::max(clip.left, rectangle->left);
            clip.top = std::max(clip.top, rectangle->top);
            clip.right = std::min(clip.right, rectangle->right);
            clip.bottom = std::min(clip.bottom, rectangle->bottom);
        }
        // A fresh DC has OPAQUE background mode, including when ETO_OPAQUE's
        // optional rectangle is null (as in the debug-atlas constructor).
        RECT background{static_cast<LONG>(left),
            static_cast<LONG>(top),
            static_cast<LONG>(left + width),
            static_cast<LONG>(top + font->height)};
        if ((options & ETO_OPAQUE) && rectangle)
        {
            background = *rectangle;
        }
        background.left = std::max(background.left, clip.left);
        background.top = std::max(background.top, clip.top);
        background.right = std::min(background.right, clip.right);
        background.bottom = std::min(background.bottom, clip.bottom);
        fill(bitmap, background, dc->background);
        std::int64_t pen = left;
        for (unsigned i = 0; i < length; ++i)
        {
            if (!glyph(*font, static_cast<BYTE>(text[i]), true))
            {
                return FALSE;
            }
            const auto slot = font->face->glyph;
            const auto& coverage = slot->bitmap;
            if (coverage.pixel_mode != FT_PIXEL_MODE_GRAY &&
                coverage.pixel_mode != FT_PIXEL_MODE_MONO)
            {
                unsupported("ExtTextOutA", "unsupported FreeType bitmap pixel mode");
            }
            for (unsigned row = 0; row < coverage.rows; ++row)
            {
                const BYTE* line =
                    coverage.buffer + (coverage.pitch >= 0 ? row : coverage.rows - 1 - row) *
                                          std::abs(coverage.pitch);
                const std::int64_t py = top + font->ascent - slot->bitmap_top + row;
                if (py < clip.top || py >= clip.bottom)
                {
                    continue;
                }
                for (unsigned column = 0; column < coverage.width; ++column)
                {
                    const std::int64_t px = pen + slot->bitmap_left + column;
                    if (px < clip.left || px >= clip.right)
                    {
                        continue;
                    }
                    const unsigned alpha =
                        coverage.pixel_mode == FT_PIXEL_MODE_MONO
                            ? ((line[column / 8] & (0x80 >> (column % 8))) ? 255 : 0)
                            : (unsigned(line[column]) * 255 /
                                  std::max(1U, unsigned(coverage.num_grays) - 1));
                    write_pixel(bitmap, px, py, dc->text, alpha);
                }
            }
            pen += spacing ? spacing[i] : pixels(slot->advance.x);
        }
        const auto rule = [&](std::int64_t y_rule)
        {
            const int thickness = std::max(1,
                pixels(
                    FT_MulFix(font->face->underline_thickness, font->face->size->metrics.y_scale)));
            for (std::int64_t py = std::max<std::int64_t>(clip.top, y_rule);
                py < std::min<std::int64_t>(clip.bottom, std::int64_t(y_rule) + thickness);
                ++py)
            {
                for (std::int64_t px = std::max<std::int64_t>(clip.left, left);
                    px < std::min<std::int64_t>(clip.right, left + width);
                    ++px)
                {
                    write_pixel(bitmap, px, py, dc->text);
                }
            }
        };
        if (font->underline)
        {
            rule(top + font->ascent -
                 pixels(
                     FT_MulFix(font->face->underline_position, font->face->size->metrics.y_scale)));
        }
        if (font->strikeout)
        {
            rule(top + font->ascent * 2 / 3);
        }
        return TRUE;
    }
    DWORD WINAPI GetKerningPairsA(HDC handle, DWORD count, LPKERNINGPAIR pairs)
    {
        auto* dc = context(handle);
        if (!dc)
        {
            return 0;
        }
        auto* font = selected_font(*dc, "GetKerningPairsA");
        if (!FT_HAS_KERNING(font->face))
        {
            return 0;
        }
        DWORD total = 0;
        for (unsigned first = 0; first < 256; ++first)
        {
            for (unsigned second = 0; second < 256; ++second)
            {
                const auto a = FT_Get_Char_Index(font->face, unicode(first)),
                           b = FT_Get_Char_Index(font->face, unicode(second));
                if (!a || !b)
                {
                    continue;
                }
                FT_Vector delta{};
                if (FT_Get_Kerning(font->face, a, b, FT_KERNING_DEFAULT, &delta))
                {
                    return 0;
                }
                const int amount = pixels(delta.x);
                if (!amount)
                {
                    continue;
                }
                if (pairs && total < count)
                {
                    pairs[total] = {static_cast<WORD>(first), static_cast<WORD>(second), amount};
                }
                ++total;
            }
        }
        return pairs ? std::min(total, count) : total;
    }
    int WINAPI StretchDIBits(HDC handle,
        int x,
        int y,
        int width,
        int height,
        int source_x,
        int source_y,
        int source_width,
        int source_height,
        const void* bits,
        const BITMAPINFO* info,
        UINT usage,
        DWORD operation)
    {
        auto* dc = context(handle);
        if (!dc || !bits || !info)
        {
            return 0;
        }
        if (usage != DIB_RGB_COLORS || operation != 0x00cc0020)
        {
            unsupported("StretchDIBits", "only DIB_RGB_COLORS with SRCCOPY is available");
        }
        validate_format(info, "StretchDIBits");
        DibLayout layout;
        if (!describe_dib(info, layout))
        {
            return 0;
        }
        if (!width || !height || !source_width || !source_height)
        {
            return 0;
        }
        if (width < 0 || height < 0 || source_width < 0 || source_height < 0)
        {
            unsupported("StretchDIBits", "mirrored stretches");
        }
        // For a bottom-up DIB ySrc is measured from its bottom edge.
        const std::int64_t source_top =
            layout.top_down ? source_y : std::int64_t(layout.height) - source_y - source_height;
        if (source_x < 0 || source_top < 0 ||
            std::int64_t(source_x) + source_width > layout.width ||
            source_top + source_height > layout.height)
        {
            return 0;
        }
        if (std::int64_t(x) + width > std::numeric_limits<int>::max() ||
            std::int64_t(y) + height > std::numeric_limits<int>::max())
        {
            return 0;
        }
        try
        {
            beachlife::platform::Frame frame;
            Bitmap* destination = nullptr;
            int target_width, target_height;
            if (dc->window)
            {
                // Partial window copies would need to retain the GPU's previous
                // frame; there is deliberately no invented black background.
                if (x || y)
                {
                    unsupported("StretchDIBits", "partial GPU-window drawing");
                }
                target_width = width;
                target_height = height;
                frame.width = width;
                frame.height = height;
                if (std::size_t(width) > frame.pixels.max_size() / height)
                {
                    return 0;
                }
                frame.pixels.resize(std::size_t(width) * height);
            }
            else
            {
                destination = dc->bitmap;
                if (destination->stock)
                {
                    return 0;
                }
                if (destination->layout.depth != 32 || destination->layout.red != 0xff0000 ||
                    destination->layout.green != 0xff00 || destination->layout.blue != 0xff)
                {
                    unsupported("StretchDIBits", "memory destination must be a 32-bit BGR DIB");
                }
                target_width = destination->layout.width;
                target_height = destination->layout.height;
            }
            for (int dy = std::max(0, y); dy < std::min(target_height, y + height); ++dy)
            {
                for (int dx = std::max(0, x); dx < std::min(target_width, x + width); ++dx)
                {
                    const int sx = source_x + (std::int64_t(dx) - x) * source_width / width,
                              sy = source_top + (std::int64_t(dy) - y) * source_height / height;
                    const auto rgba = dib_pixel(layout, bits, sx, sy);
                    if (destination)
                    {
                        const COLORREF colour = rgba & 0xffffff;
                        write_pixel(*destination, dx, dy, colour);
                    }
                    else
                    {
                        frame.pixels[std::size_t(dy) * width + dx] = rgba;
                    }
                }
            }
            if (dc->window)
            {
                beachlife::compat::video(dc->window)->present(frame, nullptr);
            }
            return source_height;
        }
        catch (...)
        {
            return 0;
        }
    }
} // extern "C"
