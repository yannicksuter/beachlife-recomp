#pragma once
#include "compat_host.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <thread>

namespace beachlife::compat::movie
{
inline constexpr GUID unknown_iid = {0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
inline constexpr HRESULT no_interface = static_cast<HRESULT>(0x80004002);
inline constexpr HRESULT pointer_error = static_cast<HRESULT>(0x80004003);
inline bool same(REFGUID a, REFGUID b)
{
    return std::memcmp(&a, &b, sizeof(GUID)) == 0;
}
inline bool skipped()
{
    const char* value = SDL_getenv("BEACHLIFE_BOOT_SKIP_MOVIE");
    return value && std::strcmp(value, "1") == 0;
}
inline void thread_check(std::thread::id thread, const char* api)
{
    if (thread != std::this_thread::get_id() || !SDL_IsMainThread())
    {
        unsupported(api, "movie APIs require the SDL main thread");
    }
}
template <class F> HRESULT boundary(const char* api, F&& f) noexcept
{
    try
    {
        return f();
    }
    catch (const std::bad_alloc&)
    {
        return static_cast<HRESULT>(0x8007000e);
    }
    catch (const std::exception& error)
    {
        unsupported(api, error.what());
    }
    catch (...)
    {
        unsupported(api, "unexpected host exception");
    }
}
} // namespace beachlife::compat::movie
