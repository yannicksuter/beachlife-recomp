#include "launcher.h"
#include "image.h"
#include "build_version.h"

#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace beachlife::launcher {
namespace {
namespace fs = std::filesystem;

constexpr ImU32 background = IM_COL32(10, 16, 24, 255);
constexpr ImU32 sidebar = IM_COL32(14, 22, 33, 255);
constexpr ImU32 card = IM_COL32(18, 29, 42, 255);
constexpr ImU32 border = IM_COL32(39, 56, 73, 255);
constexpr ImU32 text = IM_COL32(233, 241, 247, 255);
constexpr ImU32 muted = IM_COL32(145, 164, 184, 255);
constexpr ImU32 dim = IM_COL32(100, 122, 144, 255);
constexpr ImU32 accent = IM_COL32(66, 220, 209, 255);
constexpr ImU32 amber = IM_COL32(245, 194, 116, 255);
constexpr std::array<const char*, 4> pages = {"Play", "Game files", "Appearance", "About"};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
}

SDL_Texture* fade_texture(SDL_Renderer* renderer, bool vertical) {
    std::array<unsigned char, 256*4> pixels{};
    for (int i = 0; i < 256; ++i) {
        pixels[i*4] = 10; pixels[i*4+1] = 16; pixels[i*4+2] = 24;
        pixels[i*4+3] = static_cast<unsigned char>(i);
    }
    auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
        vertical ? 1 : 256, vertical ? 256 : 1);
    require(texture != nullptr, "Cannot create the header fade");
    if (!SDL_UpdateTexture(texture, nullptr, pixels.data(), vertical ? 4 : 1024)) {
        SDL_DestroyTexture(texture);
        require(false, "Cannot initialize the header fade");
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return texture;
}

struct DialogResult {
    std::mutex mutex;
    bool pending = false;
    bool completed = false;
    std::string path;
    std::string error;
    SourceKind kind = SourceKind::Iso;
};

void SDLCALL dialog_result(void* data, const char* const* files, int) {
    std::unique_ptr<std::shared_ptr<DialogResult>> owner(static_cast<std::shared_ptr<DialogResult>*>(data));
    auto& result = **owner;
    const std::lock_guard lock(result.mutex);
    if (!files) result.error = SDL_GetError();
    else if (files[0]) result.path = files[0];
    result.completed = true;
    result.pending = false;
}

class Launcher {
public:
    explicit Launcher(const Options& options) : options_(options), page_(options.page) {}
    ~Launcher() {
        if (renderer_backend_) ImGui_ImplSDLRenderer3_Shutdown();
        if (platform_backend_) ImGui_ImplSDL3_Shutdown();
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        SDL_DestroyTexture(artwork_);
        SDL_DestroyTexture(fade_horizontal_);
        SDL_DestroyTexture(fade_vertical_);
        SDL_DestroyRenderer(renderer_);
        SDL_DestroyWindow(window_);
        SDL_Quit();
    }

    int run() {
        initialize();
        int frame = 0;
        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                    close_requested_ = true;
                if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
                    settings_.source = event.drop.data;
                    std::error_code error;
                    settings_.kind = fs::is_directory(path_from_utf8(settings_.source), error)
                        ? SourceKind::Folder : SourceKind::Iso;
                    source_ = inspect_source(settings_);
                    page_ = 1;
                }
            }
            consume_dialog();
            if (options_.smoke && options_.screenshot.empty()) page_ = (frame / 3) % 4;
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            const auto display = ImGui::GetIO().DisplaySize;
            scale_ = std::min({settings_.scale / 100.0f, display.x / 960.0f, display.y / 620.0f});
            ImGui::GetStyle() = base_style_;
            ImGui::GetStyle().ScaleAllSizes(scale_);
            ImGui::GetIO().FontGlobalScale = scale_ / density_;
            ImGui::NewFrame();
            draw(display);
            if (close_requested_) {
                if (settings_ == saved_) running = false;
                else ImGui::OpenPopup("Save changes?");
                close_requested_ = false;
            }
            if (ImGui::BeginPopupModal("Save changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Save your launcher changes before closing?");
                ImGui::Spacing();
                if (ImGui::Button("Save and close") && save()) running = false;
                ImGui::SameLine();
                if (ImGui::Button("Discard")) running = false;
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                if (!notice_.empty()) ImGui::TextWrapped("%s", notice_.c_str());
                ImGui::EndPopup();
            }
            ImGui::Render();
            require(SDL_SetRenderDrawColor(renderer_, 10, 16, 24, 255), "Cannot set the background");
            require(SDL_RenderClear(renderer_), "Cannot clear the launcher");
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
            if (!options_.screenshot.empty() && frame == 4) {
                auto* surface = SDL_RenderReadPixels(renderer_, nullptr);
                require(surface != nullptr, "Cannot capture the launcher");
                const bool saved = SDL_SavePNG(surface, path_utf8(options_.screenshot).c_str());
                SDL_DestroySurface(surface);
                require(saved, "Cannot save the screenshot");
                running = false;
            }
            require(SDL_RenderPresent(renderer_), "Cannot present the launcher");
            if (options_.smoke && ++frame >= 12) running = false;
            else if (!options_.smoke) ++frame;
            SDL_Delay(8);
        }
        if (options_.smoke) std::puts("Launcher window, artwork, fonts and pages verified.");
        if (options_.smoke || !options_.screenshot.empty())
            std::printf("Launcher renderer: %s\n", SDL_GetRendererName(renderer_));
        return 0;
    }

