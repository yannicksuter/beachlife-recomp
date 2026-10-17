// The performance overlay's numbers, layout and frame rate windows (Recharged's checks, three columns).
#include "runtime/fps_overlay_layout.h"

#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace beachlife::runtime;
using namespace std::chrono_literals;
unsigned checks;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

int main() {
    try {
        check(overlay_number(59.6, 999) == 60 && overlay_number(0, 100) == 0, "Measured values round; zero is valid");
        check(overlay_number(123, 100) == 100 && overlay_number(1e100, 999) == 999, "Clamp before integer conversion");
        check(!overlay_number(std::nullopt, 100) && !overlay_number(-1, 100) &&
              !overlay_number(std::numeric_limits<double>::infinity(), 999) &&
              !overlay_number(std::numeric_limits<double>::quiet_NaN(), 999), "Unknown/invalid measurements stay N/A");
        check(overlay_font_size(1920, 1080) == 15 && overlay_font_size(3840, 2160) == 30,
              "Physical font size follows display resolution");
        check(overlay_font_size(320, 200) == 12, "Small windows retain readable text");

        const std::uint32_t sizes[][2] = {{320, 200}, {960, 540}, {1280, 720}, {1920, 1080}, {3840, 2160}, {600, 900},
                                          {1440, 1080}};
        const OverlayCorner corners[] = {OverlayCorner::TopLeft, OverlayCorner::TopRight, OverlayCorner::BottomLeft,
                                         OverlayCorner::BottomRight};
        for (const auto& size : sizes)
            for (const auto corner : corners) {
                const float font = overlay_font_size(size[0], size[1]), scale = font/15.0f;
                std::array<OverlayText, 3> text = {{{25*scale, 17*scale, 26*scale}, {28*scale, 25*scale, 36*scale},
                                                    {28*scale, 17*scale, 36*scale}}};
                const auto layout = layout_overlay(size[0], size[1], corner, font, text);
                const float margin = std::max(8.0f, std::round(std::min(size[0], size[1])*0.015f));
                const bool right = corner == OverlayCorner::TopRight || corner == OverlayCorner::BottomRight;
                const bool bottom = corner == OverlayCorner::BottomLeft || corner == OverlayCorner::BottomRight;
                check(right ? layout.right == size[0] - margin : layout.left == margin, "Horizontal corner margin");
                check(bottom ? layout.bottom == size[1] - margin : layout.top == margin, "Vertical corner margin");
                check(layout.left >= 0 && layout.top >= 0 && layout.right <= size[0] && layout.bottom <= size[1],
                      "All metrics stay in the window");
                for (std::size_t n = 0; n < text.size(); ++n) {
                    check(layout.label_x[n] == std::round(layout.label_x[n]) &&
                          layout.value_x[n] == std::round(layout.value_x[n]) &&
                          layout.text_y == std::round(layout.text_y), "Whole pixel glyph positions");
                    check(layout.value_x[n] >= layout.label_x[n] + text[n].label_width, "Labels precede their values");
                    if (n < layout.separator_x.size())
                        check(layout.separator_x[n] > layout.value_x[n] + text[n].value_width &&
                              layout.separator_x[n] < layout.label_x[n + 1], "Dividers separate metric groups");
                }
                for (auto& metric : text) metric.value_width = metric.value_slot;
                const auto widest = layout_overlay(size[0], size[1], corner, font, text);
                check(layout.left == widest.left && layout.right == widest.right && layout.label_x == widest.label_x &&
                      layout.separator_x == widest.separator_x, "Changing digit counts or N/A never shifts columns");
            }

        const FrameRateMeter::Clock::time_point t0{};
        FrameRateMeter meter;
        check(!meter.sample(100, t0), "First sample only opens the window");
        check(!meter.sample(115, t0 + 500ms), "No rate before one second");
        auto rate = meter.sample(130, t0 + 1s);
        check(rate && *rate == 30.0, "Thirty frames in one second");
        check(!meter.sample(150, t0 + 1500ms), "Window restarts after each rate");
        check(meter.sample(189, t0 + 2s) == 59.0, "Rate covers only the new window");
        check(meter.sample(219, t0 + 4s) == 15.0, "Long windows average their frames");
        rate = meter.sample(219, t0 + 5s);
        check(rate && *rate == 0.0, "Stalled presentation reports zero");
        rate = meter.sample(248, t0 + 6s + 50ms);
        check(rate && std::abs(*rate - 29.0/1.05) < 1e-9, "Rate uses the actual elapsed time");
        check(!meter.sample(10, t0 + 8s), "A restarted counter opens a new window");
        check(meter.sample(70, t0 + 9s) == 60.0, "Measurement resumes after a restart");
        std::cout << checks << " performance overlay checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
