#pragma once
#include "imgui.h"

#include <SDL3/SDL.h>
#include <initializer_list>
#include <string>
#include <vector>

// Visual building blocks of the launcher: palette, fonts and drawn widgets.
// Sizes are design units, converted to window units with dp().
namespace beachlife::launcher::ui {

namespace color {
inline constexpr ImVec4 ocean{0.027f, 0.071f, 0.122f, 1};   // #07121F window
inline constexpr ImVec4 deep{0.039f, 0.094f, 0.157f, 1};    // #0A1828 panels
inline constexpr ImVec4 card{0.063f, 0.141f, 0.224f, 1};    // #102439
inline constexpr ImVec4 field{0.082f, 0.173f, 0.267f, 1};   // #152C44
inline constexpr ImVec4 border{0.149f, 0.267f, 0.384f, 1};  // #264462
inline constexpr ImVec4 text{0.953f, 0.973f, 0.988f, 1};    // #F3F8FC
inline constexpr ImVec4 muted{0.627f, 0.710f, 0.792f, 1};   // #A0B5CA
inline constexpr ImVec4 dim{0.420f, 0.510f, 0.600f, 1};     // #6B8299
inline constexpr ImVec4 sun{1.000f, 0.800f, 0.380f, 1};     // #FFCC61
inline constexpr ImVec4 coral{1.000f, 0.431f, 0.353f, 1};   // #FF6E5A
inline constexpr ImVec4 lagoon{0.290f, 0.859f, 0.886f, 1};  // #4ADBE2
inline constexpr ImVec4 palm{0.369f, 0.902f, 0.584f, 1};    // #5EE695
inline constexpr ImVec4 amber{1.000f, 0.741f, 0.341f, 1};   // #FFBD57
inline constexpr ImVec4 white{1, 1, 1, 1};
} // namespace color

ImU32 col(const ImVec4& color, float alpha = 1);
ImVec4 mix(const ImVec4& a, const ImVec4& b, float t);

enum Font { Body, Caption, Label, Heading, Title, Button, FontCount };
enum class Icon { None, Play, Disc, Sliders, Info, Folder, Check, Warning, Globe, Sun, Chevron, Link, Shield,
                  Monitor, Revert, Speaker, Gear, Terminal, Refresh, Download };
enum class Side { Left, Right, Top, Bottom };

void set_scale(float scale);
float dp(float value);
// Rasterizes every font for `raster` framebuffer pixels per design unit.
bool load_fonts(const std::string& ttf, float raster, float density);
ImFont* font(Font which);
float font_size(Font which);
ImVec2 text_size(Font which, const char* value, float wrap = 0);

void text(ImDrawList* draw, Font which, ImVec2 pos, ImU32 color, const char* value, float wrap = 0);
void text_shadowed(ImDrawList* draw, Font which, ImVec2 pos, ImU32 color, const char* value, float wrap = 0);
// Medium weight thickened by a sub-pixel double strike, for titles.
void text_bold(ImDrawList* draw, Font which, ImVec2 pos, ImU32 color, const char* value, float wrap = 0,
               bool shadow = false);
void text_gradient(ImDrawList* draw, Font which, ImVec2 pos, ImU32 top, ImU32 bottom, const char* value,
                   float wrap = 0);
void tracked(ImDrawList* draw, Font which, ImVec2 pos, ImU32 color, const char* value, float tracking,
             bool shadow = false);
float tracked_width(Font which, const char* value, float tracking);
// Ellipsized to `width` at its end, or at its start to keep a file name visible.
std::string fit(Font which, const std::string& value, float width, bool keep_end = false);

void gradient(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 first, ImU32 second, float rounding, bool horizontal);
void icon(ImDrawList* draw, Icon kind, ImVec2 center, float size, ImU32 color);
float animate(ImGuiID id, bool target, float speed = 12);

// Eased fades and soft glows from the textures created by image.cpp.
void set_textures(SDL_Texture* ramp, SDL_Texture* spot);
void fade(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color, Side opaque);
void glow(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color);

void apply_theme(ImGuiStyle& style);
void panel(ImDrawList* draw, ImVec2 a, ImVec2 b, float opacity = 0.72f);
float chip(ImDrawList* draw, ImVec2 pos, const char* label, const ImVec4& tint);

bool nav_item(const char* label, Icon kind, bool selected, ImVec2 pos, float width);
bool primary_button(const char* id, const char* label, Icon kind, ImVec2 pos, ImVec2 size, bool enabled = true,
                    bool glowing = false);
bool secondary_button(const char* id, const char* label, Icon kind, ImVec2 pos, ImVec2 size, bool enabled = true);
struct Segment { const char* label; Icon kind; };
bool segmented(const char* id, int* selected, std::initializer_list<Segment> items, ImVec2 pos, ImVec2 size);
bool toggle(const char* id, bool* value, ImVec2 pos);
// A square button showing only an icon (a tooltip names it).
bool icon_button(const char* id, Icon kind, const char* tooltip, ImVec2 pos, float size, bool enabled = true);
void spinner(ImDrawList* draw, ImVec2 center, float radius, ImU32 color);
inline constexpr ImVec2 toggle_size{46, 26}; // design units
// A field that opens a list below it, or above it near the window's bottom;
// `selected` may be -1 (nothing chosen).
bool dropdown(const char* id, const std::vector<std::string>& items, int* selected, ImVec2 pos, ImVec2 size);

} // namespace beachlife::launcher::ui
