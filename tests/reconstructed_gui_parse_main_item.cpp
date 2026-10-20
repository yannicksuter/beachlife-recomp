#include "child_ring_seed.h"
#include "renderer_queue_test_state.h"
#include "startup_gui_parser.h"
#include "startup_screen_child_link.h"
#include "startup_screen_frame_item.h"
#include "startup_screen_manager.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>

namespace
{
static_assert(CHAR_BIT == 8 && sizeof(unsigned int) == 4 && sizeof(void*) == 8);
static_assert(std::endian::native == std::endian::little);
static_assert(std::is_aggregate_v<ScreenManager> && std::is_standard_layout_v<ScreenManager>);
static_assert(std::is_aggregate_v<GuiParserState> && std::is_standard_layout_v<GuiParserState>);
static_assert(sizeof(ScreenManager) == 424 && offsetof(ScreenManager, item_90) == 232);
static_assert(offsetof(ScreenManager, unknown_94) == 240 && offsetof(ScreenManager, active) == 280);
static_assert(offsetof(ScreenManager, mode) == 392 && offsetof(ScreenManager, value_138) == 416);
static_assert(sizeof(GuiParserState) == 120 && offsetof(GuiParserState, word_3c) == 116);
using Seed = child_ring_fixture::Seed<4>;
unsigned checks = 0, calls = 0, frames = 0, released_frames = 0, pops = 0;
const char* current_case = "setup";

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
    std::array<unsigned char, 16> before;
    T value{};
    std::array<unsigned char, 16> after;
    Guarded()
    {
        before.fill(0x36);
        after.fill(0xc9);
    }
    void verify() const
    {
        require(std::all_of(before.begin(),
                    before.end(),
                    [](auto b)
                    {
                        return b == 0x36;
                    }) &&
                    std::all_of(after.begin(),
                        after.end(),
                        [](auto b)
                        {
                            return b == 0xc9;
                        }),
            "outside guard changed");
    }
};

struct Environment
{
    Guarded<ScreenManager> manager;
    Guarded<GuiParserState> parser;
    Guarded<ScreenRequestRenderer> renderer;
    ScreenItem prior{{7, 11, 19, 23}};
    ScreenChildLink unused{nullptr};
    CircularListNode sentinel{};
    std::array<std::unique_ptr<ScreenItem>, 2> owners;
    ScreenRequestRenderer* old_renderer = screen_request_renderer;
    ScreenManager* old_manager = screen_manager;

    Environment()
    {
        // Typed borrowed read models only; real Frame/base/ring objects below
        // use their genuine constructors and complete vtables.
        renderer_queue_test::initialize(renderer.value, 29);
        renderer.value.width = 800;
        manager.value.renderer = &renderer.value;
        manager.value.registry = &unused;
        manager.value.item_90 = &prior;
        sentinel = {&sentinel, &sentinel};
        manager.value.active.head = &sentinel;
        manager.value.mode = -7;
        manager.value.previous_mode = 13;
        manager.value.item_134 = &prior;
        manager.value.value_138 = 2.5f;
        renderer_queue_test::pattern(manager.value.unknown_54, 0x21);
        renderer_queue_test::pattern(manager.value.unknown_94, 0x42);
        renderer_queue_test::pattern(manager.value.unknown_c0, 0x63);
        prior.kind_28 = 0;
        prior.word_2c = 0x12345678U;
        parser.value.manager = &manager.value;
        parser.value.alternate_context_10 = &prior;
        parser.value.alternate_context_14 = &prior;
        parser.value.previous_context = &prior;
        parser.value.last_registered_item = &prior;
        parser.value.registry_head = &unused;
        parser.value.registry_tail = &unused;
        parser.value.items_head = &unused;
        parser.value.items_tail = &unused;
        parser.value.word_38 = 0x13579bdfU;
        parser.value.word_3c = 0x2468ace0U;
        renderer_queue_test::pattern(parser.value.unknown_24, 0x75);
        screen_request_renderer = &renderer.value;
        screen_manager = &manager.value;
    }
    ~Environment()
    {
        // History links borrow these real items. Detach any outstanding
        // borrowed reference before genuine link destruction on failure.
        while (parser.value.context_head)
        {
            auto* link = parser.value.context_head;
            parser.value.context_head = link->next;
            link->item = nullptr;
            delete link;
        }
        parser.value.current_context = nullptr;
        parser.value.previous_context = nullptr;
        manager.value.item_90 = nullptr;
        for (auto i = owners.rbegin(); i != owners.rend(); ++i)
        {
            if (*i)
            {
                i->reset();
                ++released_frames;
            }
        }
        screen_manager = old_manager;
        screen_request_renderer = old_renderer;
    }
    ScreenFrameItem* adopt(unsigned slot, ScreenItem* previous)
    {
        require(slot < owners.size() && !owners[slot], "invalid fixture ownership slot");
        if (manager.value.item_90 != previous)
        {
            owners[slot].reset(manager.value.item_90);
        }
        require(owners[slot] != nullptr, "MAIN did not construct/publish a Frame");
        auto* frame = dynamic_cast<ScreenFrameItem*>(owners[slot].get());
        require(frame != nullptr, "published object is not the genuine Frame class");
        ++frames;
        return frame;
    }
    void verify() const
    {
        manager.verify();
        parser.verify();
        renderer.verify();
        require(screen_request_renderer == &renderer.value && screen_manager == &manager.value,
            "borrowed global changed");
        require(renderer_queue_test::same_rectangle(prior.rectangle, {7, 11, 19, 23}) &&
                    prior.kind_28 == 0 && prior.word_2c == 0x12345678U &&
                    prior.owned_text == nullptr,
            "old published item was changed");
        require(unused.next == nullptr && unused.item == nullptr && sentinel.next == &sentinel &&
                    sentinel.previous == &sentinel,
            "unrelated live link changed");
    }
};

