#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct ImFont;

namespace beachlife::platform
{
class Video;
struct Frame;
} // namespace beachlife::platform

namespace beachlife::runtime
{

// The loading screen shown before the game starts: a label and percentage over a
// thin progress bar, centred on black. It fades in only when the startup work
// takes longer than a moment and fades out to the black the game starts from.
// Presentation only: ImGui draws over the game's back buffer before the game runs.
// Adapted from Recharged's graphics preparation screen.
class LoadingView
{
  public:
    // `background` is the game's back buffer, black until its first frame. A
    // missing font is an error: the package is incomplete.
    LoadingView(platform::Video& video,
        const platform::Frame& background,
        const std::filesystem::path& font);
    // One frame while the work runs: its label, and its share when known
    // (none sweeps a short segment across the bar).
    void draw(const std::string& label, std::optional<float> share);
    // One frame after the work: completes the bar, then fades out. Returns false
    // once a black frame has been presented and nothing remains shown.
    bool draw_finished();

  private:
    using Clock = std::chrono::steady_clock;
    void frame(bool finished, const std::optional<float>* share);
    void build_font(float size, float density);

    platform::Video& video_;
    const platform::Frame& background_;
    std::vector<unsigned char> font_data_;
    ImFont* font_ = nullptr;
    float font_size_ = 0;
    float font_density_ = 0;
    std::string label_;
    bool started_ = false;
    Clock::time_point start_{};
    Clock::time_point last_{};
    Clock::time_point finish_start_{};
    bool finishing_ = false;
    bool determinate_ = false;
    float target_ = 0;   // finished share of the known work
    float progress_ = 0; // displayed share, eased and never decreasing
    float opacity_ = 0;  // of everything drawn over the black background
};

} // namespace beachlife::runtime
