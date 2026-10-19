#ifndef BEACHLIFE_TEST_POOL_LOCK_WINDOWS_H
#define BEACHLIFE_TEST_POOL_LOCK_WINDOWS_H

// Test-only provider for the observed 32-bit platform contract. These symbols
// are implemented by the deterministic fixture, with no operating-system calls.
using LONG = int;
using DWORD = unsigned int;
static_assert(sizeof(LONG) == 4 && sizeof(DWORD) == 4);
// Keep generated providers distinct from system imports used by the host CRT.
#define InterlockedExchange BeachlifePoolLockTestExchange
#define Sleep BeachlifePoolLockTestSleep
extern "C" LONG InterlockedExchange(LONG* target, LONG value);
extern "C" void Sleep(DWORD milliseconds);

#endif
