#include "child_ring_seed.h"
#include "renderer_queue_test_state.h"
#include "startup_gui_parser.h"
#include "startup_screen_child_link.h"
#include "startup_screen_dialog.h"
#include "startup_screen_dialog_resource.h"
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
static_assert(std::is_aggregate_v<GuiParserState> && std::is_standard_layout_v<GuiParserState>);
static_assert(std::is_aggregate_v<ScreenManager> && std::is_standard_layout_v<ScreenManager>);
static_assert(sizeof(GuiParserState) == 120 && offsetof(GuiParserState, word_38) == 112);
static_assert(offsetof(GuiParserState, word_3c) == 116);
static_assert(sizeof(ScreenManager) == 424 && offsetof(ScreenManager, item_90) == 232);
static_assert(sizeof(ScreenDialog) == 160 && !std::is_aggregate_v<ScreenDialog>);
static_assert(BEACHLIFE_DECOMP_DIALOG_OWNER == 1);
using Seed = child_ring_fixture::Seed<12>;

unsigned checks = 0, calls = 0, pops = 0, released_items = 0;
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

// Genuine renderer ownership; only method inputs are borrowed.
template <> struct Guarded<ScreenRequestRenderer>
{
    renderer_owner_test::Bootstrap renderer_bootstrap;
    std::array<unsigned char, 16> before;
    ScreenRequestRenderer value{renderer_bootstrap.video(), 8, 4, 1};
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
    renderer_owner_test::RestoreOwnedState renderer_restore{value, renderer_bootstrap};
};

template <std::size_t N> Guarded<std::array<char, N>> input(const char (&literal)[N])
{
    Guarded<std::array<char, N>> result;
    std::copy_n(literal, N, result.value.begin());
    return result;
}

void empty_ring(const ScreenChildRing& ring)
{
    require(ring.head && ring.head->next == ring.head && ring.head->previous == ring.head,
        "genuine child ring is not empty");
    // The sentinel payload is deliberately uninitialized and is never read.
}
void single_child(const ScreenChildRing& ring, const ScreenItem* item)
{
    require(ring.head && ring.head->next != ring.head && ring.head->previous == ring.head->next,
        "missing single genuine child node");
    auto* node = ring.head->next;
    require(node->next == ring.head && node->previous == ring.head && node->item == item,
        "borrowed child payload or ring links differ");
}

struct Environment
{
    Guarded<ScreenManager> manager;
    Guarded<GuiParserState> parser;
    Guarded<ScreenRequestRenderer> renderer;
    ScreenItem prior{{7, 11, 19, 23}};
    CircularListNode active_head{};
    std::unique_ptr<ScreenFrameItem> alternate;
    ScreenManager* old_manager = screen_manager;
    ScreenRequestRenderer* old_renderer = screen_request_renderer;

