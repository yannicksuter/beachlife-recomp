#include "retail_cursor.h"

#include <exception>

namespace beachlife::runtime::retail_cursor
{
namespace
{
thread_local Session* current_session = nullptr;
} // namespace

Session::Session(Provider& provider) noexcept
    : provider_(provider)
    , previous_(current_session)
    , thread_(std::this_thread::get_id())
{
    current_session = this;
}

Session::~Session()
{
    if (thread_ != std::this_thread::get_id() || current_session != this)
    {
        std::terminate();
    }
    current_session = previous_;
}

void* load_image(void* instance,
    std::uint16_t resource,
    std::uint32_t type,
    std::int32_t width,
    std::int32_t height,
    std::uint32_t flags) noexcept
{
    if (!current_session)
    {
        std::terminate();
    }
    return current_session->provider_.load_image(instance, resource, type, width, height, flags);
}

void* set_cursor(void* cursor) noexcept
{
    if (!current_session)
    {
        std::terminate();
    }
    return current_session->provider_.set_cursor(cursor);
}

} // namespace beachlife::runtime::retail_cursor
