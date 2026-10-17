#pragma once

#include "launcher/settings.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

// The performance overlay's numbers and layout, adapted from Recharged without
// its network latency column. Everything here is in physical pixels.
namespace beachlife::runtime
{

using launcher::OverlayCorner;

inline constexpr std::array<const char*, 3> performance_labels = {"FPS", "GPU", "CPU"};

// A measured value as shown, or none for N/A.
inline std::optional<int> overlay_number(std::optional<double> value, int maximum)
{
    if (!value || !std::isfinite(*value) || *value < 0)
    {
        return std::nullopt;
    }
    return static_cast<int>(std::lround(std::min(*value, static_cast<double>(maximum))));
}

inline float overlay_short_side(std::uint32_t width, std::uint32_t height)
{
    return static_cast<float>(std::min(width, height));
}

// Same size for labels and values; the values use a heavier weight.
inline float overlay_font_size(std::uint32_t width, std::uint32_t height)
{
    return std::max(12.0f, std::round(overlay_short_side(width, height) * 0.014f));
}

struct OverlayText
{
    float label_width;
    float value_width;
    float value_slot; // the widest value, so no column ever moves
};

struct OverlayLayout
{
    float left, top, right, bottom;
    float text_y;
    std::array<float, 3> label_x, value_x;
    std::array<float, 2> separator_x;
};

inline OverlayLayout layout_overlay(std::uint32_t surface_width,
    std::uint32_t surface_height,
    OverlayCorner corner,
    float font_size,
    const std::array<OverlayText, 3>& text)
{
    const float gap = std::round(font_size * 0.4f);
    const float separator_gap = std::round(font_size * 0.9f);
    const float height = std::round(font_size * 1.4f);
    float width = 2 * static_cast<float>(text.size() - 1) * separator_gap;
    for (const auto& metric : text)
    {
        width += std::ceil(metric.label_width) + gap + std::ceil(metric.value_slot);
    }
    const float margin =
        std::max(8.0f, std::round(overlay_short_side(surface_width, surface_height) * 0.015f));
    const bool right = corner == OverlayCorner::TopRight || corner == OverlayCorner::BottomRight;
    const bool bottom = corner == OverlayCorner::BottomLeft || corner == OverlayCorner::BottomRight;
    OverlayLayout layout{};
    layout.left =
        right ? std::max(0.0f, static_cast<float>(surface_width) - margin - width) : margin;
    layout.top =
        bottom ? std::max(0.0f, static_cast<float>(surface_height) - margin - height) : margin;
    layout.right = layout.left + width;
    layout.bottom = layout.top + height;
    layout.text_y = std::round(layout.top + (height - font_size) * 0.5f);
    float x = layout.left;
    for (std::size_t n = 0; n < text.size(); ++n)
    {
        layout.label_x[n] = x;
        x += std::ceil(text[n].label_width) + gap + std::ceil(text[n].value_slot);
        layout.value_x[n] = x - std::ceil(text[n].value_width);
        if (n < layout.separator_x.size())
        {
            layout.separator_x[n] = x + separator_gap;
            x += 2 * separator_gap;
        }
    }
    return layout;
}

// Frames presented per second, measured over windows of at least one second.
class FrameRateMeter
{
  public:
    using Clock = std::chrono::steady_clock;

    // The rate each time a measurement window closes.
    std::optional<double> sample(std::uint64_t frames, Clock::time_point now)
    {
        if (!start_ || frames < start_frames_)
        {
            start_ = now;
            start_frames_ = frames;
            return std::nullopt;
        }
        const std::chrono::duration<double> elapsed = now - *start_;
        if (elapsed < std::chrono::seconds(1))
        {
            return std::nullopt;
        }
        const double rate = static_cast<double>(frames - start_frames_) / elapsed.count();
        start_ = now;
        start_frames_ = frames;
        return rate;
    }

  private:
    std::optional<Clock::time_point> start_;
    std::uint64_t start_frames_ = 0;
};

} // namespace beachlife::runtime
