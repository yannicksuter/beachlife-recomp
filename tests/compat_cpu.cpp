#include "startup_timer.h"
#include "startup_timestamp_counter.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

std::uint64_t beachlife_startup_timestamp_counter();

int main()
{
    if (!(beachlife_cpu_capabilities() & 0x800000))
    {
        std::fprintf(stderr, "The compat CPU must support the retail startup operations\n");
        return 1;
    }
    const auto start = beachlife_startup_timestamp_counter();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto elapsed = beachlife_startup_timestamp_counter() - start;
    if (elapsed < 10000000 || elapsed > 10000000000ULL)
    {
        std::fprintf(stderr, "The monotonic adapter must advance in nanoseconds\n");
        return 1;
    }
    StartupProfiler timer;
    timer.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    timer.Stop();
    if (timer.elapsed == 0 || timer.elapsed > ReadStartupTimestampCounter() - timer.start)
    {
        std::fprintf(stderr, "The retail profiler must subtract its measured overhead\n");
        return 1;
    }
#if defined(__aarch64__) || defined(_M_ARM64)
    if (timer.elapsed / 1000 < 10000 || timer.elapsed / 1000 > 10000000)
    {
        std::fprintf(stderr, "ARM retail /1000 profiling values must be microseconds\n");
        return 1;
    }
#endif
    return 0;
}
