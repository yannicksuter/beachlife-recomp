#include "cursor_provider.h"

#include "runtime/retail_cursor.h"
#include "windows.h"

namespace beachlife::compat
{
namespace
{
class CursorProvider final : public runtime::retail_cursor::Provider
{
  public:
    void* load_image(void* instance,
        std::uint16_t resource,
        std::uint32_t type,
        std::int32_t width,
        std::int32_t height,
        std::uint32_t flags) noexcept override
    {
        return LoadImageA(static_cast<HINSTANCE>(instance),
            reinterpret_cast<LPCSTR>(std::uintptr_t(resource)),
            type,
            width,
            height,
            flags);
    }

    void* set_cursor(void* cursor) noexcept override
    {
        return SetCursor(static_cast<HCURSOR>(cursor));
    }
};
} // namespace

runtime::retail_cursor::Provider& retail_cursor_provider() noexcept
{
    static CursorProvider provider;
    return provider;
}
} // namespace beachlife::compat
