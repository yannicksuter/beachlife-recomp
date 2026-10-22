#pragma once

#include "runtime/renderer_word_surface.h"

namespace beachlife::runtime::renderer_display
{
// Owns two stable surface roles. A successful flip exchanges their allocations,
// never the surface objects, so a retained back-surface reference stays a back
// reference. Destruction ends both roles; borrowed references cannot outlive it.
// Pixels remain uninitialized until the caller writes them.
class FlipChain16
{
  public:
    ~FlipChain16() = default;
    FlipChain16(const FlipChain16&) = delete;
    FlipChain16& operator=(const FlipChain16&) = delete;

    int width() const noexcept
    {
        return front_.description().width;
    }
    int height() const noexcept
    {
        return front_.description().height;
    }
    std::size_t pitch() const noexcept
    {
        return front_.description().pitch;
    }
    WordFormat format() const noexcept
    {
        return front_.description().format;
    }
    bool locked() const
    {
        return front_.locked() || back_.locked();
    }

    WordSurface16& front_surface() noexcept
    {
        return front_;
    }
    WordSurface16& back_surface() noexcept
    {
        return back_;
    }
    std::span<std::uint16_t> lock_back();
    void unlock_back();
    // Contents are defined only after the caller initializes the front storage
    // or a completed flip publishes initialized back storage here.
    std::span<const std::uint16_t> front_pixels() const;

  private:
    friend class FullscreenDisplay;
    FlipChain16(int width, int height, WordFormat format);
    void present(platform::Video& video, ImDrawData* overlay, bool& presentation_failed);

    WordSurface16 front_, back_;
};

// Borrows the actual Video/window and uses its existing SDL GPU presentation.
// Video/window must outlive this service. Mode changes are explicit, including
// restoration at the original device-release point; destruction only frees
// host storage and does not invent a game rollback after a failed operation.
class FullscreenDisplay
{
  public:
    explicit FullscreenDisplay(platform::Video& video);
    FullscreenDisplay(const FullscreenDisplay&) = delete;
    FullscreenDisplay& operator=(const FullscreenDisplay&) = delete;

    std::vector<DisplayMode> enumerate_modes() const;
    void enter_fullscreen();
    void set_display_mode(int width, int height, unsigned int bits);
    std::unique_ptr<FlipChain16> create_primary(WordFormat format) const;
    Rectangle screen_rectangle() const;
    void present(FlipChain16& chain, ImDrawData* overlay = nullptr);
    void restore_windowed();

    // Loss here means that the established exact display mode no longer
    // applies to this same live window/display. CPU pixels survive that
    // transition. This is not a query or recreation of a lost GPU device.
    bool surface_lost(const FlipChain16& chain, const WordSurface16& surface) const;
    void restore_surface(const FlipChain16& chain, WordSurface16& surface);

  private:
    SDL_Window* window() const;
    void require_mode() const;
    bool mode_changed() const;
    void validate_surface(const FlipChain16& chain, const WordSurface16& surface) const;
    platform::Video& video_;
    SDL_WindowID window_;
    std::thread::id thread_;
    int width_ = 0, height_ = 0;
    bool mode_ready_ = false;
    SDL_DisplayID display_ = 0;
    bool presentation_failed_ = false;
};
} // namespace beachlife::runtime::renderer_display