    Environment()
    {
        // Manager/parser remain typed borrowed read models; the renderer is a
        // genuine owner with a restored method view.
        // Every constructed ScreenItem, Frame, Dialog and ring has its real body.
        require(screen_dialog_resources == nullptr, "unexpected active resource owner");
        renderer_queue_test::initialize(renderer.value, 29);
        renderer.value.width = 800;
        renderer.value.height = 600;
        manager.value.renderer = &renderer.value;
        manager.value.item_90 = &prior;
        active_head = {&active_head, &active_head};
        manager.value.active.head = &active_head;
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
        parser.value.previous_context = &prior;
        parser.value.last_registered_item = &prior;
        parser.value.word_3c = 0x2468ace0U;
#if defined(BEACHLIFE_DECOMP_DEFINED_ITEMS)
        parser.value.defined_items = nullptr;
#else
        renderer_queue_test::pattern(parser.value.unknown_24, 0x75);
#endif
        screen_manager = &manager.value;
        screen_request_renderer = &renderer.value;
        alternate = std::make_unique<ScreenFrameItem>(ScreenRectangle{100, 200, 200, 400}, 0);
        alternate->word_2c = 0x87654321U;
    }
    ~Environment()
    {
        clear();
        alternate.reset();
        screen_manager = old_manager;
        screen_request_renderer = old_renderer;
    }
    void clear()
    {
        // History and registry duplicate borrowed references. Detach them before
        // their real link destructors. The all-items chain owns each Dialog once.
        while (parser.value.context_head)
        {
            auto* link = parser.value.context_head;
            parser.value.context_head = link->next;
            link->item = nullptr;
            delete link;
        }
        parser.value.current_context = nullptr;
        parser.value.previous_context = nullptr;
        parser.value.alternate_context_10 = nullptr;
        parser.value.alternate_context_14 = nullptr;
        while (parser.value.registry_head)
        {
            auto* link = parser.value.registry_head;
            parser.value.registry_head = link->next;
            link->item = nullptr;
            delete link;
        }
        parser.value.registry_tail = nullptr;
        if (alternate)
        {
            alternate->children.Clear(); // Actual node cleanup; payloads remain alive.
        }
        while (parser.value.items_head)
        {
            auto* link = parser.value.items_head;
            parser.value.items_head = link->next;
            delete link; // Actual kind4 -> Dialog -> owned name/Frame/ring -> base.
            ++released_items;
        }
        parser.value.items_tail = nullptr;
        parser.value.last_registered_item = nullptr;
    }
    void verify() const
    {
        manager.verify();
        parser.verify();
        renderer.verify();
        require(screen_manager == &manager.value && screen_request_renderer == &renderer.value &&
                    screen_dialog_resources == nullptr,
            "borrowed global changed");
        require(renderer_queue_test::same_rectangle(prior.rectangle, {7, 11, 19, 23}) &&
                    prior.kind_28 == 0 && prior.word_2c == 0x12345678U &&
                    prior.owned_text == nullptr,
            "unrelated live item changed");
        require(active_head.next == &active_head && active_head.previous == &active_head,
            "unrelated active ring changed");
        require(renderer_queue_test::same_rectangle(alternate->rectangle, {100, 200, 200, 400}) &&
                    alternate->kind_28 == 11 && alternate->word_2c == 0x87654321U,
            "real alternate Frame changed beyond attachment");
    }
};

void preserved(const Environment& environment,
    const GuiParserState& old,
    const ScreenManager& manager,
    const renderer_queue_test::Snapshot& renderer)
{
    const auto& p = environment.parser.value;
    require(p.manager == old.manager && p.buffer == old.buffer &&
                p.alternate_context_10 == old.alternate_context_10 &&
                p.alternate_context_14 == old.alternate_context_14 && p.word_3c == old.word_3c &&
#if defined(BEACHLIFE_DECOMP_DEFINED_ITEMS)
                p.defined_items == old.defined_items,
#else
                renderer_queue_test::same_bytes(p.unknown_24, old.unknown_24),
#endif
        "unrelated parser field changed");
    const auto& m = environment.manager.value;
    require(m.renderer == manager.renderer && m.registry == manager.registry &&
                m.item_90 == manager.item_90 && m.active.head == manager.active.head &&
                m.mode == manager.mode && m.previous_mode == manager.previous_mode &&
                m.auxiliary_130 == manager.auxiliary_130 && m.item_134 == manager.item_134 &&
                m.value_138 == manager.value_138 &&
                std::equal(
                    std::begin(m.fonts_04), std::end(m.fonts_04), std::begin(manager.fonts_04)) &&
                std::equal(
                    std::begin(m.fonts_2c), std::end(m.fonts_2c), std::begin(manager.fonts_2c)) &&
                renderer_queue_test::same_bytes(m.unknown_54, manager.unknown_54) &&
                renderer_queue_test::same_bytes(m.unknown_94, manager.unknown_94) &&
                renderer_queue_test::same_bytes(m.unknown_c0, manager.unknown_c0),
        "unrelated manager member changed");
    require(renderer_queue_test::equal(environment.renderer.value, renderer),
        "renderer changed during parsing");
    environment.verify();
}

