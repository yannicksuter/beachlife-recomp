#include "pool_allocate.h"
#include "pool_chunk_acquire.h"
#include "pool_lock_acquire.h"
#include "pool_lock_guard.h"
#include "startup_screen_list_box.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <thread>
#include <type_traits>

namespace
{
const char* current_case = "startup";
unsigned groups = 0;
unsigned ranges = 0;
unsigned pool_allocations = 0;
unsigned pool_returns = 0;
unsigned scalar_releases = 0;
unsigned exchanges = 0;
unsigned sleeps = 0;
unsigned long long checks = 0;
unsigned long long byte_checks = 0;

struct CheckFailure
{
    const char* reason;
};

void require(bool value, const char* reason)
{
    ++checks;
    if (!value)
    {
        throw CheckFailure{reason};
    }
}

struct DeleteWatch
{
    std::uintptr_t address = 0;
    unsigned releases = 0;
} delete_watch;

void check_bytes(const unsigned char* actual, const unsigned char* expected, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        ++byte_checks;
        require(actual[i] == expected[i], "retained byte representation changed");
    }
}

template <std::size_t Bytes> struct GuardedBytes
{
    unsigned char before[16];
    alignas(std::max_align_t) unsigned char bytes[Bytes];
    unsigned char after[16];

    GuardedBytes()
    {
        std::memset(before, 0x39, sizeof(before));
        std::memset(after, 0xc7, sizeof(after));
    }
    void check_guards() const
    {
        for (unsigned i = 0; i < sizeof(before); ++i)
        {
            byte_checks += 2;
            require(before[i] == 0x39 && after[i] == 0xc7, "outside storage guard changed");
        }
    }
};

template <class Range> struct RangeOwner
{
    GuardedBytes<sizeof(Range)> backing;
    unsigned char saved[sizeof(Range)];
    Range* live = nullptr;

    template <class Pointer> RangeOwner(Pointer begin, Pointer end, Pointer capacity)
    {
        static_assert(std::is_standard_layout_v<Range>);
        static_assert(offsetof(Range, begin) == 0);
        static_assert(offsetof(Range, end) == sizeof(Pointer));
        static_assert(offsetof(Range, capacity) == 2 * sizeof(Pointer));
        static_assert(sizeof(Range) == 3 * sizeof(Pointer));
        static_assert(alignof(Range) <= alignof(std::max_align_t));
        live = ::new (static_cast<void*>(backing.bytes)) Range{begin, end, capacity};
        std::memcpy(saved, backing.bytes, sizeof(saved));
        ++ranges;
    }
    ~RangeOwner()
    {
        if (live != nullptr)
        {
            live->~Range();
        }
    }
    RangeOwner(const RangeOwner&) = delete;
    RangeOwner& operator=(const RangeOwner&) = delete;

    void release()
    {
        Range* receiver = live;
        live = nullptr;
        receiver->~Range();
        // The Range lifetime has ended. Only its still-live byte backing is read.
        check_bytes(backing.bytes, saved, sizeof(saved));
        backing.check_guards();
    }
};

struct PoolScope
{
    unsigned saved_spin = pool_spin_limit;
    unsigned saved_last = pool_last_spin;

    PoolScope()
    {
        check_common(16);
    }
    ~PoolScope()
    {
        // Backing owners outlive this scope. A failing control never drives
        // cleanup through a changed free-list topology or a dead array object.
        for (auto& head : pool_chunk_buckets)
        {
            head = nullptr;
        }
        pool_chunk_begin = nullptr;
        pool_chunk_end = nullptr;
        pool_heap_bytes = 0;
        pool_lock_word = 0;
        pool_spin_limit = saved_spin;
        pool_last_spin = saved_last;
    }
    PoolScope(const PoolScope&) = delete;
    PoolScope& operator=(const PoolScope&) = delete;

