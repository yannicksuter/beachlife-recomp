#include "kernel_files.h"
#include "native_kernel.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#ifndef _WIN32
#include <sys/file.h>
#include <unistd.h>
#endif

namespace
{
using namespace beachlife::compat::kernel;
#ifndef _WIN32
struct Descriptor
{
    int value;
    explicit Descriptor(int descriptor)
        : value(descriptor)
    {
    }
    ~Descriptor()
    {
        if (value >= 0)
        {
            ::close(value);
        }
    }
};
#endif
struct Mutex
{
#ifdef _WIN32
    void* native = nullptr;
#else
    int descriptor = -1;
#endif
    std::thread::id owner;
    ~Mutex()
    {
#ifdef _WIN32
        if (native)
        {
            beachlife::compat::native::close_handle(native);
        }
#else
        if (descriptor >= 0)
        {
            ::close(descriptor);
        }
#endif
    }
};
struct MutexHandle
{
    std::shared_ptr<Mutex> object;
};
std::mutex mutex_registry;
std::map<std::string, std::weak_ptr<Mutex>> named_mutexes;
std::map<HANDLE, std::unique_ptr<MutexHandle>> mutex_handles;
int current_module;

std::shared_ptr<Mutex> named_mutex(const char* name, bool initial_owner, bool& existed)
{
    const std::string key(name);
    if (key.empty() || key.size() >= MAX_PATH)
    {
        last_error = ERROR_INVALID_NAME;
        return nullptr;
    }
    if (key.find('\\') != std::string::npos)
    {
        beachlife::compat::unsupported("CreateMutexA", "Windows session or global namespaces");
    }
#ifdef _WIN32
    auto object = std::make_shared<Mutex>();
    object->native = beachlife::compat::native::create_mutex(name, initial_owner, last_error);
    if (!object->native)
    {
        return nullptr;
    }
    existed = last_error == ERROR_ALREADY_EXISTS;
    return object;
#else
    if (const auto found = named_mutexes.find(key); found != named_mutexes.end())
    {
        if (auto object = found->second.lock())
        {
            existed = true;
            return object;
        }
    }
    const auto root = user_path(".compat-mutexes");
    std::filesystem::create_directories(root);
    Descriptor guard_file(
        ::open((root / "registry").c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600));
    if (guard_file.value < 0)
    {
        last_error = file_error(errno);
        return nullptr;
    }
    while (::flock(guard_file.value, LOCK_EX) != 0)
    {
        if (errno != EINTR)
        {
            last_error = file_error(errno);
            return nullptr;
        }
    }
    std::string encoded;
    for (unsigned char ch : key)
    {
        encoded += "0123456789abcdef"[ch >> 4];
        encoded += "0123456789abcdef"[ch & 15];
    }
    auto path = root;
    for (std::size_t offset = 0; offset < encoded.size(); offset += 120)
    {
        path /= encoded.substr(offset, 120);
    }
    std::filesystem::create_directories(path);
    auto object = std::make_shared<Mutex>();
    object->descriptor =
        ::open((path / "object").c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (object->descriptor < 0)
    {
        last_error = file_error(errno);
        return nullptr;
    }
    if (::flock(object->descriptor, LOCK_EX | LOCK_NB) != 0)
    {
        if (errno != EWOULDBLOCK && errno != EAGAIN)
        {
            last_error = file_error(errno);
            return nullptr;
        }
        existed = true;
    }
    // Every process holds a shared lifetime lock. The separate registry lock
    // makes the exclusive-probe/shared conversion atomic to other creators.
    if (::flock(object->descriptor, LOCK_SH) != 0)
    {
        last_error = file_error(errno);
        return nullptr;
    }
    if (initial_owner && !existed)
    {
        object->owner = std::this_thread::get_id();
    }
    named_mutexes[key] = object;
    return object;
#endif
}
} // namespace

extern "C"
{
    void WINAPI GetStartupInfoA(LPSTARTUPINFOA info)
    {
        if (!info)
        {
            last_error = ERROR_INVALID_PARAMETER;
            return;
        }
        // The host starts with no inherited Win32 STARTUPINFO overrides.
        std::memset(info, 0, sizeof(*info));
        info->cb = sizeof(*info);
    }

    HMODULE WINAPI GetModuleHandleA(LPCSTR name)
    {
        if (name)
        {
            beachlife::compat::unsupported(
                "GetModuleHandleA", "the host does not load Windows modules");
        }
        return reinterpret_cast<HMODULE>(&current_module);
    }

    HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES security, BOOL owner, LPCSTR name)
    {
        return guard(
            [&]() -> HANDLE
            {
                if (security && (security->lpSecurityDescriptor || security->bInheritHandle))
                {
                    beachlife::compat::unsupported(
                        "CreateMutexA", "custom security or handle inheritance");
                }
                std::lock_guard lock(mutex_registry);
                bool existed = false;
                auto object =
                    name ? named_mutex(name, owner != FALSE, existed) : std::make_shared<Mutex>();
                if (!object)
                {
                    return nullptr;
                }
#ifdef _WIN32
                if (!name)
                {
                    object->native = beachlife::compat::native::create_mutex(
                        nullptr, owner != FALSE, last_error);
                    if (!object->native)
                    {
                        return nullptr;
                    }
                }
#endif
                if (!name && owner)
                {
                    object->owner = std::this_thread::get_id();
                }
                auto handle = std::make_unique<MutexHandle>();
                handle->object = std::move(object);
                HANDLE result = handle.get();
                mutex_handles.emplace(result, std::move(handle));
                last_error = existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS;
                return result;
            },
            HANDLE(nullptr));
    }

    BOOL WINAPI ReleaseMutex(HANDLE handle)
    {
        return guard(
            [&]() -> BOOL
            {
                std::lock_guard lock(mutex_registry);
                const auto found = mutex_handles.find(handle);
                if (found == mutex_handles.end())
                {
                    last_error = ERROR_INVALID_HANDLE;
                    return FALSE;
                }
                auto& mutex = *found->second->object;
#ifdef _WIN32
                return beachlife::compat::native::release_mutex(mutex.native, last_error) ? TRUE
                                                                                          : FALSE;
#else
                if (mutex.owner != std::this_thread::get_id())
                {
                    last_error = ERROR_NOT_OWNER;
                    return FALSE;
                }
                mutex.owner = {};
                return TRUE;
#endif
            },
            FALSE);
    }

    BOOL WINAPI CloseHandle(HANDLE handle)
    {
        return guard(
            [&]() -> BOOL
            {
                {
                    std::lock_guard lock(mutex_registry);
                    if (mutex_handles.erase(handle))
                    {
                        return TRUE;
                    }
                }
                if (const auto file = close_file_handle(handle))
                {
                    return *file ? TRUE : FALSE;
                }
                if (const auto mapping = close_mapping_handle(handle))
                {
                    return *mapping ? TRUE : FALSE;
                }
                last_error = ERROR_INVALID_HANDLE;
                return FALSE;
            },
            FALSE);
    }
}
