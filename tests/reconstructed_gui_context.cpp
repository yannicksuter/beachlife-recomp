#include "startup_gui_parser.h"
#include "startup_screen_child_link.h"
#include "startup_screen_item.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

namespace
{
constexpr unsigned guard_size = 32;
constexpr unsigned max_nodes = 4;
enum Item : unsigned
{
    None,
    A,
    B,
    C
};

struct History
{
    unsigned count;
    std::array<Item, 3> items;
};
struct State
{
    Item current;
    Item previous;
    History history;
};
struct Allocation
{
    bool expected;
    State observed;
    Item replace_current;
    bool throw_host_failure;
};
struct Call
{
    Item incoming;
    Allocation allocation;
    bool host_failure;
    State expected;
};
struct Literal
{
    const char* name;
    State initial;
    unsigned count;
    std::array<Call, 4> calls;
};

// BEGIN REVIEWED LITERALS
constexpr std::array<Literal, 11> literals{{
    {"accepted four-call sequence",
        {None, None, {0, {None, None, None}}},
        4,
        {{
            {A, {false, {}, None, false}, false, {A, A, {0, {None, None, None}}}},
            {B,
                {true, {A, A, {0, {None, None, None}}}, None, false},
                false,
                {B, A, {1, {A, None, None}}}},
            {A,
                {true, {B, B, {1, {A, None, None}}}, None, false},
                false,
                {A, B, {2, {A, B, None}}}},
            {A, {true, {A, A, {2, {A, B, None}}}, None, false}, false, {A, A, {3, {A, B, A}}}},
        }}},
    {"null incoming then null-current history",
        {A, C, {0, {None, None, None}}},
        3,
        {{
            {None,
                {true, {A, A, {0, {None, None, None}}}, None, false},
                false,
                {None, A, {1, {A, None, None}}}},
            {B, {false, {}, None, false}, false, {B, B, {1, {A, None, None}}}},
            {C,
                {true, {B, B, {1, {A, None, None}}}, None, false},
                false,
                {C, B, {2, {A, B, None}}}},
        }}},
    {"append after two existing nodes",
        {C, B, {2, {A, B, None}}},
        1,
        {{
            {A, {true, {C, C, {2, {A, B, None}}}, None, false}, false, {A, C, {3, {A, B, C}}}},
        }}},
    {"existing history survives two null-current calls",
        {None, C, {2, {A, B, None}}},
        3,
        {{
            {None, {false, {}, None, false}, false, {None, None, {2, {A, B, None}}}},
            {B, {false, {}, None, false}, false, {B, B, {2, {A, B, None}}}},
            {A, {true, {B, B, {2, {A, B, None}}}, None, false}, false, {A, B, {3, {A, B, B}}}},
        }}},
    {"null existing payload is never dereferenced",
        {B, A, {2, {None, A, None}}},
        1,
        {{
            {C,
                {true, {B, B, {2, {None, A, None}}}, None, false},
                false,
                {C, B, {3, {None, A, B}}}},
        }}},
    {"same current and incoming item",
        {A, C, {0, {None, None, None}}},
        1,
        {{
            {A,
                {true, {A, A, {0, {None, None, None}}}, None, false},
                false,
                {A, A, {1, {A, None, None}}}},
        }}},
    {"all-null state",
        {None, None, {0, {None, None, None}}},
        1,
        {{
            {None, {false, {}, None, false}, false, {None, None, {0, {None, None, None}}}},
        }}},
    {"first-head allocation changes current before constructor argument read",
        {A, B, {0, {None, None, None}}},
        1,
        {{
            {B,
                {true, {A, A, {0, {None, None, None}}}, C, false},
                false,
                {B, A, {1, {C, None, None}}}},
        }}},
    {"append allocation changes current before constructor argument read",
        {A, B, {1, {B, None, None}}},
        1,
        {{
            {None,
                {true, {A, A, {1, {B, None, None}}}, C, false},
                false,
                {None, A, {2, {B, C, None}}}},
        }}},
    {"host allocation failure before first history node",
        {A, C, {0, {None, None, None}}},
        1,
        {{
            {B,
                {true, {A, A, {0, {None, None, None}}}, None, true},
                true,
                {A, A, {0, {None, None, None}}}},
        }}},
    {"host allocation failure while appending",
        {C, A, {2, {A, B, None}}},
        1,
        {{
            {None, {true, {C, C, {2, {A, B, None}}}, None, true}, true, {C, C, {2, {A, B, None}}}},
        }}},
}};
// END REVIEWED LITERALS

struct Counters
{
    unsigned cases = 0;
    unsigned calls = 0;
    unsigned returns = 0;
    unsigned exceptions = 0;
    unsigned method_requests = 0;
    unsigned method_nodes = 0;
    unsigned seed_nodes = 0;
    unsigned node_destructions = 0;
    unsigned item_constructions = 0;
    unsigned item_destructions = 0;
    unsigned allocation_mutations = 0;
    unsigned probe_allocations = 0;
    unsigned owned_releases = 0;
    unsigned sized_releases = 0;
    unsigned unsized_releases = 0;
    unsigned ordinary_allocations = 0;
    unsigned ordinary_releases = 0;
    unsigned long long checks = 0;
    unsigned long long byte_checks = 0;
};
Counters counts;
unsigned current_case = 0;
unsigned current_operation = 0;

struct Failure
{
    unsigned scenario;
    unsigned operation;
    const char* reason;
};

void require(bool condition, const char* reason)
{
    ++counts.checks;
    if (!condition)
    {
        throw Failure{current_case, current_operation, reason};
    }
}

void cleanup_require(bool condition, const char* reason) noexcept
{
    ++counts.checks;
    if (!condition)
    {
        std::fprintf(
            stderr, "case %u operation %u: cleanup: %s\n", current_case, current_operation, reason);
        std::fflush(stderr);
        std::_Exit(2);
    }
}

struct HostAllocationFailure final : std::bad_alloc
{
    const char* what() const noexcept override
    {
        return "generated GUI context allocation failure";
    }
};

struct Owned
{
    unsigned char* backing = nullptr;
    unsigned char* returned = nullptr;
    ScreenChildLink* node = nullptr;
    bool live = false;
    bool node_live = false;
};
struct Ledger
{
    std::array<Owned, max_nodes> entries{};
    unsigned count = 0;
};

struct Scenario;
enum class ScopeKind
{
    Seed,
    Method,
    Probe
};
struct AllocationScope;
struct ReleaseScope;
thread_local AllocationScope* active_allocation = nullptr;
thread_local ReleaseScope* active_release = nullptr;

struct AllocationScope
{
    AllocationScope* previous;
    Ledger& ledger;
    Scenario* scenario;
    ScopeKind kind;
    const Allocation* expected;
    unsigned requests = 0;