    void check_common(unsigned used_bucket) const
    {
        require(pool_chunk_begin == nullptr && pool_chunk_end == nullptr && pool_heap_bytes == 0,
            "storage operation changed pool span or accounting");
        require(
            pool_lock_word == 0 && pool_spin_limit == saved_spin && pool_last_spin == saved_last,
            "uncontended storage operation changed lock or spin state");
        for (unsigned i = 0; i < 16; ++i)
        {
            if (i != used_bucket)
            {
                require(pool_chunk_buckets[i] == nullptr, "unrelated pool head changed");
            }
        }
        require(sleeps == 0, "seeded operation unexpectedly waited");
    }
};

template <class Element, std::size_t Count> Element* make_array(void* storage)
{
    Element* array = ::new (storage) Element[Count]{};
    require(static_cast<void*>(array) == storage, "nonallocating array placement added an offset");
    if constexpr (std::is_same_v<Element, unsigned int>)
    {
        for (std::size_t i = 0; i < Count; ++i)
        {
            array[i] = 0x71000000u + static_cast<unsigned int>(i);
        }
    }
    return array;
}

template <class Range, class Element> void null_range()
{
    Element unrelated[3]{};
    unsigned char saved[sizeof(unrelated)];
    std::memcpy(saved, unrelated, sizeof(saved));
    PoolScope pool;
    RangeOwner<Range> range(static_cast<Element*>(nullptr), unrelated + 1, unrelated + 3);
    range.release();
    check_bytes(reinterpret_cast<const unsigned char*>(unrelated), saved, sizeof(saved));
    pool.check_common(16);
}

template <class Range, class Element, std::size_t Bytes> void pooled_range()
{
    static_assert(Bytes == 16 || Bytes == 128);
    static_assert(Bytes % sizeof(Element) == 0 && Bytes % 8 == 0);
    static_assert(Bytes >= sizeof(PoolChunkLink));
    constexpr std::size_t count = Bytes / sizeof(Element);
    constexpr unsigned bucket = (Bytes - 1) / 8;
    GuardedBytes<Bytes> first;
    GuardedBytes<Bytes> second;
    std::memset(second.bytes, 0x6b, Bytes);
    PoolScope pool;
    auto* second_link = ::new (static_cast<void*>(second.bytes)) PoolChunkLink{nullptr};
    auto* first_link = ::new (static_cast<void*>(first.bytes)) PoolChunkLink{second_link};
    pool_chunk_buckets[bucket] = first_link;
    unsigned char saved_second[Bytes];
    std::memcpy(saved_second, second.bytes, Bytes);

    for (unsigned pass = 0; pass < 2; ++pass)
    {
        void* storage = AllocatePoolBytes(Bytes);
        ++pool_allocations;
        require(storage == first.bytes && pool_chunk_buckets[bucket] == second_link,
            "real allocator did not return the first seeded/reused slot");
        // The allocator returned a live trivial free link. End it before
        // constructing the actual pointer/word array in the owned slot.
        static_cast<PoolChunkLink*>(storage)->~PoolChunkLink();
        Element* array = make_array<Element, count>(storage);
        unsigned char saved[Bytes];
        std::memcpy(saved, first.bytes, Bytes);
        RangeOwner<Range> range(array, array + (pass == 0 ? 1 : 0), array + count);
        range.release();

        // The sole control first fails here: omit ReturnPool in the entry
        // destructor. The backing remains owned and cleanup never traverses it.
        require(pool_chunk_buckets[bucket] == static_cast<void*>(first.bytes),
            "entry/selection storage was not published to its byte-sized pool bucket");
        PoolChunkLink* returned = pool_chunk_buckets[bucket];
        require(returned->next == second_link && second_link->next == nullptr,
            "returned storage link did not preserve the previous head");
        ++pool_returns;
        // ReturnPool began PoolChunkLink lifetime; do not access the old array.
        check_bytes(first.bytes + sizeof(PoolChunkLink),
            saved + sizeof(PoolChunkLink),
            Bytes - sizeof(PoolChunkLink));
        check_bytes(second.bytes, saved_second, Bytes);
        first.check_guards();
        second.check_guards();
        pool.check_common(bucket);
    }
}

struct ScalarBacking
{
    void* storage;

