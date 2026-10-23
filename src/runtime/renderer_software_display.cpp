#include "renderer_software_display.h"

#include <limits>
#include <stdexcept>
#include <string>

namespace beachlife::runtime::renderer_display
{
namespace
{
[[noreturn]] void fail(const char* operation)
{
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

SDL_Window* checked_window(platform::Video& video, SDL_WindowID id, std::thread::id thread)
{
    if (!SDL_IsMainThread() || thread != std::this_thread::get_id())
    {
        throw std::logic_error("Renderer display operation is on the wrong thread");
    }
    SDL_Window* window = SDL_GetWindowFromID(id);
    if (!window)
    {
        fail("Renderer window is no longer available");
    }
    if (window != video.window())
    {
        throw std::logic_error("Renderer display window identity changed");
    }
    return window;
}

int coordinate(std::int64_t value)
{
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
    {
        throw std::overflow_error("Renderer window coordinate is out of range");
    }
    return static_cast<int>(value);
}

struct Borders
{
    int top, left, bottom, right;
};

Borders borders(SDL_Window* window)
{
    Borders result{};
    if (!SDL_GetWindowBordersSize(window, &result.top, &result.left, &result.bottom, &result.right))
    {
        fail("Cannot query renderer window borders");
    }
    return result;
}

void sync_window(SDL_Window* window)
{
    if (!SDL_SyncWindow(window))
    {
        fail("Cannot synchronize renderer window change");
    }
}
} // namespace

platform::Frame convert_rgb555(
    std::span<const std::byte> pixels, int width, int height, std::size_t row_stride)
{
    if (width <= 0 || height <= 0)
    {
        throw std::invalid_argument("Renderer frame dimensions must be positive");
    }
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    const auto limit = std::numeric_limits<std::size_t>::max();
    if (w > limit / 2 || w > limit / h)
    {
        throw std::overflow_error("Renderer frame dimensions overflow");
    }
    const std::size_t row_bytes = w * 2;
    if (row_stride < row_bytes || (h - 1) > (limit - row_bytes) / row_stride)
    {
        throw std::invalid_argument("Renderer DIB row extent is invalid");
    }
    const std::size_t required = (h - 1) * row_stride + row_bytes;
    if (pixels.size() < required)
    {
        throw std::invalid_argument("Renderer DIB storage is incomplete");
    }

    platform::Frame frame;
    frame.width = width;
    frame.height = height;
    frame.pixels.resize(w * h);
    for (std::size_t y = 0; y < h; ++y)
    {
        const std::byte* row = pixels.data() + y * row_stride;
        for (std::size_t x = 0; x < w; ++x)
        {
            // DIB words are little-endian, regardless of the host byte order.
            const auto low = std::to_integer<std::uint32_t>(row[x * 2]);
            const auto high = std::to_integer<std::uint32_t>(row[x * 2 + 1]);
            const std::uint32_t pixel = low | (high << 8);
            const std::uint32_t red = (pixel >> 10) & 31;
            const std::uint32_t green = (pixel >> 5) & 31;
            const std::uint32_t blue = pixel & 31;
            const std::uint32_t r8 = (red << 3) | (red >> 2);
            const std::uint32_t g8 = (green << 3) | (green >> 2);
            const std::uint32_t b8 = (blue << 3) | (blue >> 2);
            frame.pixels[y * w + x] = r8 | (g8 << 8) | (b8 << 16) | 0xff000000u;
        }
    }
    return frame;
}

DrawingContext::DrawingContext(platform::Video& video, SDL_WindowID window, std::thread::id thread)
    : video_(video)
    , window_(window)
    , thread_(thread)
{
}

void DrawingContext::present_rgb555(std::span<const std::byte> pixels,
    int width,
    int height,
    std::size_t row_stride,
    ImDrawData* overlay)
{
    checked_window(video_, window_, thread_);
    frame_ = convert_rgb555(pixels, width, height, row_stride);
    video_.set_frame_size(width, height);
    video_.present(frame_, overlay);
}

SoftwareDisplay::SoftwareDisplay(platform::Video& video)
    : video_(video)
    , window_(0)
    , thread_(std::this_thread::get_id())
{
    if (!SDL_IsMainThread())
    {
        throw std::logic_error("Renderer display must be created on SDL's main thread");
    }
    SDL_Window* window = video.window();
    if (!window)
    {
        throw std::invalid_argument("Renderer display needs an existing game window");
    }
    window_ = SDL_GetWindowID(window);
    if (!window_)
    {
        fail("Cannot identify renderer window");
    }
}

SDL_Window* SoftwareDisplay::window() const
{
    return checked_window(video_, window_, thread_);
}

std::vector<DisplayMode> SoftwareDisplay::enumerate_modes() const
{
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window());
    if (!display)
    {
        fail("Cannot identify renderer display");
    }
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
    if (!modes)
    {
        fail("Cannot enumerate renderer display modes");
    }
    const std::unique_ptr<void, decltype(&SDL_free)> storage(modes, SDL_free);
    std::vector<DisplayMode> result;
    result.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index)
    {
        const SDL_DisplayMode& mode = *modes[index];
        result.push_back({mode.w, mode.h, mode.format, mode.refresh_rate});
    }
    return result;
}

