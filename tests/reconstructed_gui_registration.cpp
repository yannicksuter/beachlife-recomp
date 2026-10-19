#include "startup_gui_parser.h"
#include "startup_screen_child_link.h"
#include "startup_screen_item.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>

namespace
{
constexpr unsigned guard_size = 32;
constexpr unsigned capacity = 8;
constexpr unsigned no_slot = capacity;
enum Item : unsigned
{
    None,
    A,
    B,
    C
};
struct Node
{
    unsigned slot;
    Item item;
};
struct Chain
{
    unsigned count;
    std::array<Node, 5> nodes;
    unsigned tail;
};
struct Call
{
    Item item;
    unsigned char named;
    std::array<Item, 3> contexts;
    Item callback;
    unsigned int before_word;
    unsigned int after_word;
    unsigned allocations;
    Chain registry;
    Chain items;
};
struct Literal
{
    const char* name;
    bool seed_heads_without_tails;
    unsigned count;
    std::array<Call, 4> calls;
};

// Literal node numbers are allocation identities, never inferred by traversing
// the implementation's output. Each list has distinct nodes and borrowed items.
constexpr std::array<Literal, 5> literals{{
    {"post-callback word and two empty lists",
        false,
        1,
        {{
            {B,
                1,
                {A, B, C},
                C,
                0x13579bdfU,
                0x2468ace0U,
                2,
                {1, {{{0, B}}}, 0},
                {1, {{{1, B}}}, 1}},
        }}},
    {"append, repeated payloads and zero flags",
        false,
        4,
        {{
            {A, 0, {None, None, None}, None, 0, 0, 1, {0, {}, no_slot}, {1, {{{0, A}}}, 0}},
            {B,
                0xff,
                {A, B, None},
                B,
                0xdeadbeefU,
                0x80000000U,
                2,
                {1, {{{1, B}}}, 1},
                {2, {{{0, A}, {2, B}}}, 2}},
            {A,
                0x80,
                {C, None, None},
                C,
                0x7fffffffU,
                0xffffffffU,
                2,
                {2, {{{1, B}, {3, A}}}, 3},
                {3, {{{0, A}, {2, B}, {4, A}}}, 4}},
            {A,
                0,
                {None, None, None},
                None,
                0x2468ace0U,
                0x2468ace0U,
                1,
                {2, {{{1, B}, {3, A}}}, 3},
                {4, {{{0, A}, {2, B}, {4, A}, {5, A}}}, 5}},
        }}},
    {"receiver aliases the incoming item",
        false,
        2,
        {{
            {A,
                2,
                {A, A, None},
                A,
                0xfffffff0U,
                0x10203040U,
                2,
                {1, {{{0, A}}}, 0},
                {1, {{{1, A}}}, 1}},
            {C,
                1,
                {A, B, C},
                C,
                0x12345678U,
                0,
                2,
                {2, {{{0, A}, {2, C}}}, 2},
                {2, {{{1, A}, {3, C}}}, 3}},
        }}},
    {"null tails replace live heads without releasing old links",
        true,
        1,
        {{
            {C,
                1,
                {None, None, None},
                None,
                0xa5a5a5a5U,
                0xa5a5a5a5U,
                2,
                {1, {{{2, C}}}, 2},
                {1, {{{3, C}}}, 3}},
        }}},
    {"unnamed registration leaves the null-tail registry alone",
        true,
        2,
        {{
            {C,
                0,
                {None, None, None},
                None,
                0x01020304U,
                0x01020304U,
                1,
                {1, {{{0, A}}}, no_slot},
                {1, {{{2, C}}}, 2}},
            {A,
                1,
                {B, A, None},
                A,
                0x33333333U,
                0x55555555U,
                2,
                {1, {{{3, A}}}, 3},
                {2, {{{2, C}, {4, A}}}, 4}},
        }}},
}};

struct Counts
{
    unsigned calls = 0;
    unsigned callbacks = 0;
    unsigned links = 0;
    unsigned releases = 0;
    unsigned item_constructions = 0;
    unsigned item_destructions = 0;
    unsigned long long checks = 0;
    unsigned long long byte_checks = 0;
} counts;
unsigned current_case = 0;
unsigned current_operation = 0;

void require(bool condition, const char* reason) noexcept
{
    ++counts.checks;
    if (!condition)
    {
        std::fprintf(stderr, "case %u operation %u: %s\n", current_case, current_operation, reason);
        std::fflush(stderr);
        std::_Exit(2);
    }
}

void same_bytes(const void* actual, const void* expected, std::size_t size, const char* reason)
{
    const auto* a = static_cast<const unsigned char*>(actual);
    const auto* b = static_cast<const unsigned char*>(expected);
    bool same = true;
    for (std::size_t i = 0; i < size; ++i)
    {
        ++counts.byte_checks;
        same = (a[i] == b[i]) && same;
    }
    require(same, reason);
}

void guard(const unsigned char* bytes, unsigned char value)
{
    for (unsigned i = 0; i < guard_size; ++i)
    {
        ++counts.byte_checks;
        require(bytes[i] == value, "outside guard changed");
    }
}

struct Fixture;
thread_local Fixture* active = nullptr;
void observe_callback(ScreenItem* receiver, ScreenItem* item);
void* allocate_link(std::size_t size);
void release_link(void* pointer, std::size_t size, bool sized) noexcept;

// These overrides observe the generated test interface only. They do not
// implement any original Dialog/Table/Tab attachment method.
struct GeneratedItem final : ScreenItem
{
    explicit GeneratedItem(unsigned index)
        : ScreenItem(ScreenRectangle{-7, 11, 31, 53})
    {
        constexpr std::array<int, 3> kinds{0, 1, 22};
        kind_28 = kinds[index];
        word_2c = 0x13572468U + index;
        std::memset(unknown_31, 0x57 + static_cast<int>(index), sizeof(unknown_31));
        ++counts.item_constructions;
    }
    ~GeneratedItem() override
    {
        ++counts.item_destructions;
    }
    void Unresolved04() override
    {
        require(false, "unobserved item slot called");
    }
    void Unresolved08() override
    {
        require(false, "unobserved item slot called");
    }
    void Unresolved0c() override
    {
        require(false, "unobserved item slot called");
    }
    void Unresolved10(ScreenItem* item) override
    {
        observe_callback(this, item);
    }
    void Unresolved1c() override
    {
        require(false, "unobserved item slot called");
    }
    void Unresolved20() override
    {
        require(false, "unobserved item slot called");
    }
    void Unresolved24() override
    {
        require(false, "unobserved item slot called");
    }
    void Unresolved28() override
    {
        require(false, "unobserved item slot called");
    }
};

template <typename T> struct Guarded
{
    std::array<unsigned char, guard_size> before{};
    T value{};
    std::array<unsigned char, guard_size> after{};
};
struct ItemStorage
{
    std::array<unsigned char, guard_size> before{};
    alignas(GeneratedItem) unsigned char bytes[sizeof(GeneratedItem)]{};
    std::array<unsigned char, guard_size> after{};
};
struct Allocation
{
    void* backing = nullptr;
    unsigned char* storage = nullptr;
    ScreenChildLink* node = nullptr;
    bool live = false;
};
enum class Stage
{
    Idle,
    Seed,
    Register,
    Cleanup
};
} // namespace

