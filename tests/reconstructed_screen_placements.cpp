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
struct Allocation
{
    unsigned char* backing = nullptr;
    unsigned char* memory = nullptr;
    std::size_t size = 0;
    bool released = false;
};
Allocation heap;
constexpr unsigned guard = 32;
constexpr unsigned slot = 24;
constexpr unsigned max_nodes = 11;

class AllocationScope
{
  public:
    AllocationScope()
    {
        require(!pool_allocation, "nested allocation scope");
        pool_allocation = true;
    }
    ~AllocationScope()
    {
        pool_allocation = false;
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

void check_heap_guards()
{
    if (heap.backing && !heap.released)
    {
        for (unsigned i = 0; i < guard; ++i)
        {
            require(heap.backing[i] == 0xa6 && heap.backing[guard + heap.size + i] == 0xa6,
                "allocation guard changed");
        }
    }
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
    require(bytes == 960, "literal pool allocation size differs");
    require(pool_lock_word == 1, "pool allocation precedes acquisition");
    require(heap.backing == nullptr && !heap.released, "unexpected second pool heap");
    void* raw = std::malloc(bytes + 2 * guard);
    if (!raw)
    {
        throw std::bad_alloc();
    }
    auto* backing = ::new (raw) unsigned char[bytes + 2 * guard];
    std::memset(backing, 0xa6, bytes + 2 * guard);
    auto* memory = backing + guard;
    require(address(memory) % alignof(std::max_align_t) == 0, "provider alignment differs");
    heap = Allocation{backing, memory, bytes, false};
    ++allocations;
    return memory;
}

void release(void* memory) noexcept
{
    if (!memory)
    {
        return;
    }
    require(!pool_allocation, "release inside allocation scope");
    if (memory == heap.memory)
    {
        require(!heap.released && heap.backing != nullptr, "duplicate scalar release");
        check_heap_guards();
        std::free(heap.backing);
        heap.backing = nullptr;
        heap.memory = nullptr;
        heap.released = true;
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
            AllocationScope scope;
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
            AllocationScope scope;
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
    std::printf(
        "{\"sessions\":2,\"checks\":%llu,\"queries\":%llu,\"insertions\":%llu,\"returns\":%llu,"
        "\"allocations\":%llu,\"releases\":%llu,\"exchanges\":%llu}\n",
        checks,
        queries,
        insertions,
        returns,
        allocations,
        releases,
        exchanges);
}