    explicit ScalarBacking(std::size_t bytes)
        : storage(::operator new(bytes))
    {
        delete_watch = {reinterpret_cast<std::uintptr_t>(storage), 0};
    }
    ~ScalarBacking()
    {
        if (delete_watch.releases == 0)
        {
            ::operator delete(storage);
        }
        delete_watch = {};
    }
    ScalarBacking(const ScalarBacking&) = delete;
    ScalarBacking& operator=(const ScalarBacking&) = delete;
};

template <class Range, class Element> void large_range()
{
    constexpr std::size_t count = 128 / sizeof(Element) + 1;
    constexpr std::size_t bytes = count * sizeof(Element);
    static_assert(bytes > 128);
    PoolScope pool;
    ScalarBacking backing(bytes);
    Element* array = make_array<Element, count>(backing.storage);
    RangeOwner<Range> range(array, array + 1, array + count);
    range.release();
    require(delete_watch.releases == 1, "large backing did not reach real scalar delete once");
    ++scalar_releases;
    pool.check_common(16);
    // No read of the deallocated array/backing or its former pointer values.
}
} // namespace

// Transparent Linux linker observation of the genuine host scalar boundary.
// Every call, including the watched one, is forwarded without changing storage.
extern "C" void __real__ZdlPv(void* memory) noexcept;
extern "C" void __wrap__ZdlPv(void* memory) noexcept
{
    if (delete_watch.address != 0 &&
        reinterpret_cast<std::uintptr_t>(memory) == delete_watch.address)
    {
        ++delete_watch.releases;
    }
    __real__ZdlPv(memory);
}

// Actual host implementations of the existing 32-bit platform boundary. No
// retail allocator or game method is replaced; seeded operations never sleep.
extern "C" LONG InterlockedExchange(LONG* target, LONG value)
{
    ++exchanges;
    return std::atomic_ref<LONG>(*target).exchange(value, std::memory_order_seq_cst);
}

extern "C" void Sleep(DWORD milliseconds)
{
    ++sleeps;
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

int main()
{
    static_assert(sizeof(ScreenListBoxEntry*) == 8 && sizeof(unsigned int) == 4);
    static_assert(sizeof(PoolChunkLink) == 8);
    try
    {
        current_case = "null-begin-retained-pointers";
        null_range<ScreenListBoxEntryRange, ScreenListBoxEntry*>();
        null_range<ScreenListBoxSelectionRange, unsigned int>();
        ++groups;

        current_case = "pooled-entry-16";
        pooled_range<ScreenListBoxEntryRange, ScreenListBoxEntry*, 16>();
        current_case = "pooled-entry-128";
        pooled_range<ScreenListBoxEntryRange, ScreenListBoxEntry*, 128>();
        current_case = "pooled-selection-16";
        pooled_range<ScreenListBoxSelectionRange, unsigned int, 16>();
        current_case = "pooled-selection-128";
        pooled_range<ScreenListBoxSelectionRange, unsigned int, 128>();
        ++groups;

        current_case = "large-entry-136";
        large_range<ScreenListBoxEntryRange, ScreenListBoxEntry*>();
        current_case = "large-selection-132";
        large_range<ScreenListBoxSelectionRange, unsigned int>();
        ++groups;
        require(ranges == 12 && pool_allocations == 8 && pool_returns == 8 && scalar_releases == 2,
            "storage lifetime protocol differs");
        require(exchanges == 16 && sleeps == 0, "uncontended platform protocol differs");
        std::printf("ListBox storage: %u groups, %u ranges, %u pool allocations, %u returns, "
                    "%u scalar releases, %u exchanges, %llu checks, %llu byte checks; "
                    "range sizes %zu/%zu, element sizes %zu/%zu\n",
            groups,
            ranges,
            pool_allocations,
            pool_returns,
            scalar_releases,
            exchanges,
            checks,
            byte_checks,
            sizeof(ScreenListBoxEntryRange),
            sizeof(ScreenListBoxSelectionRange),
            sizeof(ScreenListBoxEntry*),
            sizeof(unsigned int));
        return 0;
    }
    catch (const CheckFailure& failure)
    {
        std::fprintf(stderr, "%s: %s\n", current_case, failure.reason);
        return 2;
    }
}
