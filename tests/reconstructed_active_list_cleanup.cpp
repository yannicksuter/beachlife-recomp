#include "pool_chunk_acquire.h"
#include "pool_lock_acquire.h"
#include "pool_lock_guard.h"
#include "pool_return.h"
#include "startup_active_screen_visit.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <type_traits>

namespace
{
constexpr unsigned slot_bytes = circular_list_node_slot_bytes;
constexpr unsigned node_bucket = (slot_bytes - 1u) / 8u;
constexpr unsigned max_slots = 8;
constexpr int head_id = -1;
constexpr int other_head_id = -2;
constexpr int null_link_id = -3;
constexpr unsigned char pattern = 0xa6;
const char* current_case = "startup";
unsigned long long checks = 0;
unsigned long long byte_checks = 0;
unsigned visitor_calls = 0;
unsigned callback_attempts = 0;
unsigned clear_calls = 0;
unsigned node_creations = 0;
unsigned node_returns = 0;
unsigned payload_allocations = 0;
unsigned callback_payload_releases = 0;
unsigned payload_releases = 0;
unsigned exchanges = 0;

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

void equal_bytes(const void* actual, const void* expected, std::size_t size, const char* reason)
{
    const auto* a = static_cast<const unsigned char*>(actual);
    const auto* b = static_cast<const unsigned char*>(expected);
    for (std::size_t i = 0; i < size; ++i)
    {
        ++byte_checks;
        require(a[i] == b[i], reason);
    }
}

enum class PoolOperation
{
    none,
    create,
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
    unsigned char before[16]{};
    alignas(CircularListValueNode) unsigned char bytes[slot_bytes]{};
    unsigned char after[16]{};

