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
constexpr unsigned capacity = 6;
constexpr unsigned no_slot = capacity;
enum Item : unsigned
{
    None,
    A,
    B,
    C
};
enum class Action
{
    Pop,
    Push
};
enum class Stage
{
    Seed,
    Push,
    Pop,
    Cleanup
};
struct State
{
    Item current;
    Item previous;
    unsigned count;
    std::array<Item, 3> history;
};
struct Call
{
    Action action;
    Item incoming;
    bool allocates;
    State expected;
};
struct Literal
{
    const char* name;
    State initial;
    unsigned count;
    std::array<Call, 6> calls;
};

// BEGIN LITERAL INPUT/OUTPUT RECORDS
constexpr std::array<Literal, 12> literals{{
    {"empty history clears both contexts",
        {A, C, 0, {None, None, None}},
        1,
        {{
            {Action::Pop, None, false, {None, None, 0, {None, None, None}}},
        }}},
    {"single borrowed history and final clear",
        {B, C, 1, {A, None, None}},
        2,
        {{
            {Action::Pop, None, false, {A, None, 0, {None, None, None}}},
            {Action::Pop, None, false, {None, None, 0, {None, None, None}}},
        }}},
    {"two links publish predecessor before release",
        {C, A, 2, {A, B, None}},
        3,
        {{
            {Action::Pop, None, false, {B, A, 1, {A, None, None}}},
            {Action::Pop, None, false, {A, None, 0, {None, None, None}}},
            {Action::Pop, None, false, {None, None, 0, {None, None, None}}},
        }}},
    {"repeated aliases remain borrowed",
        {A, C, 3, {A, A, A}},
        4,
        {{
            {Action::Pop, None, false, {A, A, 2, {A, A, None}}},
            {Action::Pop, None, false, {A, A, 1, {A, None, None}}},
            {Action::Pop, None, false, {A, None, 0, {None, None, None}}},
            {Action::Pop, None, false, {None, None, 0, {None, None, None}}},
        }}},
    {"three distinct saved payloads",
        {C, C, 3, {C, A, B}},
        4,
        {{
            {Action::Pop, None, false, {B, A, 2, {C, A, None}}},
            {Action::Pop, None, false, {A, C, 1, {C, None, None}}},
            {Action::Pop, None, false, {C, None, 0, {None, None, None}}},
            {Action::Pop, None, false, {None, None, 0, {None, None, None}}},
        }}},
    {"null tail publishes null without traversing payload",
        {B, A, 2, {A, None, None}},
        1,
        {{
            {Action::Pop, None, false, {None, A, 1, {A, None, None}}},
        }}},
    {"null predecessor publication",
        {C, B, 2, {None, A, None}},
        2,
        {{
            {Action::Pop, None, false, {A, None, 1, {None, None, None}}},
            {Action::Pop, None, false, {None, None, 0, {None, None, None}}},
        }}},
    {"single null borrowed payload",
        {A, C, 1, {None, None, None}},
        1,
        {{
            {Action::Pop, None, false, {None, None, 0, {None, None, None}}},
        }}},
    {"null head and distinct later payloads",
        {C, A, 3, {None, B, A}},
        3,
        {{
            {Action::Pop, None, false, {A, B, 2, {None, B, None}}},
            {Action::Pop, None, false, {B, None, 1, {None, None, None}}},
            {Action::Pop, None, false, {None, None, 0, {None, None, None}}},
        }}},
    {"actual push and pop round trip",
        {None, None, 0, {None, None, None}},
        6,
        {{
            {Action::Push, A, false, {A, A, 0, {None, None, None}}},
            {Action::Push, B, true, {B, A, 1, {A, None, None}}},
            {Action::Push, C, true, {C, B, 2, {A, B, None}}},
            {Action::Pop, None, false, {B, A, 1, {A, None, None}}},
            {Action::Pop, None, false, {A, None, 0, {None, None, None}}},
            {Action::Pop, None, false, {None, None, 0, {None, None, None}}},
        }}},
    {"actual push after a pop",
        {C, B, 2, {A, B, None}},
        5,
        {{
            {Action::Pop, None, false, {B, A, 1, {A, None, None}}},
            {Action::Push, C, true, {C, B, 2, {A, B, None}}},
            {Action::Pop, None, false, {B, A, 1, {A, None, None}}},
            {Action::Pop, None, false, {A, None, 0, {None, None, None}}},
            {Action::Pop, None, false, {None, None, 0, {None, None, None}}},
        }}},
    {"null current with history reopened by actual push",
        {None, C, 2, {A, B, None}},
        4,
        {{
            {Action::Push, C, false, {C, C, 2, {A, B, None}}},
            {Action::Pop, None, false, {B, A, 1, {A, None, None}}},
            {Action::Pop, None, false, {A, None, 0, {None, None, None}}},
            {Action::Pop, None, false, {None, None, 0, {None, None, None}}},
        }}},
}};
// END LITERAL INPUT/OUTPUT RECORDS

