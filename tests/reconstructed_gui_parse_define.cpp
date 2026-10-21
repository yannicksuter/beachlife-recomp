#include "child_ring_seed.h"
#include "startup_gui_defined_item.h"
#include "startup_gui_defined_item_globals.h"
#include "startup_gui_parser.h"
#include "startup_screen_child_link.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

namespace
{
static_assert(CHAR_BIT == 8 && sizeof(unsigned int) == 4 && sizeof(void*) == 8);
static_assert(BEACHLIFE_DECOMP_DEFINED_ITEMS == 1);
static_assert(std::is_aggregate_v<GuiParserState> && std::is_standard_layout_v<GuiParserState>);
static_assert(sizeof(GuiParserState) == 120 && offsetof(GuiParserState, defined_items) == 72);
static_assert(offsetof(GuiParserState, registry_head) == 80);
static_assert(
    offsetof(GuiParserState, items_head) == 88 && offsetof(GuiParserState, items_tail) == 96);
static_assert(offsetof(GuiParserState, registry_tail) == 104);
static_assert(offsetof(GuiParserState, word_38) == 112 && offsetof(GuiParserState, word_3c) == 116);
static_assert(sizeof(GuiDefinedItem) == 96 && sizeof(ScreenChildLink) == 16);
using Seed = child_ring_fixture::Seed<8>;

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
        for (unsigned i = 0; i < before.size(); ++i)
        {
            require(before[i] == 0x36 && after[i] == 0xc9, "outside guard changed");
        }
    }
};
template <std::size_t N> Guarded<std::array<char, N>> input(const char (&literal)[N])
{
    Guarded<std::array<char, N>> result;
    std::copy_n(literal, N, result.value.begin());
    return result;
}

std::uintptr_t id(const void* pointer)
{
    return reinterpret_cast<std::uintptr_t>(pointer);
}
struct Publication
{
    unsigned cursor = 0;
    std::uintptr_t current = 0, previous = 0, last = 0, history = 0;
    std::uintptr_t items = 0, tail = 0, definitions = 0;
    explicit Publication(const GuiParserState& p)
        : cursor(p.cursor)
        , current(id(p.current_context))
        , previous(id(p.previous_context))
        , last(id(p.last_registered_item))
        , history(id(p.context_head))
        , items(id(p.items_head))
        , tail(id(p.items_tail))
        , definitions(id(p.defined_items))
    {
    }
    Publication() = default;
};
struct Allocation
{
    void* live = nullptr;
    std::size_t size = 0;
    Publication before;
};
struct Ledger
{
    std::array<Allocation, 24> entries{};
    unsigned count = 0, releases = 0;
    bool overflow = false;
    GuiParserState* parser = nullptr;
};
Ledger* observed = nullptr;

void observe_allocation(void* pointer, std::size_t bytes) noexcept
{
    if (observed && observed->parser)
    {
        if (observed->count == observed->entries.size())
        {
            observed->overflow = true;
        }
        else
        {
            observed->entries[observed->count++] = {pointer, bytes, Publication(*observed->parser)};
        }
    }
}
void release(void* pointer) noexcept
{
    if (observed && pointer)
    {
        for (unsigned i = 0; i < observed->count; ++i)
        {
            if (observed->entries[i].live == pointer)
            {
                observed->entries[i].live = nullptr;
                ++observed->releases;
                break;
            }
        }
    }
    std::free(pointer);
}