    AllocationScope(Ledger& owner, Scenario* value, ScopeKind mode, const Allocation* event)
        : previous(active_allocation)
        , ledger(owner)
        , scenario(value)
        , kind(mode)
        , expected(event)
    {
        require(previous == nullptr && active_release == nullptr, "nested allocation scope");
        active_allocation = this;
    }
    ~AllocationScope()
    {
        active_allocation = previous;
    }
    AllocationScope(const AllocationScope&) = delete;
    AllocationScope& operator=(const AllocationScope&) = delete;
};

struct ReleaseScope
{
    ReleaseScope* previous;
    Owned& owned;
    unsigned releases = 0;

    explicit ReleaseScope(Owned& value) noexcept
        : previous(active_release)
        , owned(value)
    {
        cleanup_require(previous == nullptr && active_allocation == nullptr,
            "nested or premature release scope");
        cleanup_require(
            owned.live && owned.backing && owned.returned, "unknown or duplicate owned release");
        active_release = this;
    }
    ~ReleaseScope()
    {
        cleanup_require(releases == 1 && !owned.live, "owned storage was not released once");
        active_release = previous;
    }
    ReleaseScope(const ReleaseScope&) = delete;
    ReleaseScope& operator=(const ReleaseScope&) = delete;
};

void* allocate_owned(std::size_t bytes);
void release_scalar(void* memory, std::size_t bytes, bool sized) noexcept;
} // namespace

