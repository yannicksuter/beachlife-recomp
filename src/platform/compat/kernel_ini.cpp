#include "kernel_files.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace beachlife::compat::kernel;
std::mutex ini_mutex;

std::string_view trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

bool equal(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        auto fold = [](char ch)
        {
            return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
        };
        if (fold(left[index]) != fold(right[index]))
        {
            return false;
        }
    }
    return true;
}

struct Document
{
    std::vector<std::string> lines;

    explicit Document(const char* file)
    {
        const auto bytes = read_file(file);
        if (!bytes)
        {
            if (last_error != ERROR_FILE_NOT_FOUND)
            {
                throw std::runtime_error("Cannot read INI file");
            }
            return;
        }
        if (bytes->size() >= 2 && (((*bytes)[0] == 0xff && (*bytes)[1] == 0xfe) ||
                                      ((*bytes)[0] == 0xfe && (*bytes)[1] == 0xff)))
        {
            beachlife::compat::unsupported("INI file", "Unicode INI files are not implemented");
        }
        if (std::find(bytes->begin(), bytes->end(), 0) != bytes->end())
        {
            beachlife::compat::unsupported("INI file", "embedded nulls in an ANSI INI file");
        }
        const std::string text(bytes->begin(), bytes->end());
        for (std::size_t position = 0; position < text.size();)
        {
            const auto end = text.find('\n', position);
            auto line = text.substr(position, end == std::string::npos ? end : end - position);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            lines.push_back(std::move(line));
            position = end == std::string::npos ? text.size() : end + 1;
        }
    }

    static std::string_view section(std::string_view line)
    {
        line = trim(line);
        if (line.size() < 2 || line.front() != '[')
        {
            return {};
        }
        const auto end = line.find(']');
        return end == std::string_view::npos ? std::string_view{} : trim(line.substr(1, end - 1));
    }

    static std::string_view key(std::string_view line)
    {
        line = trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '[')
        {
            return {};
        }
        const auto equals = line.find('=');
        return equals == std::string_view::npos ? std::string_view{} : trim(line.substr(0, equals));
    }

    std::pair<std::size_t, std::size_t> range(std::string_view name) const
    {
        for (std::size_t index = 0; index < lines.size(); ++index)
        {
            if (section(lines[index]).empty() || !equal(section(lines[index]), name))
            {
                continue;
            }
            std::size_t end = index + 1;
            while (end < lines.size() && section(lines[end]).empty())
            {
                ++end;
            }
            return {index, end};
        }
        return {lines.size(), lines.size()};
    }

    std::string serialize() const
    {
        std::string result;
        for (const auto& line : lines)
        {
            result += line + "\r\n";
        }
        return result;
    }
};

DWORD copy_string(std::string_view value, LPSTR output, DWORD size)
{
    if (!size)
    {
        return 0;
    }
    const auto count = std::min<std::size_t>(value.size(), size - 1);
    std::memcpy(output, value.data(), count);
    output[count] = 0;
    return static_cast<DWORD>(count);
}

DWORD copy_list(const std::vector<std::string>& values, LPSTR output, DWORD size)
{
    if (!size)
    {
        return 0;
    }
    output[0] = 0;
    if (size == 1)
    {
        return 0;
    }
    std::string list;
    for (const auto& value : values)
    {
        list += value;
        list += '\0';
    }
    if (list.empty())
    {
        output[1] = 0;
        return 0;
    }
    const bool truncated = list.size() + 1 > size;
    const auto count = std::min<std::size_t>(list.size(), size - 1);
    std::memcpy(output, list.data(), count);
    output[count] = 0;
    if (truncated)
    {
        output[size - 2] = 0;
        return size - 2;
    }
    return static_cast<DWORD>(list.size());
}

void require_filename(const char* file, const char* api)
{
    if (!file || !*file)
    {
        beachlife::compat::unsupported(api, "implicit Windows win.ini has no host equivalent");
    }
}
} // namespace

