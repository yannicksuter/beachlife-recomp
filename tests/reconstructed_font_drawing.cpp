#include "font_owner_test_data.h"
#include "renderer_queue_test_state.h"
#include "startup_screen_text.h"
#include "tga_image.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>

namespace
{
namespace generated = font_owner_test;
using Guard = std::array<unsigned char, 16>;
using Words = std::array<unsigned short, 512>;
using Text = std::array<unsigned short, 8>;

static_assert(std::is_aggregate_v<ScreenRequestRenderer>);
static_assert(std::is_aggregate_v<ScreenSurfacePosition>);
static_assert(std::is_trivially_copyable_v<ScreenFontRenderer>);

const char* current_case = "setup";
unsigned int calls = 0;
unsigned int setters = 0;
unsigned int checks = 0;
unsigned int constructions = 0;
unsigned int destructions = 0;

struct Failure
{
    const char* reason;
};

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        throw Failure{reason};
    }
}

template <typename T> struct Guarded
{
    Guard before{};
    T value{};
    Guard after{};

    Guarded()
    {
        before.fill(0xa6);
        after.fill(0x6a);
    }

    void guards() const
    {
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            require(before[i] == 0xa6 && after[i] == 0x6a, "outside guard changed");
        }
    }
};

// Reuse the owner's metric encoders, extending only the generated TIFF to a
// complete 4x4 allocation. The 2x2 glyph has a real leading row/column prefix.
struct Inputs
{
    generated::Input metrics;
    std::array<unsigned char, 528> tiff{};

    Inputs()
    {
        generated::dword(metrics.coordinates, 0, 2);
        for (unsigned glyph : {65U, 66U, 67U, 68U})
        {
            const std::size_t offset = 8 + glyph * 8;
            generated::word(metrics.coordinates, offset, 1);
            generated::word(metrics.coordinates, offset + 2, 1);
            generated::word(metrics.coordinates, offset + 4, 3);
            generated::word(metrics.coordinates, offset + 6, 3);
        }
        for (unsigned glyph : {32U, 126U})
        {
            const std::size_t offset = 8 + glyph * 8;
            generated::word(metrics.coordinates, offset, 1);
            generated::word(metrics.coordinates, offset + 2, 1);
            generated::word(metrics.coordinates, offset + 4, glyph == 32 ? 2 : 1);
            generated::word(metrics.coordinates, offset + 6, 1);
        }
        std::copy(metrics.tiff.begin(), metrics.tiff.begin() + 512, tiff.begin());
        // Existing eight-entry directory: width, height, bits, compression,
        // samples, offset, rows/strip, byte count. All values fit inline.
        generated::dword(tiff, 18, 4);
        generated::dword(tiff, 30, 4);
        generated::dword(tiff, 90, 4);
        generated::dword(tiff, 102, 16);
        tiff[512 + 5] = 1;
        tiff[512 + 9] = 3;
        tiff[512 + 10] = 1;
    }
};

class GeneratedFiles
{
  public:
    GeneratedFiles(const generated::fs::path& root, const Inputs& input)
        : root_(root)
    {
        generated::host_require(!generated::fs::exists(root_), "generated path already exists");
        generated::fs::create_directories(root_ / "Data/Fonts");
        write("Data/Fonts/DrawingTexCoord.Dat", input.metrics.coordinates);
        write("Data/Fonts/Drawing.tiff", input.tiff);
        write("Data/Fonts/Drawing.Dat", input.metrics.kerning);
    }
    ~GeneratedFiles()
    {
        std::error_code ignored;
        generated::fs::remove_all(root_, ignored);
    }
    GeneratedFiles(const GeneratedFiles&) = delete;
    GeneratedFiles& operator=(const GeneratedFiles&) = delete;

    std::shared_ptr<const beachlife::data::FileSource> mount() const
    {
        return std::make_shared<beachlife::data::FolderSource>(root_, entries_);
    }

  private:
    struct Close
    {
        void operator()(FILE* file) const
        {
            std::fclose(file);
        }
    };
    void write(const std::string& name, std::span<const unsigned char> bytes)
    {
        const auto path = root_ / name;
        std::unique_ptr<FILE, Close> file(std::fopen(path.string().c_str(), "wb"));
        generated::host_require(file != nullptr, "generated file open failed");
        generated::host_require(
            std::fwrite(bytes.data(), 1, bytes.size(), file.get()) == bytes.size(),
            "generated file write failed");
        generated::host_require(std::fflush(file.get()) == 0, "generated file flush failed");
        entries_.push_back({name, bytes.size()});
    }

    generated::fs::path root_;
    std::vector<beachlife::data::SourceEntry> entries_;
};

struct Owner
{
    Guard before{};
    std::optional<ScreenFontData> value;
    Guard after{};

