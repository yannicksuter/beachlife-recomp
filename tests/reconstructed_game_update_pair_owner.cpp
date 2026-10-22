#include "startup_game_update_pair.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>
#include <type_traits>

namespace
{
static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && sizeof(float) == 4);
static_assert(sizeof(void*) == 8 && sizeof(GameSoundNode) == 24);
static_assert(std::is_standard_layout_v<GameUpdatePair>);
static_assert(!std::is_polymorphic_v<GameUpdatePair>);
static_assert(std::is_empty_v<GameUpdatePairRegistration>);
static_assert(offsetof(GameUpdatePair, enabled_00) == 0);
static_assert(offsetof(GameUpdatePair, unknown_01) == 1);
static_assert(offsetof(GameUpdatePair, point_sounds_04) == 8);
static_assert(offsetof(GameUpdatePair, object_sounds_08) == 16);
static_assert(sizeof(GameUpdatePair) == 24 && alignof(GameUpdatePair) == 8);
static_assert(offsetof(GameSoundNode, next) == 0);
static_assert(offsetof(GameSoundNode, previous) == 8);
static_assert(offsetof(GameSoundNode, sound) == 16);
static_assert(sizeof(GamePointSound) == 64 && sizeof(GameObjectSound) == 48);

struct Failure
{
    const char* message;
};
unsigned checks = 0, groups = 0, owners = 0, destructions = 0;
void require(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw Failure{message};
    }
}

struct Tracked
{
    void* pointer = nullptr;
    unsigned id = 0;
};
std::array<Tracked, 4> payloads{};
std::array<unsigned, 4> release_order{};
unsigned releases = 0;
bool release_state_ok = true;
GameUpdatePair* releasing_owner = nullptr;
PoolChunkLink* expected_free_head = nullptr;
constexpr unsigned bucket = (sizeof(GameSoundNode) - 1) / 8;

// Observe only scalar allocation/free. All game cleanup and pool operations
// are the real selected objects, including their actual registration lifetime.
void observe_free(void* memory) noexcept
{
    for (auto& payload : payloads)
    {
        if (payload.pointer && payload.pointer == memory)
        {
            release_state_ok = release_state_ok && releases < release_order.size() &&
                               game_update_pair == releasing_owner && pool_lock_word == 0 &&
                               pool_chunk_buckets[bucket] == expected_free_head;
            if (releases < release_order.size())
            {
                release_order[releases] = payload.id;
            }
            ++releases;
            payload.pointer = nullptr;
            return;
        }
    }
}

struct Pool
{
    static constexpr unsigned slots = 12;
    std::array<unsigned char, 16> before{};
    alignas(GameSoundNode) unsigned char storage[slots * sizeof(GameSoundNode)];
    std::array<unsigned char, 16> after{};

    Pool()
    {
        require(pool_lock_word == 0 && pool_heap_bytes == 0 && !pool_chunk_begin && !pool_chunk_end,
            "pool has a live lock or heap span");
        for (auto* entry : pool_chunk_buckets)
        {
            require(!entry, "pool bucket already occupied");
        }
        before.fill(0x39);
        after.fill(0xc6);
        PoolChunkLink* next = nullptr;
        for (unsigned i = slots; i != 0;)
        {
            --i;
            next = ::new (address(i)) PoolChunkLink{next};
        }
        pool_chunk_buckets[bucket] = next;
    }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    ~Pool()
    {
        pool_chunk_buckets[bucket] = nullptr;
    }
    void* address(unsigned index)
    {
        return static_cast<void*>(storage + index * sizeof(GameSoundNode));
    }
    void restored(const std::array<unsigned, slots>& order)
    {
        auto* link = pool_chunk_buckets[bucket];
        for (auto slot : order)
        {
            require(static_cast<void*>(link) == address(slot), "returned pool-node order differs");
            link = link->next;
        }
        require(!link, "pool has an extra node or cycle");
        require(pool_lock_word == 0 && pool_heap_bytes == 0 && !pool_chunk_begin && !pool_chunk_end,
            "scheduler retained a pool lock or allocated a heap span");
        for (unsigned i = 0; i != 16; ++i)
        {
            require(before[i] == 0x39 && after[i] == 0xc6, "pool outside guard changed");
            if (i != bucket)
            {
                require(!pool_chunk_buckets[i], "unrelated pool bucket changed");
            }
        }
    }
};

