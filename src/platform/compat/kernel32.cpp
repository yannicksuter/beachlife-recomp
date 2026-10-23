#include "kernel_internal.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <thread>

// KERNEL32 functions with a direct host equivalent.

namespace beachlife::compat::kernel
{
thread_local DWORD last_error = 0;
} // namespace beachlife::compat::kernel

using beachlife::compat::kernel::last_error;

extern "C"
{
    DWORD WINAPI GetTickCount(void)
    {
        return static_cast<DWORD>(SDL_GetTicks());
    }

    void WINAPI Sleep(DWORD milliseconds)
    {
        if (milliseconds == 0)
        {
            std::this_thread::yield();
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        }
    }

    DWORD WINAPI GetLastError(void)
    {
        return last_error;
    }

    void WINAPI SetLastError(DWORD error)
    {
        last_error = error;
    }

    LONG WINAPI InterlockedExchange(LONG* target, LONG value)
    {
        return std::atomic_ref<LONG>(*target).exchange(value);
    }

    // Windows rounds half away from zero and returns -1 for a zero denominator.
    int WINAPI MulDiv(int number, int numerator, int denominator)
    {
        if (denominator == 0)
        {
            return -1;
        }
        const long long product = static_cast<long long>(number) * numerator;
        const long long magnitude = product < 0 ? -product : product;
        const long long divisor =
            denominator < 0 ? -static_cast<long long>(denominator) : denominator;
        const long long rounded = (magnitude + divisor / 2) / divisor;
        const long long result = (product < 0) != (denominator < 0) ? -rounded : rounded;
        if (result > INT32_MAX || result < INT32_MIN)
        {
            return -1;
        }
        return static_cast<int>(result);
    }

} // extern "C"
