#include "kernel_internal.h"
#include "msvcrt.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using beachlife::compat::unsupported;
using beachlife::compat::kernel::guard;

// Formats can be either wchar_t (on the short-wchar game side) or unsigned
// short arrays loaded from a language file. Access their representation using
// memcpy, without assuming an array lifetime or aliasing between those types.
uint16_t unit(const void* text, std::size_t index)
{
    uint16_t value;
    std::memcpy(&value, static_cast<const unsigned char*>(text) + index * 2, 2);
    return value;
}

template <typename Value> std::string numeric(const std::string& format, Value value)
{
    const int length = std::snprintf(nullptr, 0, format.c_str(), value);
    if (length < 0)
    {
        throw std::runtime_error("Numeric formatting failed");
    }
    std::vector<char> bytes(static_cast<std::size_t>(length) + 1);
    if (std::snprintf(bytes.data(), bytes.size(), format.c_str(), value) != length)
    {
        throw std::runtime_error("Numeric formatting changed size");
    }
    return std::string(bytes.data(), static_cast<std::size_t>(length));
}

int decimal(const void* format, std::size_t& position)
{
    int result = 0;
    while (unit(format, position) >= '0' && unit(format, position) <= '9')
    {
        const int digit = unit(format, position++) - '0';
        if (result > (INT_MAX - digit) / 10)
        {
            throw std::invalid_argument("Format width or precision overflows");
        }
        result = result * 10 + digit;
    }
    return result;
}