// Executable-only scalar replacement. All prepared method bodies and their
// ordinary new expressions remain unchanged. Other allocation forms are untouched.
void* operator new(std::size_t bytes)
{
    if (active_allocation)
    {
        return allocate_owned(bytes);
    }
    for (;;)
    {
        if (void* memory = std::malloc(bytes ? bytes : 1))
        {
            ++counts.ordinary_allocations;
            return memory;
        }
        const auto handler = std::get_new_handler();
        if (!handler)
        {
            throw std::bad_alloc();
        }
        handler();
    }
}

void operator delete(void* memory) noexcept
{
    release_scalar(memory, 0, false);
}
void operator delete(void* memory, std::size_t bytes) noexcept
{
    release_scalar(memory, bytes, true);
}

namespace
{
static_assert(sizeof(int) == 4 && sizeof(unsigned int) == 4);
static_assert(std::is_standard_layout_v<GuiParserState>);
static_assert(std::is_trivially_copyable_v<GuiParserState>);
static_assert(std::is_standard_layout_v<ScreenChildLink>);
static_assert(alignof(ScreenChildLink) <= alignof(std::max_align_t));
static_assert(guard_size % alignof(std::max_align_t) == 0);

std::uintptr_t address(const void* value)
{
    return reinterpret_cast<std::uintptr_t>(value);
}

void unchanged(const void* actual, const void* before, std::size_t bytes, const char* reason)
{
    const auto* left = static_cast<const unsigned char*>(actual);
    const auto* right = static_cast<const unsigned char*>(before);
    bool same = true;
    for (std::size_t i = 0; i < bytes; ++i)
    {
        ++counts.byte_checks;
        same = (left[i] == right[i]) && same;
    }
    require(same, reason);
}

void guard(
    const std::array<unsigned char, guard_size>& bytes, unsigned char expected, const char* reason)
{
    bool same = true;
    for (unsigned char byte : bytes)
    {
        ++counts.byte_checks;
        same = (byte == expected) && same;
    }
    require(same, reason);
}

void cleanup_guard(const std::array<unsigned char, guard_size>& bytes,
    unsigned char expected,
    const char* reason) noexcept
{
    bool same = true;
    for (unsigned char byte : bytes)
    {
        ++counts.byte_checks;
        same = (byte == expected) && same;
    }
    cleanup_require(same, reason);
}

void owned_guards(const Owned& owned)
{
    require(owned.live && owned.backing && owned.returned, "node lacks live provider storage");
    bool same = true;
    for (unsigned i = 0; i < guard_size; ++i)
    {
        counts.byte_checks += 2;
        same = (owned.backing[i] == 0xa6 &&
                   owned.backing[guard_size + sizeof(ScreenChildLink) + i] == 0xa6) &&
               same;
    }
    require(same, "node allocation guard differs");
}

struct GeneratedItem final : ScreenItem
{
    explicit GeneratedItem(ScreenRectangle initial, unsigned index)
        : ScreenItem(initial)
    {
        // Only these fields are untouched by the actual reconstructed constructor.
        constexpr std::array<int, 3> kinds{0, 1, 22};
        kind_28 = kinds[index];
        word_2c = 0x36363636u + index;
        std::memset(unknown_31, 0x69 + static_cast<int>(index), sizeof(unknown_31));
        ++counts.item_constructions;
    }
    ~GeneratedItem() override
    {
        ++counts.item_destructions;
        // The compiler invokes the actual reconstructed base destructor next.
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
    void Unresolved24() override
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

bool within(std::size_t at, std::size_t offset, std::size_t size)
{
    return at >= offset && at - offset < size;
}

struct Scenario
{
    Ledger ledger{};
    Guarded<GuiParserState> parser{};
    Guarded<std::array<char, 8>> input{};
    std::array<ItemStorage, 3> storage{};
    std::array<GeneratedItem*, 3> items{};
    std::array<std::array<unsigned char, sizeof(GeneratedItem)>, 3> item_before{};
    std::array<unsigned char, sizeof(GuiParserState)> parser_before{};
    std::array<char, 8> input_before{};
    unsigned items_live = 0;
    unsigned destroyed_before = counts.item_destructions;

    ScreenItem* item(Item id) const
    {
        return id == None ? nullptr : items[static_cast<unsigned>(id) - 1];
    }

    void initialize(const State& initial)
    {
        parser.before.fill(0xa5);
        parser.after.fill(0x5a);
        input.before.fill(0xa5);
        input.after.fill(0x5a);
        input.value = {'Q', 'R', 'S', '\0', 'x', 'y', 'z', '!'};
        input_before = input.value;
        constexpr std::array<ScreenRectangle, 3> rectangles{ScreenRectangle{2, 3, 41, 52},
            ScreenRectangle{-8, 7, 60, 91},
            ScreenRectangle{11, -12, 70, 80}};
        for (unsigned i = 0; i < items.size(); ++i)
        {
            storage[i].before.fill(0xa5);
            storage[i].after.fill(0x5a);
            std::memset(storage[i].bytes, 0xc7, sizeof(storage[i].bytes));
            items[i] = ::new (static_cast<void*>(storage[i].bytes)) GeneratedItem(rectangles[i], i);
            ++items_live;
            std::memcpy(
                item_before[i].data(), static_cast<const void*>(items[i]), sizeof(GeneratedItem));
        }
        auto& state = parser.value;
        state.manager = nullptr;
        state.cursor = 0x1234u;
        state.buffer = input.value.data();
        state.current_context = item(initial.current);
        state.alternate_context_10 = item(A);
        state.alternate_context_14 = item(B);
        state.previous_context = item(initial.previous);
        state.last_registered_item = item(C);
        std::memset(state.unknown_24, 0x79, sizeof(state.unknown_24));
        state.word_38 = 0x86421357u;
        ScreenChildLink* tail = nullptr;
        for (unsigned i = 0; i < initial.history.count; ++i)
        {
            ScreenChildLink* node;
            {
                AllocationScope scope(ledger, this, ScopeKind::Seed, nullptr);
                node = new ScreenChildLink(item(initial.history.items[i]));
                require(scope.requests == 1, "seed allocation count differs");
            }
            auto& owned = ledger.entries[ledger.count - 1];
            owned.node = node;
            owned.node_live = true;
            ++counts.seed_nodes;
            require(static_cast<void*>(node) == owned.returned, "seed storage identity differs");
            if (tail)
            {
                tail->next = node;
            }
            else
            {
                state.context_head = node;
            }
            tail = node;
        }
        std::memcpy(parser_before.data(), &state, sizeof(state));
        check_state(initial);
        check_untouched();
    }

    void check_state(const State& expected)
    {
        require(parser.value.current_context == item(expected.current), "current context differs");
        require(
            parser.value.previous_context == item(expected.previous), "previous context differs");
        require(ledger.count == expected.history.count, "history allocation count differs");
        require(parser.value.context_head ==
                    (expected.history.count ? ledger.entries[0].node : nullptr),
            "history head identity differs");
        for (unsigned i = 0; i < expected.history.count; ++i)
        {
            const auto& owned = ledger.entries[i];
            require(owned.live && owned.node_live && owned.node &&
                        static_cast<void*>(owned.node) == owned.returned,
                "history node is outside its live allocation");
            require(address(owned.node) % alignof(ScreenChildLink) == 0,
                "history node alignment differs");
            owned_guards(owned);
            require(owned.node->item == item(expected.history.items[i]), "history payload differs");
            require(owned.node->next ==
                        (i + 1 < expected.history.count ? ledger.entries[i + 1].node : nullptr),
                "history next identity differs");
        }
    }

    void check_untouched()
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&parser.value);
        bool same = true;
        for (std::size_t i = 0; i < sizeof(GuiParserState); ++i)
        {
            if (!within(i, offsetof(GuiParserState, current_context), sizeof(ScreenItem*)) &&
                !within(i, offsetof(GuiParserState, previous_context), sizeof(ScreenItem*)) &&
                !within(i, offsetof(GuiParserState, context_head), sizeof(ScreenChildLink*)))
            {
                ++counts.byte_checks;
                same = (bytes[i] == parser_before[i]) && same;
            }
        }
        require(same, "unrelated parser storage changed");
        guard(parser.before, 0xa5, "parser leading guard changed");
        guard(parser.after, 0x5a, "parser trailing guard changed");
        guard(input.before, 0xa5, "input leading guard changed");
        guard(input.after, 0x5a, "input trailing guard changed");
        unchanged(input.value.data(), input_before.data(), input_before.size(), "input changed");
        for (unsigned i = 0; i < items_live; ++i)
        {
            unchanged(static_cast<const void*>(items[i]),
                item_before[i].data(),
                sizeof(GeneratedItem),
                "borrowed item changed");
            guard(storage[i].before, 0xa5, "item leading guard changed");
            guard(storage[i].after, 0x5a, "item trailing guard changed");
        }
        require(counts.item_destructions == destroyed_before, "borrowed item was destroyed");
    }