void SoftwareDisplay::use_windowed_mode()
{
    SDL_Window* target = window();
    if (!SDL_SetWindowFullscreen(target, false))
    {
        fail("Cannot select renderer windowed mode");
    }
    sync_window(target);
}

void SoftwareDisplay::resize_client(int width, int height)
{
    SDL_Window* target = window();
    if (!SDL_SetWindowSize(target, width, height))
    {
        fail("Cannot resize renderer client area");
    }
    sync_window(target);
    video_.set_game_mode(width, height, false);
}

void SoftwareDisplay::clear_topmost()
{
    if (!SDL_SetWindowAlwaysOnTop(window(), false))
    {
        fail("Cannot clear renderer topmost state");
    }
}

Rectangle SoftwareDisplay::work_area() const
{
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window());
    if (!display)
    {
        fail("Cannot identify renderer work-area display");
    }
    SDL_Rect rectangle{};
    if (!SDL_GetDisplayUsableBounds(display, &rectangle))
    {
        fail("Cannot query renderer work area");
    }
    return {rectangle.x,
        rectangle.y,
        coordinate(static_cast<std::int64_t>(rectangle.x) + rectangle.w),
        coordinate(static_cast<std::int64_t>(rectangle.y) + rectangle.h)};
}

Rectangle SoftwareDisplay::client_rectangle_in_screen() const
{
    SDL_Window* target = window();
    int x, y, width, height;
    if (!SDL_GetWindowPosition(target, &x, &y) || !SDL_GetWindowSize(target, &width, &height))
    {
        fail("Cannot query renderer client rectangle");
    }
    return {x,
        y,
        coordinate(static_cast<std::int64_t>(x) + width),
        coordinate(static_cast<std::int64_t>(y) + height)};
}

Rectangle SoftwareDisplay::outer_rectangle() const
{
    SDL_Window* target = window();
    const Rectangle client = client_rectangle_in_screen();
    const Borders border = borders(target);
    return {coordinate(static_cast<std::int64_t>(client.left) - border.left),
        coordinate(static_cast<std::int64_t>(client.top) - border.top),
        coordinate(static_cast<std::int64_t>(client.right) + border.right),
        coordinate(static_cast<std::int64_t>(client.bottom) + border.bottom)};
}

void SoftwareDisplay::move_outer(int left, int top)
{
    SDL_Window* target = window();
    const Borders border = borders(target);
    const int x = coordinate(static_cast<std::int64_t>(left) + border.left);
    const int y = coordinate(static_cast<std::int64_t>(top) + border.top);
    if (!SDL_SetWindowPosition(target, x, y))
    {
        fail("Cannot move renderer window");
    }
    sync_window(target);
}

std::unique_ptr<DrawingContext> SoftwareDisplay::acquire_context() const
{
    window();
    return std::unique_ptr<DrawingContext>(new DrawingContext(video_, window_, thread_));
}
} // namespace beachlife::runtime::renderer_display
