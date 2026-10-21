#include "font_owner_test_data.h"
#include "renderer_queue_test_state.h"
#include "startup_screen_manager.h"
#include "startup_screen_text.h"
#include "startup_screen_text_item.h"
#include "text_lookup_owner_test_data.h"
#include "tga_image.h"

#include <array>
#include <clocale>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <type_traits>

namespace
{
namespace generated = font_owner_test;
using Guard = std::array<unsigned char, 16>;
using Words = std::array<unsigned short, 512>;
using Text = std::array<unsigned short, 8>;

static_assert(sizeof(int) == 4);
static_assert(std::is_aggregate_v<ScreenManager>);
static_assert(std::is_aggregate_v<ScreenRequestRenderer>);
static_assert(std::is_polymorphic_v<ScreenTextItem>);
static_assert(std::is_same_v<decltype(&ScreenTextItem::Unresolved24),
    void (ScreenTextItem::*)(unsigned char)>);

const char* current_case = "setup";
unsigned int checks = 0;
unsigned int calls = 0;
unsigned int constructions = 0;
unsigned int destructions = 0;
unsigned int pixel_words = 0;

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
        require(std::all_of(before.begin(),
                    before.end(),
                    [](auto c)
                    {
                        return c == 0xa6;
                    }) &&
                    std::all_of(after.begin(),
                        after.end(),
                        [](auto c)
                        {
                            return c == 0x6a;
                        }),
            "outside guard changed");
    }
};

