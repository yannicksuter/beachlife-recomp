#include "kernel_internal.h"
#include "msvcrt.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{
using beachlife::compat::unsupported;
using beachlife::compat::kernel::guard;
using beachlife::compat::kernel::last_error;

constexpr std::array<WCHAR, 32> cp1252 = {0x20ac,
    0x0081,
    0x201a,
    0x0192,
    0x201e,
    0x2026,
    0x2020,
    0x2021,
    0x02c6,
    0x2030,
    0x0160,
    0x2039,
    0x0152,
    0x008d,
    0x017d,
    0x008f,
    0x0090,
    0x2018,
    0x2019,
    0x201c,
    0x201d,
    0x2022,
    0x2013,
    0x2014,
    0x02dc,
    0x2122,
    0x0161,
    0x203a,
    0x0153,
    0x009d,
    0x017e,
    0x0178};

void code_page(UINT page, const char* api)
{
    if (page != CP_ACP && page != CP_WINDOWS_1252 && page != CP_UTF8)
    {
        unsupported(api, "code page is not implemented");
    }
}

void append_utf8(std::string& result, uint32_t code)
{
    if (code < 0x80)
    {
        result += static_cast<char>(code);
    }
    else if (code < 0x800)
    {
        result += static_cast<char>(0xc0 | (code >> 6));
        result += static_cast<char>(0x80 | (code & 0x3f));
    }
    else if (code < 0x10000)
    {
        result += static_cast<char>(0xe0 | (code >> 12));
        result += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        result += static_cast<char>(0x80 | (code & 0x3f));
    }
    else
    {
        result += static_cast<char>(0xf0 | (code >> 18));
        result += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
        result += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        result += static_cast<char>(0x80 | (code & 0x3f));
    }
}

template <typename Character> Character* integer(int value, Character* result, int radix)
{
    if (!result || radix < 2 || radix > 36)
    {
        errno = EINVAL;
        if (result)
        {
            *result = 0;
        }
        return nullptr;
    }
    const bool negative = radix == 10 && value < 0;
    uint32_t magnitude =
        negative ? 0u - static_cast<uint32_t>(value) : static_cast<uint32_t>(value);
    Character* cursor = result;
    if (negative)
    {
        *cursor++ = '-';
    }
    Character* first = cursor;
    do
    {
        *cursor++ =
            static_cast<Character>("0123456789abcdefghijklmnopqrstuvwxyz"[magnitude % radix]);
        magnitude /= radix;
    } while (magnitude);
    *cursor = 0;
    std::reverse(first, cursor);
    return result;
}

char ascii_lower(char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}
} // namespace