// Paired fixture-only host scalar providers; the archive's ordinary methods
// retain their new/delete expressions and do not contain allocator substitutes.
void* operator new(std::size_t size)
{
    if (active)
    {
        return allocate_link(size);
    }
    for (;;)
    {
        if (void* pointer = std::malloc(size ? size : 1))
        {
            return pointer;
        }
        std::new_handler handler = std::get_new_handler();
        if (!handler)
        {
            throw std::bad_alloc();
        }
        handler();
    }
}
void operator delete(void* pointer) noexcept
{
    release_link(pointer, 0, false);
}
void operator delete(void* pointer, std::size_t size) noexcept
{
    release_link(pointer, size, true);
}

namespace
{
static_assert(std::numeric_limits<unsigned char>::digits == 8);
static_assert(std::numeric_limits<unsigned int>::digits == 32);
static_assert(sizeof(void*) == 8);
static_assert(sizeof(GuiParserState) == 120);
static_assert(sizeof(ScreenChildLink) == 16);
static_assert(guard_size % alignof(ScreenChildLink) == 0);
static_assert(std::is_standard_layout_v<GuiParserState>);
static_assert(std::is_trivially_copyable_v<GuiParserState>);
static_assert(std::is_standard_layout_v<ScreenChildLink>);

bool within(std::size_t position, std::size_t offset, std::size_t size)
{
    return position >= offset && position - offset < size;
}

struct Fixture
{
    Guarded<GuiParserState> parser{};
    ScreenChildLink history{nullptr};
    std::array<ItemStorage, 3> storage{};
    std::array<GeneratedItem*, 3> items{};
    std::array<std::array<unsigned char, sizeof(GeneratedItem)>, 3> expected_items{};
    std::array<unsigned char, sizeof(GuiParserState)> before_parser{};
    std::array<unsigned char, sizeof(ScreenChildLink)> before_history{};
    std::array<std::array<unsigned char, sizeof(ScreenChildLink)>, 2> seed_before{};
    std::array<Allocation, capacity> ledger{};
    const Literal& literal;
    const Call* call = nullptr;
    Stage stage = Stage::Idle;
    unsigned allocated = 0;
    unsigned before_allocations = 0;
    unsigned callback_count = 0;
    unsigned destroyed_before = counts.item_destructions;
    ScreenItem* before_last = nullptr;
    unsigned int before_item_word = 0;