    Owner()
    {
        before.fill(0xb6);
        after.fill(0x6b);
        value.emplace("dRaWiNg");
        ++constructions;
    }
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;

    void guards() const
    {
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            require(before[i] == 0xb6 && after[i] == 0x6b, "font owner guard changed");
        }
    }
    void close()
    {
        require(value.has_value(), "duplicate owner destruction");
        value.reset();
        ++destructions;
        guards();
    }
};

struct Environment
{
    Guarded<ScreenRequestRenderer> renderer;
    Guarded<ScreenSurfacePosition> surface;
    Guarded<std::array<ScreenSurfacePosition*, 1>> table;
    Guarded<Words> output;
    ScreenRequestRenderer* previous = screen_request_renderer;

    Environment()
    {
        renderer_queue_test::initialize(renderer.value, 0x35);
        table.value[0] = &surface.value;
        screen_request_renderer = &renderer.value;
    }
    ~Environment()
    {
        screen_request_renderer = previous;
    }

    void reset(bool software_surface, RenderRectangle clip, bool null_pixels = false)
    {
        auto& r = renderer.value;
        r.software_mode = 1;
        r.width = 16;
        r.height = 16;
        r.clip_rectangle = clip;
        r.format_44 = 0;
        r.unscaled_clip_5c = software_surface ? 1 : 0;
        r.pixels_1c = output.value.data();
        r.surfaces = table.value.data();
        r.current_surface = 0;
        r.pixels_locked = 0x6d;
        surface.value = {};
        surface.value.active = 1;
        surface.value.software = 1;
        surface.value.pixels = null_pixels ? nullptr : output.value.data();
        surface.value.width = 16;
        surface.value.height = 16;
        surface.value.pitch = 36;
        surface.value.position = {3, 4};
        surface.value.prepared = 0x73;
        output.value.fill(0x7777);
        if (!software_surface)
        {
            for (std::size_t i = 1; i < output.value.size(); i += 2)
            {
                output.value[i] = 0xaaaa;
            }
        }
    }
};

struct Font
{
    Guard before{};
    ScreenFontRenderer value;
    Guard after{};

    explicit Font(ScreenFontData& data)
        : value(&data, nullptr)
    {
        before.fill(0xc6);
        after.fill(0x6c);
        value.unknown_15[0] = 0x37;
        value.unknown_15[1] = 0x59;
        value.unknown_15[2] = 0x7b;
    }

    void configure(RenderRectangle bounds, unsigned alignment, unsigned char state)
    {
        value.SetRectangle(bounds.x, bounds.y, bounds.width, bounds.height);
        value.SetWord10(0xab2f8040U);
        value.SetState14(state);
        value.SetWord18(alignment);
        setters += 4;
        require(value.rectangle.x == bounds.x && value.rectangle.y == bounds.y &&
                    value.rectangle.width == bounds.width &&
                    value.rectangle.height == bounds.height && value.word_10 == 0xab2f8040U &&
                    value.word_18 == alignment && value.state_14 == state,
            "actual setting method result differs");
    }
};

bool same_surface(const ScreenSurfacePosition& a, const ScreenSurfacePosition& b)
{
    return a.active == b.active && a.software == b.software &&
           std::equal(std::begin(a.unknown_02), std::end(a.unknown_02), std::begin(b.unknown_02)) &&
           a.pixels == b.pixels && a.surface == b.surface &&
           std::equal(std::begin(a.unknown_0c), std::end(a.unknown_0c), std::begin(b.unknown_0c)) &&
           a.width == b.width && a.height == b.height && a.pitch == b.pitch &&
           a.position.x == b.position.x && a.position.y == b.position.y && a.prepared == b.prepared;
}

struct Pixel
{
    std::size_t x;
    std::size_t y;
    unsigned short value;
};

