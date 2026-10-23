#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace beachlife::compat
{
// The retail ANSI interfaces use Windows-1252. Keep it independent of the
// host locale (in particular, UTF-8 on Unix).
constexpr std::array<std::uint32_t, 32> ansi_punctuation{0x20ac,
    0x81,
    0x201a,
    0x192,
    0x201e,
    0x2026,
    0x2020,
    0x2021,
    0x2c6,
    0x2030,
    0x160,
    0x2039,
    0x152,
    0x8d,
    0x17d,
    0x8f,
    0x90,
    0x2018,
    0x2019,
    0x201c,
    0x201d,
    0x2022,
    0x2013,
    0x2014,
    0x2dc,
    0x2122,
    0x161,
    0x203a,
    0x153,
    0x9d,
    0x17e,
    0x178};
constexpr std::uint32_t ansi_codepoint(unsigned char byte)
{
    return byte >= 0x80 && byte <= 0x9f ? ansi_punctuation[byte - 0x80] : byte;
}
inline std::string ansi_to_utf8(std::string_view text)
{
    std::string result;
    for (const unsigned char byte : text)
    {
        const auto code = ansi_codepoint(byte);
        if (code < 0x80)
        {
            result += static_cast<char>(code);
        }
        else if (code < 0x800)
        {
            result += static_cast<char>(0xc0 | code >> 6);
            result += static_cast<char>(0x80 | (code & 0x3f));
        }
        else
        {
            result += static_cast<char>(0xe0 | code >> 12);
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (code & 0x3f));
        }
    }
    return result;
}
// SDL supplies valid Unicode, but reject malformed UTF-8 as well as characters
// that cannot be represented in the retail ANSI message queue.
inline bool utf8_to_ansi(std::string_view text, std::string& result)
{
    std::string converted;
    for (std::size_t offset = 0; offset < text.size();)
    {
        const unsigned char first = text[offset++];
        std::uint32_t code = first;
        unsigned tails = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            tails = 1;
            code &= 0x1f;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            tails = 2;
            code &= 0xf;
        }
        else if (first >= 0x80)
        {
            return false;
        }
        if (tails > text.size() - offset)
        {
            return false;
        }
        for (unsigned i = 0; i < tails; ++i)
        {
            const unsigned char tail = text[offset++];
            if ((tail & 0xc0) != 0x80)
            {
                return false;
            }
            code = (code << 6) | (tail & 0x3f);
        }
        if ((tails == 1 && code < 0x80) || (tails == 2 && code < 0x800))
        {
            return false;
        }
        int byte = -1;
        if (code < 0x80 || (code >= 0xa0 && code <= 0xff))
        {
            byte = code;
        }
        else
        {
            for (unsigned i = 0; i < ansi_punctuation.size(); ++i)
            {
                if (ansi_punctuation[i] == code)
                {
                    byte = 0x80 + i;
                    break;
                }
            }
        }
        if (byte < 0)
        {
            return false;
        }
        converted += static_cast<char>(byte);
    }
    result = std::move(converted);
    return true;
}
} // namespace beachlife::compat