    explicit Fixture(const Literal& value)
        : literal(value)
    {
        parser.before.fill(0x43);
        parser.after.fill(0x91);
        for (unsigned i = 0; i < items.size(); ++i)
        {
            storage[i].before.fill(0x26);
            storage[i].after.fill(0xd3);
            items[i] = ::new (static_cast<void*>(storage[i].bytes)) GeneratedItem(i);
            std::memcpy(expected_items[i].data(),
                static_cast<const void*>(items[i]),
                sizeof(GeneratedItem));
        }
        history.item = item(A);
        std::memcpy(before_history.data(), &history, sizeof(history));
        parser.value.cursor = 0xfedcba98U;
        parser.value.buffer = "unconsumed generated input";
        parser.value.context_head = &history;
        parser.value.previous_context = item(B);
        parser.value.last_registered_item = item(C);
        std::memset(parser.value.unknown_24, 0xa7, sizeof(parser.value.unknown_24));
    }

    ScreenItem* item(Item value) const
    {
        require(value <= C, "invalid literal item");
        return value == None ? nullptr : items[static_cast<unsigned>(value) - 1];
    }
    ScreenChildLink* node(unsigned slot) const
    {
        if (slot == no_slot)
        {
            return nullptr;
        }
        require(slot < allocated && ledger[slot].live, "literal node is not live");
        return std::launder(ledger[slot].node);
    }
    void check_chain(ScreenChildLink* head, ScreenChildLink* tail, const Chain& expected)
    {
        require(expected.count <= expected.nodes.size(), "literal chain exceeds bounds");
        require(
            head == (expected.count ? node(expected.nodes[0].slot) : nullptr), "list head differs");
        require(tail == node(expected.tail), "list tail differs");
        for (unsigned i = 0; i < expected.count; ++i)
        {
            // Access each independently known live allocation; do not follow an
            // unverified pointer from the implementation's graph.
            ScreenChildLink* current = node(expected.nodes[i].slot);
            require(current->item == item(expected.nodes[i].item), "list payload differs");
            ScreenChildLink* next =
                i + 1 < expected.count ? node(expected.nodes[i + 1].slot) : nullptr;
            require(current->next == next, "list next differs");
        }
    }
    void check_lists()
    {
        require(call != nullptr, "missing literal call");
        check_chain(parser.value.registry_head, parser.value.registry_tail, call->registry);
        check_chain(parser.value.items_head, parser.value.items_tail, call->items);
    }
    void check_preserved()
    {
        const auto* now = reinterpret_cast<const unsigned char*>(&parser.value);
        for (std::size_t i = 0; i < sizeof(GuiParserState); ++i)
        {
            if (!within(i, offsetof(GuiParserState, registry_head), sizeof(ScreenChildLink*)) &&
                !within(i, offsetof(GuiParserState, registry_tail), sizeof(ScreenChildLink*)) &&
                !within(i, offsetof(GuiParserState, items_head), sizeof(ScreenChildLink*)) &&
                !within(i, offsetof(GuiParserState, items_tail), sizeof(ScreenChildLink*)) &&
                !within(i, offsetof(GuiParserState, last_registered_item), sizeof(ScreenItem*)) &&
                !within(i, offsetof(GuiParserState, word_38), sizeof(unsigned int)))
            {
                ++counts.byte_checks;
                require(now[i] == before_parser[i], "unrelated parser byte changed");
            }
        }
        same_bytes(&history, before_history.data(), sizeof(history), "context history changed");
        for (unsigned i = 0; i < items.size(); ++i)
        {
            same_bytes(items[i],
                expected_items[i].data(),
                sizeof(GeneratedItem),
                "unrelated item byte changed");
            guard(storage[i].before.data(), 0x26);
            guard(storage[i].after.data(), 0xd3);
        }
        guard(parser.before.data(), 0x43);
        guard(parser.after.data(), 0x91);
        for (unsigned i = 0; i < allocated; ++i)
        {
            require(ledger[i].live, "registration released a link");
            guard(static_cast<unsigned char*>(ledger[i].backing), 0x62);
            guard(ledger[i].storage + sizeof(ScreenChildLink), 0xbc);
        }
        if (literal.seed_heads_without_tails)
        {
            for (unsigned i = 0; i < 2; ++i)
            {
                same_bytes(node(i),
                    seed_before[i].data(),
                    sizeof(ScreenChildLink),
                    "retained old head changed");
            }
        }
        require(counts.item_destructions == destroyed_before,
            "borrowed item destroyed during registration");
    }
    void callback(ScreenItem* receiver, ScreenItem* incoming)
    {
        require(stage == Stage::Register && call, "callback outside registration");
        require(receiver == item(call->callback) && call->callback != None,
            "callback receiver priority differs");
        require(incoming == item(call->item), "callback item identity differs");
        require(callback_count++ == 0, "more than one callback");
        require(allocated == before_allocations + call->allocations,
            "callback preceded list allocation");
        check_lists();
        require(parser.value.last_registered_item == before_last,
            "last item published before callback");
        require(incoming->word_2c == before_item_word, "item word published before callback");
        require(parser.value.word_38 == call->before_word, "word changed before callback");
        check_preserved();
        parser.value.word_38 = call->after_word;
        ++counts.callbacks;
    }
    void run()
    {
        require(active == nullptr, "nested fixture allocator");
        active = this;
        if (literal.seed_heads_without_tails)
        {
            stage = Stage::Seed;
            parser.value.registry_head = new ScreenChildLink(item(A));
            parser.value.items_head = new ScreenChildLink(item(B));
            require(allocated == 2, "seed allocation count differs");
            for (unsigned i = 0; i < 2; ++i)
            {
                std::memcpy(seed_before[i].data(), node(i), sizeof(ScreenChildLink));
            }
            // Both tail fields intentionally remain null. The method's head
            // replacement is preserved, not repaired by this fixture.
        }
        for (current_operation = 0; current_operation < literal.count; ++current_operation)
        {
            call = &literal.calls[current_operation];
            parser.value.current_context = item(call->contexts[0]);
            parser.value.alternate_context_10 = item(call->contexts[1]);
            parser.value.alternate_context_14 = item(call->contexts[2]);
            parser.value.word_38 = call->before_word;
            before_allocations = allocated;
            callback_count = 0;
            before_last = parser.value.last_registered_item;
            before_item_word = item(call->item)->word_2c;
            std::memcpy(before_parser.data(), &parser.value, sizeof(GuiParserState));
            stage = Stage::Register;
            parser.value.RegisterItem(item(call->item), call->named);
            stage = Stage::Idle;
            ++counts.calls;
            require(allocated == before_allocations + call->allocations,
                "registration allocation count differs");
            require(callback_count == (call->callback == None ? 0U : 1U), "callback count differs");
            require(parser.value.last_registered_item == item(call->item),
                "last registered item differs");
            require(parser.value.word_38 == call->after_word, "parser word differs");
            require(item(call->item)->word_2c == call->after_word, "registered item word differs");
            const unsigned index = static_cast<unsigned>(call->item) - 1;
            const auto* base = reinterpret_cast<const unsigned char*>(items[index]);
            const auto* field = reinterpret_cast<const unsigned char*>(&items[index]->word_2c);
            const std::size_t offset = static_cast<std::size_t>(field - base);
            require(
                offset + sizeof(unsigned int) <= sizeof(GeneratedItem), "item word outside object");
            std::memcpy(
                expected_items[index].data() + offset, &call->after_word, sizeof(unsigned int));
            check_lists();
            check_preserved();
        }
        // Explicit test ownership: detach both observed lists, retain every
        // allocation in the ledger, and clear borrowed payloads before actual
        // link destruction/free. Orphaned old heads are included, not leaked.
        parser.value.registry_head = parser.value.registry_tail = nullptr;
        parser.value.items_head = parser.value.items_tail = nullptr;
        stage = Stage::Cleanup;
        for (unsigned i = 0; i < allocated; ++i)
        {
            ScreenChildLink* link = node(i);
            link->next = nullptr;
            link->item = nullptr;
            delete link;
            require(!ledger[i].live, "link cleanup did not release storage");
        }
        active = nullptr;
        stage = Stage::Idle;
        require(
            counts.item_destructions == destroyed_before, "link cleanup destroyed borrowed item");
        history.item = nullptr;
        for (unsigned i = static_cast<unsigned>(items.size()); i > 0; --i)
        {
            items[i - 1]->~GeneratedItem();
        }
        require(counts.item_destructions == destroyed_before + 3, "item lifetime count differs");
    }
};

void observe_callback(ScreenItem* receiver, ScreenItem* item)
{
    require(active != nullptr, "callback lacks a live fixture");
    active->callback(receiver, item);
}

void* allocate_link(std::size_t size)
{
    require(active && (active->stage == Stage::Seed || active->stage == Stage::Register),
        "unexpected allocation stage");
    require(size == sizeof(ScreenChildLink) && active->allocated < capacity,
        "unexpected allocation request");
    if (active->stage == Stage::Register)
    {
        require(active->allocated < active->before_allocations + active->call->allocations,
            "extra registration allocation");
    }
    constexpr std::size_t bytes = 2 * guard_size + sizeof(ScreenChildLink);
    void* backing = std::malloc(bytes);
    require(backing != nullptr, "fixture storage allocation failed");
    auto* raw = ::new (backing) unsigned char[bytes];
    std::memset(raw, 0x62, guard_size);
    std::memset(raw + guard_size, 0xc5, sizeof(ScreenChildLink));
    std::memset(raw + guard_size + sizeof(ScreenChildLink), 0xbc, guard_size);
    auto& entry = active->ledger[active->allocated++];
    entry = {backing,
        raw + guard_size,
        static_cast<ScreenChildLink*>(static_cast<void*>(raw + guard_size)),
        true};
    require(reinterpret_cast<std::uintptr_t>(entry.storage) % alignof(ScreenChildLink) == 0,
        "node storage is misaligned");
    ++counts.links;
    return entry.storage;
}

void release_link(void* pointer, std::size_t size, bool sized) noexcept
{
    if (!pointer)
    {
        return;
    }
    if (!active)
    {
        std::free(pointer);
        return;
    }
    require(active->stage == Stage::Cleanup, "registration unexpectedly released storage");
    Allocation* found = nullptr;
    for (unsigned i = 0; i < active->allocated; ++i)
    {
        if (active->ledger[i].storage == pointer)
        {
            require(found == nullptr, "duplicate allocation identity");
            found = &active->ledger[i];
        }
    }
    require(found && found->live, "release is not a live fixture allocation");
    require(!sized || size == sizeof(ScreenChildLink), "sized delete extent differs");
    guard(static_cast<unsigned char*>(found->backing), 0x62);
    guard(found->storage + sizeof(ScreenChildLink), 0xbc);
    ScreenChildLink* null_next = nullptr;
    ScreenItem* null_item = nullptr;
    // The typed node is dead here. Inspect its retained raw backing only.
    same_bytes(found->storage + offsetof(ScreenChildLink, next),
        &null_next,
        sizeof(null_next),
        "node was not detached before release");
    same_bytes(found->storage + offsetof(ScreenChildLink, item),
        &null_item,
        sizeof(null_item),
        "borrowed payload was not cleared before release");
    found->live = false;
    std::free(found->backing);
    ++counts.releases;
}
} // namespace

int main()
{
    for (current_case = 0; current_case < literals.size(); ++current_case)
    {
        Fixture fixture(literals[current_case]);
        fixture.run();
    }
    require(counts.calls == 10 && counts.callbacks == 6, "literal call totals differ");
    require(counts.links == 21 && counts.releases == 21, "literal link totals differ");
    require(counts.item_constructions == 15 && counts.item_destructions == 15,
        "borrowed item totals differ");
    std::printf(
        "reconstructed GUI registration: 5 cases, %u calls, %u callbacks, %u allocated links, "
        "%u item lifetimes, %llu checks, %llu byte checks\n",
        counts.calls,
        counts.callbacks,
        counts.links,
        counts.item_destructions,
        counts.checks,
        counts.byte_checks);
}
