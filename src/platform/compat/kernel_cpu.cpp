#include <chrono>
#include <cstdint>

#if defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#endif

int beachlife_cpu_capabilities()
{
#if defined(__i386__) || defined(__x86_64__)
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
    {
        return 0;
    }
    return static_cast<int>((eax & 0xf00) | (edx & 0xfffff0ff));
#elif defined(__aarch64__) || defined(_M_ARM64)
    // WinMain requires the MMX bit. The reconstructed operations use portable
    // C++, so this advertises compat support, not a physical ARM instruction.
    return 0x800000;
#else
#error Unsupported boot CPU architecture
#endif
}

std::uint64_t beachlife_startup_timestamp_counter()
{
    // A fixed 1 GHz profiling counter: retail's /1000 consumers see microseconds.
    // GetTickCount remains the independent millisecond game/frame clock.
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
