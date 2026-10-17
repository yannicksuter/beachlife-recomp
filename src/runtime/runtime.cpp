#include "runtime/runtime.h"
#include "data/content_manifest.h"
#include "platform/app_icon.h"
#include "platform/path.h"
#include "platform/video.h"
#include "runtime/content_startup.h"
#include "runtime/fps_overlay.h"
#include "runtime/loading_view.h"
#include "runtime/startup.h"

#include "imgui.h"
#include "imgui_freetype.h"
#include "imgui_impl_sdl3.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace beachlife::runtime
{
namespace
{
// The game's back buffer; the original's display modes are not mapped yet.
constexpr int frame_width = 800, frame_height = 600;
// The simulation's fixed step; the original's rate is not known yet either.
constexpr double tick = 1.0 / 60;

platform::VideoSettings video_settings(const launcher::Settings& settings, bool hidden)
{
    const auto& display = settings.display;
    platform::VideoSettings video;
    video.fullscreen = display.mode == launcher::WindowMode::Fullscreen;
    video.width = display.width;
    video.height = display.height;
    video.display = display.monitor;
    video.vsync = display.vsync;
    video.stretch = display.scaling == launcher::Scaling::Stretch;
    video.api = display.api == launcher::GraphicsApi::Direct3D12 ? platform::GraphicsApi::Direct3D12
                : display.api == launcher::GraphicsApi::Vulkan   ? platform::GraphicsApi::Vulkan
                : display.api == launcher::GraphicsApi::Metal    ? platform::GraphicsApi::Metal
                                                                 : platform::GraphicsApi::Automatic;
    video.hidden = hidden;
    return video;
}

std::string api_title(const std::string& driver)
{
    if (driver == "direct3d12")
    {
        return "Direct3D 12";
    }
    if (driver == "vulkan")
    {
        return "Vulkan";
    }
    if (driver == "metal")
    {
        return "Metal";
    }
    return driver;
}

// Esc or closing the window returns to the launcher.
bool leaves(const SDL_Event& event)
{
    return event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
           (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE);
}

// Runs the startup steps on a worker behind the loading screen, then fades out to
// the game's back buffer. Returns false when the player leaves first; the
// error of a failed step is thrown.
bool start_game(platform::Video& video,
    const platform::Frame& background,
    const std::filesystem::path& font,
    std::vector<StartupStep> steps)
{
    LoadingView view(video, background, font);
    StartupWork work(std::move(steps));
    const auto left = []
    {
        bool leave = false;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            leave |= leaves(event);
        }
        return leave;
    };
    // About 60 frames per second; the worker keeps the rest.
    auto next = std::chrono::steady_clock::now();
    const auto pause = [&next]
    {
        next = std::max(std::chrono::steady_clock::now(), next + std::chrono::microseconds(16667));
        std::this_thread::sleep_until(next);
    };
    for (;;)
    {
        if (left())
        {
            return false; // an already queued leave wins over finished work
        }
        const auto status = work.status();
        if (status.failed)
        {
            work.rethrow();
        }
        if (status.ready)
        {
            break;
        }
        view.draw(status.label, status.share());
        pause();
    }
    // Completes the bar and fades out: a single black frame when nothing was shown.
    for (;;)
    {
        if (left())
        {
            return false;
        }
        if (!view.draw_finished())
        {
            return true;
        }
        pause();
    }
}

// Shuts the parts down in reverse order, also after an error.
struct Session
{
    std::unique_ptr<platform::Video> video;
    bool sdl = false, input = false;
    ~Session()
    {
        if (input)
        {
            ImGui_ImplSDL3_Shutdown();
        }
        video.reset();
        if (ImGui::GetCurrentContext())
        {
            ImGui::DestroyContext();
        }
        if (sdl)
        {
            SDL_Quit();
        }
    }
};

int run_game(const Options& options)
{
    const auto& settings = options.settings;
    Session session;
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        throw std::runtime_error(std::string("Cannot start the game: ") + SDL_GetError());
    }
    session.sdl = true;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.Fonts->FontBuilderIO =
        ImGuiFreeType::GetBuilderForFreeType(); // the game window's text, as in Recharged
    session.video = platform::create_video(video_settings(settings, options.hidden));
    auto& video = *session.video;
    platform::set_app_icon(video.window(), options.resources.parent_path() / "app" / "icon.png");
    if (!ImGui_ImplSDL3_InitForSDLGPU(video.window()))
    {
        throw std::runtime_error("Cannot set up the overlay's input");
    }
    session.input = true;

    // The back buffer the game will draw into; black while there is no game code.
    platform::Frame frame;
    frame.width = frame_width;
    frame.height = frame_height;
    frame.pixels.assign(static_cast<std::size_t>(frame_width) * frame_height, 0xFF000000u);
    // The window is open before any slow ISO work. The owned mount survives until
    // the session ends, when the launcher may safely offer repair again.
    ContentStartup content(settings,
        options.cache.empty() ? data::default_content_directory() : options.cache,
        options.config,
        options.prepare_only);
    std::vector<StartupStep> steps;
    steps.push_back(content.prepare());
    if (!options.prepare_only)
    {
        steps.push_back(content.read());
    }
    auto preview = std::make_shared<data::TgaImage>();
    if (!options.prepare_only && !options.preview_screen.empty())
    {
        steps.push_back(content.preview(options.preview_screen, preview));
    }
    if (!start_game(video, frame, options.resources / "Roboto-Medium.ttf", std::move(steps)))
    {
        return 0;
    }
    if (options.prepare_only)
    {
        return 0;
    }
    if (!options.preview_screen.empty())
    {
        frame.width = preview->width;
        frame.height = preview->height;
        frame.pixels = std::move(preview->pixels);
        SDL_SetWindowTitle(video.window(), "Beach Life - Image preview");
    }

    // The performance overlay, when the Display settings show it.
    std::unique_ptr<FpsOverlay> fps_overlay;
    if (settings.display.fps_overlay)
    {
        fps_overlay = std::make_unique<FpsOverlay>(
            options.resources / "Roboto-Medium.ttf", settings.display.fps_overlay_corner);
    }
    FrameRateMeter frame_rate;
    const auto window_pixels = [&video]
    {
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(video.window(), &width, &height);
        return std::pair{width, height};
    };
    // Replaces the loading screen's font: the overlay's Roboto at the window's pixel
    // density, and the performance overlay's fonts for the window's current size.
    float font_density = 0;
    const auto build_fonts = [&](int pixel_width, int pixel_height, float pixel_density)
    {
        io.Fonts->Clear();
        io.FontDefault = io.Fonts->AddFontFromFileTTF(
            platform::utf8(options.resources / "Roboto-Medium.ttf").c_str(), 15 * pixel_density);
        if (!io.FontDefault)
        {
            throw std::runtime_error("The overlay font is missing. Restore the complete package.");
        }
        io.FontGlobalScale = 1 / pixel_density;
        if (fps_overlay)
        {
            fps_overlay->add_fonts(*io.Fonts, pixel_width, pixel_height, pixel_density);
        }
        font_density = pixel_density;
    };
    {
        const auto [width, height] = window_pixels();
        build_fonts(width, height, std::max(1.0f, SDL_GetWindowPixelDensity(video.window())));
    }
    ImGui::GetStyle().WindowRounding = 10;
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = {0.04f, 0.09f, 0.16f, 0.86f};

    const std::string graphics = api_title(video.info().api) + ", " + video.info().present_mode;
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    std::uint64_t last = SDL_GetPerformanceCounter(), measured_since = last, ticks = 0, frames = 0,
                  measured_frames = 0;
    double accumulator = 0, fps = 0;
    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (leaves(event))
            {
                running = false;
            }
        }
        // Fixed simulation steps for the time that passed (at most a quarter second after a stall).
        const std::uint64_t now = SDL_GetPerformanceCounter();
        accumulator += std::min(static_cast<double>(now - last) / frequency, 0.25);
        last = now;
        for (; accumulator >= tick; accumulator -= tick)
        {
            ++ticks;
        }

        const auto [pixel_width, pixel_height] = window_pixels();
        const float pixel_density = std::max(1.0f, SDL_GetWindowPixelDensity(video.window()));
        // A move to a display with another pixel density, or a resize the
        // performance overlay's text follows, rebuilds the fonts.
        if (pixel_density != font_density ||
            (fps_overlay && fps_overlay->needs_fonts(pixel_width, pixel_height, pixel_density)))
        {
            build_fonts(pixel_width, pixel_height, pixel_density);
        }
        video.new_overlay_frame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({16, 16});
        ImGui::Begin("runtime",
            nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted(options.preview_screen.empty() ? "Beach Life" : "Image preview");
        if (!options.preview_screen.empty())
        {
            ImGui::TextUnformatted(options.preview_screen.c_str());
            ImGui::TextDisabled(
                "%d x %d pixels. Esc returns to the launcher.", frame.width, frame.height);
        }
        else
        {
            ImGui::TextDisabled("No game code yet. Esc returns to the launcher.");
        }
        ImGui::TextDisabled("%s, %.0f FPS", graphics.c_str(), fps);
        ImGui::End();
        if (fps_overlay)
        {
            fps_overlay->draw(pixel_width, pixel_height, pixel_density);
        }
        ImGui::Render();
        if (!options.screenshot.empty() && options.frames &&
            frames + 1 == static_cast<std::uint64_t>(options.frames))
        {
            video.save_next(options.screenshot);
        }
        video.present(frame, ImGui::GetDrawData());

        ++frames;
        ++measured_frames;
        if (fps_overlay)
        {
            if (const auto rate = frame_rate.sample(frames, std::chrono::steady_clock::now()))
            {
                fps_overlay->set_rate(*rate);
            }
        }
        const double elapsed =
            static_cast<double>(SDL_GetPerformanceCounter() - measured_since) / frequency;
        if (elapsed >= 0.5)
        {
            fps = static_cast<double>(measured_frames) / elapsed;
            measured_since = SDL_GetPerformanceCounter();
            measured_frames = 0;
        }
        if (options.frames && frames >= static_cast<std::uint64_t>(options.frames))
        {
            running = false;
        }
    }
    if (options.frames)
    {
        std::printf("Game runtime: %s, %s, %llu frames, %llu ticks.\n",
            video.info().api.c_str(),
            video.info().present_mode.c_str(),
            static_cast<unsigned long long>(frames),
            static_cast<unsigned long long>(ticks));
    }
    return 0;
}
} // namespace

int run(const Options& options)
{
    try
    {
        return run_game(options);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Beach Life: %s\n", error.what());
        if (!options.hidden)
        {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Beach Life", error.what(), nullptr);
        }
        return 1;
    }
}

} // namespace beachlife::runtime