void invoke(Environment& environment,
    Font& font,
    const Text& input,
    bool wrapped,
    int x,
    int y,
    std::span<const Pixel> pixels,
    bool null_surface = false)
{
    Guarded<Text> text;
    text.value = input;
    require(text.value[0] == 0, "text prefix differs");
    const auto before_font = font.value;
    auto expected_renderer = environment.renderer.value;
    auto expected_surface = environment.surface.value;
    const bool surface = expected_renderer.unscaled_clip_5c != 0;
    if (surface)
    {
        expected_renderer.pixels_locked = null_surface ? 1 : 0;
        expected_surface.prepared = 0;
    }
    Words expected = environment.output.value;
    for (const Pixel& pixel : pixels)
    {
        const std::size_t index = surface ? pixel.y * 18 + pixel.x : (pixel.y * 16 + pixel.x) * 2;
        require(index < expected.size(), "literal pixel outside fixture");
        expected[index] = pixel.value;
    }

    if (wrapped)
    {
        font.value.DrawText(x, y, text.value.data() + 1);
    }
    else
    {
        font.value.DrawTextLine(x, y, text.value.data() + 1);
    }
    ++calls;

    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        require(environment.output.value[i] == expected[i], "literal pixel differs");
    }
    require(renderer_queue_test::equal(environment.renderer.value, expected_renderer),
        "renderer state differs");
    require(same_surface(environment.surface.value, expected_surface), "surface state differs");
    require(environment.table.value[0] == &environment.surface.value, "surface table changed");
    require(screen_request_renderer == &environment.renderer.value, "renderer global changed");
    require(font.value.rectangle.x == before_font.rectangle.x &&
                font.value.rectangle.y == before_font.rectangle.y &&
                font.value.rectangle.width == before_font.rectangle.width &&
                font.value.rectangle.height == before_font.rectangle.height &&
                font.value.word_10 == before_font.word_10 &&
                font.value.state_14 == before_font.state_14 &&
                font.value.word_18 == before_font.word_18 &&
                font.value.data_1c == before_font.data_1c &&
                font.value.lookup_20 == before_font.lookup_20,
        "font member changed");
    for (std::size_t i = 0; i < sizeof(font.value.unknown_15); ++i)
    {
        require(font.value.unknown_15[i] == before_font.unknown_15[i], "font opaque byte changed");
    }
    require(text.value == input, "text changed");
    for (std::size_t i = 0; i < font.before.size(); ++i)
    {
        require(font.before[i] == 0xc6 && font.after[i] == 0x6c, "font guard changed");
    }
    text.guards();
    environment.renderer.guards();
    environment.surface.guards();
    environment.table.guards();
    environment.output.guards();
}

struct DataSnapshot
{
    generated::Snapshot data;
    const unsigned char* pixels;
    std::array<unsigned char, 16> bytes{};

    explicit DataSnapshot(const ScreenFontData& owner)
        : data(owner)
        , pixels(owner.image->pixels)
    {
        std::copy_n(pixels, bytes.size(), bytes.begin());
    }

    void compare(const Owner& owner) const
    {
        const auto& live = *owner.value;
        require(live.image == data.image && live.image->pixels == pixels,
            "image owner identity changed");
        require(live.image->width == 4 && live.image->height == 4 &&
                    live.image->bits_per_pixel == 8 && live.image->bytes_per_pixel == 1 &&
                    live.image->palette == nullptr && live.image->converted_palette == nullptr,
            "image metadata changed");
        require(std::equal(bytes.begin(), bytes.end(), pixels), "owned atlas bytes changed");
        for (std::size_t i = 0; i < 4; ++i)
        {
            require(live.image->unknown_1c[i] == 0, "image tail changed");
        }
        for (std::size_t i = 0; i < 256; ++i)
        {
            require(std::equal(std::begin(live.glyphs[i]),
                        std::end(live.glyphs[i]),
                        std::begin(data.glyphs[i])),
                "glyph metrics changed");
        }
        require(
            std::equal(std::begin(live.kerning), std::end(live.kerning), std::begin(data.kerning)),
            "kerning changed");
        require(live.unknown_10804[0] == data.unknown_10804[0] &&
                    live.unknown_10804[1] == data.unknown_10804[1],
            "font height words changed");
        owner.guards();
    }
};

void direct_group(Environment& environment, Font& font, Owner& owner)
{
    owner.value->kerning[65 * 255 + 66] = -1;
    const DataSnapshot before(*owner.value);
    constexpr Text pair{0, 65, 66, 0, 0, 0, 0, 0};
    constexpr Text one{0, 65, 0, 0, 0, 0, 0, 0};
    constexpr std::array<Pixel, 5> center{
        {{5, 3, 0x2c08}, {6, 3, 0x2c08}, {5, 4, 0x2c08}, {6, 4, 0x2c08}, {7, 4, 0x2c08}}};
    constexpr std::array<Pixel, 5> right{
        {{9, 3, 0x2c08}, {10, 3, 0x2c08}, {9, 4, 0x2c08}, {10, 4, 0x2c08}, {11, 4, 0x2c08}}};
    constexpr std::array<Pixel, 1> clipped{{{2, 2, 0x2c08}}};

    current_case = "direct-center";
    environment.reset(false, {0, 0, 16, 16});
    font.configure({2, 2, 10, 10}, 2, 0);
    invoke(environment, font, pair, false, 0, 1, center);

    current_case = "direct-right";
    environment.reset(false, {0, 0, 16, 16});
    font.configure({2, 2, 10, 10}, 1, 0);
    invoke(environment, font, pair, false, 0, 1, right);

    current_case = "direct-clip";
    environment.reset(false, {2, 2, 1, 1});
    font.configure({1, 1, 10, 10}, 0, 0);
    invoke(environment, font, one, false, 0, 0, clipped);

    current_case = "direct-equal-edge";
    environment.reset(false, {2, 2, 1, 1});
    font.configure({3, 2, 10, 10}, 0, 0);
    invoke(environment, font, one, false, 0, 0, {});
    before.compare(owner);
}