    void invoke(const Call& call)
    {
        require(active_allocation == nullptr && active_release == nullptr,
            "prior provider scope remains active");
        const unsigned old_nodes = ledger.count;
        std::array<std::array<unsigned char, sizeof(ScreenChildLink)>, max_nodes> before{};
        for (unsigned i = 0; i < old_nodes; ++i)
        {
            std::memcpy(before[i].data(), ledger.entries[i].node, sizeof(ScreenChildLink));
        }
        bool caught = false;
        unsigned requests = 0;
        ++counts.calls;
        {
            AllocationScope scope(ledger, this, ScopeKind::Method, &call.allocation);
            try
            {
                parser.value.PushContext(item(call.incoming));
                ++counts.returns;
            }
            catch (const HostAllocationFailure&)
            {
                caught = true;
                ++counts.exceptions;
            }
            requests = scope.requests;
        }
        // On a normal return each new storage block contains the complete actual
        // link. This typed pointer denotes that live object, not an unrelated cast.
        for (unsigned i = old_nodes; i < ledger.count; ++i)
        {
            auto& owned = ledger.entries[i];
            owned.node = static_cast<ScreenChildLink*>(static_cast<void*>(owned.returned));
            owned.node_live = true;
            ++counts.method_nodes;
        }
        require(active_allocation == nullptr, "allocation scope was not restored");
        require(caught == call.host_failure, "host exception outcome differs");
        require(requests == (call.allocation.expected ? 1u : 0u), "method request count differs");
        check_state(call.expected);
        for (unsigned i = 0; i < old_nodes; ++i)
        {
            const auto* bytes = reinterpret_cast<const unsigned char*>(ledger.entries[i].node);
            bool same = true;
            for (std::size_t n = 0; n < sizeof(ScreenChildLink); ++n)
            {
                const bool changed_tail =
                    call.allocation.expected && !caught && i + 1 == old_nodes &&
                    within(n, offsetof(ScreenChildLink, next), sizeof(ScreenChildLink*));
                if (!changed_tail)
                {
                    ++counts.byte_checks;
                    same = (bytes[n] == before[i][n]) && same;
                }
            }
            require(same, "old history node storage changed");
        }
        check_untouched();
    }