// A genuine owner with a scoped borrowed table view for these method cases.
template <> struct Guarded<ScreenTextLookup>
{
    text_owner_test::Bootstrap bootstrap;
    Guard before{};
    ScreenTextLookup value{"owner.txt"};
    Guard after{};

    Guarded()
    {
        before.fill(0xa6);
        after.fill(0x6a);
    }
    void guards() const
    {
        require(std::all_of(before.begin(),
                    before.end(),
                    [](auto c)
                    {
                        return c == 0xa6;
                    }) &&
                    std::all_of(after.begin(),
                        after.end(),
                        [](auto c)
                        {
                            return c == 0x6a;
                        }),
            "outside guard changed");
    }
    text_owner_test::RestoreOwnedTable restore{value, bootstrap};
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

// These aggregates are borrowed read models, never full game-owner lifetimes.
// Every reconstructed TU uses the same current prepared declarations.
struct Environment
{
    Guarded<ScreenRequestRenderer> renderer;
    Guarded<ScreenManager> manager;
    Guarded<ScreenTextLookup> lookup;
    Guarded<std::array<ScreenTextBucket, 2>> buckets;
    Guarded<ScreenTextValue> value;
    Guarded<std::array<char, 6>> key;
    Guarded<Text> borrowed;
    Guarded<Words> output;
    ScreenRequestRenderer* old_renderer = screen_request_renderer;
    ScreenManager* old_manager = screen_manager;
    ScreenTextLookup* old_lookup = screen_text_lookup;

    Environment()
    {
        renderer_queue_test::initialize(renderer.value, 0x35);
        auto& r = renderer.value;
        r.software_mode = 1;
        r.width = 16;
        r.height = 16;
        r.clip_rectangle = {0, 0, 16, 16};
        r.format_44 = 0;
        r.unscaled_clip_5c = 0;
        r.pixels_1c = output.value.data();
        r.surfaces = nullptr;
        r.current_surface = 0;
        manager.value.renderer = &r;
        manager.value.item_134 = nullptr;
        manager.value.auxiliary_130 = nullptr;
        manager.value.value_138 = 7.5f;
        key.value = {'L', 'a', 'b', 'e', 'l', 0};
        borrowed.value = {0, 65, 0, 0, 0, 0, 0, 0};
        value.value = {key.value.data(), borrowed.value.data() + 1};
        buckets.value[0] = text_owner_test::bucket(key.value.data(), &value.value, 0);
        buckets.value[1] = text_owner_test::bucket(nullptr, nullptr, 1);
        lookup.value.end_index = 1;
        lookup.value.bucket_count = 1;
        lookup.value.capacity_04 = 2;
        lookup.value.buckets = buckets.value.data();
        screen_request_renderer = &r;
        screen_manager = &manager.value;
        screen_text_lookup = &lookup.value;
        clear_pixels();
    }
    ~Environment()
    {
        screen_text_lookup = old_lookup;
        screen_manager = old_manager;
        screen_request_renderer = old_renderer;
    }
    void clear_pixels()
    {
        output.value.fill(0x7777);
        for (std::size_t i = 1; i < output.value.size(); i += 2)
        {
            output.value[i] = 0xaaaa;
        }
    }
};

struct Font
{
    Guard before{};
    ScreenFontRenderer value;
    Guard after{};

    Font(ScreenFontData& owner, ScreenTextLookup& lookup)
        : value(&owner, &lookup)
    {
        before.fill(0xc6);
        after.fill(0x6c);
        value.unknown_15[0] = 0x37;
        value.unknown_15[1] = 0x59;
        value.unknown_15[2] = 0x7b;
    }
};

struct ItemSlot
{
    Guard before{};
    alignas(ScreenTextItem) std::array<unsigned char, sizeof(ScreenTextItem)> storage{};
    Guard after{};
    ScreenTextItem* item = nullptr;

    ItemSlot(ScreenRectangle rectangle,
        const char* key,
        int value,
        int flags,
        const unsigned short* expected_text)
    {
        before.fill(0xb6);
        after.fill(0x6b);
        // Observable pre-lifetime setup; never pattern a live object or its vptr.
        volatile unsigned char* bytes = storage.data();
        for (std::size_t i = 0; i < storage.size(); ++i)
        {
            bytes[i] = 0x5a;
        }
        item = ::new (static_cast<void*>(storage.data()))
            ScreenTextItem(rectangle, key, 3, value, flags);
        ++constructions;
        require(item->kind_28 == 12 && item->font_index == 3 && item->word_50 == value &&
                    item->word_54 == flags && item->state_58 == 1 &&
                    item->owned_text_48 == nullptr && item->borrowed_text == expected_text,
            "real Text constructor differs");
        require(renderer_queue_test::same_rectangle(item->rectangle, rectangle) &&
                    renderer_queue_test::same_rectangle(item->saved_rectangle, rectangle) &&
                    item->elapsed_24 == 0.0f && item->state_30 == 0 && item->last_x == 0 &&
                    item->last_y == 0 && item->owned_text == nullptr && item->state_40 == 0 &&
                    item->state_41 == 0 && item->state_42 == 0 && item->preparation_flag == 1,
            "real base constructor differs");
        for (std::size_t i = 0; i < 3; ++i)
        {
            require(item->unknown_31[i] == 0x5a && item->unknown_59[i] == 0x5a,
                "preserved opaque byte differs");
        }
        // The untouched unsigned word gets a valid typed value before snapshots.
        item->word_2c = 0x13572468U;
    }
    ~ItemSlot()
    {
        if (item)
        {
            std::destroy_at(item);
        }
    }
    ItemSlot(const ItemSlot&) = delete;
    ItemSlot& operator=(const ItemSlot&) = delete;
    void close()
    {
        require(item != nullptr, "duplicate Text destruction");
        std::destroy_at(item);
        item = nullptr;
        ++destructions;
        guards();
    }
    void guards() const
    {
        require(std::all_of(before.begin(),
                    before.end(),
                    [](auto c)
                    {
                        return c == 0xb6;
                    }) &&
                    std::all_of(after.begin(),
                        after.end(),
                        [](auto c)
                        {
                            return c == 0x6b;
                        }),
            "Text outside guard changed");
    }
};

// Copies explicit members only: no Text copy constructor, vptr or padding oracle.
struct ItemState
{
    ScreenRectangle rectangle;
    ScreenRectangle saved;
    float elapsed;
    int kind;
    unsigned int word;
    unsigned char enabled;
    int x;
    int y;
    char* base_text;
    std::array<unsigned char, 4> states;
    const unsigned short* borrowed;
    unsigned short* owned;
    int font;
    int colour;
    int flags;
    unsigned char phase;
    std::array<unsigned char, 3> opaque31;
    std::array<unsigned char, 3> opaque59;

    explicit ItemState(const ScreenTextItem& v)
        : rectangle(v.rectangle)
        , saved(v.saved_rectangle)
        , elapsed(v.elapsed_24)
        , kind(v.kind_28)
        , word(v.word_2c)
        , enabled(v.state_30)
        , x(v.last_x)
        , y(v.last_y)
        , base_text(v.owned_text)
        , states{v.state_40, v.state_41, v.state_42, v.preparation_flag}
        , borrowed(v.borrowed_text)
        , owned(v.owned_text_48)
        , font(v.font_index)
        , colour(v.word_50)
        , flags(v.word_54)
        , phase(v.state_58)
        , opaque31{v.unknown_31[0], v.unknown_31[1], v.unknown_31[2]}
        , opaque59{v.unknown_59[0], v.unknown_59[1], v.unknown_59[2]}
    {
    }
    void verify(const ScreenTextItem& v) const
    {
        require(v.preparation_flag == states[3], "preparation flag differs");
        require(renderer_queue_test::same_rectangle(v.rectangle, rectangle) &&
                    renderer_queue_test::same_rectangle(v.saved_rectangle, saved) &&
                    v.elapsed_24 == elapsed && v.kind_28 == kind && v.word_2c == word &&
                    v.state_30 == enabled && v.last_x == x && v.last_y == y &&
                    v.owned_text == base_text && v.state_40 == states[0] &&
                    v.state_41 == states[1] && v.state_42 == states[2] &&
                    v.borrowed_text == borrowed && v.owned_text_48 == owned &&
                    v.font_index == font && v.word_50 == colour && v.word_54 == flags &&
                    v.state_58 == phase,
            "Text member changed");
        for (std::size_t i = 0; i < 3; ++i)
        {
            require(v.unknown_31[i] == opaque31[i] && v.unknown_59[i] == opaque59[i],
                "Text opaque byte changed");
        }
    }
};

struct Pixel
{
    std::size_t x;
    std::size_t y;
    unsigned short value;
};
constexpr std::array<Pixel, 3> borrowed_pixels{{{2, 2, 0x2c08}, {2, 3, 0x2c08}, {3, 3, 0x2c08}}};
constexpr std::array<Pixel, 6> fallback_pixels{
    {{8, 2, 0xffff}, {8, 3, 0}, {9, 3, 0xffff}, {10, 2, 0xffff}, {10, 3, 0}, {11, 3, 0xffff}}};
constexpr std::array<unsigned short, 6> fallback_text{65, 66, 0, 0x6a6a, 0x7b7b, 0x8c8c};

void adopt_fallback(ScreenTextItem& item)
{
    // Real destructor uses scalar delete. Keep the allocation start as owner.
    // C++20 memcpy begins the implicit unsigned-short array lifetime.
    auto* allocation = static_cast<unsigned short*>(::operator new(sizeof(fallback_text)));
    std::memcpy(allocation, fallback_text.data(), sizeof(fallback_text));
    item.owned_text_48 = allocation;
}

void pixels(Environment& env, std::span<const Pixel> literal)
{
    Words expected;
    expected.fill(0x7777);
    for (std::size_t i = 1; i < expected.size(); i += 2)
    {
        expected[i] = 0xaaaa;
    }
    for (const Pixel& p : literal)
    {
        expected[(p.y * 16 + p.x) * 2] = p.value;
    }
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        require(env.output.value[i] == expected[i], "literal pixel differs");
        ++pixel_words;
    }
    env.output.guards();
}

