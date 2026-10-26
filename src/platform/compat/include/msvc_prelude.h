// Included first in every decompiled game source in the boot build. It lets
// the retail code's MSVC spellings compile with the host compiler; the calling
// conventions only matter on 32-bit Windows and have no effect here.
#pragma once

#define BEACHLIFE_COMPAT 1
// Compatibility capabilities consumed by the entry's legacy CPUID check.
int beachlife_cpu_capabilities();

#if !defined(_WIN32)
#define __stdcall
#define __cdecl
#define __fastcall
#define __declspec(x)
#define __forceinline inline
#define __int64 long long
#endif

#include "msvcrt.h"

#define fopen beachlife_crt_fopen
#define fseek beachlife_crt_fseek
#define ftell beachlife_crt_ftell
#define fgetpos beachlife_crt_fgetpos
#define rand beachlife_crt_rand
#define srand beachlife_crt_srand

#define wcslen beachlife_msvc_wcslen
#define wcscpy beachlife_msvc_wcscpy
#define wcscat beachlife_msvc_wcscat
#define wcscmp beachlife_msvc_wcscmp
#define wcsrchr beachlife_msvc_wcsrchr
#define wcstod beachlife_msvc_wcstod
#define swprintf beachlife_msvc_swprintf
#undef _itow
#define _itow beachlife_msvc_itow
#undef _wtol
#define _wtol beachlife_msvc_wtol
