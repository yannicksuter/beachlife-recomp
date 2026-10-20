#include "pool_chunk_acquire.h"
#include "pool_lock_acquire.h"
#include "pool_lock_guard.h"
#include "startup_list.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <memory>
#include <new>
#include <type_traits>

namespace
{
constexpr unsigned slot_bytes = circular_list_node_slot_bytes;
constexpr unsigned node_bucket = (slot_bytes - 1u) / 8u;
constexpr unsigned slot_count = 4;
constexpr unsigned char pattern = 0xa6;
const char* current_case = "startup";
unsigned sessions = 0;
unsigned insert_calls = 0;
unsigned erase_calls = 0;
unsigned clear_calls = 0;
unsigned payload_allocations = 0;
unsigned payload_releases = 0;
unsigned early_payload_releases = 0;
unsigned exchanges = 0;
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

enum class PoolOperation
{
    none,
    create,
    erase,
    clear,
    cleanup
};
PoolOperation pool_operation = PoolOperation::none;

class PoolScope
{
  public:
    explicit PoolScope(PoolOperation operation)
    {
        require(pool_operation == PoolOperation::none, "nested pool operation");
        pool_operation = operation;
    }
    ~PoolScope()
    {
        pool_operation = PoolOperation::none;
    }
    PoolScope(const PoolScope&) = delete;
    PoolScope& operator=(const PoolScope&) = delete;
};

template <typename T> struct Guarded
{
    std::array<unsigned char, 16> before{};
    T value{};
    std::array<unsigned char, 16> after{};

    Guarded()
    {
        before.fill(pattern);
        after.fill(pattern);
    }
    void check() const
    {
        for (unsigned i = 0; i < before.size(); ++i)
        {
            byte_checks += 2;
            require(before[i] == pattern && after[i] == pattern, "typed owner guard changed");
        }
    }
};

struct Slot
{
    std::array<unsigned char, 16> before{};
    alignas(CircularListValueNode) unsigned char bytes[slot_bytes];
    std::array<unsigned char, 16> after{};

    Slot()
    {
        before.fill(pattern);
        after.fill(pattern);
    }
    void check() const
    {
        for (unsigned i = 0; i < before.size(); ++i)
        {
            byte_checks += 2;
            require(before[i] == pattern && after[i] == pattern, "pool backing guard changed");
        }
    }
};

class Session
{
    std::array<Slot, slot_count> slots;
    std::array<std::unique_ptr<Guarded<ActiveScreenEntry>>, 3> payloads;
    std::array<CircularListValueNode*, 3> live_nodes{};
    Guarded<PoolChunkLink> unrelated;
    unsigned inserted = 0;
    bool cleared = false;

  public:
    Guarded<CircularListNode> sentinel;
    Guarded<CircularList> list;

    Session()
    {
        require(pool_chunk_begin == nullptr && pool_chunk_end == nullptr && pool_heap_bytes == 0 &&
                    pool_lock_word == 0,
            "prior pool storage remains");
        for (auto* bucket : pool_chunk_buckets)
        {
            require(bucket == nullptr, "prior pool bucket remains");
        }
        require(pool_spin_limit == 30 && pool_last_spin == 0, "prior spin state differs");
        sentinel.value = {&sentinel.value, &sentinel.value};
        list.value.head = &sentinel.value;
        unrelated.value.next = &unrelated.value;
        // Existing backing objects outlive every actual node/link lifetime.
        PoolChunkLink* following = nullptr;
        for (unsigned n = slot_count; n != 0; --n)
        {
            following = ::new (static_cast<void*>(slots[n - 1].bytes)) PoolChunkLink{following};
        }
        pool_chunk_buckets[node_bucket] = following;
        pool_chunk_buckets[15] = &unrelated.value;
        ++sessions;
    }

