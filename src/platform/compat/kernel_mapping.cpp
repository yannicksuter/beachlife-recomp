#include "kernel_files.h"
#include "native_kernel.h"

#include <cerrno>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#ifndef _WIN32
#include <sys/mman.h>
#endif

namespace
{
using namespace beachlife::compat::kernel;
namespace host = beachlife::compat::native;
struct Mapping
{
    int descriptor = -1;
    uint64_t size = 0;
    DWORD protection = PAGE_READONLY;
    ~Mapping()
    {
        if (descriptor >= 0)
        {
            host::close_descriptor(descriptor);
        }
    }
};
std::mutex mapping_mutex;
std::map<HANDLE, std::unique_ptr<Mapping>> mappings;
std::map<LPCVOID, std::size_t> views;
} // namespace

namespace beachlife::compat::kernel
{
std::optional<bool> close_mapping_handle(HANDLE handle)
{
    std::lock_guard lock(mapping_mutex);
    const auto found = mappings.find(handle);
    if (found == mappings.end())
    {
        return std::nullopt;
    }
    mappings.erase(found);
    return true;
}
} // namespace beachlife::compat::kernel

extern "C"
{
    HANDLE WINAPI CreateFileMappingA(HANDLE file,
        LPSECURITY_ATTRIBUTES security,
        DWORD protection,
        DWORD size_high,
        DWORD size_low,
        LPCSTR name)
    {
        return guard(
            [&]() -> HANDLE
            {
                if (name && *name)
                {
                    beachlife::compat::unsupported("CreateFileMappingA", "named shared mappings");
                }
                if (security && (security->lpSecurityDescriptor || security->bInheritHandle))
                {
                    beachlife::compat::unsupported(
                        "CreateFileMappingA", "custom security or inheritance");
                }
                if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
                    protection != PAGE_WRITECOPY)
                {
                    beachlife::compat::unsupported(
                        "CreateFileMappingA", "mapping protection flags");
                }
                auto mapping = std::make_unique<Mapping>();
                mapping->protection = protection;
                mapping->size = (static_cast<uint64_t>(size_high) << 32) | size_low;
                if (file != INVALID_HANDLE_VALUE)
                {
                    mapping->descriptor =
                        mapping_descriptor(file, protection == PAGE_READWRITE, mapping->size);
                }
                else
                {
                    if (!mapping->size || mapping->size > INT64_MAX)
                    {
                        last_error = ERROR_INVALID_PARAMETER;
                        return nullptr;
                    }
                    std::unique_ptr<std::FILE, decltype(&std::fclose)> backing(
                        std::tmpfile(), std::fclose);
                    if (!backing || host::resize_descriptor(
                                        host::file_descriptor(backing.get()), mapping->size) != 0)
                    {
                        last_error = file_error(errno);
                        return nullptr;
                    }
                    mapping->descriptor =
                        host::duplicate_descriptor(host::file_descriptor(backing.get()));
                    if (mapping->descriptor < 0)
                    {
                        last_error = file_error(errno);
                    }
                }
                if (mapping->descriptor < 0)
                {
                    return nullptr;
                }
                HANDLE result = mapping.get();
                std::lock_guard lock(mapping_mutex);
                mappings.emplace(result, std::move(mapping));
                return result;
            },
            HANDLE(nullptr));
    }

    LPVOID WINAPI MapViewOfFile(
        HANDLE handle, DWORD access, DWORD offset_high, DWORD offset_low, SIZE_T bytes)
    {
        return guard(
            [&]() -> LPVOID
            {
                std::lock_guard lock(mapping_mutex);
                const auto found = mappings.find(handle);
                if (found == mappings.end())
                {
                    last_error = ERROR_INVALID_HANDLE;
                    return nullptr;
                }
                const auto& mapping = *found->second;
                const bool copy = access == FILE_MAP_COPY;
                const bool write = access == FILE_MAP_ALL_ACCESS || (access & FILE_MAP_WRITE);
                if (access != FILE_MAP_ALL_ACCESS && access != FILE_MAP_COPY &&
                    access != FILE_MAP_READ && access != FILE_MAP_WRITE &&
                    access != (FILE_MAP_READ | FILE_MAP_WRITE))
                {
                    beachlife::compat::unsupported("MapViewOfFile", "view access flags");
                }
                if (write && mapping.protection != PAGE_READWRITE)
                {
                    last_error = ERROR_ACCESS_DENIED;
                    return nullptr;
                }
                const uint64_t offset = (static_cast<uint64_t>(offset_high) << 32) | offset_low;
                if ((offset & 0xffff) || offset >= mapping.size || bytes > mapping.size - offset ||
                    (!bytes && mapping.size - offset > SIZE_MAX))
                {
                    last_error = ERROR_INVALID_PARAMETER;
                    return nullptr;
                }
                if (!bytes)
                {
                    bytes = static_cast<SIZE_T>(mapping.size - offset);
                }
#ifdef _WIN32
                void* view = host::map(
                    mapping.descriptor, mapping.protection, access, offset, bytes, last_error);
                if (!view)
                {
                    return nullptr;
                }
#else
                void* view = ::mmap(nullptr,
                    bytes,
                    PROT_READ | ((copy || write) ? PROT_WRITE : 0),
                    copy ? MAP_PRIVATE : MAP_SHARED,
                    mapping.descriptor,
                    static_cast<off_t>(offset));
                if (view == MAP_FAILED)
                {
                    last_error = file_error(errno);
                    return nullptr;
                }
#endif
                try
                {
                    views.emplace(view, bytes);
                }
                catch (...)
                {
#ifdef _WIN32
                    host::unmap(view, last_error);
#else
                    ::munmap(view, bytes);
#endif
                    throw;
                }
                return view;
            },
            LPVOID(nullptr));
    }

    BOOL WINAPI UnmapViewOfFile(LPCVOID view)
    {
        return guard(
            [&]() -> BOOL
            {
                std::lock_guard lock(mapping_mutex);
                const auto found = views.find(view);
                if (found == views.end())
                {
                    last_error = ERROR_INVALID_PARAMETER;
                    return FALSE;
                }
#ifdef _WIN32
                if (!host::unmap(view, last_error))
                {
                    return FALSE;
                }
#else
                if (::munmap(const_cast<void*>(view), found->second) != 0)
                {
                    last_error = file_error(errno);
                    return FALSE;
                }
#endif
                views.erase(found);
                return TRUE;
            },
            FALSE);
    }
}
