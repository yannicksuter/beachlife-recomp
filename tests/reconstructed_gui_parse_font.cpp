#include "font_owner_test_data.h"
#include "renderer_queue_test_state.h"
#include "startup_gui_parser.h"
#include "startup_screen_manager.h"
#include "startup_screen_text.h"
#include "tga_image.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>

namespace
{
namespace generated = font_owner_test;
static_assert(sizeof(void*) == 8 && sizeof(unsigned int) == 4);
static_assert(std::is_aggregate_v<ScreenManager> && std::is_standard_layout_v<ScreenManager>);
static_assert(std::is_aggregate_v<GuiParserState> && std::is_standard_layout_v<GuiParserState>);
static_assert(std::is_same_v<decltype(ScreenManager::fonts_04), ScreenFontData* [10]>);
static_assert(std::is_same_v<decltype(ScreenManager::fonts_2c), ScreenFontRenderer* [10]>);
static_assert(offsetof(ScreenManager, fonts_04) == 8);
static_assert(offsetof(ScreenManager, fonts_2c) == 88);
static_assert(offsetof(ScreenManager, unknown_54) == 168 && sizeof(ScreenManager) == 424);
static_assert(offsetof(GuiParserState, word_38) == 112);
static_assert(offsetof(GuiParserState, word_3c) == 116 && sizeof(GuiParserState) == 120);
static_assert(sizeof(ScreenFontData) == 67600 && sizeof(ScreenFontRenderer) == 48);
static_assert(std::is_trivially_destructible_v<ScreenFontRenderer>);

struct Failure
{
    const char* reason;
};
unsigned checks = 0;
unsigned calls = 0;
unsigned released_data = 0;
unsigned released_renderers = 0;
unsigned observed_publications = 0;
const char* group = "setup";

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
    std::array<unsigned char, 16> before;
    T value{};
    std::array<unsigned char, 16> after;

    Guarded()
    {
        before.fill(0x3d);
        after.fill(0xa7);
    }
    void verify() const
    {
        require(std::all_of(before.begin(),
                    before.end(),
                    [](auto byte)
                    {
                        return byte == 0x3d;
                    }) &&
                    std::all_of(after.begin(),
                        after.end(),
                        [](auto byte)
                        {
                            return byte == 0xa7;
                        }),
            "outside guard differs");
    }
};

// Typed aggregate prefixes are borrowed subsystem models, not complete retail
// manager/parser lifetimes. Every published font here has a genuine lifetime.
struct Model
{
    Guarded<ScreenManager> manager;
    Guarded<GuiParserState> parser;

    Model()
    {
        parser.value.manager = &manager.value;
        parser.value.word_38 = 0x13579bdfU;
#if defined(BEACHLIFE_DECOMP_DEFINED_ITEMS)
        parser.value.defined_items = nullptr;
#else
        std::fill(std::begin(parser.value.unknown_24), std::end(parser.value.unknown_24), 0x65);
#endif
    }
    ~Model()
    {
        // These finite cases publish distinct owners into initially empty slots.
        // Destroy borrowers first, then the real FontData -> TgaImage ownership.
        for (unsigned i = 0; i < 10; ++i)
        {
            auto* renderer = manager.value.fonts_2c[i];
            manager.value.fonts_2c[i] = nullptr;
            if (renderer)
            {
                delete renderer;
                ++released_renderers;
            }
            auto* data = manager.value.fonts_04[i];
            manager.value.fonts_04[i] = nullptr;
            if (data)
            {
                delete data;
                ++released_data;
            }
        }
    }
};

struct AllocationObservation;
AllocationObservation* allocation_observer = nullptr;

// One Linux test-link forwarding wrapper observes the genuine second scalar
// allocation. It supplies no game method or constructor and never rewrites an
// allocation size or successful allocation. Only this explicit point may throw.
struct AllocationObservation
{
    GuiParserState& parser;
    ScreenManager& manager;
    unsigned slot;
    bool fail;
    unsigned matches = 0;
    unsigned cursor = 0;
    unsigned index = 0;
    ScreenFontData* published = nullptr;
    ScreenFontRenderer* prior_renderer = nullptr;

    AllocationObservation(Model& model, unsigned position, bool inject_failure)
        : parser(model.parser.value)
        , manager(model.manager.value)
        , slot(position)
        , fail(inject_failure)
    {
        require(allocation_observer == nullptr && position < 10 &&
                    manager.fonts_04[position] == nullptr && manager.fonts_2c[position] == nullptr,
            "invalid allocation observation scope");
        allocation_observer = this;
    }
    ~AllocationObservation()
    {
        allocation_observer = nullptr;
    }
    void verify(unsigned expected_cursor) const
    {
        require(matches == 1 && published == manager.fonts_04[slot] && published != nullptr &&
                    prior_renderer == nullptr && cursor == expected_cursor && index == slot,
            "FontData must be published before the renderer allocation");
    }
};

