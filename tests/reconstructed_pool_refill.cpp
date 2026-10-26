#include "pool_refill.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>

namespace
{
const char* current_case = "startup";
unsigned sessions = 0;
unsigned calls = 0;
unsigned free_links = 0;
unsigned fresh_backings = 0;
unsigned fresh_releases = 0;
unsigned caller_slots = 0;
unsigned long long checks = 0;
unsigned long long byte_checks = 0;

struct CheckFailure
{
    const char* reason;
};

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        throw CheckFailure{reason};
    }
}

// Literal offsets, independent of Refill's round-up and chain-building loops.
constexpr std::array<std::size_t, 19> full_chain{
    16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240, 256, 272, 288, 304};
constexpr std::array<std::size_t, 2> reduced_chain{24, 48};
constexpr std::array<std::size_t, 0> no_chain{};

template <std::size_t Bytes> struct GuardedSpan
{
    unsigned char before[16];
    alignas(8) unsigned char bytes[Bytes];
    unsigned char after[16];

    GuardedSpan()
    {
        static_assert(Bytes % 8 == 0);
        std::memset(before, 0x39, sizeof(before));
        std::memset(bytes, 0xa5, sizeof(bytes));
        std::memset(after, 0xc7, sizeof(after));
    }
};

struct PoolScope
{
    PoolChunkLink untouched{nullptr};
    void* owned_backing = nullptr;
    bool seeded = false;

    PoolScope()
    {
        untouched.next = &untouched;
        require(pool_chunk_begin == nullptr && pool_chunk_end == nullptr && pool_heap_bytes == 0,
            "session inherited a pool span");
        for (auto* head : pool_chunk_buckets)
        {
            require(head == nullptr, "session inherited a free-list head");
        }
        ++sessions;
    }
    ~PoolScope()
    {
        // Cleanup never traverses possibly changed control topology. All global
        // references end before either the stack span or scalar backing owner.
        for (auto& head : pool_chunk_buckets)
        {
            head = nullptr;
        }
        pool_chunk_begin = nullptr;
        pool_chunk_end = nullptr;
        pool_heap_bytes = 0;
        if (owned_backing != nullptr)
        {
            ::operator delete(owned_backing);
            ++fresh_releases;
        }
    }
    PoolScope(const PoolScope&) = delete;
    PoolScope& operator=(const PoolScope&) = delete;

    void seed(unsigned char* begin, unsigned char* end)
    {
        pool_chunk_begin = begin;
        pool_chunk_end = end;
        pool_heap_bytes = 0x230;
        pool_chunk_buckets[15] = &untouched;
        seeded = true;
    }
    void check_other_buckets(unsigned target)
    {
        for (unsigned index = 0; index < 16; ++index)
        {
            if (index != target)
            {
                PoolChunkLink* expected = seeded && index == 15 ? &untouched : nullptr;
                require(pool_chunk_buckets[index] == expected, "unrelated bucket changed");
            }
        }
        require(untouched.next == &untouched, "unrelated live link changed");
    }
};

template <std::size_t Count>
void check_chain(
    unsigned char* backing, unsigned bucket, const std::array<std::size_t, Count>& offsets)
{
    PoolChunkLink* current = pool_chunk_buckets[bucket];
    for (std::size_t offset : offsets)
    {
        // Bound every read by its literal address before accessing the real
        // PoolChunkLink whose lifetime the reconstructed method began.
        require(static_cast<void*>(current) == static_cast<void*>(backing + offset),
            "literal free-link identity or order differs");
        current = current->next;
        ++free_links;
    }
    require(current == nullptr, "literal free-link tail is not null");
}

template <std::size_t Bytes, std::size_t Count>
void check_span(const GuardedSpan<Bytes>& span, const std::array<std::size_t, Count>& offsets)
{
    for (std::size_t index = 0; index < sizeof(span.before); ++index)
    {
        byte_checks += 2;
        require(
            span.before[index] == 0x39 && span.after[index] == 0xc7, "pool backing guard changed");
    }
    for (std::size_t index = 0; index < Bytes; ++index)
    {
        bool link_representation = false;
        for (std::size_t offset : offsets)
        {
            link_representation |= index >= offset && index < offset + sizeof(PoolChunkLink);
        }
        if (!link_representation)
        {
            ++byte_checks;
            require(span.bytes[index] == 0xa5, "raw slot, slot padding or unused tail changed");
        }
    }
}

