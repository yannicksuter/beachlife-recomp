#pragma once

#include "compat_host.h"

#include <cerrno>
#include <new>
#include <stdexcept>
#include <utility>

namespace beachlife::compat::kernel
{
extern thread_local DWORD last_error;

// A host allocation or filesystem failure must never unwind into retail code.
template <typename Function, typename Result>
Result guard(Function&& function, Result failure) noexcept
{
    try
    {
        return std::forward<Function>(function)();
    }
    catch (const std::bad_alloc&)
    {
        last_error = ERROR_NOT_ENOUGH_MEMORY;
        errno = ENOMEM;
    }
    catch (const std::invalid_argument&)
    {
        last_error = ERROR_INVALID_NAME;
        errno = EINVAL;
    }
    catch (...)
    {
        last_error = ERROR_GEN_FAILURE;
        errno = EIO;
    }
    return failure;
}
} // namespace beachlife::compat::kernel
