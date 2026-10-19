#include "pool_chunk_acquire.h"
#include "pool_lock_acquire.h"
#include "pool_lock_guard.h"
#include "pool_return.h"
#include "startup_list.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

// Generated identity objects; reconstructed code only stores these pointers.
struct ActiveScreenEntry
{
    unsigned identity;
};

namespace
{
constexpr unsigned max_nodes = 192;
constexpr unsigned guard_size = 32;
constexpr unsigned slot_bytes = 24;
constexpr std::array<unsigned, 4> requests{960, 1024, 1088, 1152};
constexpr std::array<unsigned, 4> request_at{0, 40, 82, 127};
constexpr std::array<unsigned, 4> heap_first_id{0, 40, 82, 127};
constexpr std::array<unsigned, 4> heap_last_id{40, 82, 127, 175};
constexpr std::array<DWORD, 18> wait_sequence{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 4, 8};

unsigned cases = 0;
unsigned long long checks = 0;
unsigned long long insertions = 0;
unsigned long long returns = 0;
unsigned long long total_exchanges = 0;
unsigned long long total_sleeps = 0;
unsigned long long allocation_events = 0;
unsigned long long exception_events = 0;
unsigned long long allocation_releases = 0;
thread_local unsigned long long ordinary_allocations = 0;
thread_local unsigned long long ordinary_deallocations = 0;
unsigned long long operation_scopes = 0;
unsigned operation = 0;
unsigned failures = 0;
unsigned exchanges = 0;
unsigned sleeps = 0;
unsigned allocation_count = 0;
unsigned throwing_call = 99;
bool single_slot_setup = false;

// A labelled host allocation failure; no original CRT failure behavior is inferred.
struct PoolAllocationFailure final : std::bad_alloc
{
    const char* what() const noexcept override
    {
        return "generated screen-list pool allocation failure";
    }
};

class PoolOperationScope;
thread_local PoolOperationScope* active_pool_operation = nullptr;
struct Heap
{
    unsigned char* backing = nullptr;
    unsigned char* bytes = nullptr;
    unsigned size = 0;
    bool released = false;
};
std::array<Heap, 8> heaps;
unsigned heap_count = 0;

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "case %u operation %u: %s\n", cases, operation, reason);
        std::exit(2);
    }
}

class PoolOperationScope
{
    PoolOperationScope* previous;

  public:
    PoolOperationScope() noexcept
        : previous(active_pool_operation)
    {
        require(previous == nullptr, "nested pool allocation scope");
        active_pool_operation = this;
        ++operation_scopes;
    }
    ~PoolOperationScope()
    {
        active_pool_operation = previous;
    }
    PoolOperationScope(const PoolOperationScope&) = delete;
    PoolOperationScope& operator=(const PoolOperationScope&) = delete;
};

void* allocate_pool(std::size_t bytes);
void release_scalar(void* memory) noexcept;
} // namespace

