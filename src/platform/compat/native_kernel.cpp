#include "native_kernel.h"

#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace beachlife::compat::native
{
FILE* open_file(const std::filesystem::path& path, const char* mode)
{
#ifdef _WIN32
    const std::string narrow(mode);
    return ::_wfopen(path.c_str(), std::wstring(narrow.begin(), narrow.end()).c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

int open_descriptor(const std::filesystem::path& path, int flags)
{
#ifdef _WIN32
    return ::_wopen(path.c_str(), flags | _O_BINARY | _O_NOINHERIT, 0600);
#else
    return ::open(path.c_str(), flags | O_CLOEXEC, 0600);
#endif
}

int close_descriptor(int descriptor)
{
#ifdef _WIN32
    return ::_close(descriptor);
#else
    return ::close(descriptor);
#endif
}

int duplicate_descriptor(int descriptor)
{
#ifdef _WIN32
    return ::_dup(descriptor);
#else
    return ::dup(descriptor);
#endif
}

int resize_descriptor(int descriptor, uint64_t size)
{
#ifdef _WIN32
    const int error = ::_chsize_s(descriptor, size);
    if (error)
    {
        errno = error;
        return -1;
    }
    return 0;
#else
    return ::ftruncate(descriptor, static_cast<off_t>(size));
#endif
}

int file_descriptor(FILE* file)
{
#ifdef _WIN32
    return ::_fileno(file);
#else
    return ::fileno(file);
#endif
}

FILE* descriptor_stream(int descriptor, const char* mode)
{
#ifdef _WIN32
    return ::_fdopen(descriptor, mode);
#else
    return ::fdopen(descriptor, mode);
#endif
}

int seek(FILE* file, int64_t offset, int origin)
{
#ifdef _WIN32
    return ::_fseeki64(file, offset, origin);
#else
    return ::fseeko(file, static_cast<off_t>(offset), origin);
#endif
}

int64_t tell(FILE* file)
{
#ifdef _WIN32
    return ::_ftelli64(file);
#else
    return ::ftello(file);
#endif
}

int temporary_file(std::filesystem::path& path)
{
#ifdef _WIN32
    static std::atomic<unsigned> serial{0};
    const auto base = path;
    for (unsigned attempt = 0; attempt < 100; ++attempt)
    {
        path = base;
        path += "." + std::to_string(::_getpid()) + "." + std::to_string(serial++);
        const int descriptor = open_descriptor(path, _O_RDWR | _O_CREAT | _O_EXCL);
        if (descriptor >= 0 || errno != EEXIST)
        {
            return descriptor;
        }
    }
    return -1;
#else
    auto pattern = path.string() + ".XXXXXX";
    const int descriptor = ::mkstemp(pattern.data());
    path = pattern;
    return descriptor;
#endif
}

bool replace_file(const std::filesystem::path& from, const std::filesystem::path& to)
{
#ifdef _WIN32
    return ::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return ::rename(from.c_str(), to.c_str()) == 0;
#endif
}

#ifdef _WIN32
bool text_mode(FILE* file)
{
    return ::_setmode(::_fileno(file), _O_TEXT) != -1;
}

void* map(int descriptor,
    uint32_t protection,
    uint32_t access,
    uint64_t offset,
    std::size_t bytes,
    uint32_t& error)
{
    const auto file = reinterpret_cast<HANDLE>(::_get_osfhandle(descriptor));
    // A copy view is private even when the retail mapping was read-only.
    const DWORD view_protection = access == FILE_MAP_COPY ? PAGE_WRITECOPY : protection;
    HANDLE mapping = ::CreateFileMappingW(file, nullptr, view_protection, 0, 0, nullptr);
    if (!mapping)
    {
        error = ::GetLastError();
        return nullptr;
    }
    void* view = ::MapViewOfFile(
        mapping, access, static_cast<DWORD>(offset >> 32), static_cast<DWORD>(offset), bytes);
    if (!view)
    {
        error = ::GetLastError();
    }
    ::CloseHandle(mapping);
    return view;
}

bool unmap(const void* view, uint32_t& error)
{
    if (::UnmapViewOfFile(view))
    {
        return true;
    }
    error = ::GetLastError();
    return false;
}

void* create_mutex(const char* name, bool owner, uint32_t& error)
{
    void* result = ::CreateMutexA(nullptr, owner, name);
    error = ::GetLastError();
    return result;
}

bool release_mutex(void* mutex, uint32_t& error)
{
    if (::ReleaseMutex(mutex))
    {
        return true;
    }
    error = ::GetLastError();
    return false;
}

void close_handle(void* handle)
{
    ::CloseHandle(handle);
}
#endif
} // namespace beachlife::compat::native
