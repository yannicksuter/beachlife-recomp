#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
// Let the host library bind its own wchar_t functions before adding overloads.
// In particular, libstdc++ takes std::wcstod's address while defining stod.
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#endif

// These are Windows UTF-16 units, never the host libc's wchar_t ABI. The
// game uses both unsigned-short arrays and short-wchar literals.
typedef uint16_t MSVCRT_WCHAR;

#ifdef __cplusplus
extern "C"
{
#endif
    char* _strupr(char* text);
    int _stricmp(const char* left, const char* right);
    char* _itoa(int value, char* result, int radix);
    MSVCRT_WCHAR* _itow(int value, MSVCRT_WCHAR* result, int radix);
    int32_t _wtol(const MSVCRT_WCHAR* text);
    int beachlife_crt_vswprintf(void* destination, const void* format, va_list arguments);
    int beachlife_crt_swprintf(void* destination, const void* format, ...);
    FILE* beachlife_crt_fopen(const char* path, const char* mode);
    int beachlife_crt_fseek(FILE* stream, int32_t offset, int origin);
    int32_t beachlife_crt_ftell(FILE* stream);
    int beachlife_crt_fgetpos(FILE* stream, void* position);
#ifdef __cplusplus
}

// Overloads retain the retail CRT spellings without interposing on the host
// libc's 32-bit wide functions or changing std::wstring's ABI.
size_t wcslen(const MSVCRT_WCHAR* text);
MSVCRT_WCHAR* wcscpy(MSVCRT_WCHAR* destination, const MSVCRT_WCHAR* source);
MSVCRT_WCHAR* wcscat(MSVCRT_WCHAR* destination, const MSVCRT_WCHAR* source);
int wcscmp(const MSVCRT_WCHAR* left, const MSVCRT_WCHAR* right);
const MSVCRT_WCHAR* wcsrchr(const MSVCRT_WCHAR* text, int character);
MSVCRT_WCHAR* wcsrchr(MSVCRT_WCHAR* text, int character);
double wcstod(const MSVCRT_WCHAR* text, MSVCRT_WCHAR** end);
int swprintf(MSVCRT_WCHAR* destination, const MSVCRT_WCHAR* format, ...);

// The prelude redirects only game calls. Host code and libc keep their native
// wchar_t ABI; game wchar_t and existing unsigned-short strings both use these
// typed adapters. No short-wchar pointer is ever passed to host wcs functions.
template <typename Character> size_t beachlife_msvc_wcslen(const Character* text)
{
    static_assert(sizeof(Character) == 2);
    const auto* end = text;
    while (*end)
    {
        ++end;
    }
    return static_cast<size_t>(end - text);
}
template <typename Destination, typename Source>
Destination* beachlife_msvc_wcscpy(Destination* destination, const Source* source)
{
    static_assert(sizeof(Destination) == 2 && sizeof(Source) == 2);
    auto* cursor = destination;
    do
    {
        *cursor++ = static_cast<Destination>(*source);
    } while (*source++);
    return destination;
}
template <typename Destination, typename Source>
Destination* beachlife_msvc_wcscat(Destination* destination, const Source* source)
{
    beachlife_msvc_wcscpy(destination + beachlife_msvc_wcslen(destination), source);
    return destination;
}
template <typename Left, typename Right>
int beachlife_msvc_wcscmp(const Left* left, const Right* right)
{
    static_assert(sizeof(Left) == 2 && sizeof(Right) == 2);
    while (*left && *left == *right)
    {
        ++left;
        ++right;
    }
    return static_cast<uint16_t>(*left) - static_cast<int>(static_cast<uint16_t>(*right));
}
template <typename Character> Character* beachlife_msvc_wcsrchr(Character* text, int character)
{
    static_assert(sizeof(Character) == 2);
    Character* result = nullptr;
    do
    {
        if (static_cast<uint16_t>(*text) == static_cast<uint16_t>(character))
        {
            result = text;
        }
    } while (*text++);
    return result;
}
template <typename Character> double beachlife_msvc_wcstod(const Character* text, Character** end)
{
    static_assert(sizeof(Character) == 2);
    try
    {
        std::string ascii;
        for (const auto* cursor = text; *cursor && static_cast<uint16_t>(*cursor) <= 0x7f; ++cursor)
        {
            ascii += static_cast<char>(*cursor);
        }
        char* stop;
        const double value = std::strtod(ascii.c_str(), &stop);
        if (end)
        {
            *end = const_cast<Character*>(text + (stop - ascii.c_str()));
        }
        return value;
    }
    catch (...)
    {
        errno = ENOMEM;
        if (end)
        {
            *end = const_cast<Character*>(text);
        }
        return 0;
    }
}
template <typename Character>
Character* beachlife_msvc_itow(int value, Character* output, int radix)
{
    static_assert(sizeof(Character) == 2);
    char narrow[35];
    if (!output)
    {
        errno = EINVAL;
        return nullptr;
    }
    if (!_itoa(value, narrow, radix))
    {
        return nullptr;
    }
    for (size_t index = 0;; ++index)
    {
        output[index] = static_cast<Character>(narrow[index]);
        if (!narrow[index])
        {
            break;
        }
    }
    return output;
}
template <typename Character> int32_t beachlife_msvc_wtol(const Character* text)
{
    static_assert(sizeof(Character) == 2);
    while (*text == ' ' || (*text >= '\t' && *text <= '\r'))
    {
        ++text;
    }
    const bool negative = *text == '-';
    if (*text == '+' || *text == '-')
    {
        ++text;
    }
    uint32_t value = 0;
    const uint32_t limit = negative ? 0x80000000u : 0x7fffffffu;
    while (*text >= '0' && *text <= '9')
    {
        const unsigned digit = *text++ - '0';
        if (value > (limit - digit) / 10)
        {
            errno = ERANGE;
            return negative ? INT32_MIN : INT32_MAX;
        }
        value = value * 10 + digit;
    }
    return static_cast<int32_t>(negative ? -static_cast<int64_t>(value) : value);
}
template <typename Destination, typename Format, typename... Arguments>
int beachlife_msvc_swprintf(Destination* destination, const Format* format, Arguments... arguments)
{
    static_assert(sizeof(Destination) == 2 && sizeof(Format) == 2);
    return beachlife_crt_swprintf(destination, format, arguments...);
}
#endif