private:
    float dp(float value) const { return value * scale_; }
    ImVec2 point(float x, float y) const { return {dp(x), dp(y)}; }

    void initialize() {
        SDL_SetAppMetadata("Beach Life", build::version, "org.beachlife.recomp");
        require(SDL_Init(SDL_INIT_VIDEO), "Cannot initialize the launcher");
        if (options_.config.empty()) {
            char* preference = SDL_GetPrefPath("BeachLife", "BeachLife");
            require(preference != nullptr, "Cannot locate your settings folder");
            config_ = path_from_utf8(preference) / "launcher.cfg";
            SDL_free(preference);
        } else config_ = options_.config;
        try { saved_ = settings_ = load_settings(config_); }
        catch (const std::exception& error) { notice_ = error.what(); }
        if (!options_.source.empty()) {
            settings_.source = options_.source;
            settings_.kind = options_.kind;
        }
        source_ = inspect_source(settings_);
        const auto flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
            (options_.smoke ? SDL_WINDOW_HIDDEN : 0);
        window_ = SDL_CreateWindow((std::string("Beach Life ") + build::version).c_str(), 1180, 760, flags);
        require(window_ != nullptr, "Cannot create the launcher window");
        SDL_SetWindowMinimumSize(window_, 900, 600);
#ifdef _WIN32
        const char* preferred = "direct3d12";
#elif defined(__APPLE__)
        const char* preferred = "metal";
#else
        const char* preferred = "vulkan";
#endif
        renderer_ = SDL_CreateRenderer(window_, options_.smoke ? "software" : preferred);
        // The launcher remains usable without a working GPU driver. This does
        // not choose or qualify the future native game's rendering backend.
        if (!renderer_ && !options_.smoke) renderer_ = SDL_CreateRenderer(window_, "software");
        require(renderer_ != nullptr, "Cannot create the launcher renderer");
        SDL_SetRenderVSync(renderer_, 1);
        density_ = std::max(1.0f, SDL_GetWindowPixelDensity(window_));
        const char* base = SDL_GetBasePath();
        require(base != nullptr, "Cannot locate launcher resources");
        const auto resources = path_from_utf8(base) / "assets" / "launcher";
        artwork_ = load_artwork(renderer_, path_utf8(resources / "header.jpg").c_str());
        require(artwork_ != nullptr, "Cannot load header.jpg; keep the package resources with the application");
        require(SDL_GetTextureSize(artwork_, &art_width_, &art_height_), "Cannot read the header dimensions");
        fade_horizontal_ = fade_texture(renderer_, false);
        fade_vertical_ = fade_texture(renderer_, true);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        if (!fs::is_regular_file(resources / "Roboto-Medium.ttf"))
            throw std::runtime_error("The launcher font is missing. Restore the complete package.");
        const auto font_path = path_utf8(resources / "Roboto-Medium.ttf");
        for (size_t i = 0; i < fonts_.size(); ++i) {
            fonts_[i] = io.Fonts->AddFontFromFileTTF(font_path.c_str(), font_sizes_[i] * density_);
            if (!fonts_[i]) throw std::runtime_error("Cannot load the launcher font. Restore the complete package.");
        }
        io.FontDefault = fonts_[0];
        auto& style = ImGui::GetStyle();
        ImGui::StyleColorsDark();
        style.WindowRounding = 0;
        style.WindowBorderSize = 0;
        style.WindowPadding = {0, 0};
        style.FramePadding = {13, 10};
        style.ItemSpacing = {12, 12};
        style.FrameRounding = 8;
        style.PopupRounding = 10;
        style.PopupBorderSize = 1;
        style.ScrollbarSize = 10;
        style.Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(text);
        style.Colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(dim);
        style.Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(background);
        style.Colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(card);
        style.Colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(border);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.045f, 0.075f, 0.11f, 1);
        style.Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.17f, 0.22f, 1);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.13f, 0.26f, 0.30f, 1);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.33f, 0.36f, 1);
        style.Colors[ImGuiCol_Header] = ImVec4(0.12f, 0.29f, 0.31f, 1);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.13f, 0.32f, 0.34f, 1);
        style.Colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(accent);
        base_style_ = style;
        platform_backend_ = ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
        require(platform_backend_, "Cannot initialize launcher input");
        renderer_backend_ = ImGui_ImplSDLRenderer3_Init(renderer_);
        require(renderer_backend_, "Cannot initialize launcher drawing");
    }

    void label(ImDrawList* draw, float x, float y, const std::string& value,
               ImU32 color = text, int font = 0, float wrap = 0) const {
        draw->AddText(fonts_[font], dp(font_sizes_[font]), {x, y}, color, value.c_str(), nullptr, wrap);
    }

    void icon(ImDrawList* draw, ImVec2 p, int kind, ImU32 color) const {
        const float r = dp(8), thick = dp(1.6f);
        if (kind == 0) {
            draw->AddTriangleFilled({p.x-r*0.6f,p.y-r}, {p.x-r*0.6f,p.y+r}, {p.x+r,p.y}, color);
        } else if (kind == 1) {
            draw->AddCircle(p, r, color, 24, thick);
            draw->AddCircle(p, r*0.25f, color, 16, thick);
        } else if (kind == 2) {
            for (int i = 0; i < 3; ++i) {
                const float y = p.y + (i-1)*dp(6);
                draw->AddLine({p.x-r,y}, {p.x+r,y}, color, thick);
                draw->AddCircleFilled({p.x+(i == 1 ? r*0.4f : -r*0.4f),y}, dp(2.5f), color);
            }
        } else {
            draw->AddCircle(p, r, color, 24, thick);
            draw->AddCircleFilled({p.x,p.y-dp(3)}, dp(1), color);
            draw->AddLine({p.x,p.y}, {p.x,p.y+dp(4)}, color, thick);
        }
    }

    bool button(const char* name, float x, float y, float width, float height, bool primary = false, bool enabled = true) {
        ImGui::SetCursorScreenPos({x,y});
        if (primary) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f,0.86f,0.82f,1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f,0.94f,0.88f,1));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f,0.74f,0.72f,1));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.025f,0.12f,0.14f,1));
        }
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::Button(name, {width,height});
        ImGui::EndDisabled();
        if (primary) ImGui::PopStyleColor(4);
        return clicked;
    }

    void draw(ImVec2 display) {
        ImGui::SetNextWindowPos({0,0});
        ImGui::SetNextWindowSize(display);
        ImGui::Begin("Beach Life", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        auto* draw = ImGui::GetWindowDrawList();
        const float side = dp(224), footer = display.y-dp(66);
        draw->AddRectFilled({0,0}, {side,display.y}, sidebar);
        draw->AddLine({side,0}, {side,display.y}, border);
        label(draw, dp(24), dp(28), "N A T I V E   P C   P O R T", muted, 1);
        label(draw, dp(24), dp(49), "BEACH LIFE", accent, 2);
        label(draw, dp(24), dp(84), "A classic getaway, rediscovered.", dim, 1);
        for (int i = 0; i < 4; ++i) {
            const float top = dp(126 + i*49);
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0,0.5f});
            ImGui::PushStyleColor(ImGuiCol_Button, i == page_ ? ImVec4(0.10f,0.24f,0.27f,1) : ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(i == page_ ? text : muted));
            const std::string name = "           " + std::string(pages[i]);
            if (button(name.c_str(), dp(12), top, side-dp(24), dp(42))) page_ = i;
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            icon(draw, {dp(36), top+dp(21)}, i, i == page_ ? accent : muted);
            if (page_ == i) draw->AddRectFilled({dp(12),top+dp(10)}, {dp(15),top+dp(32)}, accent, dp(2));
        }
        label(draw, dp(24), display.y-dp(68), "LAUNCHER PREVIEW", dim, 1);
        label(draw, dp(24), display.y-dp(44), build::version, muted, 1, side-dp(40));

        ImGui::SetCursorScreenPos({side,0});
        ImGui::BeginChild("page", {display.x-side,footer}, false, ImGuiWindowFlags_NoBackground);
        if (page_ == 0) draw_home(side, display.x-side);
        else draw_page(side, display.x-side);
        ImGui::EndChild();

        draw->AddRectFilled({side,footer}, display, sidebar);
        draw->AddLine({side,footer}, {display.x,footer}, border);
        const bool dirty = settings_ != saved_;
        icon(draw, {side+dp(32),footer+dp(33)}, 3, dirty ? amber : muted);
        const std::string footer_text = notice_.empty() ? (dirty ? "Unsaved changes" : "All changes saved") : notice_;
        const float available = display.x-side-dp(360);
        draw->PushClipRect({side+dp(48),footer}, {side+dp(48)+available,display.y}, true);
        label(draw, side+dp(48), footer+dp(24), footer_text, dirty ? amber : muted, 1);
        draw->PopClipRect();
        if (!notice_.empty() && ImGui::IsMouseHoveringRect({side,footer}, {display.x-dp(300),display.y}))
            ImGui::SetTooltip("%s", notice_.c_str());
        if (button("Revert", display.x-dp(278), footer+dp(13), dp(100), dp(40), false, dirty)) {
            settings_ = saved_;
            source_ = inspect_source(settings_);
            notice_.clear();
        }
        if (button("Save changes", display.x-dp(166), footer+dp(13), dp(142), dp(40), dirty, dirty)) save();
        ImGui::End();
    }

    void draw_home(float side, float width) {
        auto* draw = ImGui::GetWindowDrawList();
        const auto origin = ImGui::GetCursorScreenPos();
        const float y = origin.y, left = side+dp(36), right = side+width-dp(36), hero = dp(305);
        // Fit the artwork without stretching it, cropping only at display time.
        const float art_area = width*0.73f;
        const float visible_fraction = std::min(1.0f, hero * art_width_*0.84f / (art_area * art_height_));
        const float v0 = 0.03f;
        draw->AddImage(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(artwork_)),
            {side,y}, {side+art_area,y+hero}, {0,v0}, {0.84f,v0+visible_fraction});
        draw->AddRectFilled({side,y}, {side+art_area,y+hero}, IM_COL32(10,16,24,45));
        // Texture alpha also works in SDL's software renderer, which cannot
        // interpolate vertex alpha reliably for this header composition.
        draw->AddImage(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(fade_horizontal_)),
            {side+art_area-dp(230),y}, {side+art_area,y+hero});
        draw->AddImage(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(fade_vertical_)),
            {side,y+hero-dp(130)}, {side+width,y+hero});
        const float title_x = side+width*0.52f;
        label(draw, title_x, y+dp(63), "SUN. SEA. A FRESH START.", accent, 1);
        label(draw, title_x, y+dp(93), "BEACH LIFE", text, 4);
        draw->AddRectFilled({title_x,y+dp(156)}, {title_x+dp(48),y+dp(159)}, accent, dp(1));
        label(draw, title_x, y+dp(177), "Your island escape, rebuilt for modern PCs.", text, 0, right-title_x);
        label(draw, title_x, y+dp(229), "FIRST LAUNCHER PREVIEW", muted, 1);

        const float action_y = y+dp(323);
        if (button(source_.selected ? "Manage game files" : "Choose game files", left, action_y, dp(222), dp(52), true)) page_ = 1;
        button("Play", left+dp(236), action_y, dp(114), dp(52), false, false);
        label(draw, left+dp(370), action_y+dp(4), "Game integration in progress", text, 0, right-left-dp(370));
        label(draw, left+dp(370), action_y+dp(28), "Play will arrive in a later build.", muted, 1, right-left-dp(370));

        const float gap = dp(14), tile_width = (right-left-2*gap)/3;
        const float card_y = y+dp(408), card_height = dp(124);
        for (int i = 0; i < 3; ++i) {
            const float x = left+i*(tile_width+gap);
            draw->AddRectFilled({x,card_y}, {x+tile_width,card_y+card_height}, card, dp(12));
            draw->AddRect({x,card_y}, {x+tile_width,card_y+card_height}, border, dp(12));
            icon(draw, {x+dp(25),card_y+dp(27)}, i == 0 ? 1 : i == 1 ? 3 : 0, i == 2 ? amber : accent);
            const char* title = i == 0 ? "Game source" : i == 1 ? "Game language" : "Game status";
            label(draw, x+dp(45), card_y+dp(19), title, muted, 1);
            std::string value = i == 0 ? source_.title : i == 1 ? "Not detected yet" : "Under development";
            label(draw, x+dp(18), card_y+dp(54), value, text, 0, tile_width-dp(36));
            std::string detail = i == 0 ? (source_.readable ? "Selection saved when you click Save" : "ISO image or installed folder")
                : i == 1 ? "Available languages follow your source" : "Launcher preview available now";
            label(draw, x+dp(18), card_y+dp(88), detail, dim, 1, tile_width-dp(36));
            ImGui::SetCursorScreenPos({x,card_y});
            if (ImGui::InvisibleButton(("card"+std::to_string(i)).c_str(), {tile_width,card_height})) page_ = i == 2 ? 3 : 1;
        }
        label(draw, left, y+dp(562), "Bring your own game files", text, 0);
        label(draw, left, y+dp(590), "Choose your original disc image or installed folder. Your files stay where they are.", muted, 1, right-left);
        ImGui::SetCursorScreenPos({left,y+dp(632)});
        ImGui::Dummy({1,1});
    }

    void draw_page(float side, float width) {
        auto* draw = ImGui::GetWindowDrawList();
        const float top = ImGui::GetCursorScreenPos().y, left = side+dp(36), available = width-dp(72);
        label(draw, left, top+dp(38), pages[page_], text, 3);
        const char* subtitle = page_ == 1 ? "Choose the original files that belong to your copy of Beach Life."
            : page_ == 2 ? "Make the launcher comfortable on your screen."
            : "Beach Life, rebuilt to run natively on modern PCs.";
        label(draw, left, top+dp(86), subtitle, muted, 0, available);
        if (page_ == 1) {
            label(draw, left, top+dp(141), "SOURCE TYPE", dim, 1);
            if (button("Disc image (.iso)", left, top+dp(168), dp(200), dp(44), settings_.kind == SourceKind::Iso)) {
                settings_.kind = SourceKind::Iso; source_ = inspect_source(settings_);
            }
            if (button("Installed folder", left+dp(212), top+dp(168), dp(200), dp(44), settings_.kind == SourceKind::Folder)) {
                settings_.kind = SourceKind::Folder; source_ = inspect_source(settings_);
            }
            label(draw, left, top+dp(244), "LOCATION", dim, 1);
            ImGui::SetCursorScreenPos({left,top+dp(272)});
            ImGui::SetNextItemWidth(available-dp(124));
            if (ImGui::InputTextWithHint("##source", "Select a file, paste a path, or drag it into this window", &settings_.source))
                source_ = inspect_source(settings_);
            bool pending;
            { const std::lock_guard lock(dialog_->mutex); pending = dialog_->pending; }
            if (button("Browse...", left+available-dp(112), top+dp(272), dp(112), dp(39), false, !pending)) browse();
            draw->AddRectFilled({left,top+dp(339)}, {left+available,top+dp(444)}, card, dp(12));
            icon(draw, {left+dp(25),top+dp(368)}, 3, source_.readable ? accent : amber);
            label(draw, left+dp(46), top+dp(355), source_.title, text, 0, available-dp(66));
            label(draw, left+dp(46), top+dp(387), source_.detail, muted, 1, available-dp(66));
            label(draw, left, top+dp(483), "GAME LANGUAGE", dim, 1);
            button("Waiting for content detection", left, top+dp(510), dp(290), dp(42), false, false);
            label(draw, left, top+dp(574), "Only languages present in your game data will be offered. Installer languages are separate.", muted, 1, available);
            label(draw, left, top+dp(625), "Selecting a source does not install, extract or modify your game files.", dim, 1, available);
        } else if (page_ == 2) {
            draw->AddRectFilled({left,top+dp(148)}, {left+available,top+dp(282)}, card, dp(12));
            label(draw, left+dp(22), top+dp(170), "Interface size", text, 0);
            label(draw, left+dp(22), top+dp(204), "Adjusts text and controls. Layout adapts to fit smaller windows.", muted, 1, available-dp(240));
            ImGui::SetCursorScreenPos({left+available-dp(172),top+dp(176)});
            ImGui::SetNextItemWidth(dp(148));
            const std::string preview = std::to_string(settings_.scale)+"%";
            if (ImGui::BeginCombo("##scale", preview.c_str())) {
                for (int scale : {100,125,150}) {
                    const std::string name = std::to_string(scale)+"%";
                    if (ImGui::Selectable(name.c_str(), settings_.scale == scale)) settings_.scale = scale;
                }
                ImGui::EndCombo();
            }
            label(draw, left, top+dp(323), "Game display and audio settings will appear as those features become available.", muted, 0, available);
        } else {
            label(draw, left, top+dp(147), "LAUNCHER PREVIEW", accent, 1);
            label(draw, left, top+dp(179), build::version, text, 2, available);
            label(draw, left, top+dp(231), "This first build lets you choose and remember your game source. Gameplay and game-language detection are still in development.", muted, 0, available);
            label(draw, left, top+dp(315), "Designed for Windows, macOS and Linux", text, 0);
            label(draw, left, top+dp(347), "The native game will use DX12 / Vulkan on Windows, Metal on macOS and Vulkan on Linux.", muted, 1, available);
            draw->AddLine({left,top+dp(405)}, {left+available,top+dp(405)}, border);
            label(draw, left, top+dp(433), "CREDITS", dim, 1);
            label(draw, left, top+dp(464), "Original game: Deep Red Games / Eidos Interactive (2002).", muted, 1, available);
            label(draw, left, top+dp(493), "Launcher design reference: Recharged. Interface: SDL3, Dear ImGui and Roboto.", muted, 1, available);
            label(draw, left, top+dp(522), "Beach Life artwork from the LaunchBox Games Database. Rights remain with their respective owners.", muted, 1, available);
            if (button("View project", left, top+dp(580), dp(154), dp(42)))
                if (!SDL_OpenURL("https://github.com/yannicksuter/beachlife-recomp")) notice_ = SDL_GetError();
        }
        ImGui::SetCursorScreenPos({left,top+dp(685)});
        ImGui::Dummy({1,1});
    }

    void browse() {
        static const SDL_DialogFileFilter filters[] = {{"ISO disc image", "iso"}};
        {
            const std::lock_guard lock(dialog_->mutex);
            if (dialog_->pending) return;
            dialog_->pending = true;
            dialog_->kind = settings_.kind;
        }
        auto* owner = new std::shared_ptr<DialogResult>(dialog_);
        if (settings_.kind == SourceKind::Folder)
            SDL_ShowOpenFolderDialog(dialog_result, owner, window_, nullptr, false);
        else SDL_ShowOpenFileDialog(dialog_result, owner, window_, filters, 1, nullptr, false);
    }

    void consume_dialog() {
        const std::lock_guard lock(dialog_->mutex);
        if (!dialog_->completed) return;
        if (!dialog_->error.empty()) notice_ = "Cannot open the file picker: " + dialog_->error + ". You can paste the path instead.";
        if (!dialog_->path.empty()) {
            settings_.source = dialog_->path;
            settings_.kind = dialog_->kind;
            source_ = inspect_source(settings_);
            notice_.clear();
        }
        dialog_->path.clear();
        dialog_->error.clear();
        dialog_->completed = false;
    }

    bool save() {
        try {
            if (!settings_.source.empty())
                settings_.source = path_utf8(fs::absolute(path_from_utf8(settings_.source)).lexically_normal());
            save_settings(config_, settings_);
            saved_ = settings_;
            source_ = inspect_source(settings_);
            notice_.clear();
            return true;
        } catch (const std::exception& error) { notice_ = error.what(); return false; }
    }

    Options options_;
    fs::path config_;
    Settings settings_, saved_;
    SourceStatus source_;
    std::string notice_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* artwork_ = nullptr;
    SDL_Texture* fade_horizontal_ = nullptr;
    SDL_Texture* fade_vertical_ = nullptr;
    ImGuiStyle base_style_;
    bool platform_backend_ = false, renderer_backend_ = false, close_requested_ = false;
    int page_ = 0;
    float scale_ = 1, density_ = 1, art_width_ = 2048, art_height_ = 1536;
    const std::array<float, 5> font_sizes_ = {16,13,25,32,54};
    std::array<ImFont*,5> fonts_{};
    std::shared_ptr<DialogResult> dialog_ = std::make_shared<DialogResult>();
};
} // namespace

int run(const Options& options) {
    try { return Launcher(options).run(); }
    catch (const std::exception& error) {
        std::fprintf(stderr, "Beach Life: %s\n", error.what());
        if (!options.smoke) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Beach Life", error.what(), nullptr);
        return 1;
    }
}
} // namespace beachlife::launcher