ScreenDialog* dialog(ScreenChildLink* link,
    const char* name,
    const char* source_name,
    ScreenRectangle bounds,
    unsigned group,
    unsigned char flag,
    unsigned clear,
    unsigned word)
{
    require(link && link->item, "missing actual item link");
    auto* item = dynamic_cast<ScreenDialog*>(link->item);
    require(item != nullptr, "registered item is not the genuine Dialog class");
    require(item->word_2c == word, "dialog numbering/publication differs");
    require(item->name != source_name && std::strcmp(item->name, name) == 0,
        "Dialog did not retain its owned name copy");
    require(renderer_queue_test::same_rectangle(item->rectangle, bounds) &&
                renderer_queue_test::same_rectangle(item->saved_rectangle, bounds) &&
                item->kind_28 == 4 && item->group_7c == group && item->flag_60 == flag &&
                item->clear_5c == clear && item->resource_4c == nullptr && item->state_58 == 0 &&
                item->state_59 == 0 && item->state_61 == 0 && item->word_64 == 0 &&
                item->word_74 == 0,
        "Dialog constructor arguments/state differ");
    require(item->elapsed_24 == 0.0f && item->state_30 == 0 && item->last_x == 0 &&
                item->last_y == 0 && item->owned_text == nullptr && item->state_40 == 0 &&
                item->state_41 == 0 && item->state_42 == 0 && item->preparation_flag == 1,
        "real base construction differs");
    auto* title = dynamic_cast<ScreenFrameItem*>(item->child_44);
    const ScreenRectangle title_bounds{bounds.left, bounds.top, bounds.right, bounds.top + 30};
    require(title && item->child_44 != item && title->kind_28 == 11 && title->word_44 == 0 &&
                title->word_48 == 0 &&
                renderer_queue_test::same_rectangle(title->rectangle, title_bounds) &&
                renderer_queue_test::same_rectangle(title->saved_rectangle, title_bounds),
        "Dialog does not own the genuine title Frame");
    empty_ring(title->children);
    // Previous coordinates, transition floats, opaque bytes, title word2c and
    // sentinel payloads are not initialized by these constructors. Do not read
    // them, copy whole objects, pattern their vptrs or invent padding values.
    return item;
}