void surface_group(Environment& environment, Font& font, Owner& owner)
{
    const DataSnapshot before(*owner.value);
    constexpr Text one{0, 65, 0, 0, 0, 0, 0, 0};
    constexpr Text empty{};
    constexpr std::array<Pixel, 3> pixels{{{3, 3, 0xffff}, {3, 4, 0}, {4, 4, 0xffff}}};

    current_case = "software-surface";
    environment.reset(true, {0, 0, 16, 16});
    font.configure({5, 6, 8, 8}, 0, 255);
    invoke(environment, font, one, false, 1, 1, pixels);

    current_case = "software-empty";
    environment.reset(true, {0, 0, 16, 16});
    font.configure({5, 6, 8, 8}, 0, 255);
    invoke(environment, font, empty, false, 1, 1, {});

    current_case = "software-null-pixels";
    environment.reset(true, {0, 0, 16, 16}, true);
    font.configure({5, 6, 8, 8}, 0, 255);
    invoke(environment, font, one, false, 1, 1, {}, true);
    before.compare(owner);
}

void wrapping_group(Environment& environment, Font& font, Owner& owner)
{
    owner.value->kerning[65 * 255 + 66] = 0;
    const DataSnapshot before(*owner.value);
    constexpr Text words{0, 65, 66, 32, 67, 68, 0, 0};
    constexpr Text forced{0, 65, 66, 126, 67, 68, 0, 0};
    constexpr std::array<Pixel, 12> two_lines{{{2, 2, 0x2c08},
        {4, 2, 0x2c08},
        {2, 3, 0x2c08},
        {3, 3, 0x2c08},
        {4, 3, 0x2c08},
        {5, 3, 0x2c08},
        {2, 6, 0x2c08},
        {4, 6, 0x2c08},
        {2, 7, 0x2c08},
        {3, 7, 0x2c08},
        {4, 7, 0x2c08},
        {5, 7, 0x2c08}}};
    constexpr std::array<Pixel, 6> first_line{{{2, 2, 0x2c08},
        {4, 2, 0x2c08},
        {2, 3, 0x2c08},
        {3, 3, 0x2c08},
        {4, 3, 0x2c08},
        {5, 3, 0x2c08}}};

    current_case = "word-wrap";
    environment.reset(false, {0, 0, 16, 16});
    font.configure({2, 0, 5, 14}, 0, 0);
    invoke(environment, font, words, true, 0, 2, two_lines);

    current_case = "forced-wrap";
    environment.reset(false, {0, 0, 16, 16});
    font.configure({2, 0, 5, 14}, 0, 0);
    invoke(environment, font, forced, true, 0, 2, two_lines);

    current_case = "wrap-height-stop";
    environment.reset(false, {0, 0, 16, 16});
    font.configure({2, 0, 5, 7}, 0, 0);
    invoke(environment, font, words, true, 0, 2, first_line);
    before.compare(owner);
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        generated::host_require(argc == 2, "expected a fresh generated-font directory");
        const Inputs input;
        GeneratedFiles files(argv[1], input);
        const auto mount = files.mount();
        generated::files::Session session(mount);
        Owner owner;
        require(owner.value->image != nullptr && owner.value->image->pixels != nullptr,
            "real font image missing");
        require(owner.value->image->width == 4 && owner.value->image->height == 4 &&
                    owner.value->unknown_10804[0] == 2 && owner.value->unknown_10804[1] == 9,
            "generated font dimensions differ");
        require(std::equal(input.tiff.begin() + 512, input.tiff.end(), owner.value->image->pixels),
            "loaded glyph atlas differs");
        ScreenRequestRenderer* previous = screen_request_renderer;
        {
            Environment environment;
            environment.reset(false, {0, 0, 16, 16});
            Font font(*owner.value);
            require(font.value.data_1c == &*owner.value && font.value.lookup_20 == nullptr,
                "real font renderer borrowed identities differ");
            direct_group(environment, font, owner);
            surface_group(environment, font, owner);
            wrapping_group(environment, font, owner);
        }
        require(screen_request_renderer == previous, "renderer global restoration differs");
        owner.close();
        require(calls == 10 && setters == 40 && constructions == 1 && destructions == 1,
            "bounded call or lifetime count differs");
        std::printf(
            "font-drawing groups=3 calls=%u setters=%u owners=%u destroyed=%u checks=%u pass\n",
            calls,
            setters,
            constructions,
            destructions,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "font-drawing %s: %s\n", current_case, failure.reason);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "font-drawing host failure: %s\n", failure.what());
        return 3;
    }
}
