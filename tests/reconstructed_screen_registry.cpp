#include "pool_chunk_acquire.h"
#include "pool_lock_acquire.h"
#include "pool_lock_guard.h"
#include "pool_return.h"
#include "startup_screen_dialog.h"
#include "startup_screen_name_search.h"

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
static_assert(sizeof(unsigned int) == 4);
unsigned session = 0;
unsigned operation = 0;
unsigned long long checks = 0;
unsigned long long queries = 0;
unsigned long long insertions = 0;
unsigned long long returns = 0;
unsigned long long preparations = 0;
unsigned long long exchanges = 0;
unsigned long long allocations = 0;
unsigned long long releases = 0;
unsigned long long ordinary_allocations = 0;
unsigned long long ordinary_releases = 0;

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "session %u operation %u: %s\n", session, operation, reason);
        std::exit(2);
    }
}

// This executable supplies one paired host scalar-allocation boundary. The
// reconstructed source retains its actual operator-new/delete expressions.
enum class Domain
{
    ordinary,
    pool,
    text
};
thread_local Domain domain = Domain::ordinary;
struct Allocation
{
    unsigned char* backing = nullptr;
    unsigned char* memory = nullptr;
    std::size_t size = 0;
    Domain kind = Domain::ordinary;
    bool released = false;
};
std::array<Allocation, 16> ledger{};
unsigned ledger_size = 0;
constexpr unsigned guard = 32;
constexpr unsigned max_nodes = 11;
constexpr unsigned max_dialogs = 9;
constexpr unsigned slot = 24;

class AllocationScope
{
    Domain prior;

  public:
    explicit AllocationScope(Domain selected) noexcept
        : prior(domain)
    {
        require(prior == Domain::ordinary, "nested allocation scope");
        domain = selected;
    }
    ~AllocationScope()
    {
        domain = prior;
    }
    AllocationScope(const AllocationScope&) = delete;
    AllocationScope& operator=(const AllocationScope&) = delete;
};

void* allocate(std::size_t bytes);
void release(void* memory) noexcept;
} // namespace

void* operator new(std::size_t bytes)
{
    return allocate(bytes);
}
void operator delete(void* memory) noexcept
{
    release(memory);
}
void operator delete(void* memory, std::size_t) noexcept
{
    release(memory);
}

