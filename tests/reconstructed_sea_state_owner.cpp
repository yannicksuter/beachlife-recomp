#include "startup_renderer_sea_state.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <type_traits>

namespace
{

using Owner = RendererSeaStatePrefix;

constexpr std::size_t opaque_bytes = sizeof(Owner::unknown_04);
constexpr std::size_t sea_offset = offsetof(Owner, sea_enabled_10081);
constexpr std::size_t member_bytes = sea_offset + sizeof(Owner::sea_enabled_10081);
constexpr std::size_t guard_bytes = 32;
constexpr std::size_t owned_bytes = 37;

static_assert(CHAR_BIT == 8 && sizeof(unsigned int) == 4 && sizeof(void*) == 8);
static_assert(std::is_standard_layout_v<Owner> && !std::is_polymorphic_v<Owner>);
static_assert(offsetof(Owner, owned_data_00) == 0);
static_assert(offsetof(Owner, unknown_04) == sizeof(void*));
static_assert(opaque_bytes == 0x10078);
static_assert(offsetof(Owner, word_1007c) == offsetof(Owner, unknown_04) + opaque_bytes);
static_assert(offsetof(Owner, word_1007c) == 0x10080);
static_assert(offsetof(Owner, byte_10080) == offsetof(Owner, word_1007c) + sizeof(unsigned int));
static_assert(sea_offset == offsetof(Owner, byte_10080) + sizeof(unsigned char));
static_assert(member_bytes == 0x10086 && sizeof(Owner) == 0x10088);

struct GuardedStorage
{
    std::array<unsigned char, guard_bytes> before;
    alignas(Owner) unsigned char storage[sizeof(Owner)];
    std::array<unsigned char, guard_bytes> after;
};

static_assert(offsetof(GuardedStorage, storage) == guard_bytes);
static_assert(offsetof(GuardedStorage, after) == guard_bytes + sizeof(Owner));
static_assert(sizeof(GuardedStorage) == sizeof(Owner) + 2 * guard_bytes);

struct Counters
{
    std::size_t checks = 0;
    std::size_t representation_bytes = 0;
    std::size_t constructors = 0;
    std::size_t destructors = 0;
    std::size_t setters = 0;
    std::size_t owned_blocks_transferred = 0;
} counters;

const char* failure = nullptr;

bool require(bool condition, const char* message)
{
    ++counters.checks;
    if (!condition && failure == nullptr)
    {
        failure = message;
    }
    return condition;
}

bool same_bytes(const void* actual, const void* expected, std::size_t count, const char* message)
{
    counters.representation_bytes += count;
    return require(std::memcmp(actual, expected, count) == 0, message);
}

bool guards(const GuardedStorage& block)
{
    for (std::size_t i = 0; i < guard_bytes; ++i)
    {
        if (!require(block.before[i] == 0xb6 && block.after[i] == 0x6b, "outside guard"))
        {
            return false;
        }
    }
    return true;
}

// The later optimized-object check must bind this observable loop and the
// same storage passed to the real placement constructor. No live object exists.
[[gnu::noinline]] void establish_storage(GuardedStorage& block, unsigned char pattern)
{
    block.before.fill(0xb6);
    block.after.fill(0x6b);
    volatile unsigned char* bytes = block.storage;
    for (std::size_t i = 0; i < sizeof(block.storage); ++i)
    {
        bytes[i] = pattern;
    }
}

struct DestroyOnly
{
    void operator()(Owner* owner) const noexcept
    {
        owner->~Owner();
        ++counters.destructors;
    }
};

struct ScalarDelete
{
    void operator()(void* memory) const noexcept
    {
        ::operator delete(memory);
    }
};

// Return before process leak checking, with both the outer allocation and
// pointer-bearing snapshots gone. There are no allocation/deallocation overrides.
[[gnu::noinline]] bool exercise(bool attach_owned_data, unsigned char pattern)
{
    auto block = std::make_unique<GuardedStorage>();
    establish_storage(*block, pattern);
    std::array<unsigned char, opaque_bytes> expected_opaque;
    expected_opaque.fill(pattern);

    std::unique_ptr<Owner, DestroyOnly> owner(::new (static_cast<void*>(block->storage)) Owner);
    ++counters.constructors;
    if (!require(owner->owned_data_00 == nullptr, "constructor owned pointer") ||
        !require(owner->byte_10080 == 0, "constructor byte10080") ||
        !require(owner->word_1007c == 0xffffffffu, "constructor word1007c") ||
        !require(owner->sea_enabled_10081 == 1, "constructor sea byte") ||
        !same_bytes(
            owner->unknown_04, expected_opaque.data(), opaque_bytes, "constructor opaque bytes") ||
        !guards(*block))
    {
        return false;
    }

    if (attach_owned_data)
    {
        std::unique_ptr<void, ScalarDelete> allocation(::operator new(owned_bytes));
        std::memset(allocation.get(), 0xd3, owned_bytes);
        owner->owned_data_00 = allocation.release();
        ++counters.owned_blocks_transferred;
        owner->word_1007c = 0x13579bdfu;
        owner->byte_10080 = 0xe2;
    }

    std::array<unsigned char, member_bytes> expected;
    std::memcpy(expected.data(), block->storage, member_bytes);
    if (attach_owned_data)
    {
        constexpr std::array<unsigned char, 3> values{0, 0x80, 0xff};
        std::array<unsigned char, owned_bytes> expected_owned;
        expected_owned.fill(0xd3);
        for (unsigned char value : values)
        {
            owner->SetSeaEnabled(value);
            ++counters.setters;
            expected[sea_offset] = value;
            if (!same_bytes(block->storage,
                    expected.data(),
                    member_bytes,
                    "setter member representation") ||
                !same_bytes(
                    owner->owned_data_00, expected_owned.data(), owned_bytes, "owned payload") ||
                !guards(*block))
            {
                return false;
            }
        }
    }

    owner.reset();
    // The ordinary destructor has ended the object's lifetime. Inspect only
    // saved raw storage; the freed payload is never read or released again.
    const bool preserved =
        same_bytes(
            block->storage, expected.data(), member_bytes, "destructor member representation") &&
        guards(*block);

    // Remove saved pointer representations before leak checking. The original
    // outer byte storage is restored only after the real destructor has run.
    volatile unsigned char* saved = expected.data();
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        saved[i] = 0;
    }
    volatile unsigned char* restored = block->storage;
    for (std::size_t i = 0; i < sizeof(block->storage); ++i)
    {
        restored[i] = pattern;
    }
    bool restored_correctly = true;
    for (unsigned char value : block->storage)
    {
        restored_correctly = restored_correctly && value == pattern;
    }
    return require(preserved && restored_correctly, "outer storage restoration") && guards(*block);
}

} // namespace

int main()
{
    try
    {
        if (!exercise(false, 0xa5) || !exercise(true, 0x3c))
        {
            std::fprintf(stderr, "sea-state owner failure: %s\n", failure);
            return 2;
        }
        if (!require(counters.constructors == 2 && counters.destructors == 2 &&
                         counters.setters == 3 && counters.owned_blocks_transferred == 1,
                "lifetime and setter call counts"))
        {
            std::fprintf(stderr, "sea-state owner failure: %s\n", failure);
            return 2;
        }
        std::printf("{\"groups\":1,\"constructor_calls\":%zu,\"destructor_calls\":%zu,"
                    "\"setter_calls\":%zu,\"owned_blocks_transferred\":%zu,"
                    "\"checks\":%zu,\"representation_bytes\":%zu}\n",
            counters.constructors,
            counters.destructors,
            counters.setters,
            counters.owned_blocks_transferred,
            counters.checks,
            counters.representation_bytes);
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::fprintf(stderr, "sea-state owner setup exception: %s\n", exception.what());
        return 3;
    }
}
