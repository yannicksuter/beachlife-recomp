#include "kernel_internal.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace
{
using namespace beachlife::compat::kernel;
const char* system_message(DWORD message)
{
    switch (message)
    {
    case ERROR_SUCCESS:
        return "The operation completed successfully.\r\n";
    case ERROR_FILE_NOT_FOUND:
        return "The system cannot find the file specified.\r\n";
    case ERROR_PATH_NOT_FOUND:
        return "The system cannot find the path specified.\r\n";
    case ERROR_TOO_MANY_OPEN_FILES:
        return "The system cannot open the file.\r\n";
    case ERROR_ACCESS_DENIED:
        return "Access is denied.\r\n";
    case ERROR_INVALID_HANDLE:
        return "The handle is invalid.\r\n";
    case ERROR_NOT_ENOUGH_MEMORY:
        return "Not enough memory resources are available to process this command.\r\n";
    case ERROR_INVALID_DATA:
        return "The data is invalid.\r\n";
    case ERROR_NO_MORE_FILES:
        return "There are no more files.\r\n";
    case ERROR_WRITE_FAULT:
        return "The system cannot write to the specified device.\r\n";
    case ERROR_READ_FAULT:
        return "The system cannot read from the specified device.\r\n";
    case ERROR_GEN_FAILURE:
        return "A device attached to the system is not functioning.\r\n";
    case ERROR_SHARING_VIOLATION:
        return "The process cannot access the file because it is being used by another "
               "process.\r\n";
    case ERROR_INVALID_PARAMETER:
        return "The parameter is incorrect.\r\n";
    case ERROR_INSUFFICIENT_BUFFER:
        return "The data area passed to a system call is too small.\r\n";
    case ERROR_INVALID_NAME:
        return "The filename, directory name, or volume label syntax is incorrect.\r\n";
    case ERROR_DIR_NOT_EMPTY:
        return "The directory is not empty.\r\n";
    case ERROR_ALREADY_EXISTS:
        return "Cannot create a file when that file already exists.\r\n";
    case ERROR_NOT_OWNER:
        return "Attempt to release mutex not owned by caller.\r\n";
    case ERROR_NO_UNICODE_TRANSLATION:
        return "No mapping for the Unicode character exists in the target multi-byte code "
               "page.\r\n";
    default:
        beachlife::compat::unsupported(
            "FormatMessageA", "system message is not in the host catalog");
    }
}

std::string expand(std::string_view source, DWORD flags, va_list* arguments)
{
    std::array<const char*, 100> values{};
    if (!(flags & FORMAT_MESSAGE_IGNORE_INSERTS))
    {
        unsigned maximum = 0;
        for (std::size_t index = 0; index < source.size(); ++index)
        {
            if (source[index] != '%' || ++index == source.size())
            {
                continue;
            }
            if (source[index] == '0')
            {
                break;
            }
            if (source[index] >= '1' && source[index] <= '9')
            {
                unsigned insert = source[index] - '0';
                if (index + 1 < source.size() && source[index + 1] >= '0' &&
                    source[index + 1] <= '9')
                {
                    insert = insert * 10 + (source[++index] - '0');
                }
                maximum = std::max(maximum, insert);
                if (index + 1 < source.size() && source[index + 1] == '!')
                {
                    const auto end = source.find('!', index + 2);
                    if (end == std::string_view::npos)
                    {
                        throw std::invalid_argument("Invalid message insert");
                    }
                    const auto format = source.substr(index + 2, end - index - 2);
                    if (!format.empty() && format != "s" && format != "hs")
                    {
                        beachlife::compat::unsupported(
                            "FormatMessageA", "non-string insert formatting");
                    }
                    index = end;
                }
            }
        }
        if (maximum && !arguments)
        {
            throw std::invalid_argument("Missing message insert arguments");
        }
        if (maximum && (flags & FORMAT_MESSAGE_ARGUMENT_ARRAY))
        {
            const auto* array = reinterpret_cast<const uintptr_t*>(arguments);
            for (unsigned index = 1; index <= maximum; ++index)
            {
                values[index] = reinterpret_cast<const char*>(array[index - 1]);
            }
        }
        else if (maximum)
        {
            va_list copy;
            va_copy(copy, *arguments);
            for (unsigned index = 1; index <= maximum; ++index)
            {
                values[index] = va_arg(copy, const char*);
            }
            va_end(copy);
        }
    }
    std::string result;
    for (std::size_t index = 0; index < source.size();)
    {
        char ch = source[index++];
        if (ch != '%')
        {
            if ((flags & FORMAT_MESSAGE_MAX_WIDTH_MASK) == 255 && (ch == '\r' || ch == '\n'))
            {
                if (ch == '\r' && index < source.size() && source[index] == '\n')
                {
                    ++index;
                }
                result += ' ';
            }
            else
            {
                result += ch;
            }
            continue;
        }
        if (index == source.size())
        {
            result += '%';
            break;
        }
        ch = source[index++];
        if (ch == '0')
        {
            break;
        }
        if (ch >= '1' && ch <= '9')
        {
            const auto start = index - 2;
            unsigned insert = ch - '0';
            if (index < source.size() && source[index] >= '0' && source[index] <= '9')
            {
                insert = insert * 10 + (source[index++] - '0');
            }
            std::string_view format;
            if (index < source.size() && source[index] == '!')
            {
                const auto end = source.find('!', index + 1);
                if (end == std::string_view::npos)
                {
                    throw std::invalid_argument("Invalid message insert");
                }
                format = source.substr(index + 1, end - index - 1);
                index = end + 1;
            }
            if (flags & FORMAT_MESSAGE_IGNORE_INSERTS)
            {
                result += source.substr(start, index - start);
            }
            else
            {
                if (!format.empty() && format != "s" && format != "hs")
                {
                    beachlife::compat::unsupported(
                        "FormatMessageA", "non-string insert formatting");
                }
                if (!values[insert])
                {
                    throw std::invalid_argument("Null message string insert");
                }
                result += values[insert];
            }
        }
        else if (ch == '%')
        {
            result += '%';
        }
        else if (ch == 'n')
        {
            result += "\r\n";
        }
        else if (ch == 'r')
        {
            result += '\r';
        }
        else if (ch == 't')
        {
            result += '\t';
        }
        else if (ch == 'b')
        {
            result += ' ';
        }
        else
        {
            result += ch;
        }
    }
    return result;
}
} // namespace