constexpr std::array<unsigned, Pool::slots> initial_order{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

struct Owner
{
    std::array<unsigned char, 16> before{};
    alignas(GameUpdatePair) unsigned char storage[sizeof(GameUpdatePair)];
    std::array<unsigned char, 16> after{};
    GameUpdatePair* value = nullptr;

    explicit Owner(unsigned char enabled)
    {
        before.fill(0x5a);
        after.fill(0xa5);
        // Observable pre-lifetime stores establish the three preserved opaque
        // bytes. ABI padding is not an oracle and no live object is overwritten.
        volatile unsigned char* bytes = storage;
        for (unsigned i = 0; i != sizeof(storage); ++i)
        {
            bytes[i] = 0xa7;
        }
        value = ::new (static_cast<void*>(storage)) GameUpdatePair(enabled);
        ++owners;
    }
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
    ~Owner()
    {
        close();
    }
    void close()
    {
        if (value)
        {
            value->~GameUpdatePair();
            value = nullptr;
            ++destructions;
        }
    }
    void guards() const
    {
        for (unsigned i = 0; i != before.size(); ++i)
        {
            require(before[i] == 0x5a && after[i] == 0xa5, "scheduler outside guard changed");
        }
    }
};
static_assert(offsetof(Owner, storage) == 16);
static_assert(offsetof(Owner, after) == 16 + sizeof(GameUpdatePair));
static_assert(offsetof(Pool, storage) == 16);
static_assert(offsetof(Pool, after) == 16 + Pool::slots * sizeof(GameSoundNode));

void check_empty(Owner& owner, Pool& pool, unsigned char enabled, unsigned first)
{
    require(game_update_pair == owner.value, "constructor did not publish the genuine owner");
    require(owner.value->enabled_00 == enabled, "enabled byte differs");
    for (auto byte : owner.value->unknown_01)
    {
        require(byte == 0xa7, "opaque member byte changed");
    }
    auto* point = owner.value->point_sounds_04.head;
    auto* object = owner.value->object_sounds_08.head;
    require(static_cast<void*>(point) == pool.address(first) &&
                static_cast<void*>(object) == pool.address(first + 1),
        "actual list sentinel allocation order differs");
    require(point->next == point && point->previous == point && object->next == object &&
                object->previous == object,
        "constructor did not establish two empty circular lists");
    // Sentinel sound is not initialized by the original and is never read.
    owner.guards();
}

template <class T> T* payload(unsigned id)
{
    auto* result = ::new (::operator new(sizeof(T))) T{};
    payloads[id - 1] = {result, id};
    return result;
}

void append(GameSoundList& list, void* sound, Pool& pool, unsigned slot)
{
    void* storage = AllocatePoolBytes(sizeof(GameSoundNode));
    require(storage == pool.address(slot), "fixture node allocation order differs");
    auto* head = list.head;
    auto* node = ::new (storage) GameSoundNode{head, head->previous, sound};
    head->previous->next = node;
    head->previous = node;
}

void empty_owners()
{
    ++groups;
    Pool pool;
    GameSoundNode node{};
    GameSoundAllocator allocator;
    GamePointSoundListBase holder(allocator, &node);
    require(holder.head == &node, "head holder ignored its nonnull second argument");
    for (unsigned char enabled : {0x00, 0x80, 0xff})
    {
        Owner owner(enabled);
        check_empty(owner, pool, enabled, 0);
        owner.close();
        require(!game_update_pair, "destruction did not clear registration");
        owner.guards();
        pool.restored(initial_order);
    }
}

void populated_owner()
{
    ++groups;
    Pool pool;
    Owner owner(1);
    check_empty(owner, pool, 1, 0);
    auto& point = owner.value->point_sounds_04;
    auto& object = owner.value->object_sounds_08;
    append(point, payload<GamePointSound>(1), pool, 2);
    append(point, nullptr, pool, 3);
    append(point, payload<GamePointSound>(2), pool, 4);
    append(object, payload<GameObjectSound>(3), pool, 5);
    append(object, payload<GameObjectSound>(4), pool, 6);
    releasing_owner = owner.value;
    expected_free_head = pool_chunk_buckets[bucket];
    owner.close();
    releasing_owner = nullptr;
    require(
        release_state_ok && releases == 4 && release_order == std::array<unsigned, 4>{1, 2, 3, 4},
        "payload release order/publication or node-retention state differs");
    for (const auto& item : payloads)
    {
        require(!item.pointer, "scheduler retained an owned payload");
    }
    require(!game_update_pair, "populated destruction retained registration");
    owner.guards();
    pool.restored({0, 1, 6, 5, 4, 3, 2, 7, 8, 9, 10, 11});
}

void older_clears_younger()
{
    ++groups;
    Pool pool;
    Owner older(0x21);
    check_empty(older, pool, 0x21, 0);
    Owner younger(0xfe);
    check_empty(younger, pool, 0xfe, 2);
    older.close();
    require(!game_update_pair, "older destruction did not clear the younger registration");
    // The still-live younger owner remains intact even though the original
    // unconditional registration cleanup has removed its global publication.
    require(younger.value->enabled_00 == 0xfe &&
                younger.value->point_sounds_04.head->next == younger.value->point_sounds_04.head &&
                younger.value->object_sounds_08.head->next == younger.value->object_sounds_08.head,
        "older destruction changed the younger owner");
    older.guards();
    younger.guards();
    younger.close();
    require(!game_update_pair, "younger destruction republished registration");
    pool.restored({2, 3, 0, 1, 4, 5, 6, 7, 8, 9, 10, 11});
}
} // namespace

void* operator new(std::size_t bytes)
{
    if (void* memory = std::malloc(bytes ? bytes : 1))
    {
        return memory;
    }
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept
{
    observe_free(memory);
    std::free(memory);
}
void operator delete(void* memory, std::size_t) noexcept
{
    ::operator delete(memory);
}

int main()
{
    try
    {
        require(!game_update_pair, "registration must start empty");
        empty_owners();
        populated_owner();
        older_clears_younger();
        require(owners == 6 && destructions == 6, "owner lifetime count differs");
        std::printf("scheduler owner: %u groups, %u lifetimes, %u payload releases, %u checks\n",
            groups,
            owners,
            releases,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "scheduler owner fixture: %s\n", failure.message);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "scheduler owner exception: %s\n", failure.what());
        return 3;
    }
}