void full_rounded_chain()
{
    current_case = "full-rounded-chain";
    GuardedSpan<320> span;
    PoolScope pool;
    pool.seed(span.bytes, span.bytes + 320);
    void* result = RefillPool(9);
    ++calls;

    // The sole 20->19 source control first fails here, while still in bounds.
    require(pool_chunk_begin == span.bytes + 320, "full rounded pool cursor differs");
    require(result == span.bytes, "full chain did not return raw slot zero");
    require(pool_chunk_end == span.bytes + 320 && pool_heap_bytes == 0x230,
        "full chain changed span end or heap accounting");
    check_chain(span.bytes, 1, full_chain);
    pool.check_other_buckets(1);
    check_span(span, full_chain);
}

void single_slot()
{
    current_case = "single-slot";
    GuardedSpan<24> span;
    PoolScope pool;
    pool.seed(span.bytes, span.bytes + 24);
    void* result = RefillPool(17);
    ++calls;

    require(result == span.bytes && pool_chunk_begin == span.bytes + 24,
        "single slot return or cursor differs");
    require(pool_chunk_end == span.bytes + 24 && pool_heap_bytes == 0x230,
        "single slot changed span end or heap accounting");
    check_chain(span.bytes, 2, no_chain);
    pool.check_other_buckets(2);
    check_span(span, no_chain);
}

void reduced_chain_case()
{
    current_case = "reduced-chain";
    GuardedSpan<80> span;
    PoolScope pool;
    pool.seed(span.bytes, span.bytes + 80);
    void* result = RefillPool(17);
    ++calls;

    require(result == span.bytes && pool_chunk_begin == span.bytes + 72,
        "reduced chain return or cursor differs");
    require(pool_chunk_end == span.bytes + 80 && pool_heap_bytes == 0x230,
        "reduced chain changed span end or heap accounting");
    check_chain(span.bytes, 2, reduced_chain);
    pool.check_other_buckets(2);
    check_span(span, reduced_chain);
}

void real_fresh_allocation()
{
    current_case = "fresh-allocation";
    PoolScope pool;
    void* result = RefillPool(9);
    ++calls;
    // The real child made this scalar allocation. There is no allocator
    // replacement or fabricated game provider in this executable.
    pool.owned_backing = result;
    require(result != nullptr, "fresh allocation returned null");
    ++fresh_backings;
    auto* bytes = static_cast<unsigned char*>(result);
    require(pool_chunk_begin == bytes + 320 && pool_chunk_end == bytes + 640,
        "fresh allocation span differs");
    require(pool_heap_bytes == 640, "fresh allocation accounting differs");
    check_chain(bytes, 1, full_chain);
    pool.check_other_buckets(1);

    // Slot zero remains raw caller storage. Demonstrate a real pointer-array
    // lifetime there without pretending it is a complete renderer or surface.
    using BorrowedPointer = const int*;
    static_assert(2 * sizeof(BorrowedPointer) <= 16 && alignof(BorrowedPointer) <= 8);
    const int first = 17;
    const int second = -9;
    BorrowedPointer* pointers = ::new (result) BorrowedPointer[2]{&first, &second};
    require(pointers[0] == &first && pointers[1] == &second, "caller pointer lifetime differs");
    require(*pointers[0] == 17 && *pointers[1] == -9, "caller borrowed values changed");
    std::destroy_n(pointers, 2);
    ++caller_slots;
}
} // namespace

int main()
{
    static_assert(sizeof(unsigned int) == 4);
    static_assert(sizeof(PoolChunkLink) <= 8 && alignof(PoolChunkLink) <= 8);
    static_assert(std::is_trivially_destructible_v<PoolChunkLink>);
    try
    {
        full_rounded_chain();
        single_slot();
        reduced_chain_case();
        real_fresh_allocation();
        current_case = "cleanup";
        require(pool_chunk_begin == nullptr && pool_chunk_end == nullptr && pool_heap_bytes == 0,
            "final cleanup retained pool storage");
        for (auto* head : pool_chunk_buckets)
        {
            require(head == nullptr, "final cleanup retained a free link");
        }
        require(sessions == 4 && calls == 4 && free_links == 40 && fresh_backings == 1 &&
                    fresh_releases == 1 && caller_slots == 1,
            "literal coverage totals differ");
        std::printf("pool-refill groups=3 sessions=%u calls=%u free-links=%u fresh-backings=%u "
                    "fresh-releases=%u caller-slots=%u checks=%llu byte-checks=%llu\n",
            sessions,
            calls,
            free_links,
            fresh_backings,
            fresh_releases,
            caller_slots,
            checks,
            byte_checks);
        return 0;
    }
    catch (const CheckFailure& failure)
    {
        std::fprintf(stderr, "%s: %s\n", current_case, failure.reason);
        return 1;
    }
    catch (const std::bad_alloc&)
    {
        std::fprintf(stderr,
            "%s: allocation failed outside the successful-allocation domain\n",
            current_case);
        return 2;
    }
}