struct Counters
{
    unsigned cases = 0;
    unsigned pops = 0;
    unsigned pushes = 0;
    unsigned seed_links = 0;
    unsigned push_links = 0;
    unsigned pop_releases = 0;
    unsigned cleanup_releases = 0;
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
    const auto* left = static_cast<const unsigned char*>(actual);
    const auto* right = static_cast<const unsigned char*>(expected);
    bool same = true;
    for (std::size_t i = 0; i < size; ++i)
    {
        ++counts.byte_checks;
        same = (left[i] == right[i]) && same;
    }
    require(same, reason);
}

void guard(const std::array<unsigned char, guard_size>& bytes, unsigned char value)
{
    for (unsigned char byte : bytes)
    {
        ++counts.byte_checks;
        require(byte == value, "outside guard changed");
    }
}

struct GeneratedItem final : ScreenItem
{
    explicit GeneratedItem(ScreenRectangle rectangle, unsigned index)
        : ScreenItem(rectangle)
    {
        constexpr std::array<int, 3> kinds{0, 1, 22};
        kind_28 = kinds[index];
        word_2c = 0x13572468u + index;
        std::memset(unknown_31, 0x57 + static_cast<int>(index), sizeof(unknown_31));
        ++counts.item_constructions;
    }
    ~GeneratedItem() override
    {
        ++counts.item_destructions;
    }
    void Unresolved04() override
    {
        require(false, "unresolved item slot invoked");
    }
    void Unresolved08() override
    {
        require(false, "unresolved item slot invoked");
    }
    void Unresolved0c() override
    {
        require(false, "unresolved item slot invoked");
    }
    void Unresolved10(ScreenItem*) override
    {
        require(false, "unresolved item slot invoked");
    }
    void Unresolved1c() override
    {
        require(false, "unresolved item slot invoked");
    }
    void Unresolved20() override
    {
        require(false, "unresolved item slot invoked");
    }
    void Unresolved24(unsigned char) override
    {
        require(false, "unresolved item slot invoked");
    }
    void Unresolved28() override
    {
        require(false, "unresolved item slot invoked");
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
struct UnrelatedLink
{
    std::array<unsigned char, guard_size> before{};
    ScreenChildLink value{nullptr};
    std::array<unsigned char, guard_size> after{};
};
struct Owned
{
    unsigned char* backing = nullptr;
    unsigned char* returned = nullptr;
    ScreenChildLink* node = nullptr;
    bool live = false;
};
struct Scenario;
struct Provider;
thread_local Provider* active = nullptr;
struct Provider
{
    Scenario& scenario;
    Stage stage;
    const State* expected;
    std::array<unsigned, 3> expected_slots{};
    unsigned released_slot = no_slot;
    unsigned allocations = 0;
    unsigned releases = 0;

    Provider(Scenario& owner, Stage kind, const State* state = nullptr)
        : scenario(owner)
        , stage(kind)
        , expected(state)
    {
        require(active == nullptr, "nested provider scope");
        active = this;
    }
    ~Provider()
    {
        active = nullptr;
    }
    Provider(const Provider&) = delete;
    Provider& operator=(const Provider&) = delete;
};

void* allocate_owned(std::size_t size);
void release_scalar(void* pointer, std::size_t size, bool sized) noexcept;
} // namespace

// Executable-only allocation boundary; every game body and ordinary new/delete
// expression remains real. Unrelated allocations follow the host allocator.
void* operator new(std::size_t size)
{
    if (active)
    {
        return allocate_owned(size);
    }
    for (;;)
    {
        if (void* pointer = std::malloc(size ? size : 1))
        {
            return pointer;
        }
        const auto handler = std::get_new_handler();
        if (!handler)
        {
            throw std::bad_alloc();
        }
        handler();
    }
}
void operator delete(void* pointer) noexcept
{
    release_scalar(pointer, 0, false);
}
void operator delete(void* pointer, std::size_t size) noexcept
{
    release_scalar(pointer, size, true);
}

namespace
{
static_assert(sizeof(int) == 4 && sizeof(unsigned int) == 4);
static_assert(std::is_standard_layout_v<GuiParserState>);
static_assert(std::is_trivially_copyable_v<GuiParserState>);
static_assert(std::is_standard_layout_v<ScreenChildLink>);
static_assert(offsetof(ScreenChildLink, next) == 0);
static_assert(offsetof(ScreenChildLink, item) == sizeof(ScreenChildLink*));
static_assert(sizeof(ScreenChildLink) == sizeof(ScreenChildLink*) + sizeof(ScreenItem*));
static_assert(alignof(ScreenChildLink) <= alignof(std::max_align_t));
static_assert(guard_size % alignof(std::max_align_t) == 0);

bool within(std::size_t byte, std::size_t offset, std::size_t size)
{
    return byte >= offset && byte - offset < size;
}

void allocation_guards(const Owned& owned)
{
    require(owned.live && owned.backing && owned.returned, "allocation is not live");
    for (unsigned i = 0; i < guard_size; ++i)
    {
        counts.byte_checks += 2;
        require(owned.backing[i] == 0xa6 &&
                    owned.backing[guard_size + sizeof(ScreenChildLink) + i] == 0xa6,
            "link allocation guard changed");
    }
}

struct Scenario
{
    Guarded<GuiParserState> parser{};
    Guarded<std::array<char, 8>> input{};
    UnrelatedLink unrelated{};
    std::array<unsigned char, sizeof(ScreenChildLink)> unrelated_before{};
    std::array<ItemStorage, 3> item_storage{};
    std::array<GeneratedItem*, 3> items{};
    std::array<std::array<unsigned char, sizeof(GeneratedItem)>, 3> item_before{};
    std::array<unsigned char, sizeof(GuiParserState)> parser_before{};
    std::array<char, 8> input_before{};
    std::array<Owned, capacity> owned{};
    std::array<unsigned, 3> slots{};
    unsigned history_count = 0;
    unsigned created = 0;
    unsigned live_items = 0;
    unsigned item_destructions_before = counts.item_destructions;
    std::array<std::array<unsigned char, sizeof(ScreenChildLink)>, capacity> node_before{};
    std::array<bool, capacity> has_before{};