    Slot()
    {
        std::memset(before, pattern, sizeof(before));
        std::memset(bytes, 0x6b, sizeof(bytes));
        std::memset(after, pattern, sizeof(after));
    }
    void check() const
    {
        for (unsigned i = 0; i < sizeof(before); ++i)
        {
            byte_checks += 2;
            require(before[i] == pattern && after[i] == pattern, "pool backing guard changed");
        }
    }
};

struct CallbackSignal
{
    const void* token;
};

enum class Mutation
{
    none,
    insert,
    skip,
    stop,
    payload,
    iterators,
    release_payload,
    throw_second
};

struct VisitorPlan
{
    const char* id;
    unsigned count;
    unsigned primary_count;
    std::array<int, 3> initial_payload;
    int first;
    int last;
    unsigned callback;
    Mutation mutation;
    unsigned expected_count;
    std::array<int, 3> expected_payload;
    std::array<int, 3> final_next;
    std::array<int, 3> final_previous;
    std::array<int, 3> final_payload;
};

// Literal endpoints, event sequences and final node states are independent of
// the visitor loop. -1 payload means null; -2 skips a freed payload value.
constexpr std::array<VisitorPlan, 15> visitor_plans{{
    {"E0-empty-nonnull-callback", 0, 0, {}, head_id, head_id, 1, Mutation::none, 0, {}, {}, {}, {}},
    {"E1-empty-null-callback", 0, 0, {}, head_id, head_id, 0, Mutation::none, 0, {}, {}, {}, {}},
    {"E2-equal-null-iterators",
        0,
        0,
        {},
        null_link_id,
        null_link_id,
        2,
        Mutation::none,
        0,
        {},
        {},
        {},
        {}},
    {"N0-single", 1, 1, {0}, 0, head_id, 1, Mutation::none, 1, {0}, {head_id}, {head_id}, {0}},
    {"N1-two-alternate-function",
        2,
        2,
        {0, 1},
        0,
        head_id,
        2,
        Mutation::none,
        2,
        {0, 1},
        {1, head_id},
        {head_id, 0},
        {0, 1}},
    {"N2-null-payload-forwarded",
        1,
        1,
        {-1},
        0,
        head_id,
        1,
        Mutation::none,
        1,
        {-1},
        {head_id},
        {head_id},
        {-1}},
    {"N3-repeated-payload",
        2,
        2,
        {0, 0},
        0,
        head_id,
        1,
        Mutation::none,
        2,
        {0, 0},
        {1, head_id},
        {head_id, 0},
        {0, 0}},
    {"R0-partial-range",
        3,
        3,
        {0, 1, 2},
        0,
        2,
        1,
        Mutation::none,
        2,
        {0, 1},
        {1, 2, head_id},
        {head_id, 0, 1},
        {0, 1, 2}},
    {"M0-insert-after-current",
        3,
        2,
        {0, 1, 3},
        0,
        head_id,
        1,
        Mutation::insert,
        3,
        {0, 3, 1},
        {2, head_id, 1},
        {head_id, 0, 2},
        {0, 1, 3}},
    {"M1-skip-next",
        3,
        3,
        {0, 1, 2},
        0,
        head_id,
        1,
        Mutation::skip,
        2,
        {0, 2},
        {2, 2, head_id},
        {head_id, 0, 1},
        {0, 1, 2}},
    {"M2-stop-through-current-link",
        2,
        2,
        {0, 1},
        0,
        head_id,
        1,
        Mutation::stop,
        1,
        {0},
        {head_id, head_id},
        {head_id, 0},
        {0, 1}},
    {"M3-later-payload-reload",
        2,
        2,
        {0, 1},
        0,
        head_id,
        1,
        Mutation::payload,
        2,
        {0, 3},
        {1, head_id},
        {head_id, 0},
        {0, 3}},
    {"M4-caller-iterator-copies",
        2,
        2,
        {0, 1},
        0,
        head_id,
        1,
        Mutation::iterators,
        2,
        {0, 1},
        {1, head_id},
        {head_id, 0},
        {0, 1}},
    {"O0-release-only-current-payload",
        2,
        2,
        {0, 1},
        0,
        head_id,
        1,
        Mutation::release_payload,
        2,
        {0, 1},
        {1, head_id},
        {head_id, 0},
        {-2, -2}},
    {"X0-callback-exception",
        2,
        2,
        {0, 1},
        0,
        head_id,
        1,
        Mutation::throw_second,
        2,
        {0, 1},
        {1, null_link_id},
        {head_id, 0},
        {0, 1}},
}};

struct ClearPlan
{
    const char* id;
    unsigned count;
    unsigned first_count;
    unsigned prior_count;
    std::array<int, 4> payload;
};
constexpr std::array<ClearPlan, 11> clear_plans{{
    {"C0-empty", 0, 0, 0, {}},
    {"C1-one", 1, 1, 0, {0}},
    {"C2-three-successors", 3, 3, 0, {0, 1, 2}},
    {"C3-existing-pool-tail", 3, 3, 2, {0, 1, 2}},
    {"C4-null-payloads", 2, 2, 1, {-1, -1}},
    {"C5-repeated-payload", 3, 3, 0, {0, 0, 0}},
    {"C6-previous-not-traversed", 2, 2, 0, {0, 1}},
    {"C7-repeat", 3, 3, 1, {0, 1, 2}},
    {"C8-two-sentinels", 4, 2, 1, {0, 1, 2, 3}},
    {"C9-alias-wrappers", 2, 2, 0, {0, 1}},
    {"C10-reuse-returned-head", 3, 3, 1, {0, 1, 2}},
}};

class Session
{
    std::array<std::unique_ptr<Guarded<ActiveScreenEntry>>, 4> payloads{};
    std::array<std::array<unsigned char, sizeof(ActiveScreenEntry)>, 4> payload_before{};
    std::array<Slot, max_slots> slots{};
    std::array<CircularListValueNode*, max_slots> live_nodes{};
    std::array<bool, max_slots> value_live{};
    std::array<std::uintptr_t, max_slots> slot_addresses{};
    Guarded<PoolChunkLink> unrelated_small;
    Guarded<PoolChunkLink> unrelated_large;
    unsigned seed_count = 0;
    unsigned constructed = 0;

  public:
    Guarded<CircularListNode> sentinel;
    Guarded<CircularListNode> other_sentinel;
    Guarded<CircularList> list;
    Guarded<CircularList> other_list;
    CircularListIterator caller_first;
    CircularListIterator caller_last;
    Mutation mutation = Mutation::none;
    unsigned expected_function = 0;
    std::array<int, 3> observed_payload{};
    std::array<unsigned, 3> observed_function{};
    unsigned observed_count = 0;
    int exception_token = 0;

