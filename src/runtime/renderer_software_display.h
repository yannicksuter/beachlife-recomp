#pragma once

#include "platform/video.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <vector>

// Native platform service for the renderer's windowed software path. These are
// real SDL/Video operations, not Win32 exports or reconstructed game methods.
namespace beachlife::runtime::renderer_display
{
struct Rectangle
{
    int left, top, right, bottom;
};

struct DisplayMode
{
    int width, height;
    std::uint32_t pixel_format;
    float refresh_rate;
};

// Converts a top-down RGB555 DIB into Video's RGBA32 frame. The caller supplies
// the actual DIB row stride and complete byte extent; bit15 is ignored. This
// does not select, draw or unlock a retail surface and does not run game code.
platform::Frame convert_rgb555(
    std::span<const std::byte> pixels, int width, int height, std::size_t row_stride);

class SoftwareDisplay;

// An independent drawing lease, corresponding to the renderer's acquired
// window drawing context. Video and its SDL_Window must outlive the lease.
// Releasing a SoftwareDisplay does not invalidate an already acquired context.
class DrawingContext
{
  public:
    ~DrawingContext() = default;
    DrawingContext(const DrawingContext&) = delete;
    DrawingContext& operator=(const DrawingContext&) = delete;

    void present_rgb555(std::span<const std::byte> pixels,
        int width,
        int height,
        std::size_t row_stride,
        ImDrawData* overlay = nullptr);
    const platform::Frame& last_frame() const noexcept
    {
        return frame_;
    }

  private:
    friend class SoftwareDisplay;
    DrawingContext(platform::Video& video, SDL_WindowID window, std::thread::id thread);
    platform::Video& video_;
    SDL_WindowID window_;
    std::thread::id thread_;
    platform::Frame frame_;
};

// Borrows the existing game window; never opens another window/GPU or changes
// the launcher. Calls must run on SDL's main thread and the construction thread.
// Failures throw a host error. Mode enumeration includes the logical framebuffer
// sizes supported by the host's display emulation.
class SoftwareDisplay
{
  public:
    explicit SoftwareDisplay(platform::Video& video);
    ~SoftwareDisplay() = default;
    SoftwareDisplay(const SoftwareDisplay&) = delete;
    SoftwareDisplay& operator=(const SoftwareDisplay&) = delete;

    std::vector<DisplayMode> enumerate_modes() const;
    void use_windowed_mode();
    void resize_client(int width, int height);
    void clear_topmost();
    Rectangle work_area() const;
    Rectangle outer_rectangle() const;
    void move_outer(int left, int top);
    Rectangle client_rectangle_in_screen() const;
    std::unique_ptr<DrawingContext> acquire_context() const;

  private:
    SDL_Window* window() const;
    platform::Video& video_;
    SDL_WindowID window_;
    std::thread::id thread_;
};
} // namespace beachlife::runtime::renderer_display
