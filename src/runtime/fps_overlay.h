#pragma once

#include "platform/system_usage.h"
#include "runtime/fps_overlay_layout.h"

#include <filesystem>
#include <optional>
#include <vector>

struct ImFont;
struct ImFontAtlas;

namespace beachlife::runtime
{

// The performance overlay (display.fps_overlay): FPS | GPU % | CPU %, drawn over
// the presented picture in windowed and fullscreen mode. Host presentation only;
// the game never sees it. Adapted from Recharged's, without its latency column.
class FpsOverlay
{
  public:
    // `font` is the launcher's Roboto; a missing font is an incomplete package.
    FpsOverlay(const std::filesystem::path& font, OverlayCorner corner);

    // A new measurement, in frames per second.
    void set_rate(double rate);
    // Before ImGui::NewFrame: whether the font atlas needs this overlay's fonts
    // (again) for the window's size in pixels and its pixel density.
    bool needs_fonts(int pixel_width, int pixel_height, float density) const;
    // Adds the label and value fonts to the atlas being rebuilt.
    void add_fonts(ImFontAtlas& atlas, int pixel_width, int pixel_height, float density);
    // Inside the ImGui frame, on the foreground draw list. Unknown measurements
    // show N/A, including during the first sample window.
    void draw(int pixel_width, int pixel_height, float density) const;

  private:
    platform::SystemUsageMonitor usage_;
    std::vector<unsigned char> font_data_;
    OverlayCorner corner_;
    std::optional<int> fps_;
    float built_size_ = 0, built_density_ = 0;
    ImFont* number_ = nullptr;
    ImFont* label_ = nullptr;
};

} // namespace beachlife::runtime