int format_wide(void* destination, const void* format, va_list input)
{
    if (!destination || !format)
    {
        errno = EINVAL;
        return -1;
    }
    struct Arguments
    {
        va_list value;
        explicit Arguments(va_list input)
        {
            va_copy(value, input);
        }
        ~Arguments()
        {
            va_end(value);
        }
    } arguments(input);
    auto& args = arguments.value;
    std::vector<uint16_t> result;
    for (std::size_t position = 0; unit(format, position);)
    {
        uint16_t character = unit(format, position++);
        if (character != '%')
        {
            result.push_back(character);
            continue;
        }
        if (unit(format, position) == '%')
        {
            result.push_back('%');
            ++position;
            continue;
        }
        std::string flags;
        while (unit(format, position) && unit(format, position) < 128 &&
               std::strchr("-+ #0", unit(format, position)))
        {
            flags += static_cast<char>(unit(format, position++));
        }
        int width;
        if (unit(format, position) == '*')
        {
            ++position;
            width = va_arg(args, int);
            if (width == INT_MIN)
            {
                throw std::invalid_argument("Invalid field width");
            }
            if (width < 0)
            {
                flags += '-';
                width = -width;
            }
        }
        else
        {
            width = decimal(format, position);
        }
        int precision = -1;
        if (unit(format, position) == '.')
        {
            ++position;
            if (unit(format, position) == '*')
            {
                ++position;
                precision = va_arg(args, int);
            }
            else
            {
                precision = decimal(format, position);
            }
        }
        std::string length;
        if (unit(format, position) == 'h' || unit(format, position) == 'l')
        {
            length += static_cast<char>(unit(format, position++));
            if (unit(format, position) == length[0])
            {
                length += static_cast<char>(unit(format, position++));
            }
        }
        else if (unit(format, position) == 'I' && unit(format, position + 1) == '6' &&
                 unit(format, position + 2) == '4')
        {
            length = "ll";
            position += 3;
        }
        character = unit(format, position);
        if (!character)
        {
            unsupported("swprintf", "unterminated format conversion");
        }
        ++position;
        std::vector<uint16_t> field;
        if (character == 's' || character == 'S')
        {
            const bool narrow = length == "h" || (character == 'S' && length != "l");
            const void* text = va_arg(args, const void*);
            if (!text)
            {
                for (char ch : std::string("(null)"))
                {
                    field.push_back(ch);
                }
            }
            else
            {
                for (std::size_t index = 0;
                    precision < 0 || index < static_cast<unsigned>(precision);
                    ++index)
                {
                    const uint16_t next =
                        narrow ? static_cast<const unsigned char*>(text)[index] : unit(text, index);
                    if (!next)
                    {
                        break;
                    }
                    if (narrow && next > 0x7f)
                    {
                        unsupported("swprintf",
                            "non-ASCII ANSI string needs an explicit Windows code page");
                    }
                    field.push_back(next);
                }
            }
        }
        else if (character == 'c' || character == 'C')
        {
            const uint16_t value = static_cast<uint16_t>(va_arg(args, int));
            if ((length == "h" || (character == 'C' && length != "l")) && value > 0x7f)
            {
                unsupported(
                    "swprintf", "non-ASCII ANSI character needs an explicit Windows code page");
            }
            field.push_back(value);
        }
        else
        {
            std::string specifier = '%' + flags;
            if (width)
            {
                specifier += std::to_string(width);
            }
            if (precision >= 0)
            {
                specifier += '.' + std::to_string(precision);
            }
            std::string bytes;
            if (character == 'd' || character == 'i')
            {
                long long value;
                if (length == "ll")
                {
                    value = va_arg(args, long long);
                }
                else if (length == "l")
                {
                    value = static_cast<int32_t>(va_arg(args, long));
                }
                else
                {
                    value = va_arg(args, int);
                    if (length == "h")
                    {
                        value = static_cast<int16_t>(value);
                    }
                    if (length == "hh")
                    {
                        value = static_cast<int8_t>(value);
                    }
                }
                bytes = numeric(specifier + "ll" + static_cast<char>(character), value);
            }
            else if (character == 'u' || character == 'o' || character == 'x' || character == 'X')
            {
                unsigned long long value;
                if (length == "ll")
                {
                    value = va_arg(args, unsigned long long);
                }
                else if (length == "l")
                {
                    value = static_cast<uint32_t>(va_arg(args, unsigned long));
                }
                else
                {
                    value = va_arg(args, unsigned int);
                    if (length == "h")
                    {
                        value = static_cast<uint16_t>(value);
                    }
                    if (length == "hh")
                    {
                        value = static_cast<uint8_t>(value);
                    }
                }
                bytes = numeric(specifier + "ll" + static_cast<char>(character), value);
            }
            else if (character < 128 && std::strchr("eEfFgG", character) &&
                     (length.empty() || length == "l"))
            {
                bytes = numeric(specifier + static_cast<char>(character), va_arg(args, double));
            }
            else
            {
                unsupported("swprintf", "format conversion is not implemented");
            }
            for (unsigned char byte : bytes)
            {
                field.push_back(byte);
            }
            // snprintf has already applied the numeric field width.
            width = 0;
        }
        const auto padding = width > 0 && static_cast<std::size_t>(width) > field.size()
                                 ? static_cast<std::size_t>(width) - field.size()
                                 : 0;
        const bool left = flags.find('-') != std::string::npos;
        if (!left)
        {
            result.insert(result.end(), padding, ' ');
        }
        result.insert(result.end(), field.begin(), field.end());
        if (left)
        {
            result.insert(result.end(), padding, ' ');
        }
    }
    if (result.size() > INT_MAX)
    {
        errno = EOVERFLOW;
        return -1;
    }
    const auto count = static_cast<int>(result.size());
    result.push_back(0);
    std::memcpy(destination, result.data(), result.size() * sizeof(uint16_t));
    return count;
}
} // namespace

extern "C" int beachlife_crt_vswprintf(void* destination, const void* format, va_list arguments)
{
    return guard(
        [&]()
        {
            return format_wide(destination, format, arguments);
        },
        -1);
}

extern "C" int beachlife_crt_swprintf(void* destination, const void* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int result = beachlife_crt_vswprintf(destination, format, arguments);
    va_end(arguments);
    return result;
}

int swprintf(MSVCRT_WCHAR* destination, const MSVCRT_WCHAR* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int result = beachlife_crt_vswprintf(destination, format, arguments);
    va_end(arguments);
    return result;
}
