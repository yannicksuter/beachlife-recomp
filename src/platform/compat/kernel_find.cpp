#include "compat_files.h"
#include "data/game_path.h"
#include "io.h"
#include "kernel_files.h"
#include "platform/path.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstring>
#include <map>
#include <set>
#include <sys/stat.h>

namespace
{
using namespace beachlife::compat::kernel;
namespace fs = std::filesystem;
using Entry = beachlife::data::SourceEntry;
thread_local const std::vector<Entry>* directory_index = nullptr;
struct Search
{
    std::vector<_finddata_t> entries;
    std::size_t next = 1;
};
thread_local std::map<long, Search> searches;
std::atomic<long> next_search{1};

char fold(char ch)
{
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch;
}
std::string folded(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), fold);
    return value;
}

bool matches(std::string_view pattern, std::string_view name)
{
    // The retail calls use Win32's *.* convention, including names with no dot.
    if (pattern == "*.*")
    {
        pattern = "*";
    }
    else if (pattern.ends_with(".*") && name.find('.') == std::string_view::npos)
    {
        pattern.remove_suffix(2);
    }
    std::size_t p = 0, n = 0, star = std::string_view::npos, retry = 0;
    while (n < name.size())
    {
        if (p < pattern.size() && (pattern[p] == '?' || fold(pattern[p]) == fold(name[n])))
        {
            ++p;
            ++n;
        }
        else if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            retry = n;
        }
        else if (star != std::string_view::npos)
        {
            p = star + 1;
            n = ++retry;
        }
        else
        {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*')
    {
        ++p;
    }
    return p == pattern.size();
}

_finddata_t record(std::string_view name, unsigned attributes, uint64_t size)
{
    if (name.size() >= sizeof(_finddata_t::name) || size > UINT32_MAX)
    {
        beachlife::compat::unsupported("_findfirst", "entry exceeds the retail name or size range");
    }
    _finddata_t result{};
    result.attrib = attributes;
    // The prepared VFS does not retain filesystem timestamps. MSVCRT uses -1
    // when a filesystem does not provide a requested time.
    result.time_create = result.time_access = result.time_write = -1;
    result.size = static_cast<uint32_t>(size);
    std::memcpy(result.name, name.data(), name.size());
    return result;
}

int32_t timestamp(time_t value)
{
    return value < 0 || static_cast<uint64_t>(value) > INT32_MAX ? -1 : static_cast<int32_t>(value);
}

std::vector<_finddata_t> enumerate(const char* input)
{
    if (!input || !*input)
    {
        throw std::invalid_argument("Empty file search");
    }
    std::string text(input);
    std::replace(text.begin(), text.end(), '\\', '/');
    const bool absolute = beachlife::platform::utf8_path(text).is_absolute();
    const auto slash = text.find_last_of('/');
    const std::string directory = slash == std::string::npos ? "." : text.substr(0, slash);
    const std::string pattern = slash == std::string::npos ? text : text.substr(slash + 1);
    if (pattern.empty() || pattern.find_first_of("<>:\"|") != std::string::npos ||
        directory.find_first_of("*?") != std::string::npos)
    {
        throw std::invalid_argument("Invalid file search");
    }
    // Validate the non-wildcard part with the same retail path rules.
    const std::string logical_directory = logical_path(directory.c_str());
    const auto relative_pattern =
        logical_directory == "." ? folded(pattern) : logical_directory + '/' + folded(pattern);
    const bool writable =
        absolute || relative_pattern == "photos" || relative_pattern.starts_with("photos/") ||
        relative_pattern == "savegames" || relative_pattern.starts_with("savegames/");
    if (!directory_index && !writable)
    {
        beachlife::compat::unsupported(
            "_findfirst", "bind compat::DirectorySession to the prepared VFS manifest");
    }
    std::map<std::string, _finddata_t> entries;
    if (directory_index && !writable)
    {
        for (const auto& entry : *directory_index)
        {
            const auto path = beachlife::data::normalize_game_path(entry.path);
            const std::string prefix = logical_directory == "." ? "" : logical_directory + '/';
            if (!path.key.starts_with(prefix))
            {
                continue;
            }
            const auto rest = std::string_view(path.display).substr(prefix.size());
            const auto separator = rest.find('/');
            const auto name = rest.substr(0, separator);
            if (name.empty() || !matches(pattern, name))
            {
                continue;
            }
            const bool subdirectory = separator != std::string_view::npos;
            entries.emplace(folded(std::string(name)),
                record(name,
                    _A_RDONLY | (subdirectory ? _A_SUBDIR : 0),
                    subdirectory ? 0 : entry.size));
        }
    }
    const auto user_directory = user_path(directory.c_str());
    std::error_code error;
    const bool present = fs::is_directory(user_directory, error);
    if (error && error != std::errc::no_such_file_or_directory &&
        error != std::errc::not_a_directory)
    {
        throw fs::filesystem_error("Cannot enumerate user directory", user_directory, error);
    }
    if (present)
    {
        std::set<std::string> user_names;
        for (const auto& entry : fs::directory_iterator(user_directory))
        {
            const auto name = beachlife::platform::utf8(entry.path().filename());
            if (!matches(pattern, name))
            {
                continue;
            }
            if (entry.is_symlink())
            {
                beachlife::compat::unsupported("_findfirst", "symbolic links in a user directory");
            }
#ifdef _WIN32
            struct _stat64 status;
            const int stated = ::_wstat64(entry.path().c_str(), &status);
#else
            struct stat status;
            const int stated = ::stat(entry.path().c_str(), &status);
#endif
            if (stated != 0)
            {
                throw std::system_error(errno, std::generic_category());
            }
            if (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode))
            {
                beachlife::compat::unsupported("_findfirst", "nonregular user directory entry");
            }
            auto result = record(name,
                (S_ISDIR(status.st_mode) ? _A_SUBDIR : _A_ARCH) |
                    ((status.st_mode & S_IWUSR) ? 0 : _A_RDONLY),
                S_ISDIR(status.st_mode) ? 0 : status.st_size);
            result.time_access = timestamp(status.st_atime);
            result.time_write = timestamp(status.st_mtime);
            const auto key = folded(name);
            if (!user_names.insert(key).second)
            {
                beachlife::compat::unsupported(
                    "_findfirst", "ambiguous case-insensitive user filename");
            }
            entries[key] = result;
        }
    }
    std::vector<_finddata_t> result;
    for (const auto& [key, entry] : entries)
    {
        result.push_back(entry);
    }
    return result;
}
} // namespace

