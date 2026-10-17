#include "runtime/loading_view.h"
#include "platform/video.h"

#include "imgui.h"
#include "imgui_freetype.h"
#include "imgui_impl_sdl3.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace beachlife::runtime {
namespace {
// Nothing is drawn over black before this, so quick work never flashes.
constexpr float grace = 0.3f;
constexpr float fade_in = 0.35f;
constexpr float fade_out = 0.3f;
// After the work the bar may take this long to complete before fading out.
constexpr float complete = 0.25f;
// One way across the track while the size of the work is unknown.
constexpr float sweep = 1.1f;

ImU32 white(float alpha) { return IM_COL32(255, 255, 255, static_cast<int>(std::lround(255.0f*std::clamp(alpha, 0.0f, 1.0f)))); }

float smooth(float t) { return t*t*(3.0f - 2.0f*t); }

float ease_in_out(float t) { return t < 0.5f ? 4.0f*t*t*t : 1.0f - std::pow(-2.0f*t + 2.0f, 3.0f)/2.0f; }
} // namespace

LoadingView::LoadingView(platform::Video& video, const platform::Frame& background, const std::filesystem::path& font)
    : video_(video), background_(background) {
    std::ifstream input(font, std::ios::binary);
    if (input) font_data_.assign(std::istreambuf_iterator<char>(input), {});
    if (font_data_.empty()) throw std::runtime_error("The loading screen's font is missing. Restore the complete package.");
}

void LoadingView::build_font(float size, float density) {
    auto& io = ImGui::GetIO();
    io.Fonts->Clear(); // the video layer uploads the new atlas with the next frame
    ImFontConfig config;
    config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
    config.RasterizerDensity = density;
    // The atlas owns (and frees) its copy, so it never refers to this view's memory.
    auto* data = static_cast<unsigned char*>(IM_ALLOC(font_data_.size()));
    std::copy(font_data_.begin(), font_data_.end(), data);
    font_ = io.Fonts->AddFontFromMemoryTTF(data, static_cast<int>(font_data_.size()), size, &config);
    if (!font_) throw std::runtime_error("Cannot load the loading screen's font");
    font_size_ = size;
    font_density_ = density;
}

void LoadingView::draw(const std::string& label, std::optional<float> share) {
    label_ = label;
    frame(false, &share);
}

bool LoadingView::draw_finished() {
    frame(true, nullptr);
    return opacity_ > 0;
}

void LoadingView::frame(bool finished, const std::optional<float>* share) {
    const auto now = Clock::now();
    if (!started_) {
        started_ = true;
        start_ = last_ = now;
    }
    const float dt = std::min(std::chrono::duration<float>(now - last_).count(), 0.1f);
    last_ = now;
    const float elapsed = std::chrono::duration<float>(now - start_).count();

    if (share) {
        determinate_ = share->has_value();
        // A share that drops is not shown: the bar waits.
        target_ = determinate_ ? std::clamp(**share, 0.0f, 1.0f) : 0.0f;
        if (elapsed >= grace) opacity_ = std::min(1.0f, opacity_ + dt/fade_in);
    }
    if (finished) {
        if (!finishing_) {
            finishing_ = true;
            finish_start_ = now;
        }
        target_ = 1;
        const float since = std::chrono::duration<float>(now - finish_start_).count();
        if (!determinate_ || progress_ > 0.995f || since >= complete) opacity_ = std::max(0.0f, opacity_ - dt/fade_out);
    }
    const float rate = finishing_ ? 14.0f : 6.0f;
    progress_ = std::max(progress_, progress_ + (target_ - progress_)*(1.0f - std::exp(-dt*rate)));

    // Layout in logical window units; the font rasterizes at the physical density.
    int width = 0, height = 0, pixel_width = 0, pixel_height = 0;
    SDL_GetWindowSize(video_.window(), &width, &height);
    SDL_GetWindowSizeInPixels(video_.window(), &pixel_width, &pixel_height);
    if (width <= 0 || height <= 0 || pixel_width <= 0 || pixel_height <= 0) return;
    const float w = static_cast<float>(width), h = static_cast<float>(height);
    const float density = std::max(1.0f, std::max(static_cast<float>(pixel_width)/w, static_cast<float>(pixel_height)/h));
    const auto snap = [density](float value) { return std::round(value*density)/density; };
    const float scale = std::min(w/1920.0f, h/1080.0f);
    const float text = std::clamp(std::round(22.0f*scale), 14.0f, 52.0f);
    if (text != font_size_ || density != font_density_) build_font(text, density);

    video_.new_overlay_frame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    auto* draw = ImGui::GetForegroundDrawList();
    if (opacity_ > 0) {
        const float alpha = smooth(opacity_);
        const float bar_width = std::min(std::round(text*26.0f), w - 32.0f); // 30% wider than Recharged's
        const float bar_height = std::max(2.0f, std::round(text*0.14f));
        const float gap = std::round(text*0.8f);
        const float line = font_->FontSize;
        const float top = snap((h - (line + gap + bar_height))/2.0f);
        const float left = snap((w - bar_width)/2.0f);
        const float right = left + bar_width;
        const float bar_top = snap(top + line + gap);
        const float bar_bottom = bar_top + bar_height;
        const float radius = bar_height/2.0f;

        draw->AddText(font_, line, {left, top}, white(0.88f*alpha), label_.c_str());
        if (determinate_) {
            // 100 only once the work is done.
            const int percent = finishing_ ? static_cast<int>(std::lround(progress_*100.0f))
                                           : std::min(99, static_cast<int>(progress_*100.0f));
            char value[8];
            std::snprintf(value, sizeof(value), "%d%%", percent);
            const float value_width = font_->CalcTextSizeA(line, w, 0.0f, value).x;
            draw->AddText(font_, line, {snap(right - value_width), top}, white(0.45f*alpha), value);
        }
        draw->AddRectFilled({left, bar_top}, {right, bar_bottom}, white(0.12f*alpha), radius);
        if (determinate_) {
            const float fill = snap(bar_width*std::clamp(progress_, 0.0f, 1.0f));
            if (fill > 0)
                draw->AddRectFilled({left, bar_top}, {left + fill, bar_bottom}, white(0.92f*alpha), std::min(radius, fill/2.0f));
        } else {
            // The size of the work is not known yet: a short segment glides back and forth.
            const float segment = std::round(bar_width*0.28f);
            const float phase = std::fmod(elapsed, 2.0f*sweep)/sweep;
            const float from = snap(left + (bar_width - segment)*ease_in_out(phase < 1.0f ? phase : 2.0f - phase));
            draw->AddRectFilled({from, bar_top}, {from + segment, bar_bottom}, white(0.92f*alpha), radius);
        }
    }
    ImGui::Render();
    video_.present(background_, ImGui::GetDrawData());
}

} // namespace beachlife::runtime