void parser_preserved(const GuiParserState& p, const GuiParserState& old)
{
    require(p.manager == old.manager && p.buffer == old.buffer &&
                p.alternate_context_10 == old.alternate_context_10 &&
                p.alternate_context_14 == old.alternate_context_14 &&
                p.last_registered_item == old.last_registered_item &&
                p.registry_head == old.registry_head && p.registry_tail == old.registry_tail &&
                p.items_head == old.items_head && p.items_tail == old.items_tail &&
                p.word_38 == old.word_38 && p.word_3c == old.word_3c &&
                renderer_queue_test::same_bytes(p.unknown_24, old.unknown_24),
        "unrelated parser field changed");
}
void manager_preserved(const ScreenManager& m, const ScreenManager& old)
{
    require(
        m.renderer == old.renderer && m.registry == old.registry &&
            m.active.head == old.active.head && m.mode == old.mode &&
            m.previous_mode == old.previous_mode && m.auxiliary_130 == old.auxiliary_130 &&
            m.item_134 == old.item_134 && m.value_138 == old.value_138 &&
            std::equal(std::begin(m.fonts_04), std::end(m.fonts_04), std::begin(old.fonts_04)) &&
            std::equal(std::begin(m.fonts_2c), std::end(m.fonts_2c), std::begin(old.fonts_2c)) &&
            renderer_queue_test::same_bytes(m.unknown_54, old.unknown_54) &&
            renderer_queue_test::same_bytes(m.unknown_94, old.unknown_94) &&
            renderer_queue_test::same_bytes(m.unknown_c0, old.unknown_c0),
        "unrelated manager member changed");
}
void frame_values(const ScreenFrameItem& f)
{
    require(renderer_queue_test::same_rectangle(f.rectangle, {0, 0, 0, 0}) &&
                renderer_queue_test::same_rectangle(f.saved_rectangle, {0, 0, 0, 0}) &&
                f.kind_28 == 11 && f.word_44 == 0 && f.word_48 == 0 && f.elapsed_24 == 0.0f &&
                f.state_30 == 0 && f.last_x == 0 && f.last_y == 0 && f.owned_text == nullptr &&
                f.state_40 == 0 && f.state_41 == 0 && f.state_42 == 0 && f.preparation_flag == 1,
        "real Frame constructor arguments/state differ");
    require(f.children.head && f.children.head->next == f.children.head &&
                f.children.head->previous == f.children.head,
        "Frame does not own its genuine empty child ring");
    // word2c, base opaque bytes, sentinel payload and ABI padding are untouched
    // constructor storage, not invented initialized values or post-life reads.
}

template <std::size_t N>
void skipped(Environment& e,
    const std::array<char, N>& text,
    unsigned width,
    unsigned initial,
    unsigned expected)
{
    Guarded<std::array<char, N>> input;
    input.value = text;
    auto& p = e.parser.value;
    p.buffer = input.value.data();
    p.cursor = initial;
    e.renderer.value.width = width;
    const auto old = p;
    const auto manager = e.manager.value;
    const auto renderer = e.renderer.value;
    p.ParseMainItem();
    ++calls;
    require(p.cursor == expected && p.current_context == old.current_context &&
                p.previous_context == old.previous_context && p.context_head == old.context_head &&
                e.manager.value.item_90 == &e.prior,
        "below-800 cursor or ownership differs");
    parser_preserved(p, old);
    manager_preserved(e.manager.value, manager);
    require(renderer_queue_test::equal(e.renderer.value, renderer), "renderer changed during skip");
    require(input.value == text, "input changed during skip");
    input.verify();
    e.verify();
}