void font_state(const Font& font,
    ScreenFontData& data,
    Environment& env,
    RenderRectangle rectangle,
    unsigned int colour,
    unsigned char state,
    unsigned int alignment)
{
    const auto& v = font.value;
    require(v.rectangle.x == rectangle.x && v.rectangle.y == rectangle.y &&
                v.rectangle.width == rectangle.width && v.rectangle.height == rectangle.height &&
                v.word_10 == colour && v.state_14 == state && v.word_18 == alignment &&
                v.data_1c == &data && v.lookup_20 == &env.lookup.value && v.unknown_15[0] == 0x37 &&
                v.unknown_15[1] == 0x59 && v.unknown_15[2] == 0x7b,
        "real font settings differ");
    require(std::all_of(font.before.begin(),
                font.before.end(),
                [](auto c)
                {
                    return c == 0xc6;
                }) &&
                std::all_of(font.after.begin(),
                    font.after.end(),
                    [](auto c)
                    {
                        return c == 0x6c;
                    }),
        "font guard changed");
}

void prepared_group(Environment& env, Font& font, ScreenFontData& data)
{
    current_case = "prepared-borrowed";
    ItemSlot slot(
        {2, 2, 12, 10}, "lAbEl", static_cast<int>(0xab2f8040U), 2, env.borrowed.value.data() + 1);
    auto& item = *slot.item;
    adopt_fallback(item);
    item.SetOwnedText("base-owned");
    require(item.owned_text != nullptr && std::strcmp(item.owned_text, "base-owned") == 0,
        "real base text ownership differs");
    ItemState expected(item);
    expected.states[3] = 0;
    env.clear_pixels();
    static_cast<ScreenItem&>(item).Unresolved0c();
    ++calls;
    expected.verify(item);
    pixels(env, borrowed_pixels);
    font_state(font, data, env, {2, 2, 10, 8}, 0xab2f8040U, 0, 0);

    current_case = "already-prepared";
    env.clear_pixels();
    static_cast<ScreenItem&>(item).Unresolved0c();
    ++calls;
    expected.verify(item);
    pixels(env, {});
    font_state(font, data, env, {2, 2, 10, 8}, 0xab2f8040U, 0, 0);

    current_case = "real-activation";
    item.elapsed_24 = 4.0f;
    expected.elapsed = 0.0f;
    static_cast<ScreenItem&>(item).Unresolved24(0);
    ++calls;
    expected.verify(item);
    static_cast<ScreenItem&>(item).Unresolved24(255);
    ++calls;
    expected.enabled = 255;
    expected.states[3] = 1;
    expected.verify(item);
    static_cast<ScreenItem&>(item).Unresolved0c();
    ++calls;
    expected.states[3] = 0;
    expected.verify(item);
    pixels(env, borrowed_pixels);
    require(std::equal(fallback_text.begin(), fallback_text.end(), item.owned_text_48),
        "unselected owned text changed");
    slot.close();
    require(env.borrowed.value == Text{0, 65, 0, 0, 0, 0, 0, 0},
        "borrowed text did not survive destruction");
}

