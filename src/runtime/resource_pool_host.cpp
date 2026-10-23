#include <atomic>
#include <chrono>
#include <thread>

// The prepared pool archive currently declares these private C symbol names.
// Supply real host operations without importing its Windows test macro header.
// This preserves that archive's existing 32-bit int/unsigned-int platform ABI.
static_assert(sizeof(int) == 4 && sizeof(unsigned int) == 4);
static_assert(alignof(int) >= std::atomic_ref<int>::required_alignment);

extern "C" int BeachlifePoolLockTestExchange(int* target, int value)
{
    return std::atomic_ref<int>(*target).exchange(value, std::memory_order_seq_cst);
}

extern "C" void BeachlifePoolLockTestSleep(unsigned int milliseconds)
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