    ScreenItem* item(Item id) const
    {
        return id == None ? nullptr : items[static_cast<unsigned>(id) - 1];
    }

    void initialize(const State& initial, unsigned cursor = 0x1234u)
    {
        parser.before.fill(0xa5);
        parser.after.fill(0x5a);
        input.before.fill(0xa5);
        input.after.fill(0x5a);
        input.value = {'Q', '\0', '}', 'x', 'y', 'z', '!', '?'};
        input_before = input.value;
        unrelated.before.fill(0xa5);
        unrelated.after.fill(0x5a);
        std::memcpy(unrelated_before.data(), &unrelated.value, sizeof(unrelated.value));
        constexpr std::array<ScreenRectangle, 3> rectangles{ScreenRectangle{2, 3, 41, 52},
            ScreenRectangle{-8, 7, 60, 91},
            ScreenRectangle{11, -12, 70, 80}};
        for (unsigned i = 0; i < items.size(); ++i)
        {
            item_storage[i].before.fill(0xa5);
            item_storage[i].after.fill(0x5a);
            items[i] =
                ::new (static_cast<void*>(item_storage[i].bytes)) GeneratedItem(rectangles[i], i);
            ++live_items;
            std::memcpy(
                item_before[i].data(), static_cast<const void*>(items[i]), sizeof(GeneratedItem));
        }
        auto& state = parser.value;
        state.manager = nullptr;
        state.cursor = cursor;
        state.buffer = input.value.data();
        state.current_context = item(initial.current);
        state.previous_context = item(initial.previous);
        state.alternate_context_10 = item(A);
        state.alternate_context_14 = item(B);
        state.last_registered_item = item(C);
        state.registry_head = &unrelated.value;
        state.items_head = &unrelated.value;
        state.items_tail = &unrelated.value;
        state.registry_tail = &unrelated.value;
        std::memset(state.unknown_24, 0x79, sizeof(state.unknown_24));
        state.word_38 = 0x86421357u;
        for (unsigned i = 0; i < initial.count; ++i)
        {
            ScreenChildLink* node;
            {
                Provider provider(*this, Stage::Seed);
                node = new ScreenChildLink(item(initial.history[i]));
                require(
                    provider.allocations == 1 && provider.releases == 0, "seed allocation differs");
            }
            owned[created - 1].node = node;
            require(static_cast<void*>(node) == owned[created - 1].returned,
                "seed node storage differs");
            slots[i] = created - 1;
            if (i)
            {
                owned[slots[i - 1]].node->next = node;
            }
            else
            {
                state.context_head = node;
            }
            ++history_count;
            ++counts.seed_links;
        }
        std::memcpy(parser_before.data(), &state, sizeof(state));
        snapshot_nodes();
        check_state(initial, slots, false);
        check_untouched();
    }

