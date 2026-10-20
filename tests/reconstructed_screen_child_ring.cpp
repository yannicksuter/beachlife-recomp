#include "child_ring_seed.h"
#include "startup_screen_item.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>

namespace
{
const char* current_case = "startup";
unsigned long long checks = 0;
unsigned long long byte_checks = 0;
unsigned exchanges = 0;
unsigned rings = 0;
unsigned ring_destructions = 0;
unsigned insertions = 0;
unsigned clear_calls = 0;
unsigned payload_lifetimes = 0;
bool pool_phase = false;

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
std::uintptr_t address(const void* value)
{
    return reinterpret_cast<std::uintptr_t>(value);
}

struct Payload
{
    std::array<unsigned char, 16> before{};
    ScreenItem value;
    std::array<unsigned char, 16> after{};
    std::array<unsigned char, sizeof(ScreenItem)> saved{};

    explicit Payload(int tag)
        : value(ScreenRectangle{tag, -tag, tag + 17, 29})
    {
        before.fill(0x38);
        after.fill(0xc2);
        value.kind_28 = 0;
        value.word_2c = static_cast<unsigned>(tag + 79);
        std::memset(value.unknown_31, 0x53, sizeof(value.unknown_31));
        std::memcpy(saved.data(), static_cast<const void*>(&value), sizeof(value));
        ++payload_lifetimes;
    }
    void check() const
    {
        byte_checks += saved.size();
        require(std::memcmp(saved.data(), static_cast<const void*>(&value), saved.size()) == 0,
            "ring changed a borrowed live payload");
        for (unsigned i = 0; i < before.size(); ++i)
        {
            byte_checks += 2;
            require(before[i] == 0x38 && after[i] == 0xc2, "payload guard changed");
        }
    }
};

struct Ring
{
    struct Backing
    {
        std::array<unsigned char, 16> before{};
        alignas(ScreenChildRing) unsigned char bytes[sizeof(ScreenChildRing)]{};
        std::array<unsigned char, 16> after{};
        Backing()
        {
            before.fill(0x61);
            after.fill(0x94);
        }
    } backing;
    ScreenChildRing& value;
    Ring()
        : value(*::new (static_cast<void*>(backing.bytes)) ScreenChildRing)
    {
        ++rings;
    }
    ~Ring()
    {
        value.~ScreenChildRing();
        check_guards();
        ++ring_destructions;
    }
    Ring(const Ring&) = delete;
    Ring& operator=(const Ring&) = delete;
    void check_guards() const
    {
        for (unsigned i = 0; i < backing.before.size(); ++i)
        {
            byte_checks += 2;
            require(
                backing.before[i] == 0x61 && backing.after[i] == 0x94, "ring owner guard changed");
        }
    }
};

using Seed = child_ring_fixture::Seed<6>;
struct Session
{
    Payload p{3};
    Payload q{8};
    Seed seed{require};
    Session()
    {
        require(!pool_phase, "nested child-ring session");
        pool_phase = true;
    }
    ~Session()
    {
        pool_phase = false;
    }
    void finish(std::initializer_list<unsigned> free_order)
    {
        seed.check_free(free_order);
        p.check();
        q.check();
        byte_checks += seed.checked_bytes();
    }
};
struct Value
{
    unsigned slot;
    ScreenItem* payload;
};

void check_ring(Session& session, Ring& owner, unsigned head, std::initializer_list<Value> values)
{
    ScreenChildNode* sentinel = owner.value.head;
    require(address(sentinel) == session.seed.address(head), "literal sentinel identity differs");
    require(owner.value.End().node == sentinel, "actual End lost the sentinel identity");
    ScreenChildNode* current = sentinel->next;
    ScreenChildNode* previous = sentinel;
    for (const auto& expected : values)
    {
        require(address(current) == session.seed.address(expected.slot),
            "literal forward child identity differs");
        require(current->previous == previous && current->item == expected.payload,
            "literal child previous link or borrowed payload differs");
        previous = current;
        current = current->next;
    }
    require(current == sentinel && sentinel->previous == previous, "literal child tail differs");
    current = sentinel->previous;
    ScreenChildNode* next = sentinel;
    for (auto item = values.end(); item != values.begin();)
    {
        --item;
        require(address(current) == session.seed.address(item->slot),
            "literal reverse child identity differs");
        require(current->next == next, "literal reverse child next differs");
        next = current;
        current = current->previous;
    }
    require(current == sentinel, "literal reverse end differs");
    owner.check_guards();
    session.seed.check_common();
    session.seed.check_guards();
    session.p.check();
    session.q.check();
}

ScreenChildNode* insert(Session& session,
    Ring& receiver,
    ScreenChildIterator position,
    ScreenItem* const& payload,
    unsigned slot)
{
    const auto result = receiver.value.Insert(position, payload);
    ++insertions;
    require(address(result.node) == session.seed.address(slot), "Insert returned another slot");
    return result.node;
}
void clear(Ring& owner)
{
    owner.value.Clear();
    ++clear_calls;
}

void empty_owner()
{
    current_case = "empty-owner";
    Session session;
    {
        Ring owner;
        // Stop the lost-tail control before another allocation could refill.
        require(address(pool_chunk_buckets[2]) == session.seed.address(1),
            "constructor did not retain the next free slot");
        check_ring(session, owner, 0, {});
        session.seed.check_free({1, 2, 3, 4, 5});
        clear(owner);
        require(owner.value.head->previous == owner.value.head,
            "empty Clear changed sentinel previous");
        check_ring(session, owner, 0, {});
    }
    session.finish({0, 1, 2, 3, 4, 5});
}
void borrowed_order()
{
    current_case = "borrowed-order";
    Session session;
    {
        Ring owner;
        ScreenItem* p = &session.p.value;
        ScreenItem* q = &session.q.value;
        insert(session, owner, owner.value.End(), p, 1);
        insert(session, owner, owner.value.End(), q, 2);
        insert(session, owner, owner.value.End(), p, 3);
        check_ring(session, owner, 0, {{1, p}, {2, q}, {3, p}});
        clear(owner);
        check_ring(session, owner, 0, {});
        session.seed.check_free({3, 2, 1, 4, 5});
    }
    session.finish({0, 3, 2, 1, 4, 5});
}
void before_position()
{
    current_case = "before-position-null";
    Session session;
    {
        Ring owner;
        ScreenItem* p = &session.p.value;
        ScreenItem* q = &session.q.value;
        ScreenItem* null = nullptr;
        insert(session, owner, owner.value.End(), p, 1);
        ScreenChildIterator position;
        position.node = insert(session, owner, owner.value.End(), q, 2);
        insert(session, owner, position, null, 3);
        check_ring(session, owner, 0, {{1, p}, {3, nullptr}, {2, q}});
        clear(owner);
        check_ring(session, owner, 0, {});
        session.seed.check_free({2, 3, 1, 4, 5});
    }
    session.finish({0, 2, 3, 1, 4, 5});
}
void alias_and_reuse()
{
    current_case = "payload-alias-and-reuse";
    Session session;
    {
        Ring owner;
        ScreenItem* p = &session.p.value;
        ScreenItem* q = &session.q.value;
        ScreenChildNode* first = insert(session, owner, owner.value.End(), p, 1);
        ScreenChildNode* second = insert(session, owner, owner.value.End(), first->item, 2);
        const std::uintptr_t retired_address = address(second);
        check_ring(session, owner, 0, {{1, p}, {2, p}});
        clear(owner);
        // These node lifetimes have ended. Only the saved integer identity and
        // actual new free-link/node objects are used from here.
        first = nullptr;
        second = nullptr;
        check_ring(session, owner, 0, {});
        session.seed.check_free({2, 1, 3, 4, 5});
        ScreenChildNode* reused = insert(session, owner, owner.value.End(), q, 2);
        require(address(reused) == retired_address, "returned child slot was not reused");
        check_ring(session, owner, 0, {{2, q}});
        clear(owner);
        check_ring(session, owner, 0, {});
    }
    session.finish({0, 2, 1, 3, 4, 5});
}
void position_directed()
{
    current_case = "two-coherent-rings";
    Session session;
    {
        Ring receiver;
        {
            Ring destination;
            ScreenItem* q = &session.q.value;
            insert(session, receiver, destination.value.End(), q, 2);
            check_ring(session, receiver, 0, {});
            check_ring(session, destination, 1, {{2, q}});
        }
        session.seed.check_free({1, 2, 3, 4, 5});
        check_ring(session, receiver, 0, {});
    }
    session.finish({0, 1, 2, 3, 4, 5});
}
} // namespace