void clear_borrowed(ScreenChildLink*& head)
{
    while (head)
    {
        auto* link = head;
        head = link->next;
        link->item = nullptr;
        delete link;
    }
}
bool rectangle(const ScreenRectangle& a, const ScreenRectangle& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

// The parser is a typed borrowed prefix. Every item and link it publishes is
// genuinely constructed. No full parser/manager lifetime is claimed here.
struct Environment
{
    Guarded<GuiParserState> parser;
    Ledger ledger;
    ScreenRectangle saved_rectangle = gui_defined_item_rectangle;
    Environment()
    {
        require(observed == nullptr, "nested scalar observation");
        gui_defined_item_rectangle = {5, 7, 17, 23};
        parser.value.word_38 = 0x13579bdfU;
        parser.value.word_3c = 0x2468ace0U;
        ledger.parser = &parser.value;
        observed = &ledger;
    }
    ~Environment()
    {
        clear();
        observed = nullptr;
        gui_defined_item_rectangle = saved_rectangle;
    }
    void clear()
    {
        auto& p = parser.value;
        clear_borrowed(p.defined_items);
        clear_borrowed(p.context_head);
        clear_borrowed(p.registry_head);
        p.current_context = p.previous_context = nullptr;
        p.alternate_context_10 = p.alternate_context_14 = nullptr;
        p.registry_tail = nullptr;
        p.last_registered_item = nullptr;
        while (p.items_head)
        {
            auto* link = p.items_head;
            p.items_head = link->next;
            delete link; // Real kind3 destructor frees name/ring, never child payloads.
            ++released_items;
        }
        p.items_tail = nullptr;
    }
    void verify() const
    {
        const auto& p = parser.value;
        require(!ledger.overflow && p.manager == nullptr && p.registry_head == nullptr &&
                    p.registry_tail == nullptr && p.alternate_context_10 == nullptr &&
                    p.alternate_context_14 == nullptr && p.word_38 == 0x13579bdfU &&
                    p.word_3c == 0x2468ace0U &&
                    rectangle(gui_defined_item_rectangle, {5, 7, 17, 23}),
            "unrelated parser/global state changed");
        parser.verify();
    }
    void verify_released() const
    {
        require(ledger.count == ledger.releases, "tracked scalar owner leaked");
        for (unsigned i = 0; i < ledger.count; ++i)
        {
            require(ledger.entries[i].live == nullptr, "scalar storage remains live");
        }
    }
};

GuiDefinedItem* defined(ScreenChildLink* link, const char* name, const char* input_name)
{
    require(link && link->item, "missing real owning link");
    auto* item = dynamic_cast<GuiDefinedItem*>(link->item);
    require(item && item->kind_28 == 3 && item->identifier != input_name &&
                std::strcmp(item->identifier, name) == 0,
        "genuine defined owner or copied name differs");
    require(rectangle(item->rectangle, {5, 7, 17, 23}) &&
                rectangle(item->saved_rectangle, {5, 7, 17, 23}) && item->elapsed_24 == 0.0f &&
                item->state_30 == 0 && item->last_x == 0 && item->last_y == 0 &&
                item->owned_text == nullptr && item->state_40 == 0 && item->state_41 == 0 &&
                item->state_42 == 0 && item->preparation_flag == 1,
        "genuine base initialization differs");
    return item;
}
void empty(const ScreenChildRing& ring)
{
    require(ring.head && ring.head->next == ring.head && ring.head->previous == ring.head,
        "genuine ring is not empty");
}
void single(const ScreenChildRing& ring, const ScreenItem* child)
{
    require(ring.head && ring.head->next != ring.head && ring.head->previous == ring.head->next,
        "genuine child ring cardinality differs");
    auto* node = ring.head->next;
    require(node->next == ring.head && node->previous == ring.head && node->item == child,
        "real registration attachment differs");
}

void normal_groups()
{
    Seed seed(require);
    {
        Environment environment;
        auto& p = environment.parser.value;
        auto& ledger = environment.ledger;
        auto first = input(" \tFirst\r\n { }");
        auto second = input("First{ }");
        auto third = input("\tThird\r\n\t{rest");
        current_case = "empty definition index";
        p.buffer = first.value.data();
        ++calls;
        p.ParseDefine();
        require(p.cursor == 11 && first.value[p.cursor] == ' ', "first literal cursor differs");
        auto* a = defined(p.items_head, "First", first.value.data() + 2);
        require(a->word_2c == 0x13579bdfU && p.items_head == p.items_tail &&
                    p.items_head->next == nullptr && p.last_registered_item == a &&
                    p.current_context == a && p.previous_context == a && p.context_head == nullptr,
            "first registration/context differs");
        auto* index_a = p.defined_items;
        require(
            index_a && index_a != p.items_head && index_a->item == a && index_a->next == nullptr,
            "first borrowed definition index differs");
        empty(a->children);
        require(ledger.count == 4 && ledger.entries[0].live == a &&
                    ledger.entries[0].size == sizeof(GuiDefinedItem) &&
                    ledger.entries[1].live == a->identifier && ledger.entries[1].size == 6 &&
                    ledger.entries[2].live == p.items_head && ledger.entries[2].size == 16 &&
                    ledger.entries[3].live == index_a && ledger.entries[3].size == 16,
            "actual first allocation owners differ");
        require(ledger.entries[0].before.cursor == 10 && ledger.entries[1].before.cursor == 10 &&
                    ledger.entries[2].before.cursor == 11 && ledger.entries[2].before.items == 0 &&
                    ledger.entries[3].before.current == id(a) &&
                    ledger.entries[3].before.last == id(a) &&
                    ledger.entries[3].before.definitions == 0,
            "construction/brace/registration/context/index order differs");
        environment.verify();
        seed.check_free({1, 2, 3, 4, 5, 6, 7});

        current_case = "duplicate name and third tail append";
        auto* owner_a = p.items_head;
        p.buffer = second.value.data();
        p.cursor = 0;
        ++calls;
        p.ParseDefine();
        auto* b = defined(p.items_tail, "First", second.value.data());
        auto* owner_b = p.items_tail;
        auto* index_b = index_a->next;
        auto* history_a = p.context_head;
        require(p.cursor == 6 && b != a && b->identifier != a->identifier &&
                    b->word_2c == 0x13579bdfU && p.items_head == owner_a &&
                    owner_a->next == owner_b && owner_b->next == nullptr &&
                    p.defined_items == index_a && index_a->item == a && index_b &&
                    index_b->item == b && index_b->next == nullptr && p.current_context == b &&
                    p.previous_context == a && history_a && history_a->item == a &&
                    history_a->next == nullptr,
            "duplicate definition or nested publication differs");
        single(a->children, b);
        empty(b->children);
        require(ledger.count == 9 && ledger.entries[7].size == 16 &&
                    ledger.entries[7].before.current == id(a) &&
                    ledger.entries[7].before.tail == id(owner_b) &&
                    ledger.entries[7].before.last == id(b) &&
                    ledger.entries[8].before.current == id(b) &&
                    ledger.entries[8].before.history == id(history_a) &&
                    ledger.entries[8].before.definitions == id(index_a),
            "registration must precede history and final borrowed index");

        p.buffer = third.value.data();
        p.cursor = 0;
        ++calls;
        p.ParseDefine();
        auto* c = defined(p.items_tail, "Third", third.value.data() + 1);
        require(p.cursor == 10 && std::strcmp(p.buffer + p.cursor, "rest") == 0 &&
                    c->word_2c == 0x13579bdfU && p.items_head == owner_a &&
                    owner_a->next == owner_b && owner_b->next == p.items_tail &&
                    p.items_tail->next == nullptr && p.defined_items == index_a &&
                    index_a->next == index_b && index_b->next && index_b->next->item == c &&
                    index_b->next->next == nullptr && p.current_context == c &&
                    p.previous_context == b && p.context_head == history_a && history_a->next &&
                    history_a->next->item == b && history_a->next->next == nullptr,
            "third append changed earlier owners/index/history");
        single(a->children, b);
        single(b->children, c);
        empty(c->children);
        require(a->word_2c == 0x13579bdfU && b->word_2c == 0x13579bdfU && ledger.count == 14,
            "ParseDefine renumbered a prior item or changed allocation count");
        require(first.value == std::to_array(" \tFirst\r\n { }") &&
                    second.value == std::to_array("First{ }") &&
                    third.value == std::to_array("\tThird\r\n\t{rest"),
            "script bytes changed");
        first.verify();
        second.verify();
        third.verify();
        environment.verify();
        seed.check_free({5, 6, 7});

        p.PopContext();
        ++pops;
        require(p.current_context == b && p.previous_context == a && p.context_head == history_a,
            "first real context pop differs");
        p.PopContext();
        ++pops;
        require(p.current_context == a && p.previous_context == nullptr && !p.context_head,
            "second real context pop differs");
        p.PopContext();
        ++pops;
        require(!p.current_context && !p.previous_context && p.last_registered_item == c,
            "empty history pop changed owning publication");
        environment.clear();
        environment.verify_released();
        require(released_items == 3 && calls == 3 && pops == 3, "normal ownership totals differ");
    }
    seed.check_free({3, 1, 4, 0, 2, 5, 6, 7});
}

Seed* fatal_seed = nullptr;
Environment* fatal_environment = nullptr;
constexpr char fatal_input[] = "Broken)X";
void fatal_cleanup()
{
    try
    {
        auto& e = *fatal_environment;
        auto& p = e.parser.value;
        require(p.cursor == 7 && calls == 1 && !p.items_head && !p.items_tail && !p.defined_items &&
                    !p.current_context && !p.previous_context && !p.context_head &&
                    !p.last_registered_item && e.ledger.count == 2,
            "fatal path published the completed definition");
        require(e.ledger.entries[0].size == sizeof(GuiDefinedItem), "missing unpublished owner");
        // The allocator only recorded raw storage. Real Fatal is reached after
        // the genuine constructor completed, so this object is now live.
        auto* item = static_cast<GuiDefinedItem*>(e.ledger.entries[0].live);
        require(item && item->kind_28 == 3 && std::strcmp(item->identifier, "Broken") == 0 &&
                    e.ledger.entries[1].live == item->identifier && e.ledger.entries[1].size == 7,
            "completed unpublished owner/name differs");
        empty(item->children);
        e.verify();
        fatal_seed->check_free({1, 2, 3, 4, 5, 6, 7});
        delete item; // Observer cleanup of the original completed-item leak, not a game fix.
        ++released_items;
        e.verify_released();
        delete fatal_environment;
        fatal_environment = nullptr;
        fatal_seed->check_initial_order();
        delete fatal_seed;
        fatal_seed = nullptr;
        std::printf(
            "gui-parse-define fatal-cleanup unpublished=1 released=1 checks=%u pass\n", checks);
    }
    catch (...)
    {
        std::fputs("gui-parse-define fatal-cleanup failed\n", stderr);
        std::_Exit(2);
    }
}
} // namespace