void normal_groups()
{
    Seed seed(require);
    {
        Environment e;
        current_case = "below-800-balanced-and-no-brace";
        skipped(e, std::to_array("xx \t\r\n{a{b}c}tail"), 799, 2, 13);
        skipped(e, std::to_array("xx\t \r\n!"), 0, 2, 6);
        seed.check_initial_order();

        current_case = "800-construction-publication-context";
        Guarded<std::array<char, 13>> first;
        first.value = std::to_array("xx \t\r\n{tail}");
        auto& p = e.parser.value;
        p.buffer = first.value.data();
        p.cursor = 2;
        e.renderer.value.width = 800;
        const auto first_old = p;
        const auto manager = e.manager.value;
        const auto renderer = e.renderer.value;
        p.ParseMainItem();
        ++calls;
        auto* a = e.adopt(0, &e.prior);
        frame_values(*a);
        require(p.cursor == 7 && p.current_context == a && p.previous_context == a &&
                    p.context_head == nullptr,
            "empty-context MAIN publication differs");
        parser_preserved(p, first_old);
        manager_preserved(e.manager.value, manager);
        require(
            renderer_queue_test::equal(e.renderer.value, renderer), "renderer changed during MAIN");
        require(first.value == std::to_array("xx \t\r\n{tail}"), "first input changed");
        first.verify();
        seed.check_free({1, 2, 3});

        current_case = "unsigned-width-existing-context";
        Guarded<std::array<char, 8>> second;
        second.value = std::to_array("  {more");
        p.buffer = second.value.data();
        p.cursor = 0;
        e.renderer.value.width = std::numeric_limits<unsigned int>::max();
        const auto second_old = p;
        const auto second_renderer = e.renderer.value;
        p.ParseMainItem();
        ++calls;
        auto* b = e.adopt(1, a);
        frame_values(*b);
        frame_values(*a);
        require(p.cursor == 3 && p.current_context == b && p.previous_context == a &&
                    p.context_head && p.context_head->item == a && p.context_head->next == nullptr,
            "real context link/cursor differs");
        parser_preserved(p, second_old);
        manager_preserved(e.manager.value, manager);
        require(renderer_queue_test::equal(e.renderer.value, second_renderer),
            "renderer changed during second MAIN");
        require(second.value == std::to_array("  {more"), "second input changed");
        second.verify();
        seed.check_free({2, 3});
        p.PopContext();
        ++pops;
        require(
            p.current_context == a && p.previous_context == nullptr && p.context_head == nullptr,
            "real Pop did not detach its borrowed Frame reference");
        frame_values(*a);
        frame_values(*b);
        p.PopContext();
        ++pops;
        require(p.current_context == nullptr && p.previous_context == nullptr &&
                    e.manager.value.item_90 == b,
            "Pop changed the independently published main item");
        e.verify();
    }
    seed.check_initial_order();
    require(frames == 2 && released_frames == 2 && calls == 4 && pops == 2,
        "normal lifetime/call totals differ");
}

Environment* fatal_environment = nullptr;
Seed* fatal_seed = nullptr;
constexpr char fatal_input[] = "xx \t!";
void fatal_cleanup()
{
    try
    {
        auto& e = *fatal_environment;
        auto* frame = e.adopt(0, &e.prior);
        frame_values(*frame);
        require(e.parser.value.cursor == 4 && e.parser.value.current_context == nullptr &&
                    e.parser.value.previous_context == &e.prior &&
                    e.parser.value.context_head == nullptr,
            "fatal path did not retain completed publication before rejecting brace");
        e.verify();
        delete fatal_environment;
        fatal_environment = nullptr;
        fatal_seed->check_initial_order();
        delete fatal_seed;
        fatal_seed = nullptr;
        require(frames == 1 && released_frames == 1, "fatal publication ownership cleanup differs");
        std::printf("gui-parse-main fatal-cleanup published=1 cursor=4 released=1 pass\n");
    }
    catch (...)
    {
        std::fputs("gui-parse-main fatal-cleanup failed\n", stderr);
        std::_Exit(2);
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::strcmp(argv[1], "--fatal-open") == 0)
        {
            current_case = "missing-high-width-brace";
            fatal_seed = new Seed(require);
            fatal_environment = new Environment;
            fatal_environment->parser.value.buffer = fatal_input;
            fatal_environment->parser.value.cursor = 2;
            require(std::atexit(fatal_cleanup) == 0, "host atexit registration failed");
            std::fputs("gui-parse-main fatal-before\n", stderr);
            std::fflush(stderr);
            fatal_environment->parser.value.ParseMainItem(); // Actual StartupFatal/CRT exit0.
            std::fputs("gui-parse-main fatal-returned\n", stderr);
            std::_Exit(3);
        }
        require(argc == 1, "unexpected arguments");
        normal_groups();
        std::printf(
            "gui-parse-main groups=2 calls=%u pops=%u frames=%u released=%u checks=%u pass\n",
            calls,
            pops,
            frames,
            released_frames,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "gui-parse-main %s: %s\n", current_case, failure.reason);
        return 2;
    }
}
