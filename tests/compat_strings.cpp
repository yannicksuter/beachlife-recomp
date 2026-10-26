#include "msvcrt.h"
#include "windows.h"

#include <array>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>

namespace
{
int failures = 0;
void check(bool condition, const char* detail)
{
    if (!condition)
    {
        std::fprintf(stderr, "%s\n", detail);
        ++failures;
    }
}
DWORD message(char* output, DWORD size, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const auto length =
        FormatMessageA(FORMAT_MESSAGE_FROM_STRING, format, 0, 0, output, size, &arguments);
    va_end(arguments);
    return length;
}
} // namespace

int main()
{
    static_assert(sizeof(WCHAR) == 2 && sizeof(LONG) == 4 && sizeof(DWORD) == 4);
    const WCHAR unicode[] = {'A', 0x20ac, 0xd83c, 0xdf34, 0};
    const char utf8[] = "A\xe2\x82\xac\xf0\x9f\x8c\xb4";
    check(
        WideCharToMultiByte(CP_UTF8, 0, unicode, -1, nullptr, 0, nullptr, nullptr) == sizeof(utf8),
        "UTF-8 byte size includes the requested terminator");
    std::array<char, 32> bytes{};
    check(WideCharToMultiByte(CP_UTF8,
              WC_ERR_INVALID_CHARS,
              unicode,
              -1,
              bytes.data(),
              bytes.size(),
              nullptr,
              nullptr) == sizeof(utf8) &&
              std::memcmp(bytes.data(), utf8, sizeof(utf8)) == 0,
        "UTF-16 surrogate pair encodes as one UTF-8 scalar");
    std::array<WCHAR, 16> wide{};
    check(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide.data(), wide.size()) ==
                  5 &&
              std::memcmp(wide.data(), unicode, sizeof(unicode)) == 0,
        "UTF-8 round trip counts UTF-16 units");
    bytes.fill('!');
    check(WideCharToMultiByte(CP_UTF8, 0, unicode, -1, bytes.data(), 2, nullptr, nullptr) == 0 &&
              GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytes[0] == '!',
        "undersized conversion is an error and cannot overwrite the destination");
    const char embedded[] = {'a', 0, 'b'};
    check(MultiByteToWideChar(CP_ACP, 0, embedded, 3, wide.data(), wide.size()) == 3 &&
              wide[0] == 'a' && wide[1] == 0 && wide[2] == 'b',
        "explicit lengths retain embedded nulls without adding a terminator");
    for (const char* invalid : {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82"})
    {
        check(MultiByteToWideChar(
                  CP_UTF8, MB_ERR_INVALID_CHARS, invalid, -1, wide.data(), wide.size()) == 0 &&
                  GetLastError() == ERROR_NO_UNICODE_TRANSLATION,
            "strict UTF-8 rejects overlong, surrogate, out-of-range and truncated inputs");
    }
    check(MultiByteToWideChar(CP_UTF8, 0, "\xe2\x82!", 3, wide.data(), wide.size()) == 2 &&
              wide[0] == 0xfffd && wide[1] == '!',
        "replacement preserves the next valid character");
    const WCHAR lone[] = {0xd800, 0};
    check(WideCharToMultiByte(CP_UTF8,
              WC_ERR_INVALID_CHARS,
              lone,
              -1,
              bytes.data(),
              bytes.size(),
              nullptr,
              nullptr) == 0 &&
              GetLastError() == ERROR_NO_UNICODE_TRANSLATION,
        "strict UTF-16 rejects unpaired surrogates");
    const char ansi[] = {static_cast<char>(0x80), static_cast<char>(0xe9), 0};
    check(MultiByteToWideChar(CP_WINDOWS_1252, 0, ansi, -1, wide.data(), wide.size()) == 3 &&
              wide[0] == 0x20ac && wide[1] == 0xe9 && wide[2] == 0,
        "Windows-1252 punctuation and Latin-1 decode independently of host locale");
    check(WideCharToMultiByte(
              CP_WINDOWS_1252, 0, wide.data(), 3, bytes.data(), bytes.size(), nullptr, nullptr) ==
                  3 &&
              std::memcmp(bytes.data(), ansi, 3) == 0,
        "Windows-1252 round trip");
    check(MultiByteToWideChar(CP_ACP, 0, ansi, -1, wide.data(), wide.size()) == 3 &&
              wide[0] == 0x20ac && wide[1] == 0xe9 &&
              WideCharToMultiByte(
                  CP_ACP, 0, wide.data(), 3, bytes.data(), bytes.size(), nullptr, nullptr) == 3 &&
              std::memcmp(bytes.data(), ansi, 3) == 0,
        "the ANSI code page is the Western releases' Windows-1252");
    beachlife_crt_srand(1);
    const int random_values[] = {beachlife_crt_rand(), beachlife_crt_rand(), beachlife_crt_rand()};
    check(random_values[0] == 41 && random_values[1] == 18467 && random_values[2] == 6334,
        "rand follows the MSVCRT sequence");
    const WCHAR unmappable[] = {0x4e00, 0};
    BOOL replaced = FALSE;
    check(WideCharToMultiByte(CP_WINDOWS_1252,
              WC_NO_BEST_FIT_CHARS,
              unmappable,
              -1,
              bytes.data(),
              bytes.size(),
              "!",
              &replaced) == 2 &&
              replaced && bytes[0] == '!',
        "explicit no-best-fit conversion reports use of the default character");

    check(std::strcmp(_itoa(INT_MIN, bytes.data(), 10), "-2147483648") == 0,
        "decimal conversion handles the most negative 32-bit value");
    check(std::strcmp(_itoa(-1, bytes.data(), 16), "ffffffff") == 0,
        "non-decimal conversion treats negative integers as unsigned 32-bit words");
    _itow(INT_MIN, wide.data(), 10);
    check(_wtol(wide.data()) == INT_MIN && wcslen(wide.data()) == 11,
        "wide integer conversion round trip");
    const WCHAR overflow[] = {'2', '1', '4', '7', '4', '8', '3', '6', '4', '8', 0};
    errno = 0;
    check(_wtol(overflow) == INT_MAX && errno == ERANGE, "wide long remains 32-bit on LP64 hosts");
    const WCHAR first[] = {'a', 'b', 0};
    const WCHAR second[] = {0x20ac, 0};
    wcscpy(wide.data(), first);
    wcscat(wide.data(), second);
    check(wcslen(wide.data()) == 3 && wide[2] == 0x20ac && wcscmp(first, second) < 0 &&
              wcsrchr(wide.data(), 'b') == wide.data() + 1 &&
              wcsrchr(wide.data(), 0) == wide.data() + 3,
        "wide string operations traverse 16-bit units and include the terminating search");
    const WCHAR floating[] = {' ', '-', '1', '.', '2', '5', 'e', '2', 'x', 0};
    WCHAR* end = nullptr;
    check(wcstod(floating, &end) == -125 && end == floating + 8,
        "wide numeric parsing retains its end pointer");
    char text[] = "BeachLife";
    check(_strupr(text) == text && std::strcmp(text, "BEACHLIFE") == 0 &&
              _stricmp(text, "beachlife") == 0,
        "C-locale ANSI case operations");
    check(MulDiv(-3, 1, 2) == -2 && MulDiv(3, 1, -2) == -2 && MulDiv(INT_MIN, 1, 1) == INT_MIN &&
              MulDiv(INT_MAX, 2, 1) == -1 && MulDiv(1, 1, 0) == -1,
        "MulDiv rounds away from zero and detects overflow");
    check(message(bytes.data(), bytes.size(), "%2!s!: %1%n%%", "first", "second") == 16 &&
              std::strcmp(bytes.data(), "second: first\r\n%") == 0,
        "FormatMessage parses reordered string inserts and percent controls");
    check(FormatMessageA(
              FORMAT_MESSAGE_FROM_STRING, "done%0%99", 0, 0, bytes.data(), bytes.size(), nullptr) ==
                  4 &&
              std::strcmp(bytes.data(), "done") == 0,
        "FormatMessage stops scanning inserts at percent zero");
    const uintptr_t inserts[] = {reinterpret_cast<uintptr_t>("array")};
    check(FormatMessageA(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY,
              "%1",
              0,
              0,
              bytes.data(),
              bytes.size(),
              reinterpret_cast<va_list*>(const_cast<uintptr_t*>(inserts))) == 5 &&
              std::strcmp(bytes.data(), "array") == 0,
        "FormatMessage argument arrays use host pointer width");
    check(FormatMessageA(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_IGNORE_INSERTS,
              "%99!d!",
              0,
              0,
              bytes.data(),
              bytes.size(),
              nullptr) == 6 &&
              std::strcmp(bytes.data(), "%99!d!") == 0,
        "ignored inserts are preserved without consuming arguments");
    char* allocated = nullptr;
    check(FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
              nullptr,
              ERROR_ACCESS_DENIED,
              0,
              reinterpret_cast<char*>(&allocated),
              0,
              nullptr) == 19 &&
              allocated && std::strcmp(allocated, "Access is denied.\r\n") == 0,
        "system messages allocate caller-owned output");
    check(LocalFree(allocated) == nullptr, "LocalFree releases allocated message buffers");
    check(message(bytes.data(), 3, "long") == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "FormatMessage reports a short destination");
    return failures ? 1 : 0;
}
