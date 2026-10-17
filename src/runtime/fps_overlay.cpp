#include "runtime/fps_overlay.h"

#include "imgui.h"
#include "imgui_freetype.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace beachlife::runtime {
namespace {
// Digits, percent, slash, N/A and the metric labels.
constexpr ImWchar glyphs[] = {0x0020, 0x0055, 0};
constexpr ImU32 number_color = IM_COL32(255, 255, 255, 255);
constexpr ImU32 label_color = IM_COL32(211, 215, 221, 255);
constexpr ImU32 divider_color = IM_COL32(211, 215, 221, 190);
constexpr ImU32 shadow_color = IM_COL32(0, 0, 0, 200);
} // namespace

FpsOverlay::FpsOverlay(const std::filesystem::path& font, OverlayCorner corner) : corner_(corner) {
    std::ifstream input(font, std::ios::binary);
    if (input) font_data_.assign(std::istreambuf_iterator<char>(input), {});
    if (font_data_.empty()) throw std::runtime_error("The performance overlay's font is missing. Restore the complete package.");
}

void FpsOverlay::set_rate(double rate) { fps_ = overlay_number(rate, 999); }

bool FpsOverlay::needs_fonts(int pixel_width, int pixel_height, float density) const {
    if (pixel_width <= 0 || pixel_height <= 0) return false;
    return overlay_font_size(static_cast<std::uint32_t>(pixel_width), static_cast<std::uint32_t>(pixel_height)) != built_size_
        || density != built_density_;
}

void FpsOverlay::add_fonts(ImFontAtlas& atlas, int pixel_width, int pixel_height, float density) {
    const float size = overlay_font_size(static_cast<std::uint32_t>(std::max(pixel_width, 1)),
                                         static_cast<std::uint32_t>(std::max(pixel_height, 1)));
    ImFontConfig config;
    // Rasterized at the physical size; ImGui lays out in window units.
    config.RasterizerDensity = density;
    config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
    const auto add = [&] {
        // The atlas owns (and frees) its copy of the font.
        auto* data = static_cast<unsigned char*>(IM_ALLOC(font_data_.size()));
        std::copy(font_data_.begin(), font_data_.end(), data);
        return atlas.AddFontFromMemoryTTF(data, static_cast<int>(font_data_.size()), size/density, &config, glyphs);
    };
    label_ = add();
    config.FontBuilderFlags |= ImGuiFreeTypeBuilderFlags_Bold;
    number_ = add();
    built_size_ = size;
    built_density_ = density;
}

void FpsOverlay::draw(int pixel_width, int pixel_height, float density) const {
    if (pixel_width <= 0 || pixel_height <= 0 || !label_ || !number_) return;
    const auto width = static_cast<std::uint32_t>(pixel_width), height = static_cast<std::uint32_t>(pixel_height);
    const auto usage = usage_.latest();
    const std::array<std::optional<int>, 3> values = {
        fps_, overlay_number(usage.gpu_percent, 100), overlay_number(usage.cpu_percent, 100)};
    // Laid out in pixels as in Recharged, then drawn in window units.
    const float size = built_size_, units = size/density;
    const auto measure = [&](ImFont* font, const char* text) { return font->CalcTextSizeA(units, FLT_MAX, 0, text).x*density; };
    std::array<std::array<char, 8>, 3> strings{};
    std::array<OverlayText, 3> text{};
    for (std::size_t n = 0; n < strings.size(); ++n) {
        if (values[n]) std::snprintf(strings[n].data(), strings[n].size(), n == 0 ? "%d" : "%d%%", *values[n]);
        else std::snprintf(strings[n].data(), strings[n].size(), "N/A");
        text[n] = {measure(label_, performance_labels[n]), measure(number_, strings[n].data()),
                   std::max(measure(number_, n == 0 ? "999" : "100%"), measure(number_, "N/A"))};
    }
    const auto layout = layout_overlay(width, height, corner_, size, text);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float shadow = std::max(1.0f, std::round(size/15.0f));
    const auto at = [density](float x, float y) { return ImVec2{x/density, y/density}; };
    const auto draw_text = [&](ImFont* font, float x, ImU32 color, const char* value) {
        draw->AddText(font, units, at(x + shadow, layout.text_y + shadow), shadow_color, value);
        draw->AddText(font, units, at(x, layout.text_y), color, value);
    };
    for (std::size_t n = 0; n < strings.size(); ++n) {
        draw_text(label_, layout.label_x[n], label_color, performance_labels[n]);
        draw_text(number_, layout.value_x[n], number_color, strings[n].data());
    }
    for (const float x : layout.separator_x) {
        draw->AddLine(at(x + shadow, layout.top + shadow), at(x + shadow, layout.bottom + shadow), shadow_color, shadow/density);
        draw->AddLine(at(x, layout.top), at(x, layout.bottom), divider_color, shadow/density);
    }
}

} // namespace beachlife::runtime