// Actual host scalar allocations with bounded observation only. Reconstructed
// constructors, links, ring allocation/return, Fatal and all callbacks remain real.
void* operator new(std::size_t bytes)
{
    for (;;)
    {
        if (void* pointer = std::malloc(bytes ? bytes : 1))
        {
            observe_allocation(pointer, bytes);
            return pointer;
        }
        auto handler = std::get_new_handler();
        if (!handler)
        {
            throw std::bad_alloc{};
        }
        handler();
    }
}
void operator delete(void* pointer) noexcept
{
    release(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept
{
    release(pointer);
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::strcmp(argv[1], "--fatal-brace") == 0)
        {
            current_case = "missing opening brace";
            fatal_seed = new Seed(require);
            fatal_environment = new Environment;
            fatal_environment->parser.value.buffer = fatal_input;
            require(std::atexit(fatal_cleanup) == 0, "atexit registration failed");
            std::fputs("gui-parse-define fatal-before\n", stderr);
            std::fflush(stderr);
            ++calls;
            fatal_environment->parser.value.ParseDefine();
            std::fputs("gui-parse-define fatal-returned\n", stderr);
            std::_Exit(3);
        }
        require(argc == 1, "unexpected arguments");
        normal_groups();
        std::printf("gui-parse-define groups=2 calls=%u pops=%u released=%u checks=%u pass\n",
            calls,
            pops,
            released_items,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "gui-parse-define %s: %s\n", current_case, failure.reason);
        return 2;
    }
}
