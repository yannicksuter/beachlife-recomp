#pragma once

#include <cstdint>
#include <thread>

namespace beachlife::runtime::retail_cursor
{

// Only the observed numeric-resource image load and cursor calls are exposed.
// Handles are borrowed opaque values. This interface neither owns resources nor
// implements Win32, a renderer backend, caching or fallback behavior.
class Provider
{
  public:
    virtual ~Provider() = default;
    virtual void* load_image(void* instance,
        std::uint16_t resource,
        std::uint32_t type,
        std::int32_t width,
        std::int32_t height,
        std::uint32_t flags) noexcept = 0;
    virtual void* set_cursor(void* cursor) noexcept = 0;
};

// Borrows a provider on this thread. It must outlive its session and any active
// calls. Nested sessions restore the previous binding; destruction must occur
// on the creating thread in stack order. Calling without a session violates a
// host precondition and terminates, rather than pretending an API call failed.
class Session
{
  public:
    explicit Session(Provider& provider) noexcept;
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

  private:
    friend void* load_image(
        void*, std::uint16_t, std::uint32_t, std::int32_t, std::int32_t, std::uint32_t) noexcept;
    friend void* set_cursor(void*) noexcept;
    Provider& provider_;
    Session* previous_;
    std::thread::id thread_;
};

void* load_image(void* instance,
    std::uint16_t resource,
    std::uint32_t type,
    std::int32_t width,
    std::int32_t height,
    std::uint32_t flags) noexcept;
void* set_cursor(void* cursor) noexcept;

} // namespace beachlife::runtime::retail_cursor