// Paired test-only host scalar boundary; the seeded domain forbids an
// unexpected heap refill while actual pool/ring methods are active.
void* operator new(std::size_t bytes)
{
    require(!pool_phase, "unexpected heap refill in seeded child-ring session");
    for (;;)
    {
        if (void* result = std::malloc(bytes ? bytes : 1))
        {
            return result;
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
    std::free(memory);
}
void operator delete(void* memory, std::size_t) noexcept
{
    ::operator delete(memory);
}
extern "C" LONG InterlockedExchange(LONG* target, LONG value)
{
    volatile LONG* word = target;
    require(pool_phase && target == &pool_lock_word && value == 1 && *word == 0,
        "unexpected child-ring exchange");
    *word = value;
    ++exchanges;
    return 0;
}
extern "C" void Sleep(DWORD)
{
    require(false, "unexpected child-ring wait");
}

int main()
{
    try
    {
        empty_owner();
        borrowed_order();
        before_position();
        alias_and_reuse();
        position_directed();
        require(rings == 6 && ring_destructions == 6 && insertions == 10 && clear_calls == 5 &&
                    payload_lifetimes == 10 && exchanges == 32,
            "literal child-ring operation totals differ");
        for (auto* root : pool_chunk_buckets)
        {
            require(root == nullptr, "final pool root remains");
        }
        require(!pool_phase && pool_heap_bytes == 0 && pool_chunk_begin == nullptr &&
                    pool_chunk_end == nullptr && pool_lock_word == 0,
            "final child-ring backing or lock remains");
        std::printf("sessions=5 rings=%u insertions=%u explicit-clear=%u payload-lifetimes=%u "
                    "exchanges=%u checks=%llu byte-checks=%llu\n",
            rings,
            insertions,
            clear_calls,
            payload_lifetimes,
            exchanges,
            checks,
            byte_checks);
        return 0;
    }
    catch (const CheckFailure& failure)
    {
        std::fprintf(stderr, "%s: %s\n", current_case, failure.reason);
        return 2;
    }
}