extern "C"
{
    DWORD WINAPI FormatMessageA(DWORD flags,
        LPCVOID source,
        DWORD message,
        DWORD language,
        LPSTR buffer,
        DWORD size,
        va_list* arguments)
    {
        return guard(
            [&]() -> DWORD
            {
                constexpr DWORD allowed =
                    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS |
                    FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_MAX_WIDTH_MASK;
                if ((flags & ~allowed) || ((flags & 255) != 0 && (flags & 255) != 255))
                {
                    beachlife::compat::unsupported(
                        "FormatMessageA", "module resources, wrapping or other flags");
                }
                if (language != 0 && language != 0x0409 && language != 0x0009 && language != 0x0809)
                {
                    beachlife::compat::unsupported(
                        "FormatMessageA", "localized Windows message tables");
                }
                const bool from_string = flags & FORMAT_MESSAGE_FROM_STRING;
                const bool from_system = flags & FORMAT_MESSAGE_FROM_SYSTEM;
                if (!buffer || from_string == from_system || (from_string && !source))
                {
                    last_error = ERROR_INVALID_PARAMETER;
                    return 0;
                }
                const auto result =
                    expand(from_string ? static_cast<LPCSTR>(source) : system_message(message),
                        flags,
                        arguments);
                if (result.size() + 1 > UINT32_MAX)
                {
                    last_error = ERROR_NOT_ENOUGH_MEMORY;
                    return 0;
                }
                if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER)
                {
                    auto* memory = static_cast<char*>(
                        std::malloc(std::max<std::size_t>(size, result.size() + 1)));
                    if (!memory)
                    {
                        last_error = ERROR_NOT_ENOUGH_MEMORY;
                        return 0;
                    }
                    std::memcpy(memory, result.c_str(), result.size() + 1);
                    *reinterpret_cast<LPSTR*>(buffer) = memory;
                }
                else
                {
                    if (result.size() + 1 > size)
                    {
                        last_error = ERROR_INSUFFICIENT_BUFFER;
                        return 0;
                    }
                    std::memcpy(buffer, result.c_str(), result.size() + 1);
                }
                return static_cast<DWORD>(result.size());
            },
            DWORD(0));
    }

    HANDLE WINAPI LocalFree(HANDLE memory)
    {
        std::free(memory);
        return nullptr;
    }
}