namespace
{
std::uintptr_t address(const void* pointer)
{
    return reinterpret_cast<std::uintptr_t>(pointer);
}

void check_allocation_guards()
{
    for (unsigned n = 0; n < ledger_size; ++n)
    {
        const auto& entry = ledger[n];
        if (!entry.released)
        {
            for (unsigned i = 0; i < guard; ++i)
            {
                require(entry.backing[i] == 0xa6 && entry.backing[guard + entry.size + i] == 0xa6,
                    "allocation guard changed");
            }
        }
    }
}

void* allocate(std::size_t bytes)
{
    if (domain == Domain::ordinary)
    {
        for (;;)
        {
            if (void* memory = std::malloc(bytes ? bytes : 1))
            {
                ++ordinary_allocations;
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
    require(ledger_size < ledger.size(), "allocation ledger full");
    require(bytes == (domain == Domain::pool ? 960u : 16u), "literal allocation size differs");
    if (domain == Domain::pool)
    {
        require(pool_lock_word == 1, "pool allocation precedes acquisition");
        for (unsigned n = 0; n < ledger_size; ++n)
        {
            require(ledger[n].kind != Domain::pool, "unexpected second pool heap");
        }
    }
    void* raw = std::malloc(bytes + 2 * guard);
    if (!raw)
    {
        throw std::bad_alloc();
    }
    auto* backing = ::new (raw) unsigned char[bytes + 2 * guard];
    std::memset(backing, 0xa6, bytes + 2 * guard);
    auto* memory = backing + guard;
    require(address(memory) % alignof(std::max_align_t) == 0, "provider alignment differs");
    ledger[ledger_size++] = Allocation{backing, memory, bytes, domain, false};
    ++allocations;
    return memory;
}

void release(void* memory) noexcept
{
    if (!memory)
    {
        return;
    }
    require(domain == Domain::ordinary, "release inside allocation scope");
    for (unsigned n = 0; n < ledger_size; ++n)
    {
        auto& entry = ledger[n];
        if (memory == entry.memory)
        {
            require(!entry.released && entry.backing != nullptr, "duplicate scalar release");
            check_allocation_guards();
            std::free(entry.backing);
            entry.backing = nullptr;
            entry.memory = nullptr;
            entry.released = true;
            ++releases;
            return;
        }
    }
    std::free(memory);
    ++ordinary_releases;
}

struct GeneratedDialog final : ScreenDialog
{
    ScreenChildNode sentinel;
    explicit GeneratedDialog(const char* label)
        : ScreenDialog(ScreenRectangle{7, -3, 91, 27})
    {
        // Explicit generated construction facility reaches only the real base
        // lifetime. Borrowed Dialog tail data is not original derived lifetime.
        kind_28 = 4;
        word_2c = 0x67676767U;
        std::memset(unknown_31, 0x53, sizeof(unknown_31));
        child_44 = nullptr;
        name = label;
        std::memset(unknown_4c, 0x29, sizeof(unknown_4c));
        sentinel = {&sentinel, &sentinel, nullptr};
        children.head = &sentinel;
        AllocationScope scope(Domain::text);
        void* memory = ::operator new(16);
        owned_text = ::new (memory) char[16];
        std::memset(owned_text, 'q', 15);
        owned_text[15] = '\0';
    }
    void Unresolved04() override
    {
        require(false, "unresolved generated slot invoked");
    }
    void Unresolved08() override
    {
        require(false, "unresolved generated slot invoked");
    }
    void Unresolved0c() override
    {
        require(false, "unresolved generated slot invoked");
    }
    void Unresolved10(ScreenItem*) override
    {
        require(false, "unresolved generated slot invoked");
    }
    void Unresolved1c() override
    {
        require(false, "unresolved generated slot invoked");
    }
    void Unresolved20() override
    {
        require(false, "unresolved generated slot invoked");
    }
#if defined(BEACHLIFE_DECOMP_SCREEN_ACTIVATION)
    void Unresolved24(unsigned char) override
#else
    void Unresolved24() override
#endif
    {
        require(false, "unresolved generated slot invoked");
    }
    void Unresolved28() override
    {
        require(false, "unresolved generated slot invoked");
    }
};

struct DialogFrame
{
    std::array<unsigned char, 16> before;
    GeneratedDialog value;
    std::array<unsigned char, 16> after;
    explicit DialogFrame(const char* name)
        : value(name)
    {
        before.fill(0x38);
        after.fill(0xc2);
    }
};
struct EntryFrame
{
    std::array<unsigned char, 16> before{};
    ActiveScreenEntry value{};
    std::array<unsigned char, 16> after{};
};
struct Registry
{
    std::array<unsigned char, 16> before{};
    CircularListNode sentinel;
    CircularList value;
    std::array<unsigned char, 16> after{};
    Registry()
    {
        before.fill(0x74);
        after.fill(0x28);
        sentinel = {&sentinel, &sentinel};
        value.head = &sentinel;
    }
};

// Literal expected node order after each independently chosen insertion. Node
// IDs describe returned identities, never fabricated or cast payload objects.
constexpr int orders[11][10] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 0, 1, -1, -1, -1, -1, -1, -1, -1},
    {2, 0, 3, 1, -1, -1, -1, -1, -1, -1},
    {4, 2, 0, 3, 1, -1, -1, -1, -1, -1},
    {4, 2, 0, 3, 1, 5, -1, -1, -1, -1},
    {4, 2, 0, 6, 3, 1, 5, -1, -1, -1},
    {4, 2, 0, 6, 3, 1, 5, 7, -1, -1},
    {4, 2, 0, 6, 3, 8, 1, 5, 7, -1},
    {4, 2, 0, 9, 6, 3, 8, 1, 5, 7},
};
constexpr unsigned entry_ids[] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 8, 2};
constexpr int before_ids[] = {-1, -1, 0, 1, 2, -1, 3, -1, 1, 6};
struct Query
{
    const char* text;
    std::array<int, 11> expected;
};
constexpr Query query_table[] = {
    {"Alpha", {-1, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2}},
    {"alpha", {-1, -1, -1, -1, -1, 4, 4, 4, 4, 4, 4}},
    {"Beta", {-1, -1, 1, 1, 1, 1, 1, 1, 1, 1, 1}},
    {"", {-1, -1, -1, -1, 3, 3, 3, 3, 3, 3, 3}},
    {"Al", {-1, -1, -1, -1, -1, -1, 5, 5, 5, 5, 5}},
    {"Alphabet", {-1, -1, -1, -1, -1, -1, -1, 6, 6, 6, 6}},
    {"A", {-1, -1, -1, -1, -1, -1, -1, -1, 7, 7, 7}},
    {"Missing", {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    {"ALPHA", {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    {"\x80key", {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 9}},
    {"Alph", {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
};

struct Snapshot
{
    std::array<unsigned char, 8192> bytes{};
    std::size_t size = 0;
    void append(const void* memory, std::size_t count)
    {
        require(count <= bytes.size() - size, "snapshot capacity exceeded");
        std::memcpy(bytes.data() + size, memory, count);
        size += count;
    }
    template <class T> void append(const T& value)
    {
        append(&value, sizeof(value));
    }
};

struct Scenario
{
    std::array<DialogFrame, max_dialogs> dialogs{DialogFrame("Alpha"),
        DialogFrame("Beta"),
        DialogFrame("Alpha"),
        DialogFrame(""),
        DialogFrame("alpha"),
        DialogFrame("Al"),
        DialogFrame("Alphabet"),
        DialogFrame("A\0suffix"),
        DialogFrame("\x80key")};
    std::array<EntryFrame, max_dialogs> entries{};
    Registry registry;
    std::array<CircularListValueNode*, max_nodes> nodes{};
    Scenario()
    {
        for (unsigned n = 0; n < entries.size(); ++n)
        {
            entries[n].before.fill(0x61);
            entries[n].after.fill(0x94);
            entries[n].value = {
                &dialogs[n].value, static_cast<int>(31 + n), -static_cast<int>(7 + n), nullptr};
        }
    }
    CircularListNode* node(int id)
    {
        if (id < 0)
        {
            return &registry.sentinel;
        }
        require(
            static_cast<unsigned>(id) < nodes.size() && nodes[id], "oracle names an absent node");
        return nodes[id];
    }
    void check_order(const int* ids, unsigned count)
    {
        require(registry.value.head == &registry.sentinel, "registry head changed");
        CircularListNode* current = registry.sentinel.next;
        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned id = static_cast<unsigned>(ids[i]);
            require(current == node(ids[i]), "literal forward order differs");
            require(
                nodes[id]->previous == node(i ? ids[i - 1] : -1), "literal previous link differs");
            require(nodes[id]->next == node(i + 1 < count ? ids[i + 1] : -1),
                "literal next link differs");
            require(nodes[id]->entry == &entries[entry_ids[id]].value,
                "inserted payload identity differs");
            current = nodes[id]->next;
        }
        require(current == &registry.sentinel, "forward end differs");
        require(registry.sentinel.previous == node(count ? ids[count - 1] : -1),
            "sentinel tail differs");
        require(registry.value.Begin().node == node(count ? ids[0] : -1), "actual Begin differs");
        require(registry.value.End().node == &registry.sentinel, "actual End differs");
        for (auto v : registry.before)
        {
            require(v == 0x74, "registry guard changed");
        }
        for (auto v : registry.after)
        {
            require(v == 0x28, "registry guard changed");
        }
        for (unsigned n = 0; n < dialogs.size(); ++n)
        {
            for (auto v : dialogs[n].before)
            {
                require(v == 0x38, "Dialog guard changed");
            }
            for (auto v : dialogs[n].after)
            {
                require(v == 0xc2, "Dialog guard changed");
            }
            for (auto v : entries[n].before)
            {
                require(v == 0x61, "entry guard changed");
            }
            for (auto v : entries[n].after)
            {
                require(v == 0x94, "entry guard changed");
            }
        }
        check_allocation_guards();
    }
    Snapshot snapshot() const
    {
        Snapshot result;
        result.append(dialogs);
        result.append(entries);
        result.append(registry);
        for (unsigned n = 0; n < ledger_size; ++n)
        {
            require(!ledger[n].released, "snapshot contains dead storage");
            result.append(ledger[n].backing, ledger[n].size + 2 * guard);
        }
        for (const auto& bucket : pool_chunk_buckets)
        {
            PoolChunkLink* value = bucket;
            result.append(value);
        }
        LONG word = pool_lock_word;
        result.append(word);
        result.append(pool_chunk_begin);
        result.append(pool_chunk_end);
        result.append(pool_heap_bytes);
        result.append(pool_spin_limit);
        result.append(pool_last_spin);
        result.append(exchanges);
        result.append(allocations);
        result.append(releases);
        result.append(ordinary_allocations);
        result.append(ordinary_releases);
        return result;
    }
    void query(const char* text, int first_id, int last_id, int expected_id)
    {
        struct Output
        {
            std::array<unsigned char, 16> before;
            CircularListIterator value;
            std::array<unsigned char, 16> after;
        } output;
        output.before.fill(0x19);
        output.after.fill(0xb7);
        output.value.node = &registry.sentinel;
        CircularListIterator first;
        first.node = node(first_id);
        CircularListIterator last;
        last.node = node(last_id);
        const Snapshot before = snapshot();
        auto* returned = FindScreenByName(&output.value, first, last, text);
        const Snapshot after = snapshot();
        ++queries;
        require(returned == &output.value, "result pointer identity differs");
        require(output.value.node == node(expected_id), "literal query selected wrong node");
        require(
            first.node == node(first_id) && last.node == node(last_id), "by-value bounds changed");
        require(before.size == after.size &&
                    std::memcmp(before.bytes.data(), after.bytes.data(), before.size) == 0,
            "search changed the live graph or providers");
        for (auto v : output.before)
        {
            require(v == 0x19, "output leading guard changed");
        }
        for (auto v : output.after)
        {
            require(v == 0xb7, "output trailing guard changed");
        }
    }
    void query_step(unsigned step)
    {
        for (const auto& q : query_table)
        {
            query(q.text, step ? orders[step][0] : -1, -1, q.expected[step]);
        }
    }
    CircularListValueNode* insert(unsigned id, int before_id)
    {
        require(nodes[id] == nullptr, "node ID reused while live");
        ActiveScreenEntry* entry = &entries[entry_ids[id]].value;
        CircularListIterator position;
        position.node = node(before_id);
        auto* const old_position = position.node;
        const auto before_exchanges = exchanges;
        CircularListIterator inserted;
        {
            AllocationScope scope(Domain::pool);
            inserted = registry.value.Insert(position, entry);
        }
        require(pool_lock_word == 0 && exchanges == before_exchanges + 1,
            "insertion lock lifecycle differs");
        require(entry == &entries[entry_ids[id]].value && position.node == old_position,
            "insertion inputs changed");
        unsigned containing = 0;
        for (unsigned n = 0; n < ledger_size; ++n)
        {
            const auto& heap = ledger[n];
            if (heap.kind == Domain::pool && !heap.released &&
                address(inserted.node) >= address(heap.memory) &&
                address(inserted.node) - address(heap.memory) <=
                    heap.size - sizeof(CircularListValueNode))
            {
                ++containing;
            }
        }
        require(containing == 1 && address(inserted.node) % alignof(CircularListValueNode) == 0,
            "actual returned node is outside live aligned storage");
        for (auto* live : nodes)
        {
            require(live != inserted.node, "insertion aliases a live node");
        }
        nodes[id] = static_cast<CircularListValueNode*>(inserted.node);
        require(nodes[id]->entry == entry, "inserted payload identity differs");
        ++insertions;
        return nodes[id];
    }
    std::uintptr_t retire(unsigned id)
    {
        auto* live = nodes[id];
        require(live != nullptr, "retiring an absent node");
        const auto saved = address(live);
        // Generated ownership bookkeeping only; no retail erase/destructor claim.
        live->previous->next = live->next;
        live->next->previous = live->previous;
        nodes[id] = nullptr;
        const auto before_exchanges = exchanges;
        {
            AllocationScope scope(Domain::pool);
            ReturnPool(live, circular_list_node_slot_bytes);
        }
        require(pool_lock_word == 0 && exchanges == before_exchanges + 1,
            "return lock lifecycle differs");
        ++returns;
        return saved;
    }
    void prepare_found()
    {
        CircularListIterator found;
        auto* result =
            FindScreenByName(&found, registry.value.Begin(), registry.value.End(), "Alpha");
        ++queries;
        require(result == &found && found.node == nodes[10], "reinserted duplicate lookup differs");
        auto* entry = static_cast<CircularListValueNode*>(found.node)->entry;
        require(entry == &entries[2].value && entry->item == &dialogs[2].value,
            "found Dialog identity differs");
        entry->item->preparation_flag = 0;
        auto before = snapshot();
        entry->item->preparation_flag = 1;
        const auto expected = snapshot();
        entry->item->preparation_flag = 0;
        require(before.size == expected.size, "preparation snapshot extent differs");
        entry->item->MarkPrepared();
        ++preparations;
        const auto after = snapshot();
        require(after.size == expected.size &&
                    std::memcmp(after.bytes.data(), expected.bytes.data(), after.size) == 0,
            "actual found-Dialog preparation changed unexpected state");
    }
    void run()
    {
        check_order(orders[0], 0);
        query_step(0);
        for (operation = 0; operation < 10; ++operation)
        {
            insert(operation, before_ids[operation]);
            check_order(orders[operation + 1], operation + 1);
            query_step(operation + 1);
        }
        // Literal valid [first,last) bounds include duplicate skipping and an
        // empty non-sentinel range. Every traversed entry/item/name is live.
        query("Alpha", 0, -1, 0);
        query("Alpha", 3, -1, 8);
        query("Alpha", 8, -1, 8);
        query("Alpha", 1, -1, -1);
        query("Alpha", 2, 0, 2);
        query("Beta", 0, 3, 3);
        query("Alpha", 6, 6, 6);
        const auto retired = retire(2);
        constexpr int after_retire[] = {4, 0, 9, 6, 3, 8, 1, 5, 7};
        check_order(after_retire, 9);
        query("Alpha", 4, -1, 0);
        require(address(insert(10, 4)) == retired, "returned slot was not reused");
        constexpr int after_reinsert[] = {10, 4, 0, 9, 6, 3, 8, 1, 5, 7};
        check_order(after_reinsert, 10);
        query("Alpha", 10, -1, 10);
        query("Alpha", 4, -1, 0);
        prepare_found();
        for (int id : after_reinsert)
        {
            retire(static_cast<unsigned>(id));
        }
        check_order(orders[0], 0);
        query_step(0);
        for (auto& bucket : pool_chunk_buckets)
        {
            bucket = nullptr;
        }
        pool_chunk_begin = nullptr;
        pool_chunk_end = nullptr;
        require(pool_heap_bytes == 960, "literal heap accounting differs");
        pool_heap_bytes = 0;
        for (unsigned n = 0; n < ledger_size; ++n)
        {
            if (ledger[n].kind == Domain::pool)
            {
                ::operator delete(ledger[n].memory, ledger[n].size);
            }
        }
        // Generated Dialog/entry objects are destroyed only after the list is
        // detached and its storage released; real base cleanup frees owned_text.
    }
};
} // namespace

extern "C" LONG InterlockedExchange(LONG* target, LONG value)
{
    volatile LONG* const word = target;
    require(domain == Domain::pool && target == &pool_lock_word && value == 1 && *word == 0,
        "unexpected uncontended lock event");
    *word = 1;
    ++exchanges;
    return 0;
}
extern "C" void Sleep(DWORD)
{
    require(false, "unexpected wait in uncontended fixture");
}

int main()
{
    static_assert(sizeof(void*) == 8 && sizeof(int) == 4 && sizeof(LONG) == 4);
    static_assert(sizeof(CircularListValueNode) == slot && circular_list_node_slot_bytes == slot);
    static_assert(std::is_trivially_destructible_v<CircularListValueNode>);
    static_assert(std::is_trivially_destructible_v<PoolChunkLink>);
    static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ <= alignof(std::max_align_t));
    for (session = 0; session < 2; ++session)
    {
        operation = 0;
        require(ledger_size == 0 && pool_lock_word == 0 && pool_chunk_begin == nullptr &&
                    pool_chunk_end == nullptr && pool_heap_bytes == 0,
            "prior owner state remains");
        for (auto* bucket : pool_chunk_buckets)
        {
            require(bucket == nullptr, "prior free root remains");
        }
        {
            Scenario scenario;
            scenario.run();
        }
        require(ledger_size == 10, "literal allocation count differs");
        for (unsigned n = 0; n < ledger_size; ++n)
        {
            require(ledger[n].released && ledger[n].backing == nullptr,
                "owned allocation remains live");
            ledger[n] = Allocation{};
        }
        ledger_size = 0;
    }
    require(allocations == 20 && releases == allocations && insertions == 22 && returns == 22 &&
                preparations == 2 && exchanges == 44 && queries == 286,
        "literal operation totals differ");
    std::printf(
        "{\"sessions\":2,\"checks\":%llu,\"queries\":%llu,\"insertions\":%llu,\"returns\":%llu,"
        "\"preparations\":%llu,\"allocations\":%llu,\"releases\":%llu,\"exchanges\":%llu}\n",
        checks,
        queries,
        insertions,
        returns,
        preparations,
        allocations,
        releases,
        exchanges);
}
