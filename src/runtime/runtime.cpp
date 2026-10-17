#include "runtime/runtime.h"
#include "data/iso9660.h"
#include "platform/path.h"
#include "platform/video.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace beachlife::runtime {
namespace {
// The game's back buffer; the original's display modes are not mapped yet.
constexpr int frame_width = 800, frame_height = 600;
// The simulation's fixed step; the original's rate is not known yet either.
constexpr double tick = 1.0/60;

platform::VideoSettings video_settings(const launcher::Settings& settings, bool hidden) {
    const auto& display = settings.display;
    platform::VideoSettings video;
    video.fullscreen = display.mode == launcher::WindowMode::Fullscreen;
    video.width = display.width;
    video.height = display.height;
    video.display = display.monitor;
    video.vsync = display.vsync;
    video.stretch = display.scaling == launcher::Scaling::Stretch;
    video.api = display.api == launcher::GraphicsApi::Direct3D12 ? platform::GraphicsApi::Direct3D12
              : display.api == launcher::GraphicsApi::Vulkan ? platform::GraphicsApi::Vulkan
              : display.api == launcher::GraphicsApi::Metal ? platform::GraphicsApi::Metal : platform::GraphicsApi::Automatic;
    video.hidden = hidden;
    return video;
}

std::string api_title(const std::string& driver) {
    if (driver == "direct3d12") return "Direct3D 12";
    if (driver == "vulkan") return "Vulkan";
    if (driver == "metal") return "Metal";
    return driver;
}

// Shuts the parts down in reverse order, also after an error.
struct Session {
    std::unique_ptr<platform::Video> video;
    bool sdl = false, input = false;
    ~Session() {
        if (input) ImGui_ImplSDL3_Shutdown();
        video.reset();
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        if (sdl) SDL_Quit();
    }
};

int run_game(const Options& options) {
    const auto& settings = options.settings;
    // The game files: the selected disc image, read only.
    if (settings.source.empty()) throw std::runtime_error("Choose your game files on the launcher's Game files page first.");
    data::index_iso(platform::utf8_path(settings.source));

    Session session;
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(std::string("Cannot start the game: ") + SDL_GetError());
    session.sdl = true;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    session.video = platform::create_video(video_settings(settings, options.hidden));
    auto& video = *session.video;
    if (!ImGui_ImplSDL3_InitForSDLGPU(video.window())) throw std::runtime_error("Cannot set up the overlay's input");
    session.input = true;
    const float density = std::max(1.0f, SDL_GetWindowPixelDensity(video.window()));
    io.Fonts->Clear(); // the renderer set up ImGui's default font; the launcher's Roboto replaces it
    io.FontDefault = io.Fonts->AddFontFromFileTTF(platform::utf8(options.resources / "Roboto-Medium.ttf").c_str(), 15*density);
    if (!io.FontDefault) throw std::runtime_error("The overlay font is missing. Restore the complete package.");
    io.FontGlobalScale = 1/density;
    ImGui::GetStyle().WindowRounding = 10;
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = {0.04f, 0.09f, 0.16f, 0.86f};

    // The back buffer the game will draw into; black while there is no game code.
    platform::Frame frame;
    frame.width = frame_width;
    frame.height = frame_height;
    frame.pixels.assign(static_cast<std::size_t>(frame_width)*frame_height, 0xFF000000u);
    const std::string graphics = api_title(video.info().api) + ", " + video.info().present_mode;
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    std::uint64_t last = SDL_GetPerformanceCounter(), measured_since = last, ticks = 0, frames = 0, measured_frames = 0;
    double accumulator = 0, fps = 0;
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) running = false;
        }
        // Fixed simulation steps for the time that passed (at most a quarter second after a stall).
        const std::uint64_t now = SDL_GetPerformanceCounter();
        accumulator += std::min(static_cast<double>(now-last)/frequency, 0.25);
        last = now;
        for (; accumulator >= tick; accumulator -= tick) ++ticks;

        video.new_overlay_frame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({16, 16});
        ImGui::Begin("runtime", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted("Beach Life");
        ImGui::TextDisabled("No game code yet. Esc returns to the launcher.");
        ImGui::TextDisabled("%s, %.0f FPS", graphics.c_str(), fps);
        ImGui::End();
        ImGui::Render();
        if (!options.screenshot.empty() && options.frames && frames+1 == static_cast<std::uint64_t>(options.frames))
            video.save_next(options.screenshot);
        video.present(frame, ImGui::GetDrawData());

        ++frames;
        ++measured_frames;
        const double elapsed = static_cast<double>(SDL_GetPerformanceCounter()-measured_since)/frequency;
        if (elapsed >= 0.5) {
            fps = static_cast<double>(measured_frames)/elapsed;
            measured_since = SDL_GetPerformanceCounter();
            measured_frames = 0;
        }
        if (options.frames && frames >= static_cast<std::uint64_t>(options.frames)) running = false;
    }
    if (options.frames)
        std::printf("Game runtime: %s, %s, %llu frames, %llu ticks.\n", video.info().api.c_str(),
                    video.info().present_mode.c_str(), static_cast<unsigned long long>(frames),
                    static_cast<unsigned long long>(ticks));
    return 0;
}
} // namespace

int run(const Options& options) {
    try {
        return run_game(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Beach Life: %s\n", error.what());
        if (!options.hidden) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Beach Life", error.what(), nullptr);
        return 1;
    }
}

} // namespace beachlife::runtime