    void snapshot_nodes()
    {
        has_before.fill(false);
        for (unsigned i = 0; i < history_count; ++i)
        {
            const unsigned slot = slots[i];
            std::memcpy(node_before[slot].data(), owned[slot].node, sizeof(ScreenChildLink));
            has_before[slot] = true;
        }
    }

    void check_state(
        const State& expected, const std::array<unsigned, 3>& expected_slots, bool at_release)
    {
        require(parser.value.current_context == item(expected.current),
            at_release ? "current context at deallocation differs" : "current context differs");
        require(parser.value.previous_context == item(expected.previous),
            at_release ? "previous context at deallocation differs" : "previous context differs");
        require(
            parser.value.context_head == (expected.count ? owned[expected_slots[0]].node : nullptr),
            "history head differs");
        for (unsigned i = 0; i < expected.count; ++i)
        {
            const unsigned slot = expected_slots[i];
            const auto& allocation = owned[slot];
            require(allocation.live && allocation.node &&
                        static_cast<void*>(allocation.node) == allocation.returned,
                "history is outside its live node storage");
            allocation_guards(allocation);
            ScreenChildLink* next =
                i + 1 < expected.count ? owned[expected_slots[i + 1]].node : nullptr;
            require(allocation.node->next == next, "surviving next link differs");
            require(allocation.node->item == item(expected.history[i]), "borrowed payload differs");
            if (has_before[slot])
            {
                auto bytes = node_before[slot];
                std::memcpy(bytes.data() + offsetof(ScreenChildLink, next), &next, sizeof(next));
                same_bytes(allocation.node,
                    bytes.data(),
                    bytes.size(),
                    "surviving node representation differs");
            }
        }
    }

    void check_untouched()
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&parser.value);
        for (std::size_t i = 0; i < sizeof(GuiParserState); ++i)
        {
            if (!within(i, offsetof(GuiParserState, current_context), sizeof(ScreenItem*)) &&
                !within(i, offsetof(GuiParserState, previous_context), sizeof(ScreenItem*)) &&
                !within(i, offsetof(GuiParserState, context_head), sizeof(ScreenChildLink*)))
            {
                ++counts.byte_checks;
                require(bytes[i] == parser_before[i], "unrelated parser representation changed");
            }
        }
        guard(parser.before, 0xa5);
        guard(parser.after, 0x5a);
        guard(input.before, 0xa5);
        guard(input.after, 0x5a);
        same_bytes(input.value.data(), input_before.data(), input_before.size(), "input changed");
        same_bytes(&unrelated.value,
            unrelated_before.data(),
            unrelated_before.size(),
            "unrelated live link changed");
        guard(unrelated.before, 0xa5);
        guard(unrelated.after, 0x5a);
        for (unsigned i = 0; i < live_items; ++i)
        {
            same_bytes(static_cast<const void*>(items[i]),
                item_before[i].data(),
                sizeof(GeneratedItem),
                "borrowed item representation changed");
            guard(item_storage[i].before, 0xa5);
            guard(item_storage[i].after, 0x5a);
        }
        require(
            counts.item_destructions == item_destructions_before, "a borrowed item was destroyed");
    }

    void invoke(const Call& call)
    {
        snapshot_nodes();
        if (call.action == Action::Push)
        {
            const unsigned old_created = created;
            ++counts.pushes;
            {
                Provider provider(*this, Stage::Push);
                parser.value.PushContext(item(call.incoming));
                require(
                    provider.allocations == (call.allocates ? 1u : 0u) && provider.releases == 0,
                    "push allocation or release count differs");
            }
            require(
                created == old_created + (call.allocates ? 1u : 0u), "push ledger count differs");
            if (call.allocates)
            {
                auto& allocation = owned[created - 1];
                allocation.node =
                    static_cast<ScreenChildLink*>(static_cast<void*>(allocation.returned));
                require(history_count < slots.size(), "history capacity exceeded");
                slots[history_count++] = created - 1;
                ++counts.push_links;
            }
        }
        else
        {
            require(parser.value.current_context != nullptr, "normal fixture would invoke fatal");
            const bool releases = history_count != 0;
            require(call.expected.count == history_count - (releases ? 1u : 0u),
                "literal history count is inconsistent");
            ++counts.pops;
            {
                Provider provider(*this, Stage::Pop, &call.expected);
                provider.expected_slots = slots;
                provider.released_slot = releases ? slots[history_count - 1] : no_slot;
                parser.value.PopContext();
                require(provider.allocations == 0 && provider.releases == (releases ? 1u : 0u),
                    "pop allocation or release count differs");
            }
            history_count = call.expected.count;
        }
        require(history_count == call.expected.count, "history count differs");
        check_state(call.expected, slots, false);
        check_untouched();
    }