    ~Session()
    {
        // The only changed-source control preserves the ring. Exceptions from
        // checks occur after game calls return; real Clear reclaims live nodes.
        if (!cleared)
        {
            pool_operation = PoolOperation::cleanup;
            list.value.Clear();
            pool_operation = PoolOperation::none;
            ++clear_calls;
        }
        live_nodes.fill(nullptr);
        // Detach every global before the caller-owned backing ends lifetime.
        for (auto& bucket : pool_chunk_buckets)
        {
            bucket = nullptr;
        }
        pool_chunk_begin = nullptr;
        pool_chunk_end = nullptr;
        pool_heap_bytes = 0;
        for (auto& entry : payloads)
        {
            if (entry)
            {
                entry.reset();
                ++payload_releases;
            }
        }
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void insert(unsigned index)
    {
        require(index == inserted && index < payloads.size(), "invalid literal insertion order");
        payloads[index] = std::make_unique<Guarded<ActiveScreenEntry>>();
        ++payload_allocations;
        payloads[index]->value = {
            nullptr, -17 + static_cast<int>(index), 2300 + static_cast<int>(index), nullptr};
        ActiveScreenEntry* entry = &payloads[index]->value;
        CircularListIterator result;
        {
            PoolScope operation(PoolOperation::create);
            result = list.value.Insert(list.value.End(), entry);
        }
        live_nodes[index] = static_cast<CircularListValueNode*>(result.node);
        ++inserted;
        ++insert_calls;
        require(static_cast<void*>(result.node) == static_cast<void*>(slots[index].bytes),
            "real insertion did not consume literal pool slot");
        require(live_nodes[index]->entry == entry, "real node payload differs");
    }

    CircularListNode* node(int id) const
    {
        if (id == -1)
        {
            return const_cast<CircularListNode*>(&sentinel.value);
        }
        require(id >= 0 && id < static_cast<int>(live_nodes.size()) && live_nodes[id] != nullptr,
            "literal node is not live");
        return live_nodes[id];
    }

    void erase(unsigned index, int successor)
    {
        require(
            index < inserted && live_nodes[index] != nullptr, "erase requires a live value node");
        CircularListNode* expected = node(successor);
        CircularListIterator position;
        position.node = live_nodes[index];
        CircularListIterator result;
        {
            PoolScope operation(PoolOperation::erase);
            result = list.value.Erase(position);
        }
        // ReturnPool now owns a new PoolChunkLink in this storage. Never read
        // position or the former value node after this point.
        live_nodes[index] = nullptr;
        ++erase_calls;
        require(result.node == expected, "returned successor differs");
    }

    void release_payload_before_erase(unsigned index)
    {
        require(index < inserted && live_nodes[index] != nullptr && payloads[index] != nullptr,
            "early release requires a live borrowed entry");
        payloads[index]->check();
        payloads[index].reset();
        ++payload_releases;
        ++early_payload_releases;
        // Its linked node now holds a dangling payload value. Neither the
        // fixture nor Erase reads that member or the ended entry afterwards.
    }

    void expect_ring(std::initializer_list<int> ids) const
    {
        CircularListNode* previous = const_cast<CircularListNode*>(&sentinel.value);
        CircularListNode* current = sentinel.value.next;
        for (int id : ids)
        {
            CircularListNode* expected = node(id);
            require(current == expected, "literal forward ring differs");
            require(expected->previous == previous, "literal backward ring differs");
            previous = expected;
            current = expected->next;
        }
        require(current == &sentinel.value && sentinel.value.previous == previous,
            "literal ring endpoint differs");
        require(list.value.head == &sentinel.value, "borrowed list head changed");
        require(list.value.Begin().node == sentinel.value.next &&
                    list.value.End().node == &sentinel.value,
            "actual iterator endpoints differ");
    }

    void expect_pool(std::initializer_list<unsigned> ids) const
    {
        PoolChunkLink* current = pool_chunk_buckets[node_bucket];
        for (unsigned id : ids)
        {
            require(id < slots.size() && static_cast<const void*>(current) ==
                                             static_cast<const void*>(slots[id].bytes),
                "literal returned-pool order differs");
            // This checked pointer designates a live replacement PoolChunkLink,
            // not an ended CircularListValueNode or a speculative next address.
            current = current->next;
        }
        require(current == nullptr, "literal free-chain endpoint differs");
        for (unsigned index = 0; index < 16; ++index)
        {
            if (index != node_bucket)
            {
                require(pool_chunk_buckets[index] == (index == 15 ? &unrelated.value : nullptr),
                    "unrelated pool bucket changed");
            }
        }
        require(unrelated.value.next == &unrelated.value && pool_chunk_begin == nullptr &&
                    pool_chunk_end == nullptr && pool_heap_bytes == 0 && pool_lock_word == 0 &&
                    pool_spin_limit == 30 && pool_last_spin == 0,
            "pool span, lock or unrelated link changed");
    }

    void check_owners() const
    {
        sentinel.check();
        list.check();
        unrelated.check();
        for (const auto& slot : slots)
        {
            slot.check();
        }
        for (unsigned index = 0; index < payloads.size(); ++index)
        {
            if (payloads[index])
            {
                payloads[index]->check();
                const ActiveScreenEntry& entry = payloads[index]->value;
                require(entry.item == nullptr && entry.context == nullptr &&
                            entry.surface == -17 + static_cast<int>(index) &&
                            entry.state == 2300 + static_cast<int>(index),
                    "borrowed entry fields changed");
            }
        }
    }

    void clear()
    {
        {
            PoolScope operation(PoolOperation::clear);
            list.value.Clear();
        }
        live_nodes.fill(nullptr);
        cleared = true;
        ++clear_calls;
    }
};

void middle()
{
    current_case = "middle-successor";
    Session session;
    session.insert(0);
    session.insert(1);
    session.insert(2);
    session.expect_ring({0, 1, 2});
    session.expect_pool({3});
    session.erase(1, 2);
    session.expect_ring({0, 2});
    session.expect_pool({1, 3});
    session.check_owners();
    session.clear();
    session.expect_ring({});
    session.expect_pool({2, 0, 1, 3});
    session.check_owners();
}

void first_and_last()
{
    current_case = "first-and-last";
    Session session;
    session.insert(0);
    session.insert(1);
    session.insert(2);
    session.erase(0, 1);
    session.expect_ring({1, 2});
    session.expect_pool({0, 3});
    session.erase(2, -1);
    session.expect_ring({1});
    session.expect_pool({2, 0, 3});
    session.check_owners();
    session.clear();
    session.expect_ring({});
    session.expect_pool({1, 2, 0, 3});
    session.check_owners();
}

void sole_after_entry_lifetime()
{
    current_case = "sole-after-entry-lifetime";
    Session session;
    session.insert(0);
    session.expect_ring({0});
    session.release_payload_before_erase(0);
    session.erase(0, -1);
    session.expect_ring({});
    session.expect_pool({0, 1, 2, 3});
    session.clear();
    session.expect_ring({});
    session.expect_pool({0, 1, 2, 3});
    session.check_owners();
}
} // namespace

// Exact qualified R162 test-only OS boundary; no game or allocator provider.
extern "C" LONG InterlockedExchange(LONG* target, LONG value)
{
    volatile LONG* const word = target;
    require(pool_operation != PoolOperation::none && target == &pool_lock_word && value == 1 &&
                *word == 0,
        "unexpected uncontended exchange");
    const LONG previous = *word;
    *word = value;
    ++exchanges;
    return previous;
}
extern "C" void Sleep(DWORD)
{
    require(false, "unexpected wait in finite uncontended fixture");
}

int main()
{
    static_assert(sizeof(void*) == 8 && sizeof(int) == 4 && sizeof(LONG) == 4);
    static_assert(sizeof(CircularListValueNode) == 24 && slot_bytes == 24 && node_bucket == 2);
    static_assert((sizeof(CircularListValueNode) - 1u) / 8u == node_bucket);
    static_assert(alignof(CircularListValueNode) <= 8 && alignof(PoolChunkLink) <= 8);
    static_assert(std::is_trivially_destructible_v<CircularListValueNode>);
    static_assert(std::is_trivially_destructible_v<PoolChunkLink>);
    static_assert(std::is_trivially_destructible_v<ActiveScreenEntry>);
    try
    {
        middle();
        first_and_last();
        sole_after_entry_lifetime();
        require(sessions == 3 && insert_calls == 7 && erase_calls == 4 && clear_calls == 3,
            "literal operation totals differ");
        require(payload_allocations == 7 && payload_releases == 7 && early_payload_releases == 1 &&
                    exchanges == 14,
            "literal lifetime totals differ");
        for (auto* bucket : pool_chunk_buckets)
        {
            require(bucket == nullptr, "final pool root remains");
        }
        require(pool_chunk_begin == nullptr && pool_chunk_end == nullptr && pool_heap_bytes == 0 &&
                    pool_lock_word == 0,
            "final pool storage remains");
        std::printf(
            "groups=3 insert-calls=%u erase-calls=%u clear-calls=%u payloads=%u "
            "payload-releases=%u early-releases=%u exchanges=%u checks=%llu byte-checks=%llu "
            "native-active-node-erase=pass\n",
            insert_calls,
            erase_calls,
            clear_calls,
            payload_allocations,
            payload_releases,
            early_payload_releases,
            exchanges,
            checks,
            byte_checks);
        return 0;
    }
    catch (const CheckFailure& failure)
    {
        std::fprintf(stderr, "active-node-erase %s: %s\n", current_case, failure.reason);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "active-node-erase host failure: %s\n", failure.what());
        return 3;
    }
}
