#include "input_map.h"

#include "compat_host.h"

#include <cmath>
#include <cstring>
#include <optional>

namespace beachlife::compat::input
{
namespace
{
HWND bound = nullptr;
SDL_WindowID bound_id = 0;
std::optional<RECT> clip_screen;

SDL_Window* bound_window()
{
    return SDL_GetWindowFromID(bound_id);
}

bool apply_clip(SDL_Window* window, const Mapping& map)
{
    const auto rectangle = map.window_clip();
    if (rectangle.w <= 0 || rectangle.h <= 0)
    {
        return false;
    }
    const auto* previous = SDL_GetWindowMouseRect(window);
    return (previous && previous->x == rectangle.x && previous->y == rectangle.y &&
               previous->w == rectangle.w && previous->h == rectangle.h) ||
           SDL_SetWindowMouseRect(window, &rectangle);
}
} // namespace

SDL_Rect Mapping::window_clip() const
{
    const auto start = presentation.to_window(clip.left, clip.top);
    const auto end = presentation.to_window(clip.right, clip.bottom);
    const int x = static_cast<int>(std::ceil(start.x));
    const int y = static_cast<int>(std::ceil(start.y));
    return {x, y, static_cast<int>(std::ceil(end.x)) - x, static_cast<int>(std::ceil(end.y)) - y};
}

Point Mapping::clamp(Point point) const
{
    return {std::clamp(point.x, double(clip.left), double(clip.right - 1)),
        std::clamp(point.y, double(clip.top), double(clip.bottom - 1))};
}

Point Mapping::from_window(double x, double y) const
{
    return clamp(presentation.to_client(x, y));
}

Point Mapping::delta(double x, double y, double dx, double dy) const
{
    const auto before = from_window(x - dx, y - dy);
    const auto after = from_window(x, y);
    return {after.x - before.x, after.y - before.y};
}

void bind(HWND handle)
{
    const auto id = SDL_GetWindowID(video(handle)->window());
    if (bound != handle || bound_id != id)
    {
        bound = handle;
        bound_id = id;
        clip_screen.reset();
    }
}

bool mapping(HWND handle, Mapping& result)
{
    if (!SDL_IsMainThread())
    {
        unsupported("mouse coordinate mapping", "SDL window geometry requires the main thread");
    }
    if (!handle)
    {
        return false;
    }
    result.presentation = video(handle)->presentation_transform();
    if (!result.presentation.valid())
    {
        return false;
    }
    int x = 0, y = 0;
    if (!result.presentation.fullscreen && !SDL_GetWindowPosition(video(handle)->window(), &x, &y))
    {
        return false;
    }
    result.origin = {x, y};
    result.clip = {0, 0, result.presentation.width, result.presentation.height};
    if (bound == handle && clip_screen)
    {
        const auto edge = [](LONG screen, int origin, int limit)
        {
            return static_cast<LONG>(
                std::clamp<std::int64_t>(std::int64_t(screen) - origin, 0, limit));
        };
        result.clip = {edge(clip_screen->left, x, result.presentation.width),
            edge(clip_screen->top, y, result.presentation.height),
            edge(clip_screen->right, x, result.presentation.width),
            edge(clip_screen->bottom, y, result.presentation.height)};
    }
    return result.clip.left < result.clip.right && result.clip.top < result.clip.bottom;
}

bool cursor_position(POINT& point)
{
    // GetCursorPos never fails on Windows and the game ignores its result, so
    // when the live position is unknown (Wayland while the pointer is outside
    // the window) report the last known one instead of failing.
    static std::optional<POINT> last;
    auto* window = bound_window();
    Mapping map;
    if (!window || !mapping(bound, map))
    {
        return false;
    }
    if (clip_screen)
    {
        apply_clip(window, map);
    }
    float x = 0, y = 0;
    if (SDL_GetMouseFocus() == window)
    {
        SDL_GetMouseState(&x, &y);
    }
    else
    {
        const auto* driver = SDL_GetCurrentVideoDriver();
        int left = 0, top = 0;
        if (!driver || std::strcmp(driver, "wayland") == 0 ||
            !SDL_GetWindowPosition(window, &left, &top))
        {
            if (!last)
            {
                return false;
            }
            point = *last;
            return true;
        }
        SDL_GetGlobalMouseState(&x, &y);
        x -= left;
        y -= top;
    }
    auto client = map.from_window(x, y);
    // As on Windows, an active ClipCursor rectangle bounds the position.
    if (clip_screen)
    {
        client = map.clamp(client);
    }
    const auto sx = std::int64_t(map.origin.x) + static_cast<LONG>(std::floor(client.x));
    const auto sy = std::int64_t(map.origin.y) + static_cast<LONG>(std::floor(client.y));
    if (sx < INT32_MIN || sx > INT32_MAX || sy < INT32_MIN || sy > INT32_MAX)
    {
        return false;
    }
    point = {static_cast<LONG>(sx), static_cast<LONG>(sy)};
    last = point;
    return true;
}

bool set_cursor_position(int x, int y)
{
    auto* window = bound_window();
    Mapping map;
    if (!window || !mapping(bound, map) || (clip_screen && !apply_clip(window, map)))
    {
        return false;
    }
    const char* driver = SDL_GetCurrentVideoDriver();
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) ||
        (driver && std::strcmp(driver, "wayland") == 0 && SDL_GetMouseFocus() != window))
    {
        return false; // These SDL cases cannot move the host pointer.
    }
    const auto client = map.clamp({double(x) - map.origin.x, double(y) - map.origin.y});
    // Aim inside the logical pixel, avoiding a floor/rounding step into its
    // neighbour. Window-local warping also works on Wayland.
    const auto host = map.presentation.to_window(client.x + 0.5, client.y + 0.5);
    SDL_ClearError();
    SDL_WarpMouseInWindow(window, static_cast<float>(host.x), static_cast<float>(host.y));
    return !*SDL_GetError();
}

bool clip_cursor(const RECT* rectangle)
{
    auto* window = bound_window();
    if (!window)
    {
        return false;
    }
    if (!rectangle)
    {
        if (!SDL_SetWindowMouseGrab(window, false) ||
            (SDL_GetWindowMouseRect(window) && !SDL_SetWindowMouseRect(window, nullptr)))
        {
            return false;
        }
        clip_screen.reset();
        return true;
    }
    const auto previous = clip_screen;
    std::optional<SDL_Rect> native_previous;
    if (const auto* old = SDL_GetWindowMouseRect(window))
    {
        native_previous = *old;
    }
    clip_screen = *rectangle;
    Mapping map;
    if (!mapping(bound, map) || !apply_clip(window, map) || !SDL_SetWindowMouseGrab(window, true))
    {
        clip_screen = previous;
        SDL_SetWindowMouseRect(window, native_previous ? &*native_previous : nullptr);
        return false;
    }
    // Windows moves a cursor that lies outside a newly established clip.
    POINT point;
    if (cursor_position(point))
    {
        return set_cursor_position(point.x, point.y);
    }
    return true; // SDL constrains the cursor when this window gains focus.
}
} // namespace beachlife::compat::input
