#include "renderer_fullscreen_display.h"

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

void sync_window(SDL_Window* window)
{
    if (!SDL_SyncWindow(window))
    {
        fail("Cannot synchronize fullscreen renderer window");
    }
}
} // namespace

FlipChain16::FlipChain16(int width, int height, WordFormat format)
    : front_(width, height, format)
    , back_(width, height, format)
{
}

std::span<std::uint16_t> FlipChain16::lock_back()
{
    return back_.lock();
}

void FlipChain16::unlock_back()
{
    back_.unlock();
}

std::span<const std::uint16_t> FlipChain16::front_pixels() const
{
    return front_.pixels();
}

void FlipChain16::present(platform::Video& video, ImDrawData* overlay, bool& presentation_failed)
{
    if (locked())
    {
        throw std::logic_error("Cannot flip a locked fullscreen surface");
    }
    const auto frame = convert_words(back_.pixels(), width(), height(), format());
    try
    {
        video.present(frame, overlay);
    }
    catch (...)
    {
        // SDL GPU exposes errors on actual operations, not a renderer-reset
        // event that recreates this Video's device. Never report mode recovery
        // as recovery from a failed Video presentation.
        presentation_failed = true;
        throw;
    }
    // Roles keep their addresses and keys; only their backing allocations move.
    // A thrown conversion/presentation leaves both allocations in their roles.
    front_.exchange_storage(back_);
}

FullscreenDisplay::FullscreenDisplay(platform::Video& video)
    : video_(video)
    , window_(0)
    , thread_(std::this_thread::get_id())
{
    if (!SDL_IsMainThread())
    {
        throw std::logic_error("Fullscreen display must be created on SDL's main thread");
    }
    SDL_Window* target = video.window();
    if (!target)
    {
        throw std::invalid_argument("Fullscreen display needs an existing Video window");
    }
    window_ = SDL_GetWindowID(target);
    if (!window_)
    {
        fail("Cannot identify fullscreen renderer window");
    }
}

SDL_Window* FullscreenDisplay::window() const
{
    if (!SDL_IsMainThread() || thread_ != std::this_thread::get_id())
    {
        throw std::logic_error("Fullscreen display operation is on the wrong thread");
    }
    SDL_Window* target = SDL_GetWindowFromID(window_);
    if (!target)
    {
        fail("Fullscreen renderer window no longer exists");
    }
    if (target != video_.window())
    {
        throw std::logic_error("Fullscreen renderer window identity changed");
    }
    return target;
}

std::vector<DisplayMode> FullscreenDisplay::enumerate_modes() const
{
    window();
    return SoftwareDisplay(video_).enumerate_modes();
}

void FullscreenDisplay::enter_fullscreen()
{
    SDL_Window* target = window();
    if (!SDL_SetWindowFullscreen(target, true))
    {
        fail("Cannot enter fullscreen renderer mode");
    }
    sync_window(target);
    if (!(SDL_GetWindowFlags(target) & SDL_WINDOW_FULLSCREEN))
    {
        throw std::runtime_error("Window system did not enter fullscreen mode");
    }
}

void FullscreenDisplay::set_display_mode(int width, int height, unsigned int bits)
{
    SDL_Window* target = window();
    if (!(SDL_GetWindowFlags(target) & SDL_WINDOW_FULLSCREEN))
    {
        throw std::logic_error("Select fullscreen before setting the display mode");
    }
    (void)WordSurface16::checked_word_count(width, height);
    if (bits != 16)
    {
        throw std::invalid_argument("Fullscreen renderer needs a 16-bit logical surface");
    }
    const SDL_DisplayID display = SDL_GetDisplayForWindow(target);
    if (!display)
    {
        fail("Cannot identify fullscreen renderer display");
    }
    if (display_ && display != display_)
    {
        throw std::runtime_error("Fullscreen renderer moved to another display");
    }
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
    if (!modes)
    {
        fail("Cannot enumerate fullscreen renderer modes");
    }
    const std::unique_ptr<void, decltype(&SDL_free)> storage(modes, SDL_free);
    const SDL_DisplayMode* selected = nullptr;
    for (int index = 0; index < count; ++index)
    {
        // The first domain preserves literal pixel geometry. A different
        // density, closest resolution or borderless fallback is not selected.
        if (modes[index]->w == width && modes[index]->h == height &&
            modes[index]->pixel_density == 1.0f)
        {
            selected = modes[index];
            break;
        }
    }
    if (!selected)
    {
        throw std::runtime_error("The requested exact fullscreen mode is unavailable");
    }
    mode_ready_ = false;
    if (!SDL_SetWindowFullscreenMode(target, selected))
    {
        fail("Cannot select fullscreen renderer resolution");
    }
    sync_window(target);
    const SDL_DisplayMode* applied = SDL_GetCurrentDisplayMode(display);
    int pixel_width = 0, pixel_height = 0;
    if (!applied || !SDL_GetWindowSizeInPixels(target, &pixel_width, &pixel_height))
    {
        fail("Cannot query applied fullscreen renderer resolution");
    }
    if (applied->w != width || applied->h != height || applied->pixel_density != 1.0f ||
        pixel_width != width || pixel_height != height || !SDL_GetWindowFullscreenMode(target))
    {
        throw std::runtime_error("Window system did not apply the requested exact fullscreen mode");
    }
    width_ = width;
    height_ = height;
    display_ = display;
    mode_ready_ = true;
}