// Executable-only replaceable scalar forms. The prepared archive retains its
// actual ::operator new calls. Placement, array and aligned forms are unchanged.
void* operator new(std::size_t bytes)
{
    if (active_pool_operation)
    {
        return allocate_pool(bytes);
    }
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

void operator delete(void* memory) noexcept
{
    release_scalar(memory);
}
void operator delete(void* memory, std::size_t) noexcept
{
    release_scalar(memory);
}

namespace
{
std::uintptr_t address(const void* value)
{
    return reinterpret_cast<std::uintptr_t>(value);
}

void check_guards()
{
    for (unsigned h = 0; h < heap_count; ++h)
    {
        for (unsigned n = 0; n < guard_size; ++n)
        {
            require(heaps[h].backing[n] == 0xa6, "allocation leading guard differs");
            require(heaps[h].backing[guard_size + heaps[h].size + n] == 0xa6,
                "allocation trailing guard differs");
        }
    }
}

void require_span(const void* value, unsigned bytes, unsigned alignment)
{
    const std::uintptr_t at = address(value);
    require(at % alignment == 0, "node alignment differs");
    unsigned found = 0;
    for (unsigned h = 0; h < heap_count; ++h)
    {
        const std::uintptr_t begin = address(heaps[h].bytes);
        if (at >= begin && at - begin <= heaps[h].size && bytes <= heaps[h].size - (at - begin))
        {
            ++found;
        }
    }
    require(found == 1, "typed access is outside one live allocation");
}

void before_operation(unsigned schedule)
{
    require(active_pool_operation == nullptr, "prior allocation scope remains active");
    require(pool_lock_word == 0, "previous operation did not clear shared word");
    if (pool_chunk_begin == nullptr || pool_chunk_end == nullptr)
    {
        require(pool_chunk_begin == nullptr && pool_chunk_end == nullptr,
            "incomplete empty chunk span");
    }
    else
    {
        unsigned enclosing = 0;
        const auto begin = address(pool_chunk_begin);
        const auto end = address(pool_chunk_end);
        for (unsigned h = 0; h < heap_count; ++h)
        {
            const auto heap_begin = address(heaps[h].bytes);
            if (begin >= heap_begin && end >= begin && end - heap_begin <= heaps[h].size)
            {
                ++enclosing;
            }
        }
        require(enclosing == 1, "chunk pointers are not in one live backing array");
    }
    failures = schedule;
    exchanges = 0;
    sleeps = 0;
    pool_spin_limit = 0;
    pool_last_spin = 0x9ac17321u;
}

void after_operation()
{
    require(
        active_pool_operation == nullptr, "allocation scope did not restore on return or unwind");
    require(pool_lock_word == 0, "actual cleanup did not clear shared word");
    require(exchanges == failures + 1, "exchange schedule differs");
    require(sleeps == (failures ? failures - 1 : 0), "wait count differs");
    require(pool_spin_limit == (failures ? 30u : 0u), "adaptive limit differs");
    require(pool_last_spin == 0x9ac17321u, "adaptive history changed");
    check_guards();
}

struct Ring
{
    std::array<unsigned char, 16> leading;
    CircularListNode sentinel;
    CircularList owner;
    std::array<unsigned char, 16> trailing;
    std::array<CircularListValueNode*, max_nodes> nodes{};
    std::array<ActiveScreenEntry*, max_nodes> values{};
    std::array<unsigned, max_nodes> order{};
    unsigned count = 0;

    Ring()
    {
        leading.fill(0x37);
        trailing.fill(0xc1);
        sentinel.next = &sentinel;
        sentinel.previous = &sentinel;
        owner.head = &sentinel;
    }

    void check()
    {
        require(owner.head == &sentinel, "owner head changed");
        for (unsigned n = 0; n < leading.size(); ++n)
        {
            require(leading[n] == 0x37, "owner leading guard differs");
            require(trailing[n] == 0xc1, "owner trailing guard differs");
        }
        CircularListNode* forward = sentinel.next;
        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned id = order[i];
            require(forward == nodes[id], "forward identity sequence differs");
            require_span(nodes[id], sizeof(CircularListValueNode), alignof(CircularListValueNode));
            CircularListNode* previous =
                i ? static_cast<CircularListNode*>(nodes[order[i - 1]]) : &sentinel;
            CircularListNode* next =
                i + 1 < count ? static_cast<CircularListNode*>(nodes[order[i + 1]]) : &sentinel;
            require(nodes[id]->previous == previous, "previous link differs");
            require(nodes[id]->next == next, "next link differs");
            require(nodes[id]->entry == values[id], "payload identity differs");
            forward = nodes[id]->next;
        }
        require(forward == &sentinel, "forward end differs");
        CircularListNode* backward = sentinel.previous;
        for (unsigned i = count; i > 0; --i)
        {
            require(backward == nodes[order[i - 1]], "reverse identity sequence differs");
            backward = backward->previous;
        }
        require(backward == &sentinel, "reverse end differs");
        require(owner.Begin().node ==
                    (count ? static_cast<CircularListNode*>(nodes[order[0]]) : &sentinel),
            "actual Begin differs");
        require(owner.End().node == &sentinel, "actual End differs");
    }

    CircularListValueNode* insert(
        unsigned id, unsigned position, ActiveScreenEntry* const& value, unsigned schedule)
    {
        require(
            count < order.size() && id < nodes.size() && nodes[id] == nullptr && position <= count,
            "invalid generated insertion");
        ActiveScreenEntry* const original = value;
        CircularListIterator where;
        where.node =
            position < count ? static_cast<CircularListNode*>(nodes[order[position]]) : &sentinel;
        CircularListNode* const old_position = where.node;
        before_operation(schedule);
        CircularListIterator result;
        {
            PoolOperationScope allocation_scope;
            result = owner.Insert(where, value);
        }
        after_operation();
        require(where.node == old_position, "by-value iterator changed");
        require(value == original, "payload reference changed");
        require_span(result.node, sizeof(CircularListValueNode), alignof(CircularListValueNode));
        for (unsigned n = 0; n < count; ++n)
        {
            const auto fresh = address(result.node);
            const auto old = address(nodes[order[n]]);
            require(fresh + sizeof(CircularListValueNode) <= old ||
                        old + sizeof(CircularListValueNode) <= fresh,
                "allocation overlaps a live node");
        }
        nodes[id] = static_cast<CircularListValueNode*>(result.node);
        values[id] = original;
        for (unsigned n = count; n > position; --n)
        {
            order[n] = order[n - 1];
        }
        order[position] = id;
        ++count;
        ++insertions;
        check();
        return nodes[id];
    }

    std::uintptr_t release(unsigned position, unsigned schedule)
    {
        require(position < count, "invalid generated release");
        unsigned id = order[position];
        CircularListValueNode* value = nodes[id];
        const std::uintptr_t result = address(value);
        // Fixture-owned unlinking prepares a valid detached input to ReturnPool;
        // this is not a claim that a retail erase/destructor has been recovered.
        value->previous->next = value->next;
        value->next->previous = value->previous;
        for (unsigned n = position + 1; n < count; ++n)
        {
            order[n - 1] = order[n];
        }
        --count;
        nodes[id] = nullptr;
        values[id] = nullptr;
        before_operation(schedule);
        {
            PoolOperationScope allocation_scope;
            ReturnPool(value, circular_list_node_slot_bytes);
        }
        after_operation();
        ++returns;
        check();
        return result;
    }
};

void check_free_chain(const Ring& ring)
{
    std::array<std::uintptr_t, max_nodes> seen{};
    unsigned count = 0;
    PoolChunkLink* link = pool_chunk_buckets[2];
    while (link)
    {
        require(count < seen.size(), "free chain is cyclic or oversized");
        require_span(link, sizeof(PoolChunkLink), alignof(PoolChunkLink));
        const std::uintptr_t current = address(link);
        for (unsigned n = 0; n < count; ++n)
        {
            require(seen[n] != current, "free chain cycles");
        }
        for (unsigned n = 0; n < ring.count; ++n)
        {
            require(
                current != address(ring.nodes[ring.order[n]]), "free chain contains a live node");
        }
        seen[count++] = current;
        // The positive source explicitly constructed each of these link objects.
        link = link->next;
    }
}

void start_case(unsigned throw_at = 99, bool setup = false)
{
    ++cases;
    require(heap_count == 0, "prior case allocations remain");
    for (auto* bucket : pool_chunk_buckets)
    {
        require(bucket == nullptr, "prior pool root remains");
    }
    require(pool_chunk_begin == nullptr && pool_chunk_end == nullptr && pool_heap_bytes == 0,
        "prior chunk state remains");
    throwing_call = throw_at;
    allocation_count = 0;
    operation = 0;
    single_slot_setup = setup;
}

// Checked generated teardown uses an index and expected address before passing
// a live returned pointer to delete. Unknown/duplicate requests cannot reach free.
void release_owned_heap(unsigned index, std::uintptr_t expected_address)
{
    require(active_pool_operation == nullptr, "teardown entered inside a pool scope");
    require(index < heap_count, "unknown owned-heap teardown index");
    auto& heap = heaps[index];
    require(!heap.released, "duplicate owned-heap teardown");
    require(
        heap.backing != nullptr && heap.bytes != nullptr && address(heap.bytes) == expected_address,
        "unknown owned-heap teardown address");
    void* const returned_pointer = heap.bytes;
    require(returned_pointer != heap.backing, "returned and backing pointers were conflated");
    const auto before = allocation_releases;
    if (index % 2)
    {
        ::operator delete(returned_pointer, heap.size);
    }
    else
    {
        ::operator delete(returned_pointer);
    }
    require(heap.released && heap.backing == nullptr && heap.bytes == nullptr &&
                allocation_releases == before + 1,
        "owned-heap teardown did not release its backing exactly once");
}

void finish_case(Ring& ring)
{
    ring.check();
    check_guards();
    // Detach generated owners and roots before releasing their backing storage.
    ring.sentinel.next = ring.sentinel.previous = &ring.sentinel;
    for (auto& root : pool_chunk_buckets)
    {
        root = nullptr;
    }
    pool_chunk_begin = nullptr;
    pool_chunk_end = nullptr;
    pool_heap_bytes = 0;
    for (unsigned h = 0; h < heap_count; ++h)
    {
        release_owned_heap(h, address(heaps[h].bytes));
        heaps[h] = Heap{};
    }
    heap_count = 0;
}

void run_sequence(unsigned mode, unsigned schedule)
{
    start_case();
    Ring ring;
    std::array<ActiveScreenEntry, 16> payloads;
    for (unsigned n = 0; n < payloads.size(); ++n)
    {
        payloads[n].identity = n + 100;
    }
    for (operation = 0; operation < 128; ++operation)
    {
        unsigned position = mode == 0   ? ring.count
                            : mode == 1 ? 0
                                        : (operation * 11u) % (ring.count + 1);
        ActiveScreenEntry* value = operation % 5 ? &payloads[operation % payloads.size()] : nullptr;
        ActiveScreenEntry* const* reference = &value;
        if (operation && operation % 7 == 0)
        {
            reference = &ring.nodes[ring.order[operation % ring.count]]->entry;
        }
        CircularListValueNode* node = ring.insert(operation, position, *reference, schedule);
        unsigned heap = 0;
        while (operation >= heap_last_id[heap])
        {
            ++heap;
        }
        require(heap < heap_count, "allocation ledger heap is missing");
        require(address(node) ==
                    address(heaps[heap].bytes) + (operation - heap_first_id[heap]) * slot_bytes,
            "fresh allocation address differs from literal ledger");
        check_free_chain(ring);
    }
    require(allocation_count == 4 && heap_count == 4 && pool_heap_bytes == 4224,
        "literal heap allocation accounting differs");
    require(pool_chunk_buckets[1] == reinterpret_cast<PoolChunkLink*>(heaps[1].bytes + 1008),
        "sixteen-byte remainder bucket differs");
    require(pool_chunk_buckets[0] == reinterpret_cast<PoolChunkLink*>(heaps[2].bytes + 1080),
        "eight-byte remainder bucket differs");
    std::array<std::uintptr_t, 32> freed;
    std::array<unsigned, 32> freed_ids;
    for (unsigned n = 0; n < freed.size(); ++n)
    {
        operation = 128 + n;
        unsigned position = (n * 7u) % ring.count;
        freed_ids[n] = ring.order[position];
        freed[n] = ring.release(position, schedule);
        check_free_chain(ring);
    }
    for (unsigned n = 0; n < freed.size(); ++n)
    {
        operation = 160 + n;
        ActiveScreenEntry* value = &payloads[(n + 3) % payloads.size()];
        CircularListValueNode* node =
            ring.insert(freed_ids[31 - n], (n * 5u) % (ring.count + 1), value, schedule);
        require(address(node) == freed[31 - n], "recycled LIFO identity differs");
        require(allocation_count == 4, "reuse unexpectedly allocated");
        check_free_chain(ring);
    }
    finish_case(ring);
}

void run_allocation_failure(unsigned failed_call)
{
    start_case(failed_call);
    Ring ring;
    ActiveScreenEntry payload{71};
    ActiveScreenEntry* value = &payload;
    const unsigned successes = failed_call == 0 ? 0 : 82;
    for (operation = 0; operation < successes; ++operation)
    {
        ring.insert(operation, ring.count, value, 0);
    }
    CircularListIterator position = ring.owner.End();
    bool caught = false;
    before_operation(2);
    try
    {
        PoolOperationScope allocation_scope;
        ring.owner.Insert(position, value);
    }
    catch (const PoolAllocationFailure&)
    {
        caught = true;
    }
    after_operation();
    require(caught, "host allocation exception was swallowed");
    require(ring.count == successes && allocation_count == failed_call + 1,
        "failed insertion changed list or event count");
    ring.check();
    if (failed_call == 0)
    {
        require(pool_chunk_begin == nullptr && pool_chunk_end == nullptr && pool_heap_bytes == 0,
            "initial throw changed empty chunk state");
    }
    else
    {
        require(
            pool_chunk_begin == heaps[1].bytes + 1008 && pool_chunk_end == heaps[1].bytes + 1024,
            "throw changed old remaining span");
        require(pool_heap_bytes == 1984, "throw changed successful heap accounting");
        require(pool_chunk_buckets[1] == reinterpret_cast<PoolChunkLink*>(heaps[1].bytes + 1008),
            "pre-throw remainder publication differs");
        require(pool_chunk_buckets[1]->next == nullptr, "pre-throw remainder link differs");
    }
    // No retry: the original algorithm has already published its remainder.
    finish_case(ring);
}

void run_single_remaining_slot()
{
    start_case(99, true);
    Ring ring;
    before_operation(0);
    int count = 1;
    char* first = nullptr;
    {
        PoolOperationScope allocation_scope;
        AcquirePoolLock(&pool_lock_word);
        PoolLockGuard guard;
        first = AcquirePoolChunk(slot_bytes, count);
    }
    after_operation();
    require(count == 1 && allocation_count == 1 && address(first) == address(heaps[0].bytes) &&
                heaps[0].size == 48,
        "actual one-block setup differs");
    operation = 1;
    ActiveScreenEntry payload{9};
    ActiveScreenEntry* value = &payload;
    auto* node = ring.insert(0, 0, value, 0);
    require(address(node) == address(heaps[0].bytes) + 24 && allocation_count == 1,
        "single remaining slot differs");
    require(pool_chunk_begin == pool_chunk_end && pool_chunk_buckets[2] == nullptr,
        "single-slot refill published a free chain");
    finish_case(ring);
}
void* allocate_pool(std::size_t bytes)
{
    require(
        active_pool_operation != nullptr, "pool allocation escaped the explicit operation scope");
    require(pool_lock_word == 1, "allocation provider did not observe actual acquisition");
    require(allocation_count < requests.size(), "unbounded scalar allocation");
    require(bytes == (single_slot_setup ? 48 : requests[allocation_count]),
        "scalar request differs from literal ledger");
    require(operation == (single_slot_setup ? 0 : request_at[allocation_count]),
        "allocation event order differs");
    check_guards();
    const unsigned call = allocation_count++;
    ++allocation_events;
    if (call == throwing_call)
    {
        ++exception_events;
        throw PoolAllocationFailure{};
    }
    require(heap_count < heaps.size(), "allocation ledger is full");
    void* raw = std::malloc(bytes + 2 * guard_size);
    require(raw != nullptr, "host fixture backing allocation failed");
    auto* backing = ::new (raw) unsigned char[bytes + 2 * guard_size];
    std::memset(backing, 0xa6, bytes + 2 * guard_size);
    unsigned char* result = backing + guard_size;
    require(address(result) % alignof(std::max_align_t) == 0, "provider storage alignment differs");
    heaps[heap_count++] = Heap{backing, result, static_cast<unsigned>(bytes), false};
    return result;
}

void release_scalar(void* memory) noexcept
{
    if (!memory)
    {
        return;
    }
    for (unsigned h = 0; h < heap_count; ++h)
    {
        auto& heap = heaps[h];
        if (!heap.released && memory == heap.bytes)
        {
            require(heap.backing != nullptr && heap.bytes != nullptr,
                "owned scalar release has no live backing");
            require(active_pool_operation == nullptr,
                "reconstructed operation unexpectedly released a whole pool heap");
            for (unsigned n = 0; n < guard_size; ++n)
            {
                require(heap.backing[n] == 0xa6 && heap.backing[guard_size + heap.size + n] == 0xa6,
                    "scalar release found a changed allocation guard");
            }
            std::free(heap.backing);
            heap.backing = nullptr;
            heap.bytes = nullptr;
            heap.released = true;
            ++allocation_releases;
            return;
        }
    }
    std::free(memory);
    ++ordinary_deallocations;
}

void check_unscoped_allocation()
{
    require(active_pool_operation == nullptr && heap_count == 0,
        "ordinary allocation check entered with a live pool");
    const auto allocations_before = ordinary_allocations;
    const auto deallocations_before = ordinary_deallocations;
    const auto pool_before = allocation_events;
    for (const std::size_t bytes : {0u, 1u, 47u, 96u})
    {
        void* memory = ::operator new(bytes);
        require(memory != nullptr && address(memory) % alignof(std::max_align_t) == 0,
            "ordinary scalar allocation is null or misaligned");
        if (bytes)
        {
            auto* array = ::new (memory) unsigned char[bytes];
            array[0] = 0x79;
            array[bytes - 1] = 0x37;
            require(array[bytes - 1] == 0x37, "ordinary storage is not writable");
        }
        if (bytes % 2)
        {
            ::operator delete(memory, bytes);
        }
        else
        {
            ::operator delete(memory);
        }
    }
    ::operator delete(nullptr);
    ::operator delete(nullptr, std::size_t{0});
    require(ordinary_allocations == allocations_before + 4 &&
                ordinary_deallocations == deallocations_before + 4 &&
                allocation_events == pool_before && heap_count == 0,
        "unscoped scalar allocation or null deallocation was captured");
}
} // namespace