void normal_groups()
{
    Seed seed(require);
    {
        Environment environment;
        auto& p = environment.parser.value;
        const auto manager = environment.manager.value;
        const renderer_queue_test::Snapshot renderer = environment.renderer.value;
        auto first = input(" \tLogo(1,2,11,12,NONE,7,256) \r\n{tail");
        auto second = input("Child(1,2,11,12,NONE,3,0){tail");
        auto third = input("Third(3,4,17,25,CLEAR,-2,-1){rest");

        current_case = "full-integer-boolean-and-number-wrap";
        p.buffer = first.value.data();
        p.word_38 = std::numeric_limits<unsigned>::max();
        const auto before_first = p;
        ++calls;
        p.ParseDialogItem();
        auto* a = dialog(p.items_head, "Logo", first.value.data() + 2, {1, 2, 11, 12}, 7, 1, 0, 0);
        require(p.word_38 == 0 && p.items_head == p.items_tail && p.items_head->next == nullptr &&
                    p.registry_head == p.registry_tail && p.registry_head != p.items_head &&
                    p.registry_head->item == a && p.registry_head->next == nullptr &&
                    p.last_registered_item == a && p.current_context == a &&
                    p.previous_context == a && p.context_head == nullptr && p.cursor == 32 &&
                    std::strcmp(p.buffer + p.cursor, "tail") == 0,
            "first registration/context/cursor differs");
        empty_ring(a->children);
        require(first.value == std::to_array(" \tLogo(1,2,11,12,NONE,7,256) \r\n{tail"),
            "first input changed");
        first.verify();
        preserved(environment, before_first, manager, renderer);
        seed.check_free({3, 4, 5, 6, 7, 8, 9, 10, 11});

        current_case = "real-dialog-and-frame-attachment";
        auto* first_item_link = p.items_head;
        auto* first_registry_link = p.registry_head;
        p.buffer = second.value.data();
        p.cursor = 0;
        p.word_38 = 8;
        const auto before_second = p;
        ++calls;
        p.ParseDialogItem();
        auto* b = dialog(p.items_tail, "Child", second.value.data(), {1, 2, 11, 12}, 3, 0, 0, 9);
        require(p.word_38 == 9 && p.items_head == first_item_link &&
                    first_item_link->next == p.items_tail && p.items_tail->next == nullptr &&
                    p.registry_head == first_registry_link &&
                    first_registry_link->next == p.registry_tail &&
                    p.registry_tail->next == nullptr && p.registry_tail->item == b &&
                    p.last_registered_item == b && p.current_context == b &&
                    p.previous_context == a && p.context_head && p.context_head->item == a &&
                    p.context_head->next == nullptr && p.cursor == 26 &&
                    std::strcmp(p.buffer + p.cursor, "tail") == 0,
            "nested registration/context/cursor differs");
        single_child(a->children, b);
        empty_ring(b->children);
        require(a->word_2c == 0, "second registration changed the first item word");
        require(second.value == std::to_array("Child(1,2,11,12,NONE,3,0){tail"),
            "second input changed");
        second.verify();
        preserved(environment, before_second, manager, renderer);
        seed.check_free({6, 7, 8, 9, 10, 11});

        auto* second_item_link = p.items_tail;
        auto* second_registry_link = p.registry_tail;
        auto* first_history = p.context_head;
        p.buffer = third.value.data();
        p.cursor = 0;
        p.alternate_context_10 = a;
        p.alternate_context_14 = environment.alternate.get();
        const auto before_third = p;
        ++calls;
        p.ParseDialogItem();
        // The existing unchecked integer reader treats '-' as digit -3:
        // "-2" produces -28 and "-1" produces -29. No sign parser is added.
        auto* c = dialog(
            p.items_tail, "Third", third.value.data(), {3, 4, 17, 25}, 0xffffffe4U, 1, 1, 10);
        require(p.word_38 == 10 && second_item_link->next == p.items_tail &&
                    p.items_tail->next == nullptr &&
                    second_registry_link->next == p.registry_tail &&
                    p.registry_tail->next == nullptr && p.registry_tail->item == c &&
                    p.last_registered_item == c && p.current_context == c &&
                    p.previous_context == b && p.context_head == first_history &&
                    first_history->item == a && first_history->next &&
                    first_history->next->item == b && first_history->next->next == nullptr &&
                    p.cursor == 29 && std::strcmp(p.buffer + p.cursor, "rest") == 0,
            "alternate attachment/history/cursor differs");
        single_child(environment.alternate->children, c);
        single_child(a->children, b);
        empty_ring(b->children);
        empty_ring(c->children);
        require(
            a->word_2c == 0 && b->word_2c == 9, "later registration changed previous item words");
        require(third.value == std::to_array("Third(3,4,17,25,CLEAR,-2,-1){rest"),
            "third input changed");
        third.verify();
        preserved(environment, before_third, manager, renderer);
        seed.check_free({9, 10, 11});

        p.PopContext();
        ++pops;
        require(p.current_context == b && p.previous_context == a &&
                    p.context_head == first_history && first_history->item == a &&
                    first_history->next == nullptr,
            "first actual Pop differs");
        p.PopContext();
        ++pops;
        require(
            p.current_context == a && p.previous_context == nullptr && p.context_head == nullptr,
            "second actual Pop differs");
        p.PopContext();
        ++pops;
        require(p.current_context == nullptr && p.previous_context == nullptr &&
                    p.last_registered_item == c && p.items_tail->item == c,
            "empty-history Pop changed registration ownership");
        environment.clear();
        require(released_items == 3 && p.items_head == nullptr && p.registry_head == nullptr,
            "real owning link cleanup count differs");
        empty_ring(environment.alternate->children);
        environment.verify();
    }
    // Seven genuine sentinel lifetimes and two borrowed child nodes all ended.
    seed.check_free({0, 6, 7, 3, 4, 1, 5, 2, 8, 9, 10, 11});
    require(calls == 3 && pops == 3 && released_items == 3, "normal totals differ");
}