    ~Scenario()
    {
        cleanup_require(active_allocation == nullptr && active_release == nullptr,
            "provider scope escaped operation");
        parser.value.context_head = nullptr;
        parser.value.current_context = nullptr;
        parser.value.previous_context = nullptr;
        parser.value.alternate_context_10 = nullptr;
        parser.value.alternate_context_14 = nullptr;
        parser.value.last_registered_item = nullptr;
        for (unsigned i = 0; i < ledger.count; ++i)
        {
            auto& owned = ledger.entries[i];
            cleanup_require(
                owned.live && owned.node_live && owned.node, "cleanup lacks a complete owned link");
            // History borrows its payload. The real destructor owns nonnull
            // payloads, so detach before destruction and never read it afterwards.
            owned.node->item = nullptr;
            ScreenChildLink* const node = owned.node;
            owned.node_live = false;
            {
                ReleaseScope scope(owned);
                delete node;
            }
            owned.node = nullptr;
            ++counts.node_destructions;
        }
        cleanup_require(counts.item_destructions == destroyed_before,
            "history cleanup destroyed a borrowed item");
        for (unsigned i = items_live; i > 0; --i)
        {
            items[i - 1]->~GeneratedItem();
            items[i - 1] = nullptr;
            cleanup_guard(storage[i - 1].before, 0xa5, "item leading guard changed");
            cleanup_guard(storage[i - 1].after, 0x5a, "item trailing guard changed");
        }
        cleanup_require(counts.item_destructions == destroyed_before + items_live,
            "generated item lifetime count differs");
        cleanup_guard(parser.before, 0xa5, "parser leading guard changed");
        cleanup_guard(parser.after, 0x5a, "parser trailing guard changed");
    }
};

void* allocate_owned(std::size_t bytes)
{
    auto& scope = *active_allocation;
    require(bytes == sizeof(ScreenChildLink), "scalar request size differs");
    require(scope.requests++ == 0, "operation allocated more than once");
    require(scope.ledger.count < scope.ledger.entries.size(), "allocation ledger is full");
    if (scope.kind == ScopeKind::Method)
    {
        require(scope.expected && scope.expected->expected && scope.scenario,
            "unexpected method allocation");
        ++counts.method_requests;
        scope.scenario->check_state(scope.expected->observed);
        scope.scenario->check_untouched();
        if (scope.expected->replace_current != None)
        {
            scope.scenario->parser.value.current_context =
                scope.scenario->item(scope.expected->replace_current);
            ++counts.allocation_mutations;
        }
        if (scope.expected->throw_host_failure)
        {
            throw HostAllocationFailure{};
        }
    }
    else if (scope.kind == ScopeKind::Probe)
    {
        ++counts.probe_allocations;
    }
    void* raw = std::malloc(bytes + 2 * guard_size);
    require(raw != nullptr, "fixture backing allocation failed");
    auto* backing = ::new (raw) unsigned char[bytes + 2 * guard_size];
    std::memset(backing, 0xa6, bytes + 2 * guard_size);
    unsigned char* returned = backing + guard_size;
    cleanup_require(
        address(returned) % alignof(std::max_align_t) == 0, "provider returned misaligned storage");
    auto& owned = scope.ledger.entries[scope.ledger.count++];
    owned.backing = backing;
    owned.returned = returned;
    owned.live = true;
    return returned;
}

void release_scalar(void* memory, std::size_t bytes, bool sized) noexcept
{
    if (!memory)
    {
        return;
    }
    if (active_release)
    {
        auto& owned = active_release->owned;
        cleanup_require(active_allocation == nullptr && owned.live && memory == owned.returned &&
                            owned.returned != owned.backing && active_release->releases == 0,
            "release did not select the expected live allocation");
        cleanup_require(!sized || bytes == sizeof(ScreenChildLink), "sized release extent differs");
        bool same = true;
        for (unsigned i = 0; i < guard_size; ++i)
        {
            counts.byte_checks += 2;
            same = (owned.backing[i] == 0xa6 &&
                       owned.backing[guard_size + sizeof(ScreenChildLink) + i] == 0xa6) &&
                   same;
        }
        cleanup_require(same, "owned allocation guard changed");
        std::free(owned.backing);
        owned.backing = nullptr;
        owned.returned = nullptr;
        owned.live = false;
        ++active_release->releases;
        ++counts.owned_releases;
        if (sized)
        {
            ++counts.sized_releases;
        }
        else
        {
            ++counts.unsized_releases;
        }
        return;
    }
    cleanup_require(active_allocation == nullptr, "method unexpectedly released storage");
    std::free(memory);
    ++counts.ordinary_releases;
}

void allocator_probes()
{
    // Explicit self-probes call the actual replacements through opaque dispatch;
    // reconstructed method new expressions retain their ordinary source unchanged.
    void* (*volatile scalar_new)(std::size_t) = ::operator new;
    void (*volatile scalar_delete)(void*) noexcept = ::operator delete;
    void (*volatile sized_delete)(void*, std::size_t) noexcept = ::operator delete;
    Ledger ledger;
    for (unsigned i = 0; i < 2; ++i)
    {
        void* memory;
        {
            AllocationScope scope(ledger, nullptr, ScopeKind::Probe, nullptr);
            memory = scalar_new(sizeof(ScreenChildLink));
            require(scope.requests == 1, "raw probe allocation differs");
        }
        require(memory == ledger.entries[i].returned, "raw probe storage differs");
        {
            ReleaseScope scope(ledger.entries[i]);
            if (i)
            {
                sized_delete(memory, sizeof(ScreenChildLink));
            }
            else
            {
                scalar_delete(memory);
            }
        }
    }
    require(counts.probe_allocations == 2 && counts.owned_releases == 2 &&
                counts.sized_releases == 1 && counts.unsized_releases == 1,
        "paired owned release probes differ");
    const auto allocations_before = counts.ordinary_allocations;
    const auto releases_before = counts.ordinary_releases;
    void* zero = scalar_new(0);
    void* bytes = scalar_new(31);
    require(zero && bytes && address(zero) % alignof(std::max_align_t) == 0 &&
                address(bytes) % alignof(std::max_align_t) == 0,
        "ordinary allocation probe is null or misaligned");
    auto* writable = ::new (bytes) unsigned char[31];
    writable[0] = 0x35;
    writable[30] = 0x71;
    require(writable[0] == 0x35 && writable[30] == 0x71, "ordinary probe storage is not writable");
    scalar_delete(zero);
    sized_delete(bytes, std::size_t{31});
    scalar_delete(nullptr);
    sized_delete(nullptr, std::size_t{0});
    require(counts.ordinary_allocations == allocations_before + 2 &&
                counts.ordinary_releases == releases_before + 2 && counts.probe_allocations == 2,
        "unscoped or null allocator behavior differs");
}
} // namespace

