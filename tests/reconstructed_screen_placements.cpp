#include "pool_chunk_acquire.h"
#include "pool_lock_acquire.h"
#include "pool_return.h"
#include "startup_screen_placement.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <new>
#include <type_traits>

namespace
{
unsigned session = 0;
unsigned operation = 0;
unsigned long long checks = 0;
unsigned long long queries = 0;
unsigned long long insertions = 0;
unsigned long long returns = 0;
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

// One executable-scoped paired host scalar allocator. Actual reconstructed
// allocation expressions remain in the prepared library, with no source macro.
thread_local bool pool_allocation = false;
enum class AllocationRole
{
    record,
    pool
};
enum class AllocationPlan
{
    none,
    cold_pool,
    record,
    cold_record,
    update
};
struct AllocationEvent
{
    AllocationRole role;
    std::size_t size;
    LONG lock;
};
constexpr AllocationEvent cold_pool_events[] = {{AllocationRole::pool, 960, 1}};
constexpr AllocationEvent record_events[] = {{AllocationRole::record, 12, 0}};
constexpr AllocationEvent cold_record_events[] = {
    {AllocationRole::record, 12, 0}, {AllocationRole::pool, 960, 1}};
const AllocationEvent* expected_events = nullptr;
unsigned expected_event_count = 0;
unsigned consumed_events = 0;
bool existing_record_update = false;
struct Allocation
{
    unsigned char* backing = nullptr;
    unsigned char* memory = nullptr;
    std::size_t size = 0;
    bool released = false;
    std::uintptr_t identity = 0;
};
Allocation heap;
std::array<Allocation, 4> owned_records{};
unsigned owned_record_count = 0;
std::array<std::uintptr_t, 2> borrowed_records{};
unsigned long long record_allocations = 0;
unsigned long long record_releases = 0;
unsigned long long writes = 0;
unsigned long long updates = 0;
constexpr unsigned guard = 32;
constexpr unsigned slot = 24;
constexpr unsigned max_nodes = 11;

class AllocationScope
{
  public:
    explicit AllocationScope(AllocationPlan plan)
    {
        require(!pool_allocation, "nested allocation scope");
        expected_events = nullptr;
        expected_event_count = 0;
        consumed_events = 0;
        existing_record_update = plan == AllocationPlan::update;
        switch (plan)
        {
        case AllocationPlan::cold_pool:
            expected_events = cold_pool_events;
            expected_event_count = 1;
            break;
        case AllocationPlan::record:
            expected_events = record_events;
            expected_event_count = 1;
            break;
        case AllocationPlan::cold_record:
            expected_events = cold_record_events;
            expected_event_count = 2;
            break;
        case AllocationPlan::none:
        case AllocationPlan::update:
            break;
        }
        pool_allocation = true;
    }
    ~AllocationScope()
    {
        require(consumed_events == expected_event_count, "expected allocation event missing");
        pool_allocation = false;
        expected_events = nullptr;
        expected_event_count = 0;
        consumed_events = 0;
        existing_record_update = false;
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

void check_allocation_guards(const Allocation& allocation)
{
    if (allocation.backing && !allocation.released)
    {
        for (unsigned i = 0; i < guard; ++i)
        {
            require(allocation.backing[i] == 0xa6 &&
                        allocation.backing[guard + allocation.size + i] == 0xa6,
                "allocation guard changed");
        }
    }
}

void check_heap_guards()
{
    check_allocation_guards(heap);
}

void* allocate(std::size_t bytes)
{
    if (!pool_allocation)
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
    require(consumed_events < expected_event_count,
        existing_record_update ? "unexpected allocation during existing-record update"
                               : "unexpected scoped allocation");
    const AllocationEvent event = expected_events[consumed_events];
    require(bytes == event.size, "declared allocation size differs");
    require(pool_lock_word == event.lock, "declared allocation lock state differs");
    Allocation* allocation = &heap;
    if (event.role == AllocationRole::record)
    {
        require(owned_record_count < owned_records.size(), "record ledger capacity exceeded");
        allocation = &owned_records[owned_record_count];
    }
    require(allocation->backing == nullptr && !allocation->released,
        "allocation ledger entry already used");
    void* raw = std::malloc(bytes + 2 * guard);
    if (!raw)
    {
        throw std::bad_alloc();
    }
    auto* backing = ::new (raw) unsigned char[bytes + 2 * guard];
    std::memset(backing, 0xa6, bytes + 2 * guard);
    auto* memory = backing + guard;
    require(address(memory) % alignof(std::max_align_t) == 0, "provider alignment differs");
    *allocation = Allocation{backing, memory, bytes, false, address(memory)};
    ++consumed_events;
    if (event.role == AllocationRole::record)
    {
        ++owned_record_count;
        ++record_allocations;
    }
    else
    {
        ++allocations;
    }
    return memory;
}

void release_allocation(Allocation& allocation)
{
    require(!allocation.released && allocation.backing != nullptr, "duplicate scalar release");
    check_allocation_guards(allocation);
    std::free(allocation.backing);
    allocation.backing = nullptr;
    allocation.memory = nullptr;
    allocation.released = true;
}

void release(void* memory) noexcept
{
    if (!memory)
    {
        return;
    }
    require(!pool_allocation, "release inside allocation scope");
    for (const auto borrowed : borrowed_records)
    {
        require(address(memory) != borrowed, "borrowed record reached scalar delete");
    }
    for (auto& allocation : owned_records)
    {
        if (address(memory) == allocation.identity)
        {
            require(allocation.size == 12 && pool_lock_word == 0,
                "record release extent or lock state differs");
            release_allocation(allocation);
            ++record_releases;
            return;
        }
    }
    if (address(memory) == heap.identity)
    {
        for (unsigned i = 0; i < owned_record_count; ++i)
        {
            require(owned_records[i].released, "pool released before owned records");
        }
        release_allocation(heap);
        ++releases;
        return;
    }
    std::free(memory);
    ++ordinary_releases;
}

// Caller-owned observed prefixes, not reconstructed store/list lifetime.
struct PlacementFrame
{
    std::array<unsigned char, 16> before{};
    SavedScreenPlacement value{};
    std::array<unsigned char, 16> after{};
};
struct StoreFrame
{
    std::array<unsigned char, 16> before{};
    ScreenPlacementStore value{};
    std::array<unsigned char, 16> after{};
    CircularListNode sentinel;
    StoreFrame()
    {
        before.fill(0x74);
        after.fill(0x28);
        std::memset(value.unknown_00, 0x67, sizeof(value.unknown_00));
        sentinel = {&sentinel, &sentinel};
        value.saved.head = &sentinel;
    }
};

// Literal expected identities and values are independent of list traversal.
constexpr SavedScreenPlacement records[] = {
    {11, -9, 7},
    {INT_MIN, INT_MAX, -3},
    {123, -456, 7},
    {0, 0, 0},
    {INT_MAX, INT_MIN, INT_MIN},
    {-32768, 65535, INT_MAX},
    {-7, 91, 42},
    {999, -888, 42},
    {2147483646, -2147483647, -1},
};
constexpr unsigned record_ids[] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 8, 2};
constexpr int before_ids[] = {-1, -1, 0, 1, 2, -1, 3, -1, 1, 6};
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
struct Query
{
    int group;
    std::array<int, 11> expected;
};
constexpr Query query_table[] = {
    {7, {-1, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2}},
    {-3, {-1, -1, 1, 1, 1, 1, 1, 1, 1, 1, 1}},
    {0, {-1, -1, -1, -1, 3, 3, 3, 3, 3, 3, 3}},
    {INT_MIN, {-1, -1, -1, -1, -1, 4, 4, 4, 4, 4, 4}},
    {INT_MAX, {-1, -1, -1, -1, -1, -1, 5, 5, 5, 5, 5}},
    {42, {-1, -1, -1, -1, -1, -1, -1, 6, 6, 6, 6}},
    {-1, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 9}},
    {1, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    {8, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
};

struct Snapshot
{
    std::array<unsigned char, 4096> bytes{};
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
    std::array<PlacementFrame, 9> payloads{};
    StoreFrame store;
    std::array<SavedScreenPlacementNode*, max_nodes> nodes{};
    Scenario()
    {
        for (unsigned n = 0; n < payloads.size(); ++n)
        {
            payloads[n].before.fill(0x61);
            payloads[n].after.fill(0x94);
            payloads[n].value = records[n];
        }
    }
    CircularListNode* node(int id)
    {
        if (id < 0)
        {
            return &store.sentinel;
        }
        require(
            static_cast<unsigned>(id) < nodes.size() && nodes[id], "oracle names an absent node");
        return nodes[id];
    }
    void check_order(const int* ids, unsigned count)
    {
        require(store.value.saved.head == &store.sentinel, "store head changed");
        CircularListNode* current = store.sentinel.next;
        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned id = static_cast<unsigned>(ids[i]);
            require(current == node(ids[i]), "literal forward order differs");
            require(
                nodes[id]->previous == node(i ? ids[i - 1] : -1), "literal previous link differs");
            require(nodes[id]->next == node(i + 1 < count ? ids[i + 1] : -1),
                "literal next link differs");
            require(nodes[id]->placement == &payloads[record_ids[id]].value,
                "inserted payload identity differs");
            current = nodes[id]->next;
        }
        require(current == &store.sentinel, "forward end differs");
        require(
            store.sentinel.previous == node(count ? ids[count - 1] : -1), "sentinel tail differs");
        require(
            store.value.saved.Begin().node == node(count ? ids[0] : -1), "actual Begin differs");
        require(store.value.saved.End().node == &store.sentinel, "actual End differs");
        for (auto v : store.before)
        {
            require(v == 0x74, "store guard changed");
        }
        for (auto v : store.after)
        {
            require(v == 0x28, "store guard changed");
        }
        for (auto v : store.value.unknown_00)
        {
            require(v == 0x67, "store opaque prefix changed");
        }
        for (unsigned n = 0; n < payloads.size(); ++n)
        {
            require(payloads[n].value.x == records[n].x && payloads[n].value.y == records[n].y &&
                        payloads[n].value.group == records[n].group,
                "borrowed payload changed");
            for (auto v : payloads[n].before)
            {
                require(v == 0x61, "payload guard changed");
            }
            for (auto v : payloads[n].after)
            {
                require(v == 0x94, "payload guard changed");
            }
        }
        check_heap_guards();
    }
    Snapshot snapshot() const
    {
        Snapshot result;
        result.append(payloads);
        result.append(store);
        if (heap.backing)
        {
            require(!heap.released, "snapshot contains dead storage");
            result.append(heap.backing, heap.size + 2 * guard);
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
    void query(int group, int expected_id, bool alias = false)
    {
        struct Output
        {
            std::array<unsigned char, 16> before;
            int x;
            int y;
            std::array<unsigned char, 16> after;
        } output;
        output.before.fill(0x19);
        output.after.fill(0xb7);
        output.x = 0x13572468;
        output.y = -0x24681357;
        const Snapshot before = snapshot();
        const bool found =
            store.value.FindPlacement(group, &output.x, alias ? &output.x : &output.y);
        const Snapshot after = snapshot();
        ++queries;
        require(found == (expected_id >= 0), "literal query presence differs");
        const SavedScreenPlacement* expected =
            expected_id < 0 ? nullptr : &records[record_ids[expected_id]];
        require(output.x == (expected ? (alias ? expected->y : expected->x) : 0x13572468),
            "literal x output differs");
        require(output.y == (!alias && expected ? expected->y : -0x24681357),
            "literal y output differs");
        require(before.size == after.size &&
                    std::memcmp(before.bytes.data(), after.bytes.data(), before.size) == 0,
            "lookup changed the live graph or providers");
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
            query(q.group, q.expected[step]);
        }
    }
    SavedScreenPlacementNode* insert(unsigned id, int before_id)
    {
        require(nodes[id] == nullptr, "node ID reused while live");
        SavedScreenPlacement* placement = &payloads[record_ids[id]].value;
        CircularListIterator position;
        position.node = node(before_id);
        auto* const old_position = position.node;
        const auto before_exchanges = exchanges;
        CircularListIterator inserted;
        {
            AllocationScope scope(id == 0 ? AllocationPlan::cold_pool : AllocationPlan::none);
            inserted = store.value.saved.Insert(position, placement);
        }
        require(pool_lock_word == 0 && exchanges == before_exchanges + 1,
            "insertion lock lifecycle differs");
        require(placement == &payloads[record_ids[id]].value && position.node == old_position,
            "insertion inputs changed");
        require(heap.backing && !heap.released && address(inserted.node) >= address(heap.memory) &&
                    address(inserted.node) - address(heap.memory) <=
                        heap.size - sizeof(SavedScreenPlacementNode) &&
                    address(inserted.node) % alignof(SavedScreenPlacementNode) == 0,
            "actual returned node is outside live aligned storage");
        for (auto* live : nodes)
        {
            require(live != inserted.node, "insertion aliases a live node");
        }
        nodes[id] = static_cast<SavedScreenPlacementNode*>(inserted.node);
        require(nodes[id]->placement == placement, "inserted payload identity differs");
        ++insertions;
        return nodes[id];
    }
    std::uintptr_t retire(unsigned id)
    {
        auto* live = nodes[id];
        require(live != nullptr, "retiring an absent node");
        const auto saved = address(live);
        // Generated unlink/ownership logic, not a reconstructed erase method.
        live->previous->next = live->next;
        live->next->previous = live->previous;
        nodes[id] = nullptr;
        const auto before_exchanges = exchanges;
        {
            AllocationScope scope(AllocationPlan::none);
            ReturnPool(live, saved_placement_node_slot_bytes);
        }
        require(pool_lock_word == 0 && exchanges == before_exchanges + 1,
            "return lock lifecycle differs");
        ++returns;
        return saved;
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
        const auto retired = retire(2);
        constexpr int after_retire[] = {4, 0, 9, 6, 3, 8, 1, 5, 7};
        constexpr int retired_queries[] = {0, 1, 3, 4, 5, 6, 9, -1, -1};
        check_order(after_retire, 9);
        for (unsigned n = 0; n < std::size(query_table); ++n)
        {
            query(query_table[n].group, retired_queries[n]);
        }
        require(address(insert(10, 4)) == retired, "returned slot was not reused");
        constexpr int after_reinsert[] = {10, 4, 0, 9, 6, 3, 8, 1, 5, 7};
        constexpr int reinserted_queries[] = {10, 1, 3, 4, 5, 6, 9, -1, -1};
        check_order(after_reinsert, 10);
        for (unsigned n = 0; n < std::size(query_table); ++n)
        {
            query(query_table[n].group, reinserted_queries[n]);
        }
        // Defined aliasing: the two sequential stores leave the y coordinate.
        query(7, 10, true);
        query(INT_MIN, 4, true);
        query(8, -1, true);
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
        ::operator delete(heap.memory, heap.size);
        // All borrowed records remain live and unchanged after node retirement.
        check_order(orders[0], 0);
    }
};
// Separate literal oracle for actual WritePlacement. Record and node IDs are
// fixture identities; no expectation is computed by traversing the real list.
constexpr SavedScreenPlacement write_values[6][3] = {
    {{11, -9, 7}, {INT_MIN, INT_MAX, 7}, {INT_MAX, INT_MIN, 7}},
    {{-32768, 65535, INT_MIN}, {INT_MIN + 1, INT_MAX - 1, INT_MIN}, {}},
    {{123, -456, 7}, {}, {}},
    {{77, 88, 7}, {-1, 0, 7}, {}},
    {{0, -1, INT_MAX}, {}, {}},
    {{INT_MIN, 0, 0}, {}, {}},
};
constexpr int write_versions[13][6] = {
    {-1, -1, 0, 0, -1, -1},
    {0, -1, 0, 0, -1, -1},
    {1, -1, 0, 0, -1, -1},
    {1, 0, 0, 0, -1, -1},
    {1, 0, 0, 0, -1, -1},
    {2, 0, 0, 0, -1, -1},
    {2, 0, 0, 0, -1, -1},
    {2, 0, 0, 1, -1, -1},
    {2, 0, 0, 1, 0, -1},
    {2, 1, 0, 1, 0, -1},
    {2, 1, 0, 1, 0, -1},
    {2, 1, 0, 1, 0, -1},
    {2, 1, 0, 1, 0, 0},
};
constexpr unsigned write_lengths[] = {0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 5, 4, 5};
constexpr int write_orders[13][5] = {
    {-1, -1, -1, -1, -1},
    {0, -1, -1, -1, -1},
    {0, -1, -1, -1, -1},
    {0, 1, -1, -1, -1},
    {0, 1, 2, -1, -1},
    {0, 1, 2, -1, -1},
    {3, 0, 1, 2, -1},
    {3, 0, 1, 2, -1},
    {3, 0, 1, 2, 4},
    {3, 0, 1, 2, 4},
    {3, 0, 1, 2, 4},
    {0, 1, 2, 4, -1},
    {0, 1, 2, 4, 5},
};
struct WriteQuery
{
    int group;
    std::array<int, 13> expected;
};
constexpr WriteQuery write_queries[] = {
    {7, {-1, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 0, 0}},
    {INT_MIN, {-1, -1, -1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}},
    {INT_MAX, {-1, -1, -1, -1, -1, -1, -1, -1, 4, 4, 4, 4, 4}},
    {0, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 5}},
    {-1, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    {42, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
};

struct WriterScenario
{
    StoreFrame store;
    std::array<PlacementFrame, 2> borrowed{};
    std::array<SavedScreenPlacement*, 6> payloads{};
    std::array<SavedScreenPlacementNode*, 6> nodes{};

    WriterScenario()
    {
        require(owned_record_count == 0, "prior owned record ledger remains");
        for (unsigned i = 0; i < borrowed.size(); ++i)
        {
            borrowed[i].before.fill(0x61);
            borrowed[i].after.fill(0x94);
            borrowed[i].value = write_values[i + 2][0];
            payloads[i + 2] = &borrowed[i].value;
            require(borrowed_records[i] == 0, "prior borrowed record registration remains");
            borrowed_records[i] = address(payloads[i + 2]);
        }
    }

    CircularListNode* node(int id)
    {
        if (id < 0)
        {
            return &store.sentinel;
        }
        require(static_cast<unsigned>(id) < nodes.size() && nodes[id],
            "writer oracle names an absent node");
        return nodes[id];
    }

    void check_prefix_and_guards()
    {
        require(store.value.saved.head == &store.sentinel, "writer store head changed");
        for (const auto value : store.before)
        {
            require(value == 0x74, "writer store guard changed");
        }
        for (const auto value : store.after)
        {
            require(value == 0x28, "writer store guard changed");
        }
        for (const auto value : store.value.unknown_00)
        {
            require(value == 0x67, "writer store opaque prefix changed");
        }
        for (const auto& record : borrowed)
        {
            for (const auto value : record.before)
            {
                require(value == 0x61, "writer borrowed guard changed");
            }
            for (const auto value : record.after)
            {
                require(value == 0x94, "writer borrowed guard changed");
            }
        }
        check_heap_guards();
        for (const auto& allocation : owned_records)
        {
            check_allocation_guards(allocation);
        }
    }

    void check_values(unsigned step)
    {
        for (unsigned id = 0; id < payloads.size(); ++id)
        {
            const int version = write_versions[step][id];
            require((payloads[id] != nullptr) == (version >= 0),
                "literal writer record lifetime differs");
            if (version >= 0)
            {
                const auto& expected = write_values[id][version];
                require(payloads[id]->x == expected.x, "literal written x coordinate differs");
                require(payloads[id]->y == expected.y, "literal written y coordinate differs");
                require(payloads[id]->group == expected.group, "literal written group differs");
            }
        }
        check_prefix_and_guards();
    }

    void check_order(unsigned step)
    {
        const auto length = write_lengths[step];
        auto* current = store.sentinel.next;
        for (unsigned i = 0; i < length; ++i)
        {
            const auto id = write_orders[step][i];
            require(current == node(id), "literal writer forward order differs");
            require(nodes[id]->previous == node(i ? write_orders[step][i - 1] : -1),
                "literal writer previous link differs");
            require(nodes[id]->next == node(i + 1 < length ? write_orders[step][i + 1] : -1),
                "literal writer next link differs");
            require(nodes[id]->placement == payloads[id], "writer payload identity differs");
            current = nodes[id]->next;
        }
        require(current == &store.sentinel, "writer forward end differs");
        require(store.sentinel.previous == node(length ? write_orders[step][length - 1] : -1),
            "writer sentinel tail differs");
        require(store.value.saved.Begin().node == node(length ? write_orders[step][0] : -1),
            "writer actual Begin differs");
        require(store.value.saved.End().node == &store.sentinel, "writer actual End differs");
    }

    Snapshot snapshot(const SavedScreenPlacement* writable = nullptr) const
    {
        Snapshot result;
        const auto append = [&](const void* memory, std::size_t size)
        {
            const auto offset = result.size;
            result.append(memory, size);
            if (writable && address(writable) >= address(memory) &&
                address(writable) - address(memory) <= size &&
                size - (address(writable) - address(memory)) >= 8)
            {
                // Only the two observed coordinate words may change. All other
                // bytes, including this record's group and allocation guards,
                // remain represented in the comparison.
                std::memset(
                    result.bytes.data() + offset + address(writable) - address(memory), 0, 8);
            }
        };
        append(&store, sizeof(store));
        append(borrowed.data(), sizeof(borrowed));
        result.append(payloads);
        result.append(nodes);
        result.append(owned_records);
        result.append(owned_record_count);
        result.append(borrowed_records);
        for (const auto& allocation : owned_records)
        {
            if (allocation.backing)
            {
                require(!allocation.released, "writer snapshot contains dead record storage");
                append(allocation.backing, allocation.size + 2 * guard);
            }
        }
        if (heap.backing)
        {
            require(!heap.released, "writer snapshot contains dead pool storage");
            append(heap.backing, heap.size + 2 * guard);
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
        result.append(record_allocations);
        result.append(record_releases);
        result.append(ordinary_allocations);
        result.append(ordinary_releases);
        return result;
    }

    void query(int group, int expected_id, unsigned step)
    {
        struct Output
        {
            std::array<unsigned char, 16> before;
            int x;
            int y;
            std::array<unsigned char, 16> after;
        } output;
        output.before.fill(0x19);
        output.after.fill(0xb7);
        output.x = 0x13572468;
        output.y = -0x24681357;
        const Snapshot before = snapshot();
        const bool found = store.value.FindPlacement(group, &output.x, &output.y);
        const Snapshot after = snapshot();
        ++queries;
        require(found == (expected_id >= 0), "literal writer query presence differs");
        const SavedScreenPlacement* expected =
            expected_id < 0 ? nullptr
                            : &write_values[expected_id][write_versions[step][expected_id]];
        require(
            output.x == (expected ? expected->x : 0x13572468), "literal writer x output differs");
        require(
            output.y == (expected ? expected->y : -0x24681357), "literal writer y output differs");
        require(before.size == after.size &&
                    std::memcmp(before.bytes.data(), after.bytes.data(), before.size) == 0,
            "writer lookup changed the live graph or providers");
        for (const auto value : output.before)
        {
            require(value == 0x19, "writer output leading guard changed");
        }
        for (const auto value : output.after)
        {
            require(value == 0xb7, "writer output trailing guard changed");
        }
    }

    void verify(unsigned step)
    {
        check_order(step);
        check_values(step);
        for (const auto& expected : write_queries)
        {
            query(expected.group, expected.expected[step], step);
        }
    }

    SavedScreenPlacementNode* accept_node(CircularListNode* result, unsigned id)
    {
        require(id < nodes.size() && nodes[id] == nullptr, "writer reuses a live node ID");
        require(heap.backing && !heap.released && address(result) >= address(heap.memory) &&
                    address(result) - address(heap.memory) <=
                        heap.size - sizeof(SavedScreenPlacementNode) &&
                    address(result) % alignof(SavedScreenPlacementNode) == 0,
            "writer node is outside live aligned storage");
        for (auto* live : nodes)
        {
            require(live != result, "writer insertion aliases a live node");
        }
        nodes[id] = static_cast<SavedScreenPlacementNode*>(result);
        ++insertions;
        return nodes[id];
    }

    void write_new(unsigned id, unsigned record_id, int x, int y, int group, AllocationPlan plan)
    {
        require(owned_record_count == record_id && payloads[id] == nullptr,
            "writer record plan differs");
        const auto previous_exchanges = exchanges;
        {
            AllocationScope scope(plan);
            store.value.WritePlacement(x, y, group);
        }
        ++writes;
        require(pool_lock_word == 0 && exchanges == previous_exchanges + 1,
            "missing write lock lifecycle differs");
        require(owned_record_count == record_id + 1, "missing write allocation count differs");
        auto* inserted = accept_node(store.sentinel.previous, id);
        require(address(inserted->placement) == owned_records[record_id].identity,
            "writer did not publish its actual record allocation");
        payloads[id] = inserted->placement;
    }

    void write_existing(
        unsigned id, int x, int y, int group, unsigned next_step, bool identical = false)
    {
        require(payloads[id] != nullptr, "updating an absent writer record");
        const auto* allowed = identical ? nullptr : payloads[id];
        const Snapshot before = snapshot(allowed);
        {
            AllocationScope scope(AllocationPlan::update);
            store.value.WritePlacement(x, y, group);
        }
        const Snapshot after = snapshot(allowed);
        ++writes;
        ++updates;
        check_values(next_step);
        require(before.size == after.size &&
                    std::memcmp(before.bytes.data(), after.bytes.data(), before.size) == 0,
            identical ? "identical write changed live representation or providers"
                      : "update changed fields outside first matching coordinates");
    }

    void insert_borrowed(unsigned id, int before_id)
    {
        require(id == 2 || id == 3, "borrowed writer record ID differs");
        auto* placement = payloads[id];
        CircularListIterator position;
        position.node = node(before_id);
        const auto* previous_position = position.node;
        const auto previous_exchanges = exchanges;
        CircularListIterator inserted;
        {
            AllocationScope scope(AllocationPlan::none);
            inserted = store.value.saved.Insert(position, placement);
        }
        require(pool_lock_word == 0 && exchanges == previous_exchanges + 1,
            "borrowed insertion lock lifecycle differs");
        require(placement == payloads[id] && position.node == previous_position,
            "borrowed insertion inputs changed");
        require(accept_node(inserted.node, id)->placement == placement,
            "borrowed insertion payload identity differs");
    }

    std::uintptr_t retire(unsigned id)
    {
        auto* live = nodes[id];
        require(live != nullptr, "retiring an absent writer node");
        const auto identity = address(live);
        // Generated unlink/ownership logic, not a reconstructed erase method.
        live->previous->next = live->next;
        live->next->previous = live->previous;
        nodes[id] = nullptr;
        const auto previous_exchanges = exchanges;
        {
            AllocationScope scope(AllocationPlan::none);
            ReturnPool(live, saved_placement_node_slot_bytes);
        }
        require(pool_lock_word == 0 && exchanges == previous_exchanges + 1,
            "writer return lock lifecycle differs");
        ++returns;
        return identity;
    }

    void run()
    {
        verify(0);
        operation = 0;
        write_new(0, 0, 11, -9, 7, AllocationPlan::cold_record);
        verify(1);
        operation = 1;
        write_existing(0, INT_MIN, INT_MAX, 7, 2);
        verify(2);
        operation = 2;
        write_new(1, 1, -32768, 65535, INT_MIN, AllocationPlan::record);
        verify(3);
        operation = 3;
        insert_borrowed(2, -1);
        verify(4);
        operation = 4;
        write_existing(0, INT_MAX, INT_MIN, 7, 5);
        verify(5);
        operation = 5;
        insert_borrowed(3, 0);
        verify(6);
        operation = 6;
        write_existing(3, -1, 0, 7, 7);
        verify(7);
        operation = 7;
        write_new(4, 2, 0, -1, INT_MAX, AllocationPlan::record);
        verify(8);
        operation = 8;
        write_existing(1, INT_MIN + 1, INT_MAX - 1, INT_MIN, 9);
        verify(9);
        operation = 9;
        write_existing(4, 0, -1, INT_MAX, 10, true);
        verify(10);
        operation = 10;
        const auto retired = retire(3);
        verify(11);
        operation = 11;
        write_new(5, 3, INT_MIN, 0, 0, AllocationPlan::record);
        require(address(nodes[5]) == retired, "writer did not reuse the returned slot");
        verify(12);
        for (const int id : write_orders[12])
        {
            retire(static_cast<unsigned>(id));
        }
        check_order(0);
        check_values(12);
        for (const auto& expected : write_queries)
        {
            query(expected.group, -1, 0);
        }
        // All nodes have been detached/returned before caller-owned records die.
        // Borrowed records outlive this cleanup and are never passed to delete.
        constexpr unsigned owned_ids[] = {0, 1, 4, 5};
        for (const auto id : owned_ids)
        {
            delete payloads[id];
            payloads[id] = nullptr;
        }
        for (unsigned i = 0; i < owned_record_count; ++i)
        {
            require(owned_records[i].released && owned_records[i].backing == nullptr &&
                        owned_records[i].memory == nullptr,
                "owned writer record remains live");
        }
        for (auto& bucket : pool_chunk_buckets)
        {
            bucket = nullptr;
        }
        pool_chunk_begin = nullptr;
        pool_chunk_end = nullptr;
        require(pool_heap_bytes == 960, "writer literal heap accounting differs");
        pool_heap_bytes = 0;
        ::operator delete(heap.memory, heap.size);
        for (unsigned i = 0; i < borrowed.size(); ++i)
        {
            const unsigned id = i + 2;
            const auto& expected = write_values[id][write_versions[12][id]];
            require(borrowed[i].value.x == expected.x && borrowed[i].value.y == expected.y &&
                        borrowed[i].value.group == expected.group,
                "borrowed writer record changed during teardown");
        }
        check_prefix_and_guards();
        borrowed_records.fill(0);
    }
};

} // namespace

extern "C" LONG InterlockedExchange(LONG* target, LONG value)
{
    volatile LONG* const word = target;
    require(pool_allocation && target == &pool_lock_word && value == 1 && *word == 0,
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
    static_assert(sizeof(SavedScreenPlacement) == 12);
    static_assert(offsetof(SavedScreenPlacement, x) == 0 &&
                  offsetof(SavedScreenPlacement, y) == 4 &&
                  offsetof(SavedScreenPlacement, group) == 8);
    static_assert(
        sizeof(SavedScreenPlacementNode) == slot && saved_placement_node_slot_bytes == slot);
    static_assert(circular_list_node_slot_bytes == saved_placement_node_slot_bytes);
    static_assert(std::is_trivially_destructible_v<SavedScreenPlacementNode>);
    static_assert(std::is_trivially_destructible_v<PoolChunkLink>);
    static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ <= alignof(std::max_align_t));
    for (session = 0; session < 2; ++session)
    {
        operation = 0;
        require(heap.backing == nullptr && !heap.released && pool_lock_word == 0 &&
                    pool_chunk_begin == nullptr && pool_chunk_end == nullptr &&
                    pool_heap_bytes == 0,
            "prior owner state remains");
        for (auto* bucket : pool_chunk_buckets)
        {
            require(bucket == nullptr, "prior free root remains");
        }
        {
            Scenario scenario;
            scenario.run();
        }
        require(heap.released && heap.backing == nullptr && heap.memory == nullptr,
            "owned allocation remains live");
        heap = Allocation{};
    }
    require(allocations == 2 && releases == allocations && insertions == 22 && returns == 22 &&
                exchanges == 44 && queries == 258,
        "literal operation totals differ");
    const auto list_queries = queries;
    const auto list_insertions = insertions;
    const auto list_returns = returns;
    const auto list_exchanges = exchanges;
    for (session = 2; session < 4; ++session)
    {
        operation = 0;
        require(heap.backing == nullptr && !heap.released && pool_lock_word == 0 &&
                    pool_chunk_begin == nullptr && pool_chunk_end == nullptr &&
                    pool_heap_bytes == 0 && owned_record_count == 0,
            "prior writer owner state remains");
        for (auto* bucket : pool_chunk_buckets)
        {
            require(bucket == nullptr, "prior writer free root remains");
        }
        {
            WriterScenario scenario;
            scenario.run();
        }
        require(heap.released && heap.backing == nullptr && heap.memory == nullptr,
            "writer pool allocation remains live");
        require(owned_record_count == 4, "literal owned writer record count differs");
        for (const auto& allocation : owned_records)
        {
            require(allocation.released && allocation.backing == nullptr &&
                        allocation.memory == nullptr,
                "writer record remains live after scenario");
        }
        heap = Allocation{};
        owned_records.fill(Allocation{});
        owned_record_count = 0;
    }
    require(allocations == 4 && releases == allocations && record_allocations == 8 &&
                record_releases == record_allocations && insertions == 34 && returns == 34 &&
                exchanges == 68 && queries == 426 && writes == 18 && updates == 10,
        "literal combined operation totals differ");
    std::printf(
        "{\"sessions\":4,\"checks\":%llu,\"queries\":%llu,\"insertions\":%llu,\"returns\":%llu,"
        "\"allocations\":%llu,\"releases\":%llu,\"exchanges\":%llu,\"writes\":%llu,\"updates\":%"
        "llu,"
        "\"record_allocations\":%llu,\"record_releases\":%llu,\"list_sessions\":2,\"writer_"
        "sessions\":2,"
        "\"list_queries\":%llu,\"list_insertions\":%llu,\"list_returns\":%llu,\"list_exchanges\":%"
        "llu}\n",
        checks,
        queries,
        insertions,
        returns,
        allocations,
        releases,
        exchanges,
        writes,
        updates,
        record_allocations,
        record_releases,
        list_queries,
        list_insertions,
        list_returns,
        list_exchanges);
}
