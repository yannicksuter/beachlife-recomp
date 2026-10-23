#include "user32_cursor.h"

#include "compat_host.h"
#include "cursor_resource.h"
#include "runtime/retail_files.h"

#include <SDL3/SDL.h>

#include <exception>
#include <map>
#include <new>
#include <thread>
#include <tuple>
#include <vector>

namespace beachlife::compat
{
namespace
{
using Cursor = std::unique_ptr<SDL_Cursor, decltype(&SDL_DestroyCursor)>;
thread_local CursorResources* current = nullptr;
constexpr UINT shared = 0x8000, default_size = 0x40;

std::vector<unsigned char> read_executable()
{
    namespace files = runtime::retail_files;
    std::unique_ptr<files::File, decltype(&files::close)> file(
        files::open("BeachLife.exe", "rb"), files::close);
    files::Position size = 0;
    if (!file || files::seek(file.get(), 0, files::SeekOrigin::end) ||
        files::position(file.get(), &size) || !size || size > 64 * 1024 * 1024 ||
        files::seek(file.get(), 0, files::SeekOrigin::begin))
    {
        throw std::runtime_error("Cannot read the prepared executable's cursor resources");
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    std::size_t done = 0;
    while (done < bytes.size())
    {
        const auto received = files::read(bytes.data() + done, 1, bytes.size() - done, file.get());
        if (!received || received > bytes.size() - done)
        {
            throw std::runtime_error("Truncated executable cursor data");
        }
        done += received;
    }
    return bytes;
}
} // namespace

struct CursorResources
{
    CursorResources* previous = current;
    std::thread::id thread = std::this_thread::get_id();
    HCURSOR previous_cursor = current_cursor();
    std::vector<unsigned char> executable;
    std::vector<Cursor> owned;
    std::map<std::tuple<std::uint16_t, int, int>, SDL_Cursor*> shared_cursors;
};

ResourceCursorSession::ResourceCursorSession()
    : resources_(std::make_unique<CursorResources>())
{
    current = resources_.get();
}

ResourceCursorSession::~ResourceCursorSession()
{
    if (current != resources_.get() || resources_->thread != std::this_thread::get_id())
    {
        std::terminate();
    }
    SetCursor(resources_->previous_cursor);
    current = resources_->previous;
}

HCURSOR load_resource_cursor(
    HINSTANCE instance, LPCSTR name, UINT type, int width, int height, UINT flags) noexcept
{
    if ((instance && instance != GetModuleHandleA(nullptr)) ||
        reinterpret_cast<std::uintptr_t>(name) > 0xffff || type != 2 ||
        (flags & ~(shared | default_size)))
    {
        unsupported(
            "LoadImageA", "only numeric cursor resources from the game module are supported");
    }
    if (!current)
    {
        unsupported("LoadImageA", "executable cursors require a ResourceCursorSession");
    }
    if (width < 0 || height < 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (flags & default_size)
    {
        // The boot host's Win32 cursor metrics, matching the game's 32x32 calls.
        width = width ? width : 32;
        height = height ? height : 32;
    }
    try
    {
        const auto resource = static_cast<std::uint16_t>(reinterpret_cast<std::uintptr_t>(name));
        const auto key = std::tuple(resource, width, height);
        if (flags & shared)
        {
            const auto found = current->shared_cursors.find(key);
            if (found != current->shared_cursors.end())
            {
                return reinterpret_cast<HCURSOR>(found->second);
            }
        }
        if (current->executable.empty())
        {
            current->executable = read_executable();
        }
        auto image = decode_cursor_resource(current->executable, resource, width, height);
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
            SDL_CreateSurfaceFrom(image.width,
                image.height,
                SDL_PIXELFORMAT_RGBA32,
                image.rgba.data(),
                image.width * 4),
            SDL_DestroySurface);
        if (!surface)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return nullptr;
        }
        Cursor cursor(SDL_CreateColorCursor(surface.get(), image.hotspot_x, image.hotspot_y),
            SDL_DestroyCursor);
        if (!cursor)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return nullptr;
        }
        const auto handle = cursor.get();
        current->owned.push_back(std::move(cursor));
        if (flags & shared)
        {
            current->shared_cursors.emplace(key, handle);
        }
        return reinterpret_cast<HCURSOR>(handle);
    }
    catch (const UnsupportedCursor& error)
    {
        unsupported("LoadImageA", error.what());
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    catch (...)
    {
        SetLastError(ERROR_INVALID_DATA);
    }
    return nullptr;
}
} // namespace beachlife::compat
