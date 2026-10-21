#include "font_owner_test_data.h"
#include "renderer_queue_test_state.h"
#include "startup_screen_text.h"
#include "tga_image.h"
#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace
{
namespace generated = font_owner_test;
static_assert(std::is_standard_layout_v<ScreenFontData>);
static_assert(std::is_same_v<decltype(ScreenFontData::image), TgaImage*>);
static_assert(offsetof(ScreenFontData, glyphs) == sizeof(TgaImage*));
static_assert(offsetof(ScreenFontData, kerning) == sizeof(TgaImage*) + 2048);
static_assert(offsetof(ScreenFontData, unknown_10804) == sizeof(TgaImage*) + 67584);
static_assert(sizeof(ScreenFontData) == sizeof(TgaImage*) + 67592);

struct Failure
{
};
unsigned checks = 0;
unsigned constructions = 0;
unsigned destructions = 0;
unsigned measurements = 0;
unsigned image_owners = 0;
const char* group = "setup";

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "font-data-owner: %s: %s\n", group, reason);
        throw Failure{};
    }
}

unsigned word(const std::array<unsigned char, 2056>& source, std::size_t offset)
{
    return source[offset] | (static_cast<unsigned>(source[offset + 1]) << 8);
}

generated::Input input(unsigned seed)
{
    generated::Input result;
    for (unsigned glyph = 0; glyph < 256; ++glyph)
    {
        for (unsigned component = 0; component < 4; ++component)
        {
            generated::word(result.coordinates,
                8 + glyph * 8 + component * 2,
                (glyph * 29 + component * 101 + seed) & 0xffffU);
        }
    }
    for (unsigned i = 0; i < result.kerning.size(); ++i)
    {
        result.kerning[i] = static_cast<unsigned char>(i * 37 + seed);
    }
    // Independent short-text expectations: A=4, B=6, space=2, AB kerning=-1.
    for (const auto& [glyph, advance] :
        std::array<std::array<unsigned, 2>, 3>{{{65, 4}, {66, 6}, {32, 2}}})
    {
        generated::word(result.coordinates, 8 + glyph * 8, 0);
        generated::word(result.coordinates, 12 + glyph * 8, advance);
    }
    result.kerning[65 * 255 + 66] = 255;
    result.kerning[65 * 255 + 32] = 0;
    result.kerning[32 * 255 + 66] = 0;
    result.tiff[512] = static_cast<unsigned char>(seed);
    result.tiff[513] = 0;
    result.tiff[514] = 128;
    result.tiff[515] = 255;
    return result;
}

struct Owner
{
    std::array<unsigned char, 16> before{};
    std::optional<ScreenFontData> value;
    std::array<unsigned char, 16> after{};

    explicit Owner(const char* name)
    {
        before.fill(0x3d);
        after.fill(0xa7);
        value.emplace(name);
        ++constructions;
    }
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
    void guards() const
    {
        require(std::all_of(before.begin(),
                    before.end(),
                    [](unsigned char v)
                    {
                        return v == 0x3d;
                    }),
            "leading storage guard changed");
        require(std::all_of(after.begin(),
                    after.end(),
                    [](unsigned char v)
                    {
                        return v == 0xa7;
                    }),
            "trailing storage guard changed");
    }
    void close()
    {
        require(value.has_value(), "duplicate destruction");
        value.reset(); // Actual FontData -> TgaImage destruction and scalar image release.
        ++destructions;
        guards(); // No dead FontData or image members are read.
    }
};

void verify(const ScreenFontData& font, const generated::Input& source, bool has_image)
{
    // First changed-source witness: the control copies the second DWORD here.
    require(font.unknown_10804[0] == 5, "first TexCoord word must be 5");
    require(font.unknown_10804[1] == 9, "second TexCoord word must be 9");
    for (std::size_t glyph = 0; glyph < 256; ++glyph)
    {
        for (std::size_t component = 0; component < 4; ++component)
        {
            require(font.glyphs[glyph][component] ==
                        word(source.coordinates, 8 + glyph * 8 + component * 2),
                "loaded glyph WORD differs");
        }
    }
    for (std::size_t i = 0; i < source.kerning.size(); ++i)
    {
        require(static_cast<unsigned char>(font.kerning[i]) == source.kerning[i],
            "loaded signed kerning byte differs");
    }
    if (!has_image)
    {
        require(font.image == nullptr, "missing TIFF must remain a null image");
        return;
    }
    require(font.image != nullptr && font.image->pixels != nullptr, "real TIFF image missing");
    const auto& image = *font.image;
    require(image.width == 2 && image.height == 2 && image.bits_per_pixel == 8 &&
                image.bytes_per_pixel == 1,
        "loaded image dimensions or depth differ");
    require(image.palette == nullptr && image.converted_palette == nullptr,
        "unrequested palettes must remain null");
    for (std::size_t i = 0; i < 4; ++i)
    {
        require(image.pixels[i] == source.tiff[512 + i], "real decoded pixel differs");
        require(image.unknown_1c[i] == 0, "eight-bit image tail differs");
    }
}