void observe_allocation(std::size_t size)
{
    auto* observation = allocation_observer;
    if (observation && size == sizeof(ScreenFontRenderer) &&
        observation->manager.fonts_04[observation->slot] != nullptr)
    {
        ++observation->matches;
        ++observed_publications;
        observation->cursor = observation->parser.cursor;
        observation->index = observation->parser.word_3c;
        observation->published = observation->manager.fonts_04[observation->slot];
        observation->prior_renderer = observation->manager.fonts_2c[observation->slot];
        if (observation->fail)
        {
            throw std::bad_alloc{};
        }
    }
}

void parser_preserved(const GuiParserState& p, const GuiParserState& before)
{
    require(p.manager == before.manager && p.buffer == before.buffer &&
                p.current_context == before.current_context &&
                p.alternate_context_10 == before.alternate_context_10 &&
                p.alternate_context_14 == before.alternate_context_14 &&
                p.context_head == before.context_head &&
                p.previous_context == before.previous_context &&
                p.last_registered_item == before.last_registered_item &&
                p.registry_head == before.registry_head && p.items_head == before.items_head &&
                p.items_tail == before.items_tail && p.registry_tail == before.registry_tail &&
                p.word_38 == before.word_38 &&
#if defined(BEACHLIFE_DECOMP_DEFINED_ITEMS)
                p.defined_items == before.defined_items,
#else
                renderer_queue_test::same_bytes(p.unknown_24, before.unknown_24),
#endif
        "unrelated parser field changed");
}

void manager_preserved(const ScreenManager& m, const ScreenManager& before, unsigned changed_slot)
{
    require(m.renderer == before.renderer && m.registry == before.registry &&
                m.active.head == before.active.head && m.mode == before.mode &&
                m.previous_mode == before.previous_mode &&
                m.auxiliary_130 == before.auxiliary_130 && m.item_134 == before.item_134 &&
                m.value_138 == before.value_138 &&
                renderer_queue_test::same_bytes(m.unknown_54, before.unknown_54) &&
                m.item_90 == before.item_90 &&
                renderer_queue_test::same_bytes(m.unknown_94, before.unknown_94) &&
                renderer_queue_test::same_bytes(m.unknown_c0, before.unknown_c0),
        "unrelated manager field changed");
    for (unsigned i = 0; i < 10; ++i)
    {
        if (i != changed_slot)
        {
            require(m.fonts_04[i] == before.fonts_04[i] && m.fonts_2c[i] == before.fonts_2c[i],
                "unrelated font slot changed");
        }
    }
}

generated::Input font_input(unsigned advance, unsigned char pixel)
{
    generated::Input result;
    generated::word(result.coordinates, 8 + 65 * 8 + 4, advance);
    result.tiff[512] = pixel;
    return result;
}

void data_fields(const ScreenFontData* data, unsigned advance, unsigned char pixel)
{
    require(data != nullptr, "genuine FontData owner missing");
    require(data->unknown_10804[0] == 5 && data->unknown_10804[1] == 9 &&
                data->glyphs[65][0] == 0 && data->glyphs[65][1] == 0 &&
                data->glyphs[65][2] == advance && data->glyphs[65][3] == 0 &&
                data->kerning[65 * 255 + 66] == 0,
        "generated coordinate/kerning values differ");
    const auto* image = data->image;
    require(image && image->width == 2 && image->height == 2 && image->bits_per_pixel == 8 &&
                image->bytes_per_pixel == 1 && image->pixels && image->palette == nullptr &&
                image->converted_palette == nullptr,
        "real TIFF image owner differs");
    require(image->pixels[0] == pixel && image->pixels[1] == 2 && image->pixels[2] == 3 &&
                image->pixels[3] == 4,
        "real decoded TIFF pixels differ");
}

void renderer_fields(ScreenFontRenderer* renderer,
    ScreenFontData* data,
    ScreenTextLookup* lookup,
    RenderRectangle rectangle,
    unsigned advance)
{
    require(renderer && renderer->data_1c == data && renderer->lookup_20 == lookup &&
                renderer->rectangle.x == rectangle.x && renderer->rectangle.y == rectangle.y &&
                renderer->rectangle.width == rectangle.width &&
                renderer->rectangle.height == rectangle.height &&
                renderer->word_10 == 0xffffffffU && renderer->word_18 == 0 &&
                renderer->state_14 == 0,
        "real FontRenderer construction/publication differs");
    constexpr unsigned short text[] = {65, 0};
    require(
        renderer->MeasureText(text) == advance, "published renderer cannot measure its real font");
}

void non_opening()
{
    group = "non-opening";
    Model model;
    auto& parser = model.parser.value;
    parser.buffer = "xx!";
    parser.cursor = 2;
    parser.word_3c = 4;
    const auto before = parser;
    const auto manager_before = model.manager.value;
    parser.ParseFont();
    ++calls;
    require(parser.cursor == 2 && parser.word_3c == 4, "non-opening command changed cursor/index");
    parser_preserved(parser, before);
    manager_preserved(model.manager.value, manager_before, 10);
    model.manager.verify();
    model.parser.verify();
}