extern "C"
{
    int WINAPI WideCharToMultiByte(UINT page,
        DWORD flags,
        LPCWSTR wide,
        int length,
        LPSTR narrow,
        int capacity,
        LPCSTR fallback,
        LPBOOL used_fallback)
    {
        return guard(
            [&]() -> int
            {
                code_page(page, "WideCharToMultiByte");
                // The game's Western releases ran on Windows whose ANSI code
                // page is 1252, so that is the ACP the retail text expects.
                if (page == CP_ACP)
                {
                    page = CP_WINDOWS_1252;
                }
                if (!wide || length == 0 || length < -1 || capacity < 0 || (capacity && !narrow) ||
                    (page == CP_UTF8 && (fallback || used_fallback)))
                {
                    last_error = ERROR_INVALID_PARAMETER;
                    return 0;
                }
                const DWORD allowed = page == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
                if (flags & ~allowed)
                {
                    unsupported("WideCharToMultiByte", "conversion flags are not implemented");
                }
                if (length == -1)
                {
                    const auto units = wcslen(wide) + 1;
                    if (units > INT_MAX)
                    {
                        last_error = ERROR_INVALID_PARAMETER;
                        return 0;
                    }
                    length = static_cast<int>(units);
                }
                std::string result;
                bool used = false;
                for (int index = 0; index < length; ++index)
                {
                    uint32_t code = wide[index];
                    if (code >= 0xd800 && code <= 0xdbff && index + 1 < length &&
                        wide[index + 1] >= 0xdc00 && wide[index + 1] <= 0xdfff)
                    {
                        code = 0x10000 + ((code - 0xd800) << 10) + (wide[++index] - 0xdc00);
                    }
                    else if (code >= 0xd800 && code <= 0xdfff)
                    {
                        if (flags & WC_ERR_INVALID_CHARS)
                        {
                            last_error = ERROR_NO_UNICODE_TRANSLATION;
                            return 0;
                        }
                        code = 0xfffd;
                    }
                    if (page == CP_UTF8)
                    {
                        append_utf8(result, code);
                        continue;
                    }
                    if (code < 0x80 || (code >= 0xa0 && code <= 0xff))
                    {
                        result += static_cast<char>(code);
                    }
                    else
                    {
                        const auto found = std::find(cp1252.begin(), cp1252.end(), code);
                        if (found != cp1252.end())
                        {
                            result += static_cast<char>(0x80 + (found - cp1252.begin()));
                        }
                        else
                        {
                            if (!(flags & WC_NO_BEST_FIT_CHARS))
                            {
                                unsupported("WideCharToMultiByte",
                                    "ANSI best-fit conversion is not implemented");
                            }
                            result += fallback ? *fallback : '?';
                            used = true;
                        }
                    }
                }
                if (result.size() > INT_MAX)
                {
                    last_error = ERROR_INVALID_PARAMETER;
                    return 0;
                }
                if (used_fallback)
                {
                    *used_fallback = used ? TRUE : FALSE;
                }
                if (capacity && result.size() > static_cast<size_t>(capacity))
                {
                    last_error = ERROR_INSUFFICIENT_BUFFER;
                    return 0;
                }
                if (capacity)
                {
                    std::memcpy(narrow, result.data(), result.size());
                }
                return static_cast<int>(result.size());
            },
            0);
    }

    int WINAPI MultiByteToWideChar(
        UINT page, DWORD flags, LPCSTR narrow, int length, LPWSTR wide, int capacity)
    {
        return guard(
            [&]() -> int
            {
                code_page(page, "MultiByteToWideChar");
                // The game's Western releases ran on Windows whose ANSI code
                // page is 1252, so that is the ACP the retail text expects.
                if (page == CP_ACP)
                {
                    page = CP_WINDOWS_1252;
                }
                if (!narrow || length == 0 || length < -1 || capacity < 0 || (capacity && !wide))
                {
                    last_error = ERROR_INVALID_PARAMETER;
                    return 0;
                }
                const DWORD allowed = MB_ERR_INVALID_CHARS | (page == CP_UTF8 ? 0 : MB_PRECOMPOSED);
                if (flags & ~allowed)
                {
                    unsupported("MultiByteToWideChar", "conversion flags are not implemented");
                }
                if (length == -1)
                {
                    const auto bytes = std::strlen(narrow) + 1;
                    if (bytes > INT_MAX)
                    {
                        last_error = ERROR_INVALID_PARAMETER;
                        return 0;
                    }
                    length = static_cast<int>(bytes);
                }
                std::vector<WCHAR> result;
                for (int index = 0; index < length;)
                {
                    const auto lead = static_cast<unsigned char>(narrow[index++]);
                    uint32_t code = lead;
                    if (page == CP_WINDOWS_1252 && lead >= 0x80 && lead <= 0x9f)
                    {
                        code = cp1252[lead - 0x80];
                    }
                    if (page == CP_UTF8 && lead >= 0x80)
                    {
                        const int count = lead >= 0xc2 && lead <= 0xdf   ? 1
                                          : lead >= 0xe0 && lead <= 0xef ? 2
                                          : lead >= 0xf0 && lead <= 0xf4 ? 3
                                                                         : 0;
                        code = lead & (0x7f >> count);
                        bool valid = count != 0;
                        int consumed = 0;
                        while (consumed < count && index + consumed < length)
                        {
                            const auto next = static_cast<unsigned char>(narrow[index + consumed]);
                            if ((next & 0xc0) != 0x80 ||
                                (consumed == 0 && ((lead == 0xe0 && next < 0xa0) ||
                                                      (lead == 0xed && next >= 0xa0) ||
                                                      (lead == 0xf0 && next < 0x90) ||
                                                      (lead == 0xf4 && next >= 0x90))))
                            {
                                break;
                            }
                            code = (code << 6) | (next & 0x3f);
                            ++consumed;
                        }
                        valid = valid && consumed == count;
                        if (!valid)
                        {
                            if (flags & MB_ERR_INVALID_CHARS)
                            {
                                last_error = ERROR_NO_UNICODE_TRANSLATION;
                                return 0;
                            }
                            code = 0xfffd;
                        }
                        index += consumed;
                    }
                    if (code > 0xffff)
                    {
                        result.push_back(static_cast<WCHAR>(0xd800 + ((code - 0x10000) >> 10)));
                        result.push_back(static_cast<WCHAR>(0xdc00 + ((code - 0x10000) & 0x3ff)));
                    }
                    else
                    {
                        result.push_back(static_cast<WCHAR>(code));
                    }
                }
                if (result.size() > INT_MAX)
                {
                    last_error = ERROR_INVALID_PARAMETER;
                    return 0;
                }
                if (capacity && result.size() > static_cast<size_t>(capacity))
                {
                    last_error = ERROR_INSUFFICIENT_BUFFER;
                    return 0;
                }
                if (capacity)
                {
                    std::copy(result.begin(), result.end(), wide);
                }
                return static_cast<int>(result.size());
            },
            0);
    }

    char* _strupr(char* text)
    {
        for (char* cursor = text; *cursor; ++cursor)
        {
            if (*cursor >= 'a' && *cursor <= 'z')
            {
                *cursor = static_cast<char>(*cursor - ('a' - 'A'));
            }
        }
        return text;
    }

    int _stricmp(const char* left, const char* right)
    {
        while (*left && ascii_lower(*left) == ascii_lower(*right))
        {
            ++left;
            ++right;
        }
        return static_cast<unsigned char>(ascii_lower(*left)) -
               static_cast<unsigned char>(ascii_lower(*right));
    }

    char* _itoa(int value, char* result, int radix)
    {
        return integer(value, result, radix);
    }
    MSVCRT_WCHAR* _itow(int value, MSVCRT_WCHAR* result, int radix)
    {
        return integer(value, result, radix);
    }

    int32_t _wtol(const MSVCRT_WCHAR* text)
    {
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
}

size_t wcslen(const MSVCRT_WCHAR* text)
{
    const auto* end = text;
    while (*end)
    {
        ++end;
    }
    return static_cast<size_t>(end - text);
}

MSVCRT_WCHAR* wcscpy(MSVCRT_WCHAR* destination, const MSVCRT_WCHAR* source)
{
    auto* cursor = destination;
    do
    {
        *cursor++ = *source;
    } while (*source++);
    return destination;
}

MSVCRT_WCHAR* wcscat(MSVCRT_WCHAR* destination, const MSVCRT_WCHAR* source)
{
    wcscpy(destination + wcslen(destination), source);
    return destination;
}

int wcscmp(const MSVCRT_WCHAR* left, const MSVCRT_WCHAR* right)
{
    while (*left && *left == *right)
    {
        ++left;
        ++right;
    }
    return static_cast<int>(*left) - static_cast<int>(*right);
}

const MSVCRT_WCHAR* wcsrchr(const MSVCRT_WCHAR* text, int character)
{
    const MSVCRT_WCHAR* result = nullptr;
    do
    {
        if (*text == static_cast<MSVCRT_WCHAR>(character))
        {
            result = text;
        }
    } while (*text++);
    return result;
}

MSVCRT_WCHAR* wcsrchr(MSVCRT_WCHAR* text, int character)
{
    return const_cast<MSVCRT_WCHAR*>(wcsrchr(static_cast<const MSVCRT_WCHAR*>(text), character));
}

double wcstod(const MSVCRT_WCHAR* text, MSVCRT_WCHAR** end)
{
    return guard(
        [&]()
        {
            std::string ascii;
            for (const auto* cursor = text; *cursor && *cursor <= 0x7f; ++cursor)
            {
                ascii += static_cast<char>(*cursor);
            }
            char* stop;
            const double result = std::strtod(ascii.c_str(), &stop);
            if (end)
            {
                *end = const_cast<MSVCRT_WCHAR*>(text + (stop - ascii.c_str()));
            }
            return result;
        },
        0.0);
}