bool FullscreenDisplay::mode_changed() const
{
    SDL_Window* target = window();
    if (presentation_failed_)
    {
        throw std::runtime_error(
            "Fullscreen Video presentation failed; device recovery is unavailable");
    }
    if (!display_ || width_ <= 0 || height_ <= 0)
    {
        throw std::logic_error("Fullscreen renderer has no established mode to recover");
    }
    const SDL_DisplayID display = SDL_GetDisplayForWindow(target);
    if (!display)
    {
        fail("Cannot identify fullscreen renderer recovery display");
    }
    if (display != display_)
    {
        throw std::runtime_error("Fullscreen renderer moved to another display");
    }
    const auto flags = SDL_GetWindowFlags(target);
    if (flags & SDL_WINDOW_MINIMIZED)
    {
        throw std::runtime_error("Restore the minimized renderer window before surface recovery");
    }
    if (!mode_ready_ || !(flags & SDL_WINDOW_FULLSCREEN))
    {
        return true;
    }
    const SDL_DisplayMode* requested = SDL_GetWindowFullscreenMode(target);
    const SDL_DisplayMode* applied = SDL_GetCurrentDisplayMode(display);
    if (!applied)
    {
        fail("Cannot query fullscreen renderer recovery mode");
    }
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(target, &width, &height))
    {
        fail("Cannot query fullscreen renderer pixels");
    }
    // A null requested mode denotes borderless desktop fullscreen. Matching
    // dimensions alone must not make that an accepted exclusive mode.
    return !requested || requested->displayID != display_ || requested->w != width_ ||
           requested->h != height_ || requested->pixel_density != 1.0f ||
           applied->displayID != display_ || applied->w != width_ || applied->h != height_ ||
           applied->pixel_density != 1.0f || width != width_ || height != height_;
}

void FullscreenDisplay::require_mode() const
{
    if (mode_changed())
    {
        throw std::runtime_error("Fullscreen renderer exact mode needs recovery");
    }
}

void FullscreenDisplay::validate_surface(
    const FlipChain16& chain, const WordSurface16& surface) const
{
    chain.front_.validate_storage();
    chain.back_.validate_storage();
    surface.validate_storage();
    const auto back = chain.back_.description();
    if (chain.width() != width_ || chain.height() != height_ || back.width != width_ ||
        back.height != height_ || back.format != chain.format() ||
        surface.description().format != chain.format())
    {
        throw std::logic_error("Fullscreen surface does not match its owned chain");
    }
}

bool FullscreenDisplay::surface_lost(const FlipChain16& chain, const WordSurface16& surface) const
{
    validate_surface(chain, surface);
    return mode_changed();
}

void FullscreenDisplay::restore_surface(const FlipChain16& chain, WordSurface16& surface)
{
    validate_surface(chain, surface);
    if (chain.locked() || surface.locked())
    {
        throw std::logic_error("Cannot restore fullscreen mode while surface pixels are borrowed");
    }
    if (!mode_changed())
    {
        return;
    }
    // CPU allocations, keys and pixel values survive the actual mode change.
    // Reapply the original exact-size/density selection, not a nearest mode or
    // mode1. If either request or verification fails, the error propagates.
    enter_fullscreen();
    set_display_mode(width_, height_, 16);
    require_mode();
}

std::unique_ptr<FlipChain16> FullscreenDisplay::create_primary(WordFormat format) const
{
    require_mode();
    return std::unique_ptr<FlipChain16>(new FlipChain16(width_, height_, format));
}

Rectangle FullscreenDisplay::screen_rectangle() const
{
    require_mode();
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window());
    if (!display)
    {
        fail("Cannot identify fullscreen renderer rectangle display");
    }
    SDL_Rect bounds{};
    if (!SDL_GetDisplayBounds(display, &bounds))
    {
        fail("Cannot query fullscreen renderer screen bounds");
    }
    if (bounds.w != width_ || bounds.h != height_)
    {
        throw std::runtime_error("Fullscreen renderer display geometry differs from its mode");
    }
    // Keep the original zero-based rectangle, using this actual SDL display's
    // geometry. Original GetSystemMetrics used the Windows primary screen.
    return {0, 0, bounds.w, bounds.h};
}

void FullscreenDisplay::present(FlipChain16& chain, ImDrawData* overlay)
{
    require_mode();
    if (chain.width() != width_ || chain.height() != height_)
    {
        throw std::invalid_argument("Fullscreen chain does not match the active mode");
    }
    chain.present(video_, overlay, presentation_failed_);
}

void FullscreenDisplay::restore_windowed()
{
    SDL_Window* target = window();
    if (!SDL_SetWindowFullscreen(target, false))
    {
        fail("Cannot restore renderer windowed mode");
    }
    sync_window(target);
    if (SDL_GetWindowFlags(target) & SDL_WINDOW_FULLSCREEN)
    {
        throw std::runtime_error("Window system did not restore windowed mode");
    }
    mode_ready_ = false;
}
} // namespace beachlife::runtime::renderer_display
