#include "windows.h"

#include <climits>
#include <cstdio>

int main()
{
    static_assert(sizeof(wchar_t) == 2);
    wchar_t text[128];
    if (swprintf(text, L"%d %02d:%02d %.2f", 3, 7, 5, 1234.5) != 15 ||
        wcscmp(text, L"3 07:05 1234.50") != 0)
    {
        return 1;
    }
    if (swprintf(text, L"%*.*f|%-5.3s|%S|%I64d", 6, 1, 2.5, L"abcd", "ascii", 1234567890123LL) !=
            32 ||
        wcscmp(text, L"   2.5|abc  |ascii|1234567890123") != 0)
    {
        return 2;
    }
    if (swprintf(text, L"%d %x", INT_MIN, 0xabcdefu) != 18 ||
        wcscmp(text, L"-2147483648 abcdef") != 0)
    {
        return 3;
    }
    // A language-file format is a 16-bit unsigned-short array, while literals
    // and Game::ReadText's output are short wchar_t arrays.
    const unsigned short format[] = {'%', '0', '2', 'd', ':', '%', '0', '2', 'd', 0};
    unsigned short save[32];
    if (swprintf(save, format, 7, 3) != 5 || save[0] != '0' || save[4] != '3' || save[5] != 0)
    {
        return 4;
    }
    wcscpy(text, L"wide");
    wcscat(text, L" \U0001f334");
    if (wcslen(text) != 7 || wcsrchr(text, ' ') != text + 4)
    {
        return 5;
    }
    if (swprintf(text, L"%s", L"\U0001f334") != 2 || text[0] != 0xd83c || text[1] != 0xdf34 ||
        text[2])
    {
        return 6;
    }
    _itow(INT_MIN, text, 10);
    if (_wtol(text) != INT_MIN)
    {
        return 7;
    }
    wchar_t* end;
    if (wcstod(L"12.5x", &end) != 12.5 || *end != 'x')
    {
        return 8;
    }
    return 0;
}