extern "C"
{
    DWORD WINAPI GetPrivateProfileStringA(
        LPCSTR section, LPCSTR key, LPCSTR fallback, LPSTR result, DWORD size, LPCSTR file)
    {
        return guard(
            [&]() -> DWORD
            {
                require_filename(file, "GetPrivateProfileStringA");
                if (size && !result)
                {
                    last_error = ERROR_INVALID_PARAMETER;
                    return 0;
                }
                std::lock_guard lock(ini_mutex);
                const Document document(file);
                if (!section)
                {
                    std::vector<std::string> names;
                    for (const auto& line : document.lines)
                    {
                        const auto name = Document::section(line);
                        if (!name.empty() && std::none_of(names.begin(),
                                                 names.end(),
                                                 [&](const auto& prior)
                                                 {
                                                     return equal(prior, name);
                                                 }))
                        {
                            names.emplace_back(name);
                        }
                    }
                    return copy_list(names, result, size);
                }
                const auto [first, last] = document.range(section);
                if (!key)
                {
                    std::vector<std::string> keys;
                    for (std::size_t index = first == last ? first : first + 1; index < last;
                        ++index)
                    {
                        const auto name = Document::key(document.lines[index]);
                        if (!name.empty() && std::none_of(keys.begin(),
                                                 keys.end(),
                                                 [&](const auto& prior)
                                                 {
                                                     return equal(prior, name);
                                                 }))
                        {
                            keys.emplace_back(name);
                        }
                    }
                    return copy_list(keys, result, size);
                }
                for (std::size_t index = first == last ? first : first + 1; index < last; ++index)
                {
                    const auto& line = document.lines[index];
                    if (Document::key(line).empty() || !equal(Document::key(line), key))
                    {
                        continue;
                    }
                    auto value = trim(std::string_view(line).substr(line.find('=') + 1));
                    if (value.size() >= 2 && (value.front() == '\'' || value.front() == '"') &&
                        value.back() == value.front())
                    {
                        value = value.substr(1, value.size() - 2);
                    }
                    return copy_string(value, result, size);
                }
                std::string_view value = fallback ? fallback : "";
                const auto end = value.find_last_not_of(' ');
                value =
                    end == std::string_view::npos ? std::string_view{} : value.substr(0, end + 1);
                return copy_string(value, result, size);
            },
            DWORD(0));
    }

    BOOL WINAPI WritePrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR file)
    {
        return guard(
            [&]() -> BOOL
            {
                require_filename(file, "WritePrivateProfileStringA");
                // There is no INI cache. This request flushes an already current file.
                if (!section && !key && !value)
                {
                    return TRUE;
                }
                if (!section || !*section || std::strpbrk(section, "\r\n[]") ||
                    (key && (!*key || std::strpbrk(key, "\r\n="))) ||
                    (value && std::strpbrk(value, "\r\n")))
                {
                    last_error = ERROR_INVALID_PARAMETER;
                    return FALSE;
                }
                std::lock_guard lock(ini_mutex);
                Document document(file);
                auto [first, last] = document.range(section);
                if (!key)
                {
                    document.lines.erase(
                        document.lines.begin() + first, document.lines.begin() + last);
                }
                else
                {
                    const auto found = std::find_if(
                        document.lines.begin() + (first == last ? first : first + 1),
                        document.lines.begin() + last,
                        [&](const auto& line)
                        {
                            return !Document::key(line).empty() && equal(Document::key(line), key);
                        });
                    if (!value)
                    {
                        if (found != document.lines.begin() + last)
                        {
                            document.lines.erase(found);
                        }
                    }
                    else if (found != document.lines.begin() + last)
                    {
                        *found = std::string(key) + '=' + value;
                    }
                    else
                    {
                        if (first == last)
                        {
                            document.lines.push_back('[' + std::string(section) + ']');
                            last = document.lines.size();
                        }
                        document.lines.insert(
                            document.lines.begin() + last, std::string(key) + '=' + value);
                    }
                }
                return write_file(file, document.serialize()) ? TRUE : FALSE;
            },
            FALSE);
    }
}