    ~Scenario()
    {
        require(active == nullptr, "provider escaped a call");
        parser.value.current_context = nullptr;
        parser.value.previous_context = nullptr;
        parser.value.context_head = nullptr;
        parser.value.alternate_context_10 = nullptr;
        parser.value.alternate_context_14 = nullptr;
        parser.value.last_registered_item = nullptr;
        parser.value.registry_head = nullptr;
        parser.value.items_head = nullptr;
        parser.value.items_tail = nullptr;
        parser.value.registry_tail = nullptr;
        for (unsigned i = 0; i < created; ++i)
        {
            auto& allocation = owned[i];
            if (!allocation.live)
            {
                continue;
            }
            require(allocation.node != nullptr, "cleanup lacks a live node");
            // Any history remaining after observation still borrows its payload.
            // The fixture detaches only its own cleanup nodes before real deletion.
            allocation.node->item = nullptr;
            allocation.node->next = nullptr;
            Provider provider(*this, Stage::Cleanup);
            provider.released_slot = i;
            delete allocation.node;
            require(provider.releases == 1 && provider.allocations == 0,
                "fixture node cleanup differs");
        }
        require(counts.item_destructions == item_destructions_before,
            "node cleanup destroyed a borrowed item");
        for (unsigned i = live_items; i > 0; --i)
        {
            items[i - 1]->~GeneratedItem();
            items[i - 1] = nullptr;
            guard(item_storage[i - 1].before, 0xa5);
            guard(item_storage[i - 1].after, 0x5a);
        }
        require(counts.item_destructions == item_destructions_before + live_items,
            "item lifetime count differs");
        guard(parser.before, 0xa5);
        guard(parser.after, 0x5a);
        same_bytes(&unrelated.value,
            unrelated_before.data(),
            unrelated_before.size(),
            "unrelated link changed before actual member destruction");
        guard(unrelated.before, 0xa5);
        guard(unrelated.after, 0x5a);
    }
};

void* allocate_owned(std::size_t size)
{
    auto& provider = *active;
    auto& scenario = provider.scenario;
    require(provider.stage == Stage::Seed || provider.stage == Stage::Push,
        "pop or cleanup allocated storage");
    require(size == sizeof(ScreenChildLink) && provider.allocations++ == 0,
        "node allocation extent or count differs");
    require(scenario.created < scenario.owned.size(), "node ledger capacity exceeded");
    void* raw = std::malloc(size + 2 * guard_size);
    require(raw != nullptr, "fixture backing allocation failed");
    auto* backing = ::new (raw) unsigned char[size + 2 * guard_size];
    std::memset(backing, 0xa6, size + 2 * guard_size);
    auto& allocation = scenario.owned[scenario.created++];
    allocation.backing = backing;
    allocation.returned = backing + guard_size;
    allocation.live = true;
    require(reinterpret_cast<std::uintptr_t>(allocation.returned) % alignof(std::max_align_t) == 0,
        "node storage is misaligned");
    return allocation.returned;
}

