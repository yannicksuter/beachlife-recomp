#include "ui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace beachlife::launcher::ui
{
namespace
{
constexpr std::array<float, FontCount> sizes = {15, 13, 16.5f, 20, 30, 17};
std::array<ImFont*, FontCount> fonts{};
float scale = 1, density = 1;
SDL_Texture* ramp_texture = nullptr;
SDL_Texture* spot_texture = nullptr;

ImVec2 add(ImVec2 a, ImVec2 b)
{
    return {a.x + b.x, a.y + b.y};
}
ImVec2 sub(ImVec2 a, ImVec2 b)
{
    return {a.x - b.x, a.y - b.y};
}
ImTextureID texture_id(SDL_Texture* texture)
{
    return static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texture));
}

void hand_cursor()
{
    if (ImGui::IsItemHovered())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
}

// Blends vertex colours (alpha included, scaled by each vertex's own alpha so
// anti-aliased fringes stay soft) along one axis, from `from` to `to`.
void shade(
    ImDrawList* draw, int start, float from, float to, bool horizontal, ImU32 first, ImU32 second)
{
    const ImVec4 a = ImGui::ColorConvertU32ToFloat4(first),
                 b = ImGui::ColorConvertU32ToFloat4(second);
    const float length = std::max(1.0f, to - from);
    for (int i = start; i < draw->VtxBuffer.Size; ++i)
    {
        auto& vertex = draw->VtxBuffer[i];
        const float t =
            std::clamp(((horizontal ? vertex.pos.x : vertex.pos.y) - from) / length, 0.0f, 1.0f);
        ImVec4 color = mix(a, b, t);
        color.w *= ((vertex.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
        vertex.col = ImGui::ColorConvertFloat4ToU32(color);
    }
}

// Icon and label centred together in a button.
void button_label(ImDrawList* draw,
    Font which,
    const char* label,
    Icon kind,
    ImVec2 pos,
    ImVec2 size,
    ImU32 ink,
    bool bold)
{
    const ImVec2 measure = text_size(which, label);
    const float icon_size = kind == Icon::None ? 0 : font_size(which) * 0.95f;
    const float gap = kind == Icon::None || !*label ? 0 : dp(9);
    const float left = pos.x + (size.x - measure.x - icon_size - gap) * 0.5f;
    if (kind != Icon::None)
    {
        icon(draw, kind, {left + icon_size * 0.5f, pos.y + size.y * 0.5f}, icon_size, ink);
    }
    const ImVec2 at{left + icon_size + gap, pos.y + (size.y - measure.y) * 0.5f};
    if (bold)
    {
        text_bold(draw, which, at, ink, label);
    }
    else
    {
        text(draw, which, at, ink, label);
    }
}
} // namespace

ImU32 col(const ImVec4& color, float alpha)
{
    return ImGui::ColorConvertFloat4ToU32(
        {color.x, color.y, color.z, color.w * std::clamp(alpha, 0.0f, 1.0f)});
}

ImVec4 mix(const ImVec4& a, const ImVec4& b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

void set_scale(float value)
{
    scale = value;
}
float dp(float value)
{
    return value * scale;
}

bool load_fonts(const std::string& ttf, float raster, float pixel_density)
{
    auto& io = ImGui::GetIO();
    io.Fonts->Clear();
    static ImVector<ImWchar> ranges;
    if (ranges.empty())
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        builder.AddText(
            "\xE2\x80\x93\xE2\x80\x94\xE2\x80\x99\xE2\x80\xA2\xE2\x80\xA6"); // – — ’ • …
        builder.BuildRanges(&ranges);
    }
    for (int i = 0; i < FontCount; ++i)
    {
        ImFontConfig config;
        config.OversampleH = 2;
        config.PixelSnapH = false;
        fonts[i] = io.Fonts->AddFontFromFileTTF(
            ttf.c_str(), std::round(sizes[i] * raster), &config, ranges.Data);
        if (!fonts[i])
        {
            return false;
        }
    }
    io.FontDefault = fonts[Body];
    density = pixel_density;
    io.FontGlobalScale = 1 / density;
    return true;
}

ImFont* font(Font which)
{
    return fonts[which];
}
float font_size(Font which)
{
    return fonts[which]->FontSize / density;
}

ImVec2 text_size(Font which, const char* value, float wrap)
{
    return fonts[which]->CalcTextSizeA(font_size(which), FLT_MAX, wrap, value);
}

void text(ImDrawList* draw, Font which, ImVec2 pos, ImU32 color, const char* value, float wrap)
{
    draw->AddText(fonts[which], font_size(which), pos, color, value, nullptr, wrap);
}

void text_shadowed(
    ImDrawList* draw, Font which, ImVec2 pos, ImU32 color, const char* value, float wrap)
{
    const float alpha = ((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
    text(draw,
        which,
        add(pos, {0, dp(1.5f)}),
        col({0.0f, 0.02f, 0.06f, 1}, 0.55f * alpha),
        value,
        wrap);
    text(draw, which, pos, color, value, wrap);
}

void text_bold(ImDrawList* draw,
    Font which,
    ImVec2 pos,
    ImU32 color,
    const char* value,
    float wrap,
    bool shadow)
{
    const float stroke = std::max(0.5f, font_size(which) * 0.022f);
    if (shadow)
    {
        const float alpha = ((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
        const ImU32 shade = col({0.0f, 0.02f, 0.06f, 1}, 0.5f * alpha);
        text(draw, which, add(pos, {0, dp(2)}), shade, value, wrap);
        text(draw, which, add(pos, {stroke, dp(2)}), shade, value, wrap);
    }
    text(draw, which, pos, color, value, wrap);
    text(draw, which, add(pos, {stroke, 0}), color, value, wrap);
}

void text_gradient(ImDrawList* draw,
    Font which,
    ImVec2 pos,
    ImU32 top,
    ImU32 bottom,
    const char* value,
    float wrap)
{
    const int start = draw->VtxBuffer.Size;
    text_bold(draw, which, pos, IM_COL32_WHITE, value, wrap);
    const float height = text_size(which, value, wrap).y;
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw,
        start,
        draw->VtxBuffer.Size,
        {pos.x, pos.y + height * 0.15f},
        {pos.x, pos.y + height * 0.9f},
        top,
        bottom);
}

void tracked(ImDrawList* draw,
    Font which,
    ImVec2 pos,
    ImU32 color,
    const char* value,
    float tracking,
    bool shadow)
{
    if (shadow)
    {
        const float alpha = ((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
        tracked(draw,
            which,
            add(pos, {0, dp(1.5f)}),
            col({0.0f, 0.02f, 0.06f, 1}, 0.6f * alpha),
            value,
            tracking);
    }
    char glyph[2] = {0, 0};
    for (const char* c = value; *c; ++c)
    {
        glyph[0] = *c;
        text(draw, which, pos, color, glyph);
        pos.x += text_size(which, glyph).x + tracking;
    }
}

float tracked_width(Font which, const char* value, float tracking)
{
    float width = 0;
    char glyph[2] = {0, 0};
    for (const char* c = value; *c; ++c)
    {
        glyph[0] = *c;
        width += text_size(which, glyph).x + tracking;
    }
    return *value ? width - tracking : 0;
}

std::string fit(Font which, const std::string& value, float width, bool keep_end)
{
    if (text_size(which, value.c_str()).x <= width)
    {
        return value;
    }
    // Drop whole UTF-8 sequences, one at a time, from the trimmed side.
    auto continuation = [&](size_t i)
    {
        return (static_cast<unsigned char>(value[i]) & 0xC0) == 0x80;
    };
    size_t start = 0, end = value.size();
    while (start < end)
    {
        if (keep_end)
        {
            do
            {
                ++start;
            } while (start < end && continuation(start));
        }
        else
        {
            do
            {
                --end;
            } while (end > start && continuation(end));
        }
        const std::string candidate =
            keep_end ? "\xE2\x80\xA6" + value.substr(start) : value.substr(0, end) + "\xE2\x80\xA6";
        if (text_size(which, candidate.c_str()).x <= width)
        {
            return candidate;
        }
    }
    return "\xE2\x80\xA6";
}

void gradient(ImDrawList* draw,
    ImVec2 a,
    ImVec2 b,
    ImU32 first,
    ImU32 second,
    float rounding,
    bool horizontal)
{
    const int start = draw->VtxBuffer.Size;
    draw->AddRectFilled(a, b, IM_COL32_WHITE, rounding);
    shade(draw, start, horizontal ? a.x : a.y, horizontal ? b.x : b.y, horizontal, first, second);
}

void icon(ImDrawList* draw, Icon kind, ImVec2 c, float s, ImU32 color)
{
    const float t = std::max(1.4f, s * 0.1f);
    auto p = [&](float x, float y)
    {
        return ImVec2{c.x + x * s, c.y + y * s};
    };
    switch (kind)
    {
    case Icon::None:
        break;
    case Icon::Play:
        draw->AddTriangleFilled(p(-0.3f, -0.42f), p(-0.3f, 0.42f), p(0.42f, 0), color);
        break;
    case Icon::Disc:
        draw->AddCircle(c, s * 0.45f, color, 0, t);
        draw->AddCircle(c, s * 0.12f, color, 0, t);
        draw->PathArcTo(c, s * 0.29f, -2.75f, -1.75f, 10);
        draw->PathStroke(color, 0, t * 0.9f);
        break;
    case Icon::Sliders:
        for (int i = 0; i < 3; ++i)
        {
            const float y = (i - 1) * 0.3f, knob = i == 1 ? 0.2f : i == 0 ? -0.18f : 0.02f;
            draw->AddLine(p(-0.45f, y), p(0.45f, y), color, t);
            draw->AddCircleFilled(p(knob, y), s * 0.11f, color);
        }
        break;
    case Icon::Info:
        draw->AddCircle(c, s * 0.45f, color, 0, t);
        draw->AddCircleFilled(p(0, -0.21f), std::max(1.2f, s * 0.065f), color);
        draw->AddLine(p(0, -0.04f), p(0, 0.25f), color, t);
        break;
    case Icon::Folder:
    {
        const ImVec2 points[] = {p(-0.45f, -0.33f),
            p(-0.13f, -0.33f),
            p(0, -0.2f),
            p(0.45f, -0.2f),
            p(0.45f, 0.34f),
            p(-0.45f, 0.34f)};
        draw->AddPolyline(points, 6, color, ImDrawFlags_Closed, t);
        break;
    }
    case Icon::Check:
    {
        const ImVec2 points[] = {p(-0.36f, 0.02f), p(-0.1f, 0.27f), p(0.38f, -0.26f)};
        draw->AddPolyline(points, 3, color, 0, t * 1.25f);
        break;
    }
    case Icon::Warning:
    {
        const ImVec2 points[] = {p(0, -0.42f), p(0.47f, 0.37f), p(-0.47f, 0.37f)};
        draw->AddPolyline(points, 3, color, ImDrawFlags_Closed, t);
        draw->AddLine(p(0, -0.12f), p(0, 0.12f), color, t);
        draw->AddCircleFilled(p(0, 0.24f), std::max(1.2f, s * 0.06f), color);
        break;
    }
    case Icon::Globe:
        draw->AddCircle(c, s * 0.45f, color, 0, t);
        draw->AddEllipse(c, {s * 0.19f, s * 0.45f}, color, 0, 0, t);
        draw->AddLine(p(-0.45f, 0), p(0.45f, 0), color, t);
        break;
    case Icon::Sun:
        draw->AddCircleFilled(c, s * 0.2f, color);
        for (int i = 0; i < 8; ++i)
        {
            const float angle = i * IM_PI / 4;
            const ImVec2 d{std::cos(angle), std::sin(angle)};
            draw->AddLine(p(d.x * 0.31f, d.y * 0.31f), p(d.x * 0.46f, d.y * 0.46f), color, t);
        }
        break;
    case Icon::Chevron:
    {
        const ImVec2 points[] = {p(-0.12f, -0.26f), p(0.13f, 0), p(-0.12f, 0.26f)};
        draw->AddPolyline(points, 3, color, 0, t * 1.2f);
        break;
    }
    case Icon::Link:
    {
        const ImVec2 frame[] = {p(0.02f, -0.38f),
            p(-0.38f, -0.38f),
            p(-0.38f, 0.38f),
            p(0.38f, 0.38f),
            p(0.38f, -0.02f)};
        draw->AddPolyline(frame, 5, color, 0, t);
        const ImVec2 head[] = {p(0.12f, -0.42f), p(0.42f, -0.42f), p(0.42f, -0.12f)};
        draw->AddPolyline(head, 3, color, 0, t);
        draw->AddLine(p(-0.04f, 0.04f), p(0.4f, -0.4f), color, t);
        break;
    }
    case Icon::Shield:
    {
        const ImVec2 outline[] = {p(0, -0.45f),
            p(0.38f, -0.3f),
            p(0.33f, 0.1f),
            p(0, 0.45f),
            p(-0.33f, 0.1f),
            p(-0.38f, -0.3f)};
        draw->AddPolyline(outline, 6, color, ImDrawFlags_Closed, t);
        const ImVec2 tick[] = {p(-0.15f, 0), p(-0.03f, 0.12f), p(0.17f, -0.1f)};
        draw->AddPolyline(tick, 3, color, 0, t);
        break;
    }
    case Icon::Monitor:
        draw->AddRect(p(-0.46f, -0.36f), p(0.46f, 0.2f), color, s * 0.07f, 0, t);
        draw->AddLine(p(0, 0.2f), p(0, 0.37f), color, t);
        draw->AddLine(p(-0.22f, 0.38f), p(0.22f, 0.38f), color, t);
        break;
    case Icon::Speaker:
    {
        const ImVec2 body[] = {p(-0.42f, -0.14f),
            p(-0.2f, -0.14f),
            p(0.04f, -0.38f),
            p(0.04f, 0.38f),
            p(-0.2f, 0.14f),
            p(-0.42f, 0.14f)};
        draw->AddPolyline(body, 6, color, ImDrawFlags_Closed, t);
        for (int i = 0; i < 2; ++i)
        {
            draw->PathArcTo(p(0.04f, 0), s * (0.2f + 0.17f * i), -0.8f, 0.8f, 10);
            draw->PathStroke(color, 0, t);
        }
        break;
    }
    case Icon::Gear:
        for (int i = 0; i < 8; ++i)
        {
            const float angle = i * IM_PI / 4;
            const ImVec2 d{std::cos(angle), std::sin(angle)};
            draw->AddLine(
                p(d.x * 0.26f, d.y * 0.26f), p(d.x * 0.45f, d.y * 0.45f), color, t * 1.6f);
        }
        draw->AddCircle(c, s * 0.3f, color, 0, t);
        draw->AddCircle(c, s * 0.1f, color, 0, t);
        break;
    case Icon::Terminal:
    {
        draw->AddRect(p(-0.46f, -0.38f), p(0.46f, 0.38f), color, s * 0.08f, 0, t);
        const ImVec2 prompt[] = {p(-0.26f, -0.14f), p(-0.08f, 0.02f), p(-0.26f, 0.18f)};
        draw->AddPolyline(prompt, 3, color, 0, t);
        draw->AddLine(p(0.02f, 0.2f), p(0.26f, 0.2f), color, t);
        break;
    }
    case Icon::Refresh:
    {
        // A circle open at the top, its end arrowed clockwise.
        const float radius = s * 0.34f, from = -IM_PI * 0.5f + 0.45f, to = IM_PI * 1.5f - 0.6f;
        draw->PathArcTo(c, radius, from, to, 20);
        draw->PathStroke(color, 0, t);
        const ImVec2 radial{std::cos(to), std::sin(to)}, along{-radial.y, radial.x};
        const ImVec2 end{c.x + radial.x * radius, c.y + radial.y * radius};
        draw->AddTriangleFilled({end.x + along.x * s * 0.2f, end.y + along.y * s * 0.2f},
            {end.x + radial.x * s * 0.15f - along.x * s * 0.05f,
                end.y + radial.y * s * 0.15f - along.y * s * 0.05f},
            {end.x - radial.x * s * 0.15f - along.x * s * 0.05f,
                end.y - radial.y * s * 0.15f - along.y * s * 0.05f},
            color);
        break;
    }
    case Icon::Download:
    {
        draw->AddLine(p(0, -0.42f), p(0, 0.16f), color, t);
        const ImVec2 head[] = {p(-0.22f, -0.06f), p(0, 0.17f), p(0.22f, -0.06f)};
        draw->AddPolyline(head, 3, color, 0, t);
        const ImVec2 tray[] = {p(-0.42f, 0.12f), p(-0.42f, 0.4f), p(0.42f, 0.4f), p(0.42f, 0.12f)};
        draw->AddPolyline(tray, 4, color, 0, t);
        break;
    }
    case Icon::Revert:
    {
        draw->PathArcTo(c, s * 0.36f, -IM_PI * 0.85f, IM_PI * 0.75f, 18);
        draw->PathStroke(color, 0, t);
        const ImVec2 start{
            c.x + std::cos(-IM_PI * 0.85f) * s * 0.36f, c.y + std::sin(-IM_PI * 0.85f) * s * 0.36f};
        const ImVec2 head[] = {{start.x - s * 0.04f, start.y - s * 0.26f},
            start,
            {start.x + s * 0.24f, start.y - s * 0.02f}};
        draw->AddPolyline(head, 3, color, 0, t);
        break;
    }
    }
}

float animate(ImGuiID id, bool target, float speed)
{
    const float goal = target ? 1.0f : 0.0f;
    float* value = ImGui::GetStateStorage()->GetFloatRef(id, goal);
    *value += (goal - *value) * std::min(1.0f, ImGui::GetIO().DeltaTime * speed);
    if (std::fabs(*value - goal) < 0.002f)
    {
        *value = goal;
    }
    return *value;
}

void set_textures(SDL_Texture* ramp, SDL_Texture* spot)
{
    ramp_texture = ramp;
    spot_texture = spot;
}

void fade(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color, Side opaque)
{
    // The ramp runs from transparent (u = 0) to opaque (u = 1) along its width.
    const float tl = opaque == Side::Left || opaque == Side::Top ? 1 : 0;
    const float tr = opaque == Side::Right || opaque == Side::Top ? 1 : 0;
    const float br = opaque == Side::Right || opaque == Side::Bottom ? 1 : 0;
    const float bl = opaque == Side::Left || opaque == Side::Bottom ? 1 : 0;
    draw->AddImageQuad(texture_id(ramp_texture),
        a,
        {b.x, a.y},
        b,
        {a.x, b.y},
        {tl, 0.5f},
        {tr, 0.5f},
        {br, 0.5f},
        {bl, 0.5f},
        color);
}

void glow(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color)
{
    draw->AddImage(texture_id(spot_texture), a, b, {0, 0}, {1, 1}, color);
}

void apply_theme(ImGuiStyle& style)
{
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 0;
    style.WindowBorderSize = 0;
    style.WindowPadding = {0, 0};
    style.ChildBorderSize = 0;
    style.FramePadding = {14, 11};
    style.ItemSpacing = {12, 12};
    style.FrameRounding = 10;
    style.FrameBorderSize = 1;
    style.PopupRounding = 16;
    style.PopupBorderSize = 1;
    style.ScrollbarSize = 8;
    style.ScrollbarRounding = 8;
    auto* c = style.Colors;
    c[ImGuiCol_Text] = color::text;
    c[ImGuiCol_TextDisabled] = color::dim;
    c[ImGuiCol_WindowBg] = color::ocean;
    c[ImGuiCol_ChildBg] = {0, 0, 0, 0};
    c[ImGuiCol_PopupBg] = mix(color::deep, color::card, 0.5f);
    c[ImGuiCol_Border] = {1, 1, 1, 0.09f};
    c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg] = {0.02f, 0.05f, 0.09f, 0.72f};
    c[ImGuiCol_FrameBgHovered] = {0.04f, 0.09f, 0.15f, 0.82f};
    c[ImGuiCol_FrameBgActive] = {0.04f, 0.09f, 0.15f, 0.92f};
    c[ImGuiCol_Button] = color::field;
    c[ImGuiCol_ButtonHovered] = mix(color::field, color::border, 0.7f);
    c[ImGuiCol_ButtonActive] = mix(color::field, color::coral, 0.25f);
    c[ImGuiCol_Header] = mix(color::field, color::coral, 0.2f);
    c[ImGuiCol_HeaderHovered] = mix(color::field, color::coral, 0.12f);
    c[ImGuiCol_HeaderActive] = mix(color::field, color::coral, 0.3f);
    c[ImGuiCol_CheckMark] = color::coral;
    c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab] = {1, 1, 1, 0.12f};
    c[ImGuiCol_ScrollbarGrabHovered] = {1, 1, 1, 0.2f};
    c[ImGuiCol_ScrollbarGrabActive] = {1, 1, 1, 0.28f};
    c[ImGuiCol_TextSelectedBg] = {color::coral.x, color::coral.y, color::coral.z, 0.35f};
    c[ImGuiCol_NavCursor] = color::sun;
    c[ImGuiCol_ModalWindowDimBg] = {0.01f, 0.03f, 0.06f, 0.66f};
}

void panel(ImDrawList* draw, ImVec2 a, ImVec2 b, float opacity)
{
    const float rounding = dp(16);
    draw->AddRectFilled(a, b, col(color::deep, opacity), rounding);
    draw->AddRect(a, b, col(color::white, 0.075f), rounding, 0, 1);
}

float chip(ImDrawList* draw, ImVec2 pos, const char* label, const ImVec4& tint)
{
    const ImVec2 measure = text_size(Caption, label);
    const ImVec2 size{measure.x + dp(18), dp(23)};
    draw->AddRectFilled(pos, add(pos, size), col(tint, 0.14f), size.y * 0.5f);
    draw->AddRect(pos, add(pos, size), col(tint, 0.38f), size.y * 0.5f, 0, 1);
    text(draw, Caption, {pos.x + dp(9), pos.y + (size.y - measure.y) * 0.5f}, col(tint), label);
    return size.x;
}

bool nav_item(const char* label, Icon kind, bool selected, ImVec2 pos, float width)
{
    ImGui::PushID(label);
    const ImVec2 size{width, dp(44)};
    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton("##nav", size);
    hand_cursor();
    const float hover = animate(ImGui::GetID("##hover"), ImGui::IsItemHovered());
    const float active = animate(ImGui::GetID("##active"), selected);
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 max = add(pos, size);
    if (std::max(hover, active) > 0)
    {
        draw->AddRectFilled(pos, max, col(color::white, 0.045f * hover + 0.045f * active), dp(11));
    }
    if (active > 0)
    {
        gradient(draw,
            {pos.x, pos.y + dp(10)},
            {pos.x + dp(3.5f), max.y - dp(10)},
            col(color::sun, active),
            col(color::coral, active),
            dp(2),
            false);
        glow(draw,
            {pos.x - dp(28), pos.y - dp(6)},
            {pos.x + dp(60), max.y + dp(6)},
            col(color::coral, 0.16f * active));
    }
    const ImVec4 tint = mix(mix(color::muted, color::text, hover * 0.7f), color::sun, active);
    icon(draw, kind, {pos.x + dp(28), pos.y + size.y * 0.5f}, dp(18), col(tint));
    text(draw,
        Label,
        {pos.x + dp(52), pos.y + (size.y - font_size(Label)) * 0.5f},
        col(mix(color::muted, color::text, std::max(hover, active))),
        label);
    ImGui::PopID();
    return pressed;
}

bool primary_button(const char* id,
    const char* label,
    Icon kind,
    ImVec2 pos,
    ImVec2 size,
    bool enabled,
    bool glowing)
{
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton("##primary", size);
    const bool held = ImGui::IsItemActive();
    ImGui::EndDisabled();
    if (enabled)
    {
        hand_cursor();
    }
    const float hover = animate(ImGui::GetID("##hover"), enabled && ImGui::IsItemHovered());
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 max = add(pos, size);
    const float rounding = std::min(size.y * 0.5f, dp(14));
    if (enabled)
    {
        if (glowing)
        {
            const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 2.2f);
            const ImVec2 spread{dp(34), dp(30)};
            glow(draw,
                sub(pos, spread),
                add(max, spread),
                col(color::coral, 0.30f + 0.10f * pulse + 0.18f * hover));
        }
        const float light = held ? 0 : 0.14f * hover;
        gradient(draw,
            pos,
            max,
            col(mix(color::sun, color::white, light)),
            col(mix(color::coral, color::white, light)),
            rounding,
            true);
        // A soft gloss on the upper half, darker while pressed.
        gradient(draw,
            pos,
            {max.x, pos.y + size.y * 0.55f},
            col(held ? color::ocean : color::white, held ? 0.12f : 0.2f),
            col(color::white, 0),
            rounding,
            false);
        draw->AddRect(pos, max, col(color::white, 0.28f), rounding, 0, 1);
    }
    else
    {
        // Unavailable, yet still recognisably the main action.
        draw->AddRectFilled(pos, max, col(color::deep, 0.55f), rounding);
        gradient(draw, pos, max, col(color::sun, 0.06f), col(color::coral, 0.08f), rounding, true);
        draw->AddRect(pos, max, col(color::coral, 0.3f), rounding, 0, 1);
    }
    const Font which = size.y >= dp(50) ? Button : Label;
    button_label(draw,
        which,
        label,
        kind,
        pos,
        size,
        enabled ? IM_COL32(52, 20, 12, 255) : col(color::muted, 0.8f),
        true);
    ImGui::PopID();
    return pressed && enabled;
}

bool secondary_button(
    const char* id, const char* label, Icon kind, ImVec2 pos, ImVec2 size, bool enabled)
{
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton("##secondary", size);
    const bool held = ImGui::IsItemActive();
    ImGui::EndDisabled();
    if (enabled)
    {
        hand_cursor();
    }
    const float hover = animate(ImGui::GetID("##hover"), enabled && ImGui::IsItemHovered());
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 max = add(pos, size);
    const float rounding = std::min(size.y * 0.5f, dp(11));
    ImVec4 fill = mix(color::field, mix(color::field, color::border, 0.8f), hover);
    if (held)
    {
        fill = mix(color::field, color::coral, 0.2f);
    }
    draw->AddRectFilled(pos, max, col(fill, enabled ? 0.9f : 0.45f), rounding);
    draw->AddRect(
        pos, max, col(color::white, enabled ? 0.09f + 0.1f * hover : 0.05f), rounding, 0, 1);
    const ImU32 ink =
        col(enabled ? mix(color::muted, color::text, 0.65f + 0.35f * hover) : color::dim);
    button_label(draw, Body, label, kind, pos, size, ink, false);
    ImGui::PopID();
    return pressed && enabled;
}

bool segmented(
    const char* id, int* selected, std::initializer_list<Segment> items, ImVec2 pos, ImVec2 size)
{
    ImGui::PushID(id);
    auto* draw = ImGui::GetWindowDrawList();
    const float segment = size.x / static_cast<float>(std::max<size_t>(items.size(), 1));
    const ImVec2 max = add(pos, size);
    draw->AddRectFilled(pos, max, col({0.02f, 0.05f, 0.09f, 1}, 0.72f), dp(12));
    draw->AddRect(pos, max, col(color::white, 0.08f), dp(12), 0, 1);
    float* slide = ImGui::GetStateStorage()->GetFloatRef(
        ImGui::GetID("##slide"), static_cast<float>(*selected));
    *slide +=
        (static_cast<float>(*selected) - *slide) * std::min(1.0f, ImGui::GetIO().DeltaTime * 16);
    const ImVec2 a{pos.x + *slide * segment + dp(4), pos.y + dp(4)};
    const ImVec2 b{a.x + segment - dp(8), max.y - dp(4)};
    gradient(draw, a, b, col(color::white, 0.13f), col(color::white, 0.07f), dp(9), false);
    draw->AddRect(a, b, col(color::coral, 0.75f), dp(9), 0, 1);
    bool changed = false;
    int index = 0;
    for (const auto& item : items)
    {
        const ImVec2 at{pos.x + segment * static_cast<float>(index), pos.y};
        ImGui::PushID(index);
        ImGui::SetCursorScreenPos(at);
        if (ImGui::InvisibleButton("##segment", {segment, size.y}) && *selected != index)
        {
            *selected = index;
            changed = true;
        }
        hand_cursor();
        const float hover = animate(ImGui::GetID("##hover"), ImGui::IsItemHovered());
        const bool chosen = index == *selected;
        const ImU32 ink = col(chosen ? color::text : mix(color::muted, color::text, hover * 0.7f));
        const ImU32 mark = chosen ? col(color::sun) : ink;
        const ImVec2 measure = text_size(Body, item.label);
        const float icon_size = item.kind == Icon::None ? 0 : dp(16);
        const float gap = item.kind == Icon::None ? 0 : dp(9);
        const float left = at.x + (segment - measure.x - icon_size - gap) * 0.5f;
        if (item.kind != Icon::None)
        {
            icon(draw, item.kind, {left + icon_size * 0.5f, at.y + size.y * 0.5f}, icon_size, mark);
        }
        text(draw,
            Body,
            {left + icon_size + gap, at.y + (size.y - measure.y) * 0.5f},
            ink,
            item.label);
        ImGui::PopID();
        ++index;
    }
    ImGui::PopID();
    return changed;
}

bool toggle(const char* id, bool* value, ImVec2 pos)
{
    ImGui::PushID(id);
    const ImVec2 size{dp(toggle_size.x), dp(toggle_size.y)};
    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton("##toggle", size);
    if (pressed)
    {
        *value = !*value;
    }
    hand_cursor();
    const float hover = animate(ImGui::GetID("##hover"), ImGui::IsItemHovered());
    const float on = animate(ImGui::GetID("##on"), *value, 14);
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 max = add(pos, size);
    const float radius = size.y * 0.5f;
    draw->AddRectFilled(pos,
        max,
        col(mix(
            {0.02f, 0.05f, 0.09f, 0.72f}, mix(color::border, color::white, 0.08f), hover * 0.5f)),
        radius);
    if (on > 0)
    {
        gradient(draw, pos, max, col(color::sun, on), col(color::coral, on), radius, true);
    }
    draw->AddRect(pos, max, col(color::white, 0.1f + 0.08f * hover), radius, 0, 1);
    const ImVec2 knob{pos.x + radius + on * (size.x - size.y), pos.y + radius};
    draw->AddCircleFilled({knob.x, knob.y + dp(1)}, radius - dp(4), col(color::ocean, 0.35f));
    draw->AddCircleFilled(knob, radius - dp(4), col(mix(color::muted, color::white, on)));
    ImGui::PopID();
    return pressed;
}

bool dropdown(const char* id,
    const std::vector<std::string>& items,
    int* selected,
    ImVec2 pos,
    ImVec2 size,
    bool enabled)
{
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginDisabled(!enabled);
    if (ImGui::InvisibleButton("##field", size))
    {
        ImGui::OpenPopup("##list");
    }
    ImGui::EndDisabled();
    if (enabled)
    {
        hand_cursor();
    }
    const bool open = ImGui::IsPopupOpen("##list");
    const float hover =
        animate(ImGui::GetID("##hover"), enabled && (ImGui::IsItemHovered() || open));
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 max = add(pos, size);
    draw->AddRectFilled(pos,
        max,
        col(mix({0.02f, 0.05f, 0.09f, 0.72f}, {0.05f, 0.11f, 0.18f, 0.85f}, hover)),
        dp(12));
    draw->AddRect(pos,
        max,
        open ? col(color::coral, 0.7f) : col(color::white, 0.08f + 0.1f * hover),
        dp(12),
        0,
        1);
    const bool valid = *selected >= 0 && *selected < static_cast<int>(items.size());
    const std::string label =
        fit(Body, valid ? items[*selected] : "Choose\xE2\x80\xA6", size.x - dp(56));
    text(draw,
        Body,
        {pos.x + dp(14), pos.y + (size.y - font_size(Body)) * 0.5f},
        col(valid && enabled ? color::text : color::dim),
        label.c_str());
    const ImVec2 caret{max.x - dp(22), pos.y + size.y * 0.5f};
    const float flip = open ? -1.0f : 1.0f;
    const ImVec2 points[] = {{caret.x - dp(5), caret.y - dp(2.5f) * flip},
        {caret.x, caret.y + dp(2.5f) * flip},
        {caret.x + dp(5), caret.y - dp(2.5f) * flip}};
    draw->AddPolyline(
        points, 3, col(enabled ? mix(color::muted, color::text, hover) : color::dim), 0, dp(1.6f));

    bool changed = false;
    const float row = dp(38);
    // Below the field when the list fits there, else above it.
    const float list_height = static_cast<float>(items.size()) * (row + dp(2)) + dp(12);
    const bool above = max.y + dp(6) + list_height > ImGui::GetIO().DisplaySize.y;
    ImGui::SetNextWindowPos(
        {pos.x, above ? pos.y - dp(6) : max.y + dp(6)}, ImGuiCond_Always, {0, above ? 1.0f : 0.0f});
    ImGui::SetNextWindowSize({size.x, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(6), dp(6)});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, dp(12));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, dp(2)});
    if (ImGui::BeginPopup("##list", ImGuiWindowFlags_NoMove))
    {
        auto* list = ImGui::GetWindowDrawList();
        const float width = size.x - dp(12);
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
        {
            ImGui::PushID(i);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##item", {width, row}))
            {
                changed = *selected != i;
                *selected = i;
                ImGui::CloseCurrentPopup();
            }
            hand_cursor();
            const bool chosen = i == *selected;
            if (ImGui::IsItemHovered() || chosen)
            {
                list->AddRectFilled(at,
                    {at.x + width, at.y + row},
                    col(chosen ? color::coral : color::white, chosen ? 0.14f : 0.06f),
                    dp(8));
            }
            text(list,
                Body,
                {at.x + dp(12), at.y + (row - font_size(Body)) * 0.5f},
                col(chosen ? color::text : color::muted),
                fit(Body, items[i], width - dp(48)).c_str());
            if (chosen)
            {
                icon(list,
                    Icon::Check,
                    {at.x + width - dp(20), at.y + row * 0.5f},
                    dp(14),
                    col(color::sun));
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopID();
    return changed;
}

bool icon_button(
    const char* id, Icon kind, const char* tooltip, ImVec2 pos, float size, bool enabled)
{
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton("##icon", {size, size});
    const bool held = ImGui::IsItemActive();
    ImGui::EndDisabled();
    if (enabled)
    {
        hand_cursor();
    }
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", tooltip);
    }
    const float hover = animate(ImGui::GetID("##hover"), enabled && ImGui::IsItemHovered());
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 max = add(pos, {size, size});
    draw->AddRectFilled(pos,
        max,
        col(held ? mix(color::field, color::coral, 0.2f) : color::field, 0.55f + 0.35f * hover),
        dp(10));
    draw->AddRect(pos, max, col(color::white, 0.07f + 0.1f * hover), dp(10), 0, 1);
    if (kind != Icon::None)
    {
        icon(draw,
            kind,
            {pos.x + size * 0.5f, pos.y + size * 0.5f},
            size * 0.46f,
            col(enabled ? mix(color::muted, color::text, hover) : color::dim));
    }
    ImGui::PopID();
    return pressed && enabled;
}

void spinner(ImDrawList* draw, ImVec2 center, float radius, ImU32 color)
{
    const float start = static_cast<float>(ImGui::GetTime()) * 6.0f;
    draw->PathArcTo(center, radius, start, start + 4.2f, 24);
    draw->PathStroke(color, 0, std::max(1.5f, radius * 0.22f));
}

} // namespace beachlife::launcher::ui