void successes(ScreenRequestRenderer& renderer, ScreenTextLookup& lookup)
{
    group = "two-publications";
    Model model;
    auto& parser = model.parser.value;
    parser.word_3c = 2;
    constexpr std::array<const char*, 2> inputs{"(FontA)TAIL", "xx( \tFontB,TAIL"};
    constexpr std::array<unsigned, 2> positions{0, 2};
    constexpr std::array<unsigned, 2> cursors{7, 11};
    constexpr std::array<unsigned, 2> advances{4, 7};
    constexpr std::array<unsigned char, 2> pixels{21, 42};
    constexpr std::array<RenderRectangle, 2> clips{{{-3, 5, 31, 41}, {11, -7, 43, 53}}};
    for (unsigned i = 0; i < 2; ++i)
    {
        const unsigned slot = 2 + i;
        parser.buffer = inputs[i];
        parser.cursor = positions[i];
        renderer.clip_rectangle = clips[i];
        screen_text_lookup = i == 0 ? &lookup : nullptr;
        const auto parser_before = parser;
        const auto manager_before = model.manager.value;
        AllocationObservation observation(model, slot, false);
        parser.ParseFont();
        ++calls;
        // First control discriminator: the accepted control advances by two.
        require(parser.word_3c == slot + 1, "font index must advance once");
        require(
            parser.cursor == cursors[i] && std::strcmp(parser.buffer + parser.cursor, "TAIL") == 0,
            "literal cursor/tail differs");
        observation.verify(cursors[i]);
        data_fields(model.manager.value.fonts_04[slot], advances[i], pixels[i]);
        renderer_fields(model.manager.value.fonts_2c[slot],
            model.manager.value.fonts_04[slot],
            screen_text_lookup,
            clips[i],
            advances[i]);
        parser_preserved(parser, parser_before);
        manager_preserved(model.manager.value, manager_before, slot);
    }
    require(model.manager.value.fonts_04[2] != model.manager.value.fonts_04[3] &&
                model.manager.value.fonts_2c[2] != model.manager.value.fonts_2c[3],
        "distinct publications alias");
    model.manager.verify();
    model.parser.verify();
}

void renderer_allocation_failure()
{
    group = "renderer-allocation-failure";
    Model model;
    auto& parser = model.parser.value;
    parser.buffer = "(FontA)TAIL";
    parser.word_3c = 7;
    const auto parser_before = parser;
    const auto manager_before = model.manager.value;
    bool caught = false;
    {
        AllocationObservation observation(model, 7, true);
        ++calls;
        try
        {
            parser.ParseFont();
        }
        catch (const std::bad_alloc&)
        {
            caught = true;
        }
        observation.verify(7);
    }
    require(caught && parser.cursor == 7 && parser.word_3c == 7 &&
                model.manager.value.fonts_2c[7] == nullptr,
        "failed renderer allocation changed index or published a renderer");
    data_fields(model.manager.value.fonts_04[7], 4, 21);
    parser_preserved(parser, parser_before);
    manager_preserved(model.manager.value, manager_before, 7);
    model.manager.verify();
    model.parser.verify();
    // The completed data remains owned here until ordinary cleanup, not rollback.
}

struct Globals
{
    ScreenRequestRenderer* renderer = screen_request_renderer;
    ScreenTextLookup* lookup = screen_text_lookup;
    ~Globals()
    {
        screen_request_renderer = renderer;
        screen_text_lookup = lookup;
    }
};
} // namespace

extern "C" void* __real__Znwm(std::size_t);
extern "C" void* __wrap__Znwm(std::size_t size)
{
    observe_allocation(size);
    return __real__Znwm(size);
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "expected generated font directory");
        generated::GeneratedFonts fonts(argv[1]);
        fonts.add("FontA", font_input(4, 21));
        fonts.add("FontB", font_input(7, 42));
        generated::files::Session session(fonts.mount());
        Guarded<ScreenRequestRenderer> renderer;
        renderer_queue_test::initialize(renderer.value, 0x21);
        Guarded<ScreenTextLookup> lookup;
        Globals globals;
        screen_request_renderer = &renderer.value;
        screen_text_lookup = &lookup.value;
        non_opening();
        successes(renderer.value, lookup.value);
        renderer_allocation_failure();
        renderer.verify();
        lookup.verify();
        require(calls == 4 && released_data == 3 && released_renderers == 2 &&
                    observed_publications == 3 && allocation_observer == nullptr,
            "completion/real ownership totals differ");
        std::printf("gui-parse-font groups=3 calls=%u data_releases=%u renderer_releases=%u "
                    "publications=%u checks=%u pass\n",
            calls,
            released_data,
            released_renderers,
            observed_publications,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "gui-parse-font %s: %s\n", group, failure.reason);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "gui-parse-font %s: host failure: %s\n", group, failure.what());
        return 3;
    }
}
