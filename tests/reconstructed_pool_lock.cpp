#include "pool_lock_acquire.h"
#include "pool_lock_guard.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

// Generated, single-threaded API providers. This tests source composition,
// not OS atomic behavior or the original parent's exception-handling machinery.
namespace
{
unsigned checks = 0;
unsigned cases = 0;
unsigned exchanges = 0;
unsigned sleeps = 0;
unsigned failed_exchanges = 0;
LONG expected_initial = 0;
LONG failure_value = 1;
constexpr std::array<DWORD, 24> waits{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 4, 8, 16, 32, 64, 128, 128, 128};

void expect(bool value, const char* reason)
{
    ++checks;
    if (!value)
    {
        std::fprintf(stderr, "case %u: %s\n", cases, reason);
        std::exit(2);
    }
}

struct Marker
{
};

void exercise(unsigned failures, std::uint32_t bits, bool unwind)
{
    ++cases;
    expected_initial = std::bit_cast<LONG>(bits);
    failure_value = expected_initial == 0 ? -1 : expected_initial;
    pool_lock_word = expected_initial;
    pool_spin_limit = 0;
    pool_last_spin = bits;
    failed_exchanges = failures;
    exchanges = 0;
    sleeps = 0;
    bool observed_unwind = false;
    try
    {
        PoolLockGuard guard;
        AcquirePoolLock(&pool_lock_word);
        expect(pool_lock_word == 1, "acquisition must publish the shared lock word");
        expect(exchanges == failures + 1, "unexpected acquisition attempts");
        expect(sleeps == (failures ? failures - 1 : 0), "unexpected fallback waits");
        expect(pool_spin_limit == (failures ? 30u : 0u), "wrong adaptive fallback state");
        expect(pool_last_spin == bits, "fallback must preserve prior-spin state");
        if (unwind)
        {
            throw Marker{};
        }
    }
    catch (const Marker&)
    {
        observed_unwind = true;
    }
    expect(observed_unwind == unwind, "generated lifetime path did not complete");
    expect(pool_lock_word == 0, "cleanup must clear the same acquired word");
    expect(pool_spin_limit == (failures ? 30u : 0u), "cleanup changed adaptive limit");
    expect(pool_last_spin == bits, "cleanup changed previous-spin state");
}
} // namespace

extern "C" LONG InterlockedExchange(LONG* target, LONG value)
{
    // The legacy SDK boundary has a nonvolatile pointer type. Preserve the
    // actual object's qualification for every access inside this provider.
    volatile LONG* word = target;
    expect(target == &pool_lock_word, "provider received a different lock object");
    expect(value == 1, "wrong acquisition value");
    expect(*word == (exchanges == 0 ? expected_initial : 1), "shared lock state lost");
    // A deterministic external-release schedule, with a real write to the same
    // correctly typed live object. No pointer cast or replacement object.
    *word = exchanges < failed_exchanges ? failure_value : 0;
    const LONG previous = *word;
    *word = value;
    ++exchanges;
    return previous;
}

extern "C" void Sleep(DWORD milliseconds)
{
    expect(sleeps < waits.size(), "unexpected unbounded wait");
    expect(milliseconds == waits[sleeps], "fallback wait differs");
    expect(pool_lock_word == 1, "wait did not observe the acquired shared storage");
    ++sleeps;
}

int main()
{
    static_assert(sizeof(LONG) == 4 && sizeof(pool_lock_word) == 4);
    static_assert(std::is_same_v<std::remove_cv_t<decltype(pool_lock_word)>, LONG>);
    static_assert(std::is_same_v<decltype(&AcquirePoolLock), void (*)(decltype(&pool_lock_word))>);
    expect(pool_lock_word == 0, "actual source global must start zero");
    for (unsigned failures : {0u, 1u, 2u, 15u, 16u, 17u, 21u, 22u, 24u})
    {
        for (std::uint32_t bits : {0u, 1u, 0xffffffffu, 0x80000000u, 0x7fffffffu})
        {
            exercise(failures, bits, false);
            exercise(failures, bits, true);
        }
    }
    std::printf("cases=%u checks=%u shared-word-composition=pass\n", cases, checks);
}