extern "C" LONG InterlockedExchange(LONG* target, LONG value)
{
    volatile LONG* word = target;
    require(target == &pool_lock_word && value == 1, "actual acquisition object/value differs");
    require(*word == (exchanges ? 1 : 0), "shared word event state differs");
    *word = exchanges < failures ? 1 : 0;
    const LONG before = *word;
    *word = value;
    ++exchanges;
    ++total_exchanges;
    return before;
}

extern "C" void Sleep(DWORD milliseconds)
{
    require(sleeps < wait_sequence.size() && milliseconds == wait_sequence[sleeps],
        "literal fallback wait differs");
    require(pool_lock_word == 1, "wait provider received a different shared state");
    ++sleeps;
    ++total_sleeps;
}

int main()
{
    static_assert(sizeof(void*) == 8 && sizeof(unsigned int) == 4 && sizeof(LONG) == 4);
    static_assert(sizeof(CircularListValueNode) == 24 && alignof(CircularListValueNode) == 8);
    static_assert(circular_list_node_slot_bytes == slot_bytes);
    static_assert(std::is_trivially_default_constructible_v<CircularListValueNode>);
    static_assert(std::is_trivially_destructible_v<CircularListValueNode>);
    static_assert(std::is_trivially_default_constructible_v<PoolChunkLink>);
    static_assert(std::is_trivially_destructible_v<PoolChunkLink>);
    static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ <= alignof(std::max_align_t));
    require(pool_lock_word == 0, "shared word did not start zero");
    check_unscoped_allocation();
    for (unsigned mode : {0u, 1u, 2u})
    {
        for (unsigned schedule : {0u, 2u, 17u})
        {
            run_sequence(mode, schedule);
        }
    }
    run_allocation_failure(0);
    run_allocation_failure(2);
    run_single_remaining_slot();
    check_unscoped_allocation();
    require(allocation_releases + exception_events == allocation_events,
        "successful pool backing allocations were not all released");
    std::printf("cases=%u checks=%llu insertions=%llu returns=%llu allocations=%llu "
                "exceptions=%llu exchanges=%llu sleeps=%llu heap-releases=%llu scopes=%llu "
                "native-screen-list=pass\n",
        cases,
        checks,
        insertions,
        returns,
        allocation_events,
        exception_events,
        total_exchanges,
        total_sleeps,
        allocation_releases,
        operation_scopes);
}