void alternate_group(Environment& env, Font& font, ScreenFontData& data)
{
    current_case = "alternate-inactive";
    // Height2 takes actual DrawText's short-height path. This scalar-owned
    // string has no fictitious readable word before its allocation start.
    ItemSlot slot({2, 2, 12, 4}, nullptr, static_cast<int>(0xab2f8040U), 0x47, nullptr);
    auto& item = *slot.item;
    adopt_fallback(item);
    ItemState expected(item);
    env.clear_pixels();
    static_cast<ScreenItem&>(item).Unresolved1c();
    ++calls;
    expected.verify(item);
    pixels(env, {});
    font_state(font, data, env, {2, 2, 10, 8}, 0xab2f8040U, 0, 0);

    current_case = "alternate-owned-fallback";
    item.state_58 = 0;
    expected.phase = 0;
    static_cast<ScreenItem&>(item).Unresolved1c();
    ++calls;
    expected.verify(item);
    pixels(env, fallback_pixels);
    font_state(font, data, env, {2, 2, 10, 2}, 0xab2f8040U, 1, 1);
    require(std::equal(fallback_text.begin(), fallback_text.end(), item.owned_text_48),
        "owned text or trailing guard changed");

    current_case = "prepared-other-phase";
    env.clear_pixels();
    static_cast<ScreenItem&>(item).Unresolved0c();
    ++calls;
    expected.verify(item);
    pixels(env, {});
    slot.close();
}

void settings_group(Environment& env, Font& font, ScreenFontData& data)
{
    current_case = "settings-priority";
    ItemSlot slot({2, 2, 12, 4}, "absent", -1, 4, screen_text_empty);
    auto& item = *slot.item;
    ItemState expected(item);
    env.clear_pixels();
    item.ApplyFontSettings();
    ++calls;
    expected.verify(item);
    font_state(font, data, env, {2, 2, 10, 2}, 0xffffffffU, 0, 2);

    current_case = "alignment-preserved";
    item.word_54 = 0x40;
    expected.flags = 0x40;
    item.ApplyFontSettings();
    ++calls;
    expected.verify(item);
    font_state(font, data, env, {2, 2, 10, 2}, 0xffffffffU, 1, 2);

    current_case = "empty-lookup-result";
    static_cast<ScreenItem&>(item).Unresolved0c();
    ++calls;
    expected.states[3] = 0;
    expected.verify(item);
    pixels(env, {});
    slot.close();
}

