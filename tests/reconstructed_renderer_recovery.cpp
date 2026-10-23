#include "renderer_owner_test_data.h"
#include "runtime/renderer_fullscreen_display.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>

// Real generated TIFF owners, SDL GPU Video and complete renderer. This does
// not construct a Game or replace a screen callback with a test implementation.
namespace
{
namespace display = beachlife::runtime::renderer_display;
namespace platform = beachlife::platform;
namespace files = beachlife::runtime::retail_files;
constexpr unsigned width = 800, height = 600;
unsigned checks = 0;

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

struct ImGuiOwner
{
    ImGuiOwner()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
    }
    ~ImGuiOwner()
    {
        ImGui::DestroyContext();
    }
};

struct Masks
{
    renderer_owner_test::MaskGlobals saved;
    ~Masks()
    {
        saved.restore();
    }
};

void leave_fullscreen(platform::Video& video)
{
    require(SDL_SetWindowFullscreen(video.window(), false), "SDL fullscreen exit failed");
    require(SDL_SyncWindow(video.window()), "SDL fullscreen exit did not synchronize");
    require(!(SDL_GetWindowFlags(video.window()) & SDL_WINDOW_FULLSCREEN),
        "the real window did not leave fullscreen");
}

bool exact_mode(platform::Video& video)
{
    const auto id = SDL_GetDisplayForWindow(video.window());
    const auto* applied = SDL_GetCurrentDisplayMode(id);
    const auto* selected = SDL_GetWindowFullscreenMode(video.window());
    int w = 0, h = 0;
    return (SDL_GetWindowFlags(video.window()) & SDL_WINDOW_FULLSCREEN) && applied && selected &&
           applied->w == int(width) && applied->h == int(height) &&
           applied->pixel_density == 1.0f && selected->w == int(width) &&
           selected->h == int(height) && selected->pixel_density == 1.0f &&
           SDL_GetWindowSizeInPixels(video.window(), &w, &h) && w == int(width) && h == int(height);
}

void fill(display::WordSurface16& surface, unsigned short value)
{
    auto pixels = surface.lock();
    std::fill(pixels.begin(), pixels.end(), value);
    surface.unlock();
}

bool filled(const display::WordSurface16& surface, unsigned short value)
{
    const auto pixels = surface.pixels();
    return std::all_of(pixels.begin(),
        pixels.end(),
        [=](auto pixel)
        {
            return pixel == value;
        });
}

void recovered_pixels(const std::filesystem::path& file)
{
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> image(
        SDL_LoadPNG(file.string().c_str()), SDL_DestroySurface);
    require(image && image->w == int(width) && image->h == int(height),
        "recovered GPU frame is missing or has the wrong size");
    bool red = true;
    for (unsigned y = 0; y < height; ++y)
    {
        for (unsigned x = 0; x < width; ++x)
        {
            Uint8 r, g, b, a;
            if (!SDL_ReadSurfacePixel(image.get(), x, y, &r, &g, &b, &a))
            {
                throw std::runtime_error("cannot read recovered GPU frame");
            }
            red &= r == 255 && g == 0 && b == 0 && a == 255;
        }
    }
    require(red, "recovered GPU pixels differ from retained RGB565 words");
}