void release_scalar(void* pointer, std::size_t size, bool sized) noexcept
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
    auto& provider = *active;
    require((provider.stage == Stage::Pop || provider.stage == Stage::Cleanup) &&
                provider.released_slot < capacity && provider.releases == 0,
        "unexpected scalar release");
    auto& allocation = provider.scenario.owned[provider.released_slot];
    require(allocation.live && pointer == allocation.returned,
        "release selected a different allocation");
    require(!sized || size == sizeof(ScreenChildLink), "sized delete extent differs");
    allocation_guards(allocation);
    // The real destructor has already ended this node's lifetime. Read only its
    // retained allocation bytes, never members through the expired typed pointer.
    allocation.node = nullptr;
    ScreenChildLink* null_next = nullptr;
    ScreenItem* null_item = nullptr;
    same_bytes(allocation.returned + offsetof(ScreenChildLink, next),
        &null_next,
        sizeof(null_next),
        "released node next representation differs");
    same_bytes(allocation.returned + offsetof(ScreenChildLink, item),
        &null_item,
        sizeof(null_item),
        "borrowed payload was not detached before destruction");
    if (provider.stage == Stage::Pop)
    {
        require(provider.expected != nullptr, "release has no literal oracle");
        provider.scenario.check_state(*provider.expected, provider.expected_slots, true);
        provider.scenario.check_untouched();
        ++counts.pop_releases;
    }
    else
    {
        ++counts.cleanup_releases;
    }
    std::free(allocation.backing);
    allocation.backing = nullptr;
    allocation.returned = nullptr;
    allocation.live = false;
    ++provider.releases;
}

alignas(Scenario) unsigned char fatal_storage[sizeof(Scenario)]{};
Scenario* fatal_scenario = nullptr;
State fatal_initial{};
unsigned fatal_history_count = 0;
unsigned fatal_cursor = 0;
std::array<unsigned char, sizeof(GuiParserState)> fatal_parser_before{};

void fatal_observer()
{
    require(fatal_scenario && active == nullptr, "fatal observer was not armed");
    same_bytes(&fatal_scenario->parser.value,
        fatal_parser_before.data(),
        fatal_parser_before.size(),
        "fatal branch changed parser storage");
    fatal_scenario->check_state(fatal_initial, fatal_scenario->slots, false);
    fatal_scenario->check_untouched();
    require(counts.pop_releases == 0 && counts.push_links == 0,
        "fatal branch changed history ownership");
    fatal_scenario->~Scenario();
    fatal_scenario = nullptr;
    require(counts.seed_links == fatal_history_count &&
                counts.cleanup_releases == fatal_history_count && counts.item_destructions == 3,
        "fatal fixture cleanup differs");
    std::printf("reconstructed GUI context pop fatal: history=%u cursor=%u verified\n",
        fatal_history_count,
        fatal_cursor);
}

int fatal_case(bool history)
{
    current_case = history ? 14 : 13;
    current_operation = 1;
    fatal_history_count = history ? 2u : 0u;
    fatal_cursor = history ? std::numeric_limits<unsigned>::max() : 0u;
    fatal_initial = {None, C, fatal_history_count, {A, B, None}};
    fatal_scenario = ::new (static_cast<void*>(fatal_storage)) Scenario;
    fatal_scenario->initialize(fatal_initial, fatal_cursor);
    std::memcpy(
        fatal_parser_before.data(), &fatal_scenario->parser.value, fatal_parser_before.size());
    require(std::atexit(fatal_observer) == 0, "fatal observer registration failed");
    // Actual PopContext -> actual StartupFatal -> actual CRT exit(0). The observer
    // distinguishes that termination from returning to the test normally.
    fatal_scenario->parser.value.PopContext();
    require(false, "actual fatal branch returned");
    return 2;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--fatal-empty") == 0)
    {
        return fatal_case(false);
    }
    if (argc == 2 && std::strcmp(argv[1], "--fatal-history") == 0)
    {
        return fatal_case(true);
    }
    require(argc == 1, "unexpected fixture argument");
    for (const auto& literal : literals)
    {
        ++current_case;
        ++counts.cases;
        current_operation = 0;
        Scenario scenario;
        scenario.initialize(literal.initial);
        for (unsigned i = 0; i < literal.count; ++i)
        {
            current_operation = i + 1;
            scenario.invoke(literal.calls[i]);
        }
    }
    require(counts.cases == 12 && counts.pops == 31 && counts.pushes == 5,
        "literal case or call totals differ");
    require(counts.seed_links == 21 && counts.push_links == 3 && counts.pop_releases == 23 &&
                counts.cleanup_releases == 1,
        "actual link lifetime totals differ");
    require(counts.item_constructions == 36 && counts.item_destructions == 36 && active == nullptr,
        "borrowed item or provider lifetime differs");
    std::printf(
        "reconstructed GUI context pop: %u cases, %u pops, %u pushes, "
        "%u popped links, %u cleanup links, %u item lifetimes, %llu checks, %llu byte checks\n",
        counts.cases,
        counts.pops,
        counts.pushes,
        counts.pop_releases,
        counts.cleanup_releases,
        counts.item_destructions,
        counts.checks,
        counts.byte_checks);
    return 0;
}