int main()
{
    try
    {
        allocator_probes();
        for (const auto& literal : literals)
        {
            ++current_case;
            current_operation = 0;
            ++counts.cases;
            Scenario scenario;
            scenario.initialize(literal.initial);
            for (unsigned i = 0; i < literal.count; ++i)
            {
                current_operation = i + 1;
                scenario.invoke(literal.calls[i]);
            }
        }
        require(counts.cases == 11 && counts.calls == 18 && counts.returns == 16 &&
                    counts.exceptions == 2,
            "literal case or call totals differ");
        require(counts.method_requests == 13 && counts.method_nodes == 11 &&
                    counts.seed_nodes == 9 && counts.node_destructions == 20,
            "node allocation or lifetime totals differ");
        require(counts.item_constructions == 33 && counts.item_destructions == 33 &&
                    counts.allocation_mutations == 2,
            "item lifetime or mutation totals differ");
        require(counts.owned_releases == 22 && active_allocation == nullptr &&
                    active_release == nullptr,
            "owned storage or provider scope remains");
        std::printf(
            "reconstructed GUI context: %u cases, %u calls, %u returns, %u host exceptions, "
            "%u nodes, %u item lifetimes, %llu checks, %llu byte checks\n",
            counts.cases,
            counts.calls,
            counts.returns,
            counts.exceptions,
            counts.node_destructions,
            counts.item_destructions,
            counts.checks,
            counts.byte_checks);
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr,
            "case %u operation %u: %s\n",
            failure.scenario,
            failure.operation,
            failure.reason);
        return 2;
    }
    catch (...)
    {
        std::fprintf(stderr,
            "case %u operation %u: unexpected host exception\n",
            current_case,
            current_operation);
        return 2;
    }
    return 0;
}