void recovery(
    ScreenRequestRenderer& renderer, platform::Video& video, const std::filesystem::path& output)
{
    static_assert(sizeof(ScreenSurfacePosition) == 72);
    static_assert(offsetof(ScreenSurfacePosition, native_surface) == 56);
    static_assert(offsetof(ScreenSurfacePosition, native_surface_owned) == 64);
    require(renderer.software_mode == 0 && exact_mode(video), "real mode0 owner is absent");
    require(!renderer.draw && !renderer.primary_surface && !renderer.back_surface,
        "mode0 recovery used a COM substitute");
    auto* primary = renderer.native_primary;
    auto* back = renderer.native_back;
    fill(*primary, 0x001f);
    fill(*back, 0xf800);
    const auto* primary_storage = primary->pixels().data();
    const auto* back_storage = back->pixels().data();
    const int index = renderer.CreateSurface(2, 2, 0);
    auto* entry = renderer.surface_storage[index];
    renderer.PrepareSurface(index);
    fill(*entry->native_surface, 0xffff);
    const auto* offscreen_storage = entry->native_surface->pixels().data();
    renderer.surface_storage[0]->prepared = 1;
    require(
        entry->prepared == 1 && renderer.SurfaceLost(index) == 0 && renderer.SurfaceLost(0) == 0,
        "healthy owned surfaces were reported lost");

    // One live recovery scenario exercises both actual parent forms. There is
    // no synthetic lost flag: SDL changes the real acquired window state.
    leave_fullscreen(video);
    require(renderer.SurfaceLost(index) == 1 && renderer.SurfaceLost(0) == 1,
        "actual fullscreen exit was not detected");
    renderer.RestoreSurfaces();
    require(exact_mode(video) && renderer.SurfaceLost(0) == 0,
        "primary-before-back recovery did not re-establish exact fullscreen mode");
    require(renderer.native_primary == primary && renderer.native_back == back &&
                primary->pixels().data() == primary_storage &&
                back->pixels().data() == back_storage && filled(*primary, 0x001f) &&
                filled(*back, 0xf800) &&
                entry->native_surface->pixels().data() == offscreen_storage &&
                filled(*entry->native_surface, 0xffff) && entry->prepared == 1 &&
                renderer.surface_storage[0]->prepared == 1,
        "global recovery changed CPU storage, pixels, roles or preparation flags");

    leave_fullscreen(video);
    require(renderer.SurfaceLost(index) == 1, "indexed mode loss was not detected");
    renderer.RestoreSurface(index);
    // The omit-restore changed source must reach this diagnostic with genuine
    // cleanup on exception, before any PrepareSurface or drawing callback.
    require(exact_mode(video) && renderer.SurfaceLost(index) == 0,
        "indexed restore did not re-establish exact fullscreen mode");
    require(entry == renderer.surface_storage[index] && entry->prepared == 0 &&
                renderer.surface_storage[0]->prepared == 1 &&
                entry->native_surface->pixels().data() == offscreen_storage &&
                filled(*entry->native_surface, 0xffff) && filled(*primary, 0x001f) &&
                filled(*back, 0xf800),
        "indexed restore did not preserve storage and clear only the reloaded entry");

    // This is the actual immediate next call in RenderScreens. No substitute
    // MarkPrepared or GUI owner is supplied to pretend the full caller runs.
    renderer.PrepareSurface(index);
    require(entry->prepared == 1 && filled(*entry->native_surface, 0x07e0),
        "real preparation after recovery did not refill the selected surface");
    (void)entry->native_surface->lock();
    bool rejected = false;
    try
    {
        renderer.RestoreSurface(index);
    }
    catch (const std::logic_error&)
    {
        rejected = true;
    }
    entry->native_surface->unlock();
    require(rejected && entry->prepared == 1 && exact_mode(video),
        "locked recovery changed the display or published a prepared clear");
    video.save_next(output);
    renderer.Present();
    require(primary->pixels().data() == back_storage && back->pixels().data() == primary_storage &&
                filled(*primary, 0xf800) && renderer.surface_storage[0]->native_surface == back,
        "first recovered presentation changed role identity or failed to exchange storage");
    recovered_pixels(output);
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "usage: reconstructed_renderer_recovery_tests OUTPUT_DIRECTORY");
        const auto directory = std::filesystem::absolute(argv[1]);
        require(!std::filesystem::exists(directory), "recovery output directory already exists");
        std::filesystem::create_directories(directory);
        renderer_owner_test::GeneratedHaze haze;
        renderer_owner_test::SdlRuntime sdl;
        ImGuiOwner imgui;
        platform::VideoSettings settings;
        settings.title = "Native renderer fullscreen recovery";
        settings.width = width;
        settings.height = height;
        settings.api = platform::GraphicsApi::Vulkan;
        settings.vsync = false;
        auto video = platform::create_video(settings);
        require(video && video->info().api == "vulkan", "real Vulkan Video is absent");
        int count = 0;
        auto** modes =
            SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(video->window()), &count);
        bool available = false;
        for (int n = 0; modes && n < count; ++n)
        {
            available |= modes[n]->w == int(width) && modes[n]->h == int(height) &&
                         modes[n]->pixel_density == 1.0f;
        }
        SDL_free(modes);
        require(available, "exact density-1 800x600 fullscreen mode is unavailable");
        files::Session mount(haze.mount());
        require(!screen_request_renderer, "renderer registration already exists");
        const renderer_owner_test::MaskGlobals prior_masks;
        {
            ScreenRequestRenderer renderer(*video, width, height, 0);
            Masks restore{prior_masks};
            recovery(renderer, *video, directory / "recovered.png");
        }
        require(!screen_request_renderer &&
                    !(SDL_GetWindowFlags(video->window()) & SDL_WINDOW_FULLSCREEN),
            "genuine renderer cleanup did not release registration and fullscreen mode");
        std::printf("recovery: 1 group, 1 renderer, 6 TIFF owners, 2 mode transitions, "
                    "1 GPU present, %u checks\n",
            checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "renderer recovery: %s\n", error.what());
        return 2;
    }
}