bool same_manager(const ScreenManager& a, const ScreenManager& b)
{
    return a.renderer == b.renderer && a.registry == b.registry && a.active.head == b.active.head &&
           a.mode == b.mode && a.previous_mode == b.previous_mode &&
           a.auxiliary_130 == b.auxiliary_130 && a.item_134 == b.item_134 &&
           a.value_138 == b.value_138 &&
           std::equal(std::begin(a.fonts_2c), std::end(a.fonts_2c), std::begin(b.fonts_2c)) &&
           std::equal(std::begin(a.fonts_04), std::end(a.fonts_04), std::begin(b.fonts_04)) &&
           renderer_queue_test::same_bytes(a.unknown_54, b.unknown_54) && a.item_90 == b.item_90 &&
           renderer_queue_test::same_bytes(a.unknown_94, b.unknown_94) &&
           renderer_queue_test::same_bytes(a.unknown_c0, b.unknown_c0);
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        generated::host_require(argc == 2, "expected a fresh generated-font directory");
        generated::host_require(std::setlocale(LC_CTYPE, "C") != nullptr, "C locale unavailable");
        const Inputs input;
        GeneratedFiles files(argv[1], input);
        const auto mount = files.mount();
        generated::files::Session session(mount);
        Environment env;
        std::optional<ScreenFontData> owner;
        owner.emplace("dRaWiNg");
        require(owner->image && owner->image->pixels && owner->image->width == 4 &&
                    owner->image->height == 4 && owner->unknown_10804[0] == 2,
            "real font image/metrics differ");
        const generated::Snapshot original_data(*owner);
        Font font(*owner, env.lookup.value);
        env.manager.value.fonts_2c[3] = &font.value;
        const auto manager_before = env.manager.value;
        const auto renderer_before = env.renderer.value;
        const text_owner_test::Snapshot lookup_before(env.lookup.value);
        const auto buckets_before = env.buckets.value;
        prepared_group(env, font, *owner);
        alternate_group(env, font, *owner);
        settings_group(env, font, *owner);
        require(calls == 11 && constructions == 3 && destructions == 3,
            "bounded call/lifetime totals differ");
        require(same_manager(env.manager.value, manager_before) &&
                    renderer_queue_test::equal(env.renderer.value, renderer_before),
            "borrowed manager/renderer changed");
        require(text_owner_test::equal(env.lookup.value, lookup_before),
            "language owner or borrowed view changed");
        for (std::size_t i = 0; i < env.buckets.value.size(); ++i)
        {
            const auto& a = env.buckets.value[i];
            const auto& b = buckets_before[i];
            require(a.key == b.key && a.value == b.value && a.unavailable == b.unavailable,
                "lookup bucket changed");
        }
        require(env.key.value == std::array<char, 6>{'L', 'a', 'b', 'e', 'l', 0} &&
                    env.borrowed.value == Text{0, 65, 0, 0, 0, 0, 0, 0} &&
                    env.value.value.key == env.key.value.data() &&
                    env.value.value.text == env.borrowed.value.data() + 1 &&
                    screen_text_empty[0] == 0,
            "borrowed strings/lookup value changed");
        require(screen_manager == &env.manager.value && screen_text_lookup == &env.lookup.value &&
                    screen_request_renderer == &env.renderer.value,
            "real global owner changed");
        require(owner->image == original_data.image &&
                    std::equal(input.tiff.begin() + 512, input.tiff.end(), owner->image->pixels),
            "owned image changed");
        for (std::size_t i = 0; i < 256; ++i)
        {
            require(std::equal(std::begin(owner->glyphs[i]),
                        std::end(owner->glyphs[i]),
                        std::begin(original_data.glyphs[i])),
                "font glyph metrics changed");
        }
        require(std::equal(std::begin(owner->kerning),
                    std::end(owner->kerning),
                    std::begin(original_data.kerning)) &&
                    std::equal(std::begin(owner->unknown_10804),
                        std::end(owner->unknown_10804),
                        std::begin(original_data.unknown_10804)),
            "font kerning/height changed");
        env.renderer.guards();
        env.manager.guards();
        env.lookup.guards();
        env.buckets.guards();
        env.value.guards();
        env.key.guards();
        env.borrowed.guards();
        owner.reset();
        std::printf("gui-text-item groups=3 calls=%u constructed=%u destroyed=%u pixel_words=%u "
                    "checks=%u pass\n",
            calls,
            constructions,
            destructions,
            pixel_words,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "gui-text-item %s: %s\n", current_case, failure.reason);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "gui-text-item host failure: %s\n", failure.what());
        return 3;
    }
}