    explicit Session(unsigned available)
        : seed_count(available)
    {
        require(available <= max_slots, "too many seed slots");
        require(pool_chunk_begin == nullptr && pool_chunk_end == nullptr && pool_heap_bytes == 0 &&
                    pool_lock_word == 0,
            "prior pool storage remains");
        for (auto* bucket : pool_chunk_buckets)
        {
            require(bucket == nullptr, "prior pool bucket remains");
        }
        pool_spin_limit = 30;
        pool_last_spin = 0;
        sentinel.value = {&sentinel.value, &sentinel.value};
        other_sentinel.value = {&other_sentinel.value, &other_sentinel.value};
        list.value.head = &sentinel.value;
        other_list.value.head = &other_sentinel.value;
        caller_first.node = &sentinel.value;
        caller_last.node = &sentinel.value;
        for (unsigned n = 0; n < payloads.size(); ++n)
        {
            payloads[n] = std::make_unique<Guarded<ActiveScreenEntry>>();
            ++payload_allocations;
            payloads[n]->value = {
                nullptr, -17 + static_cast<int>(n), 2300 + static_cast<int>(n), nullptr};
            std::memcpy(payload_before[n].data(), &payloads[n]->value, sizeof(ActiveScreenEntry));
        }
        for (unsigned n = 0; n < slots.size(); ++n)
        {
            slot_addresses[n] = reinterpret_cast<std::uintptr_t>(slots[n].bytes);
            require(
                slot_addresses[n] % alignof(CircularListValueNode) == 0, "slot alignment differs");
        }
        PoolChunkLink* following = nullptr;
        for (unsigned n = available; n != 0; --n)
        {
            following = ::new (static_cast<void*>(slots[n - 1].bytes)) PoolChunkLink{following};
        }
        pool_chunk_buckets[node_bucket] = following;
        unrelated_small.value.next = nullptr;
        unrelated_large.value.next = nullptr;
        pool_chunk_buckets[0] = &unrelated_small.value;
        pool_chunk_buckets[15] = &unrelated_large.value;
        check_owners();
    }