Environment* fatal_environment = nullptr;
Seed* fatal_seed = nullptr;
bool fatal_before_construction = false;
constexpr char fatal_bracket[] = "D,?";
constexpr char fatal_brace[] = "D(1,2,11,12,NONE,0,0)!";
void fatal_cleanup()
{
    try
    {
        auto& environment = *fatal_environment;
        auto& p = environment.parser.value;
        require(calls == 1 && p.current_context == nullptr && p.context_head == nullptr &&
                    p.previous_context == &environment.prior,
            "fatal path changed context before a valid brace");
        if (fatal_before_construction)
        {
            require(p.cursor == 2 && p.word_38 == 0 && p.items_head == nullptr &&
                        p.items_tail == nullptr && p.registry_head == nullptr &&
                        p.registry_tail == nullptr && p.last_registered_item == &environment.prior,
                "missing parenthesis performed construction/publication");
            fatal_seed->check_free({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
        }
        else
        {
            auto* item = dialog(p.items_head, "D", fatal_brace, {1, 2, 11, 12}, 0, 0, 0, 1);
            require(p.cursor == 21 && p.word_38 == 1 && p.items_head == p.items_tail &&
                        p.items_head->next == nullptr && p.registry_head == p.registry_tail &&
                        p.registry_head != p.items_head && p.registry_head->item == item &&
                        p.registry_head->next == nullptr && p.last_registered_item == item,
                "missing brace lost completed registration");
            empty_ring(item->children);
            fatal_seed->check_free({3, 4, 5, 6, 7, 8, 9, 10, 11});
        }
        environment.verify();
        delete fatal_environment;
        fatal_environment = nullptr;
        fatal_seed->check_initial_order();
        delete fatal_seed;
        fatal_seed = nullptr;
        require(released_items == (fatal_before_construction ? 0U : 1U),
            "fatal observer did not release its actual owned graph");
        std::printf("gui-parse-dialog fatal-cleanup phase=%s published=%u released=%u pass\n",
            fatal_before_construction ? "bracket" : "brace",
            fatal_before_construction ? 0U : 1U,
            released_items);
    }
    catch (...)
    {
        std::fputs("gui-parse-dialog fatal-cleanup failed\n", stderr);
        std::_Exit(2);
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && (std::strcmp(argv[1], "--fatal-bracket") == 0 ||
                             std::strcmp(argv[1], "--fatal-brace") == 0))
        {
            fatal_before_construction = std::strcmp(argv[1], "--fatal-bracket") == 0;
            current_case = fatal_before_construction ? "fatal-bracket" : "fatal-brace";
            fatal_seed = new Seed(require);
            fatal_environment = new Environment;
            fatal_environment->parser.value.buffer =
                fatal_before_construction ? fatal_bracket : fatal_brace;
            require(std::atexit(fatal_cleanup) == 0, "host atexit registration failed");
            std::fputs("gui-parse-dialog fatal-before\n", stderr);
            std::fflush(stderr);
            ++calls;
            fatal_environment->parser.value.ParseDialogItem(); // Real StartupFatal and CRT exit(0).
            std::fputs("gui-parse-dialog fatal-returned\n", stderr);
            std::_Exit(3);
        }
        require(argc == 1, "unexpected arguments");
        normal_groups();
        std::printf("gui-parse-dialog groups=2 calls=%u pops=%u released=%u checks=%u pass\n",
            calls,
            pops,
            released_items,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "gui-parse-dialog %s: %s\n", current_case, failure.reason);
        return 2;
    }
}
