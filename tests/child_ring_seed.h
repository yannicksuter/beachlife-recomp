#ifndef BEACHLIFE_TEST_CHILD_RING_SEED_H
#define BEACHLIFE_TEST_CHILD_RING_SEED_H

#include "pool_chunk_acquire.h"
#include "pool_lock_acquire.h"
#include "pool_lock_guard.h"
#include "startup_screen_children.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <new>
#include <type_traits>

namespace child_ring_fixture
{
// Generated backing ownership only. Actual ring/node constructors and
// ReturnPool change the object lifetimes within these separately guarded slots.
template <unsigned Count> class Seed
{
    static constexpr unsigned slot_bytes = screen_child_node_slot_bytes;
    static constexpr unsigned bucket = (slot_bytes - 1u) / 8u;
    struct Slot
    {
        unsigned char before[16];
        alignas(ScreenChildNode) unsigned char bytes[slot_bytes];
        unsigned char after[16];
    };
    std::array<Slot, Count> slots{};
    void (*check)(bool, const char*);
    unsigned saved_spin = pool_spin_limit;
    unsigned saved_last = pool_last_spin;
    unsigned long long guard_bytes = 0;

  public:
    explicit Seed(void (*require)(bool, const char*))
        : check(require)
    {
        static_assert(Count != 0);
        static_assert(sizeof(void*) == 8 && sizeof(ScreenChildNode) == 24);
        static_assert(slot_bytes == 24 && bucket == 2);
        static_assert(alignof(ScreenChildNode) <= 8 && alignof(PoolChunkLink) <= 8);
        static_assert(std::is_trivially_destructible_v<ScreenChildNode>);
        static_assert(std::is_trivially_destructible_v<PoolChunkLink>);
        check_common();
        for (auto* root : pool_chunk_buckets)
        {
            check(root == nullptr, "child seed found an existing pool root");
        }
        for (auto& slot : slots)
        {
            std::memset(slot.before, 0xa6, sizeof(slot.before));
            std::memset(slot.bytes, 0x6b, sizeof(slot.bytes));
            std::memset(slot.after, 0xa6, sizeof(slot.after));
        }
        PoolChunkLink* next = nullptr;
        for (unsigned n = Count; n != 0; --n)
        {
            next = ::new (static_cast<void*>(slots[n - 1].bytes)) PoolChunkLink{next};
        }
        pool_chunk_buckets[bucket] = next;
    }
    ~Seed()
    {
        // All ring owners precede this backing's end. On a controlled failure,
        // cleanup does not depend on the changed free-list topology.
        pool_chunk_buckets[bucket] = nullptr;
    }
    Seed(const Seed&) = delete;
    Seed& operator=(const Seed&) = delete;

    std::uintptr_t address(unsigned id) const
    {
        check(id < Count, "child slot identity is outside the backing owner");
        return reinterpret_cast<std::uintptr_t>(slots[id].bytes);
    }
    unsigned long long checked_bytes() const
    {
        return guard_bytes;
    }
    void check_common() const
    {
        check(pool_lock_word == 0 && pool_chunk_begin == nullptr && pool_chunk_end == nullptr &&
                  pool_heap_bytes == 0,
            "child operation changed heap span/accounting or retained its lock");
        check(pool_spin_limit == saved_spin && pool_last_spin == saved_last,
            "uncontended child operation changed spin state");
        for (unsigned i = 0; i < 16; ++i)
        {
            if (i != bucket)
            {
                check(pool_chunk_buckets[i] == nullptr, "child operation changed another bucket");
            }
        }
    }
    void check_guards()
    {
        for (const auto& slot : slots)
        {
            for (unsigned i = 0; i < sizeof(slot.before); ++i)
            {
                guard_bytes += 2;
                check(slot.before[i] == 0xa6 && slot.after[i] == 0xa6, "child slot guard changed");
            }
        }
    }
    void check_free(std::initializer_list<unsigned> expected)
    {
        PoolChunkLink* current = pool_chunk_buckets[bucket];
        for (unsigned id : expected)
        {
            check(reinterpret_cast<std::uintptr_t>(current) == address(id),
                "literal child free-slot order differs");
            // The actual pool root reaches live PoolChunkLink objects, not the
            // ScreenChildNode objects whose lifetimes ended at ReturnPool.
            current = current->next;
        }
        check(current == nullptr, "child free-list has an unexpected tail");
        check_common();
        check_guards();
    }
    void check_initial_order()
    {
        PoolChunkLink* current = pool_chunk_buckets[bucket];
        for (unsigned id = 0; id < Count; ++id)
        {
            check(reinterpret_cast<std::uintptr_t>(current) == address(id),
                "generated Dialog sentinel return order differs");
            current = current->next;
        }
        check(current == nullptr, "generated Dialog sentinel free tail differs");
        check_common();
        check_guards();
    }
};
} // namespace child_ring_fixture

#endif