    ~Session()
    {
        // Broken links from observer mutations or the safe Clear control never
        // govern cleanup. Retained backing and live-object receipts do.
        sentinel.value = {&sentinel.value, &sentinel.value};
        other_sentinel.value = {&other_sentinel.value, &other_sentinel.value};
        for (unsigned n = 0; n < constructed; ++n)
        {
            if (value_live[n])
            {
                pool_operation = PoolOperation::cleanup;
                ReturnPool(live_nodes[n], sizeof(CircularListValueNode));
                pool_operation = PoolOperation::none;
                value_live[n] = false;
                live_nodes[n] = nullptr;
                ++node_returns;
            }
        }
        for (auto& bucket : pool_chunk_buckets)
        {
            bucket = nullptr;
        }
        pool_chunk_begin = nullptr;
        pool_chunk_end = nullptr;
        pool_heap_bytes = 0;
        for (auto& payload : payloads)
        {
            if (payload)
            {
                payload.reset();
                ++payload_releases;
            }
        }
        // Trivial free links end with the enclosing byte-array backing; there
        // is no invented list/sentinel owner destructor or allocator call.
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    ActiveScreenEntry* payload(int id)
    {
        if (id == -1)
        {
            return nullptr;
        }
        require(id >= 0 && id < static_cast<int>(payloads.size()) && payloads[id] != nullptr,
            "payload is not live");
        return &payloads[id]->value;
    }

    CircularListValueNode* node(unsigned id)
    {
        require(id < constructed && value_live[id], "value node lifetime is not live");
        return live_nodes[id];
    }

    CircularListNode* link(int id)
    {
        if (id == head_id)
        {
            return &sentinel.value;
        }
        if (id == other_head_id)
        {
            return &other_sentinel.value;
        }
        if (id == null_link_id)
        {
            return nullptr;
        }
        require(id >= 0, "invalid literal node label");
        return node(static_cast<unsigned>(id));
    }

    void append(unsigned id, int entry_id, bool second = false, bool detached = false)
    {
        require(id == constructed && id < seed_count, "construction order differs");
        auto* entry = payload(entry_id);
        const unsigned before_exchanges = exchanges;
        CircularListValueNode* result;
        {
            PoolScope scope(PoolOperation::create);
            if (detached)
            {
                result = list.value.CreateNode(entry);
                result->next = result;
                result->previous = result;
            }
            else
            {
                auto& target = second ? other_list.value : list.value;
                const auto inserted = target.Insert(target.End(), entry);
                result = static_cast<CircularListValueNode*>(inserted.node);
            }
        }
        require(reinterpret_cast<std::uintptr_t>(result) == slot_addresses[id],
            "real construction did not consume literal free head");
        require(result->entry == entry && exchanges == before_exchanges + 1 && pool_lock_word == 0,
            "real construction payload or lock differs");
        live_nodes[id] = result;
        value_live[id] = true;
        ++constructed;
        ++node_creations;
    }

    void check_owners()
    {
        sentinel.check();
        other_sentinel.check();
        list.check();
        other_list.check();
        require(
            list.value.head == &sentinel.value && other_list.value.head == &other_sentinel.value,
            "borrowed receiver head changed");
        for (const auto& slot : slots)
        {
            slot.check();
        }
        unrelated_small.check();
        unrelated_large.check();
        require(unrelated_small.value.next == nullptr && unrelated_large.value.next == nullptr,
            "unselected free-link content changed");
        for (unsigned n = 0; n < 16; ++n)
        {
            if (n != node_bucket)
            {
                auto* expected = n == 0    ? &unrelated_small.value
                                 : n == 15 ? &unrelated_large.value
                                           : nullptr;
                require(pool_chunk_buckets[n] == expected, "unselected pool bucket changed");
            }
        }
        require(pool_chunk_begin == nullptr && pool_chunk_end == nullptr && pool_heap_bytes == 0 &&
                    pool_lock_word == 0 && pool_spin_limit == 30 && pool_last_spin == 0,
            "uncontended pool globals or heap changed");
        for (unsigned n = 0; n < payloads.size(); ++n)
        {
            if (payloads[n])
            {
                payloads[n]->check();
                equal_bytes(static_cast<const void*>(&payloads[n]->value),
                    payload_before[n].data(),
                    sizeof(ActiveScreenEntry),
                    "live borrowed payload changed");
            }
        }
    }

    void check_pool(std::initializer_list<unsigned> order)
    {
        auto* current = pool_chunk_buckets[node_bucket];
        for (const auto id : order)
        {
            require(id < seed_count && !value_live[id], "free-link oracle refers to a live value");
            require(reinterpret_cast<std::uintptr_t>(current) == slot_addresses[id],
                "literal returned-pool order differs");
            // Only now read the live PoolChunkLink created by actual ReturnPool
            // or the initial typed free-slot setup. No old value-node read.
            current = current->next;
        }
        require(current == nullptr, "unexpected free-link tail");
        check_owners();
    }

    void clear(CircularList& target, std::initializer_list<unsigned> released)
    {
        const auto* saved_head = target.head;
        const unsigned before_exchanges = exchanges;
        {
            PoolScope scope(PoolOperation::clear);
            target.Clear();
        }
        ++clear_calls;
        for (const auto id : released)
        {
            require(id < constructed && value_live[id], "duplicate value-node release");
            value_live[id] = false;
            live_nodes[id] = nullptr;
            ++node_returns;
        }
        require(exchanges == before_exchanges + released.size(), "Clear pool-return count differs");
        require(target.head == saved_head && target.head->next == saved_head,
            "Clear head or next self-link differs");
        require(target.head->previous == saved_head, "Clear previous self-link differs");
        check_owners();
    }

    void reuse_last_head()
    {
        auto* entry = payload(3);
        const unsigned before_exchanges = exchanges;
        CircularListValueNode* result;
        {
            PoolScope scope(PoolOperation::create);
            result = list.value.CreateNode(entry);
        }
        require(reinterpret_cast<std::uintptr_t>(result) == slot_addresses[2],
            "new value lifetime did not reuse returned C");
        live_nodes[2] = result;
        value_live[2] = true;
        ++node_creations;
        result->next = &sentinel.value;
        result->previous = &sentinel.value;
        require(result->entry == entry && exchanges == before_exchanges + 1 && pool_lock_word == 0,
            "reused value payload or lock differs");
        check_pool({1, 0, 3});
        {
            PoolScope scope(PoolOperation::cleanup);
            ReturnPool(result, sizeof(CircularListValueNode));
        }
        value_live[2] = false;
        live_nodes[2] = nullptr;
        ++node_returns;
        check_pool({2, 1, 0, 3});
    }

    void observe(ActiveScreenEntry* entry, unsigned function)
    {
        require(observed_count < observed_payload.size(),
            "callback did not terminate at literal bound");
        require(function == expected_function, "callback function identity differs");
        int id = -1;
        if (entry != nullptr)
        {
            for (unsigned n = 0; n < payloads.size(); ++n)
            {
                if (payloads[n] && &payloads[n]->value == entry)
                {
                    id = static_cast<int>(n);
                }
            }
            require(id >= 0, "callback received a foreign or dead payload");
            payloads[id]->check();
            equal_bytes(
                entry, payload_before[id].data(), sizeof(*entry), "callback payload value differs");
        }
        observed_payload[observed_count] = id;
        observed_function[observed_count] = function;
        ++observed_count;
        ++callback_attempts;
        if (observed_count == 1)
        {
            switch (mutation)
            {
            case Mutation::insert:
                node(0)->next = node(2);
                break;
            case Mutation::skip:
                node(0)->next = node(2);
                break;
            case Mutation::stop:
                node(0)->next = &sentinel.value;
                break;
            case Mutation::payload:
                node(1)->entry = payload(3);
                break;
            case Mutation::iterators:
                caller_first.node = node(1);
                caller_last.node = &other_sentinel.value;
                break;
            default:
                break;
            }
        }
        if (mutation == Mutation::release_payload)
        {
            require(id == static_cast<int>(observed_count - 1), "release callback order differs");
            payloads[id].reset();
            ++callback_payload_releases;
            ++payload_releases;
            // The current node remains live, but its entry is never evaluated
            // again after this independently owned payload has been destroyed.
        }
        if (mutation == Mutation::throw_second && observed_count == 2)
        {
            node(1)->next = nullptr;
            throw CallbackSignal{&exception_token};
        }
    }
};

Session* observing = nullptr;
void observer_f(ActiveScreenEntry* entry)
{
    require(observing != nullptr, "observer has no live session");
    observing->observe(entry, 1);
}
void observer_g(ActiveScreenEntry* entry)
{
    require(observing != nullptr, "observer has no live session");
    observing->observe(entry, 2);
}

void visitor_case(const VisitorPlan& plan)
{
    current_case = plan.id;
    Session session(plan.count);
    for (unsigned n = 0; n < plan.count; ++n)
    {
        session.append(n, plan.initial_payload[n], false, n >= plan.primary_count);
    }
    if (plan.mutation == Mutation::insert)
    {
        session.node(2)->next = session.node(1);
    }
    session.mutation = plan.mutation;
    session.expected_function = plan.callback;
    session.caller_first.node = session.link(plan.first);
    session.caller_last.node = session.link(plan.last);
    if (plan.count == plan.primary_count && plan.first != null_link_id)
    {
        require(session.list.value.Begin().node == session.link(plan.count == 0 ? head_id : 0),
            "real Begin differs");
        require(session.list.value.End().node == &session.sentinel.value, "real End differs");
    }
    ActiveScreenCallback callback = plan.callback == 1   ? observer_f
                                    : plan.callback == 2 ? observer_g
                                                         : nullptr;
    ActiveScreenCallback result = observer_g;
    bool threw = false;
    observing = &session;
    try
    {
        ++visitor_calls;
        result = VisitActiveScreens(session.caller_first, session.caller_last, callback);
    }
    catch (const CallbackSignal& signal)
    {
        require(signal.token == &session.exception_token, "callback exception identity changed");
        threw = true;
    }
    observing = nullptr;
    require(threw == (plan.mutation == Mutation::throw_second), "callback exception flow differs");
    require(result == (threw ? observer_g : callback), "visitor callback-pointer return differs");
    require(session.observed_count == plan.expected_count, "literal callback count differs");
    for (unsigned n = 0; n < plan.expected_count; ++n)
    {
        require(session.observed_payload[n] == plan.expected_payload[n] &&
                    session.observed_function[n] == plan.callback,
            "literal callback order or pointer identity differs");
    }
    for (unsigned n = 0; n < plan.count; ++n)
    {
        auto* node = session.node(n);
        require(node->next == session.link(plan.final_next[n]) &&
                    node->previous == session.link(plan.final_previous[n]),
            "literal post-callback node links differ");
        if (plan.final_payload[n] != -2)
        {
            require(node->entry == session.payload(plan.final_payload[n]),
                "literal node payload differs");
        }
    }
    require(session.caller_first.node ==
                    session.link(plan.mutation == Mutation::iterators ? 1 : plan.first) &&
                session.caller_last.node ==
                    session.link(plan.mutation == Mutation::iterators ? other_head_id : plan.last),
        "caller iterator copies differ");
    require(
        session.sentinel.value.next == session.link(plan.primary_count == 0 ? head_id : 0) &&
            session.sentinel.value.previous ==
                session.link(
                    plan.primary_count == 0 ? head_id : static_cast<int>(plan.primary_count - 1)),
        "visitor changed the live sentinel");
    require(session.other_sentinel.value.next == &session.other_sentinel.value &&
                session.other_sentinel.value.previous == &session.other_sentinel.value,
        "excluded endpoint changed");
    session.check_pool({});
}

void clear_case(unsigned index)
{
    const auto& plan = clear_plans[index];
    current_case = plan.id;
    Session session(plan.count + plan.prior_count);
    for (unsigned n = 0; n < plan.count; ++n)
    {
        session.append(n, plan.payload[n], n >= plan.first_count);
    }
    if (index == 6)
    {
        session.sentinel.value.previous = &session.sentinel.value;
        session.node(0)->previous = session.node(0);
        session.node(1)->previous = &session.sentinel.value;
    }
    switch (index)
    {
    case 0:
        session.clear(session.list.value, {});
        session.check_pool({});
        break;
    case 1:
        session.clear(session.list.value, {0});
        session.check_pool({0});
        break;
    case 2:
    case 5:
        session.clear(session.list.value, {0, 1, 2});
        session.check_pool({2, 1, 0});
        break;
    case 3:
        session.clear(session.list.value, {0, 1, 2});
        session.check_pool({2, 1, 0, 3, 4});
        break;
    case 4:
        session.clear(session.list.value, {0, 1});
        session.check_pool({1, 0, 2});
        break;
    case 6:
        session.clear(session.list.value, {0, 1});
        session.check_pool({1, 0});
        break;
    case 7:
        session.clear(session.list.value, {0, 1, 2});
        session.check_pool({2, 1, 0, 3});
        session.clear(session.list.value, {});
        session.check_pool({2, 1, 0, 3});
        break;
    case 8:
        session.clear(session.list.value, {0, 1});
        session.check_pool({1, 0, 4});
        require(session.other_sentinel.value.next == session.node(2) &&
                    session.node(2)->next == session.node(3) &&
                    session.node(3)->next == &session.other_sentinel.value &&
                    session.other_sentinel.value.previous == session.node(3) &&
                    session.node(2)->previous == &session.other_sentinel.value &&
                    session.node(3)->previous == session.node(2) &&
                    session.node(2)->entry == session.payload(2) &&
                    session.node(3)->entry == session.payload(3),
            "other live ring changed during first Clear");
        session.clear(session.other_list.value, {2, 3});
        session.check_pool({3, 2, 1, 0, 4});
        break;
    case 9:
    {
        CircularList alias{&session.sentinel.value};
        session.clear(session.list.value, {0, 1});
        session.check_pool({1, 0});
        session.clear(alias, {});
        session.check_pool({1, 0});
        break;
    }
    case 10:
        session.clear(session.list.value, {0, 1, 2});
        session.check_pool({2, 1, 0, 3});
        session.reuse_last_head();
        break;
    default:
        require(false, "unknown Clear literal");
    }
    require(callback_payload_releases == 2, "Clear released a borrowed payload");
}
} // namespace

// Existing test-only, separately named OS boundary. The accepted real lock
// body executes an uncontended exchange; no fake game callback is supplied.
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
        for (const auto& plan : visitor_plans)
        {
            visitor_case(plan);
        }
        for (unsigned i = 0; i < clear_plans.size(); ++i)
        {
            clear_case(i);
        }
        require(visitor_calls == 15 && callback_attempts == 22 && clear_calls == 14,
            "literal operation totals differ");
        require(node_creations == 52 && node_returns == 52 && exchanges == 104,
            "actual node/lock lifetime totals differ");
        require(
            payload_allocations == 104 && payload_releases == 104 && callback_payload_releases == 2,
            "owned payload release totals differ");
        for (auto* bucket : pool_chunk_buckets)
        {
            require(bucket == nullptr, "final pool root remains");
        }
        require(pool_chunk_begin == nullptr && pool_chunk_end == nullptr && pool_heap_bytes == 0 &&
                    pool_lock_word == 0,
            "final global storage remains");
        std::printf("sessions=26 visitor-calls=%u callbacks=%u clear-calls=%u "
                    "node-creations=%u node-returns=%u payload-allocations=%u payload-releases=%u "
                    "callback-releases=%u exchanges=%u checks=%llu byte-checks=%llu "
                    "native-active-list-cleanup=pass\n",
            visitor_calls,
            callback_attempts,
            clear_calls,
            node_creations,
            node_returns,
            payload_allocations,
            payload_releases,
            callback_payload_releases,
            exchanges,
            checks,
            byte_checks);
        return 0;
    }
    catch (const CheckFailure& failure)
    {
        observing = nullptr;
        std::fprintf(stderr, "active-list-cleanup %s: %s\n", current_case, failure.reason);
        return 2;
    }
}