struct RendererBinding
{
    renderer_owner_test::Bootstrap renderer_bootstrap;
    ScreenRequestRenderer renderer{renderer_bootstrap.video(), 8, 4, 1};
    renderer_owner_test::RestoreOwnedState renderer_restore{renderer, renderer_bootstrap};
    ScreenRequestRenderer* previous = screen_request_renderer;
    RendererBinding()
    {
        renderer_queue_test::initialize(renderer, 0x35);
        screen_request_renderer = &renderer;
    }
    ~RendererBinding()
    {
        screen_request_renderer = previous;
    }
};

void metrics(ScreenFontData& data)
{
    RendererBinding binding;
    ScreenFontRenderer font(&data, nullptr);
    constexpr unsigned short pair[]{65, 66, 0};
    constexpr unsigned short words[]{65, 32, 66, 0};
    require(font.data_1c == &data && font.lookup_20 == nullptr,
        "real FontRenderer did not retain its borrowed inputs");
    require(font.MeasureText(pair) == 9, "AB width must include signed kerning -1");
    require(font.CountWrappedLines(words, 6, 0) == 2, "A space B must wrap into two lines");
    require(font.MeasureWrappedText(words, 6, 0) == 6, "widest wrapped line must be B");
    measurements += 3;
}

void unchanged_file(const beachlife::data::FileSource& source,
    const std::string& path,
    std::span<const unsigned char> expected)
{
    auto file = source.open(path);
    require(file != nullptr && file->size() == expected.size(), "input file size changed");
    std::vector<unsigned char> bytes(expected.size());
    require(file->read(bytes) == bytes.size(), "cannot reread complete generated input");
    require(std::equal(bytes.begin(), bytes.end(), expected.begin()), "input file changed");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        generated::host_require(argc == 2, "expected a fresh generated-font directory");
        generated::GeneratedFonts directory(argv[1]);
        const auto first = input(17);
        const auto second = input(231);
        directory.add("Fixture", first);
        directory.add("Other", second);
        directory.add("MissingImage", first, false);
        const auto mount = directory.mount();
        generated::files::Session files(mount);

        group = "complete files and metrics";
        {
            auto owner = std::make_unique<Owner>("fIxTuRe");
            ++image_owners;
            verify(*owner->value, first, true);
            metrics(*owner->value);
            owner->guards();
            owner->close();
        }
        group = "independent image lifetimes";
        {
            auto a = std::make_unique<Owner>("Fixture");
            auto b = std::make_unique<Owner>("Other");
            image_owners += 2;
            require(a->value->image != b->value->image &&
                        a->value->image->pixels != b->value->image->pixels,
                "simultaneous owners must not share image storage");
            verify(*a->value, first, true);
            verify(*b->value, second, true);
            a->close();
            verify(*b->value, second, true);
            metrics(*b->value);
            b->close();
        }
        group = "missing TIFF preserves complete metric owner";
        {
            auto owner = std::make_unique<Owner>("MissingImage");
            verify(*owner->value, first, false);
            metrics(*owner->value);
            owner->close();
        }
        for (const auto& [name, data] :
            std::array<std::pair<const char*, const generated::Input*>, 3>{
                {{"Fixture", &first}, {"Other", &second}, {"MissingImage", &first}}})
        {
            const std::string prefix = std::string("Data/Fonts/") + name;
            unchanged_file(*mount, prefix + "TexCoord.Dat", data->coordinates);
            unchanged_file(*mount, prefix + ".Dat", data->kerning);
            if (std::string_view(name) != "MissingImage")
            {
                unchanged_file(*mount, prefix + ".tiff", data->tiff);
            }
        }
        require(constructions == 4 && destructions == 4 && image_owners == 3 && measurements == 9,
            "actual owner or measurement totals differ");
        std::printf("font-data-owner: 3 groups, %u constructions, %u destructions, "
                    "%u images, %u measurements, %u checks; data %zu glyphs %zu kerning %zu\n",
            constructions,
            destructions,
            image_owners,
            measurements,
            checks,
            sizeof(ScreenFontData),
            offsetof(ScreenFontData, glyphs),
            offsetof(ScreenFontData, kerning));
        return 0;
    }
    catch (const Failure&)
    {
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "font-data-owner setup: %s\n", error.what());
        return 3;
    }
}