namespace beachlife::compat::kernel
{
std::optional<DWORD> manifest_attributes(std::string_view path)
{
    if (!directory_index)
    {
        return std::nullopt;
    }
    if (path == ".")
    {
        return FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY;
    }
    const auto prefix = std::string(path) + '/';
    for (const auto& entry : *directory_index)
    {
        const auto key = data::normalize_game_path(entry.path).key;
        if (key == path)
        {
            return FILE_ATTRIBUTE_READONLY;
        }
        if (key.starts_with(prefix))
        {
            return FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY;
        }
    }
    return INVALID_FILE_ATTRIBUTES;
}
} // namespace beachlife::compat::kernel

namespace beachlife::compat
{
DirectorySession::DirectorySession(std::span<const data::SourceEntry> entries)
    : entries_(entries.begin(), entries.end())
    , previous_(directory_index)
    , thread_(std::this_thread::get_id())
{
    directory_index = &entries_;
}
DirectorySession::~DirectorySession()
{
    if (thread_ != std::this_thread::get_id() || directory_index != &entries_)
    {
        std::terminate();
    }
    directory_index = previous_;
}
} // namespace beachlife::compat

extern "C"
{
    long _findfirst(const char* pattern, _finddata_t* result)
    {
        return guard(
            [&]() -> long
            {
                static_assert(sizeof(_finddata_t) == 280 && offsetof(_finddata_t, name) == 20);
                if (!result)
                {
                    errno = EINVAL;
                    last_error = ERROR_INVALID_PARAMETER;
                    return -1;
                }
                auto entries = enumerate(pattern);
                if (entries.empty())
                {
                    errno = ENOENT;
                    last_error = ERROR_FILE_NOT_FOUND;
                    return -1;
                }
                const long handle = next_search.fetch_add(1);
                if (handle <= 0 || handle >= INT32_MAX)
                {
                    errno = EMFILE;
                    last_error = ERROR_TOO_MANY_OPEN_FILES;
                    return -1;
                }
                const auto first = entries.front();
                searches.emplace(handle, Search{std::move(entries)});
                *result = first;
                return handle;
            },
            -1L);
    }

    int _findnext(long handle, _finddata_t* result)
    {
        return guard(
            [&]() -> int
            {
                const auto found = searches.find(handle);
                if (!result || found == searches.end())
                {
                    errno = EINVAL;
                    last_error = ERROR_INVALID_HANDLE;
                    return -1;
                }
                auto& search = found->second;
                if (search.next == search.entries.size())
                {
                    errno = ENOENT;
                    last_error = ERROR_NO_MORE_FILES;
                    return -1;
                }
                *result = search.entries[search.next++];
                return 0;
            },
            -1);
    }

    int _findclose(long handle)
    {
        return guard(
            [&]() -> int
            {
                if (searches.erase(handle))
                {
                    return 0;
                }
                errno = EINVAL;
                last_error = ERROR_INVALID_HANDLE;
                return -1;
            },
            -1);
    }
}
