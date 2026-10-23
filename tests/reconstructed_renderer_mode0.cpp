#include "renderer_owner_test_data.h"
#include "runtime/jpeg_export.h"
#include "runtime/renderer_fullscreen_display.h"
#include "startup_jpeg_export.h"
#include "startup_renderer_present_effect.h"
#include "tga_image.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <vector>

// A real renderer owner and real SDL GPU Video. Only generated TIFF/RGB inputs
// and typed observations are supplied here; no Game, manager or COM provider.
namespace
{
namespace fs = std::filesystem;
namespace display = beachlife::runtime::renderer_display;
namespace platform = beachlife::platform;
namespace files = beachlife::runtime::retail_files;
constexpr unsigned width = 800, height = 600;
unsigned checks = 0;

void require(bool value, const char* message)
{
    ++checks;
    if (!value)
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

struct Effects
{
    unsigned char active = render_effect_active, decreasing = render_effect_decreasing;
    unsigned mode = render_effect_mode, countdown = render_effect_countdown;
    unsigned step = render_effect_step;
    unsigned char hold = render_effect_hold;

    bool same() const
    {
        return active == render_effect_active && decreasing == render_effect_decreasing &&
               mode == render_effect_mode && countdown == render_effect_countdown &&
               step == render_effect_step && hold == render_effect_hold;
    }
    void restore() const
    {
        render_effect_active = active;
        render_effect_decreasing = decreasing;
        render_effect_mode = mode;
        render_effect_countdown = countdown;
        render_effect_step = step;
        render_effect_hold = hold;
    }
};

// Declared after the renderer: end borrowed observations before its raw owners
// are released. No cleanup or behavior in the reconstructed destructor changes.
struct RestoreBorrowed
{
    renderer_owner_test::MaskGlobals masks;
    Effects effects;
    ~RestoreBorrowed()
    {
        masks.restore();
        effects.restore();
        jpeg_export_properties.dib_bytes = nullptr;
        jpeg_export_properties.jpg_file = nullptr;
    }
};

class Outputs
{
  public:
    explicit Outputs(const fs::path& requested)
        : previous_(fs::current_path())
        , root_(fs::absolute(requested))
    {
        require(!fs::exists(root_), "mode0 output directory already exists");
        fs::create_directories(root_);
        fs::current_path(root_);
    }
    ~Outputs()
    {
        std::error_code ignored;
        fs::current_path(previous_, ignored);
        // Keep generated captures and failed-case artifacts for review.
    }

  private:
    fs::path previous_, root_;
};

std::vector<unsigned char> bytes(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    require(in.good(), "generated image cannot be read");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void png_pixels(const char* path, std::span<const unsigned short> expected)
{
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> image(
        SDL_LoadPNG(path), SDL_DestroySurface);
    require(image && image->w == int(width) && image->h == int(height),
        "actual GPU download PNG has wrong dimensions or is absent");
    bool same = true;
    for (unsigned y = 0; y < height; ++y)
    {
        for (unsigned x = 0; x < width; ++x)
        {
            Uint8 r, g, b, a;
            if (!SDL_ReadSurfacePixel(image.get(), x, y, &r, &g, &b, &a))
            {
                throw std::runtime_error("cannot read actual GPU download pixel");
            }
            const unsigned v = expected[y * width + x];
            const unsigned red = (v >> 11) & 31, green = (v >> 5) & 63, blue = v & 31;
            same &= r == ((red << 3) | (red >> 2)) && g == ((green << 2) | (green >> 4)) &&
                    b == ((blue << 3) | (blue >> 2)) && a == 255;
        }
    }
    require(same, "actual GPU download pixels differ from literal RGB565 image");
}

void fill_back(ScreenRequestRenderer& renderer, unsigned short value)
{
    renderer.current_surface = 0;
    auto* pixels = renderer.AccessPixels();
    require(pixels && renderer.pixels_locked && renderer.native_back->locked(),
        "actual back lock failed");
    std::fill_n(pixels, width * height, value);
    renderer.UnlockPixels();
    require(!renderer.pixels_locked && !renderer.native_back->locked(), "back unlock failed");
}

void startup_and_surfaces(ScreenRequestRenderer& renderer, std::vector<unsigned short>& expected)
{
    static_assert(sizeof(ScreenSurfacePosition) == 72);
    static_assert(offsetof(ScreenSurfacePosition, native_surface) == 56);
    static_assert(offsetof(ScreenSurfacePosition, native_surface_owned) == 64);
    require(screen_request_renderer == &renderer && renderer.software_mode == 0 &&
                renderer.initialized_168 == 1 && renderer.width == width &&
                renderer.height == height,
        "genuine mode0 renderer was not constructed");
    require(renderer.draw == nullptr && renderer.primary_surface == nullptr &&
                renderer.back_surface == nullptr && renderer.bitmap_info == nullptr,
        "mode0 owner substituted a COM or software surface");
    require(renderer.native_fullscreen && renderer.native_chain && renderer.native_primary &&
                renderer.native_back && renderer.native_display && renderer.device_context_64,
        "genuine native display or context owner missing");
    require(renderer.native_primary == &renderer.native_chain->front_surface() &&
                renderer.native_back == &renderer.native_chain->back_surface() &&
                renderer.surface_storage.size() == 1 &&
                renderer.surface_storage[0]->native_surface == renderer.native_back &&
                !renderer.surface_storage[0]->native_surface_owned &&
                renderer.native_back->description().format == display::WordFormat::Rgb565,
        "stable front/back role or borrowed entry ownership differs");
    require(renderer.format_44 == 0 && renderer.fill_color == 0x07e0 &&
                renderer.current_surface == -1 && renderer.word_60 == 0x38,
        "actual mode0 startup fields differ");
    for (unsigned n = 0; n < 6; ++n)
    {
        const auto* image = renderer.haze_images_138[n];
        require(image && image->width == 2 && image->height == 2 && image->pixels &&
                    image->pixels[0] == n * 4 + 1 && image->pixels[3] == n * 4 + 4,
            "constructor did not load the genuine mounted TIFF input");
    }

    // Both complete allocations are defined before any presentation/observation.
    auto front = renderer.native_primary->lock();
    std::fill(front.begin(), front.end(), 0xffff);
    renderer.native_primary->unlock();
    fill_back(renderer, 0);
    expected.assign(width * height, 0);

    // Exercise the four existing drawing ports whose inline unlock now reaches
    // the real word surface. Literal pixels cover the changed boundary; their
    // unchanged raster algorithms retain the earlier focused qualifications.
    std::fill_n(renderer.pixels_1c, width * height * 2, 0);
    renderer.RenderDefault();
    require(!renderer.pixels_locked && !renderer.native_back->locked(),
        "RenderDefault did not unlock the actual back role");
    renderer.unscaled_clip_5c = 1;
    const ScreenRectangle fill{12, 12, 14, 13};
    renderer.FillRectangle(fill, 0xffff);
    expected[12 * width + 12] = expected[12 * width + 13] = 0xffff;
    require(!renderer.pixels_locked && !renderer.native_back->locked(),
        "FillRectangle did not unlock the actual back role");
    renderer.DrawLine(16, 16, 18, 16, 0xf800);
    expected[16 * width + 16] = expected[16 * width + 17] = expected[16 * width + 18] = 0xf800;
    require(!renderer.pixels_locked && !renderer.native_back->locked(),
        "DrawLine did not unlock the actual back role");
    {
        TgaImage image(2, 2, 8, 1);
        image.palette[3] = image.palette[4] = image.palette[5] = 255;
        image.ConvertPalette(0);
        image.pixels[0] = image.pixels[3] = 1;
        const ScreenPoint destination{20, 20};
        const ScreenRectangle bounds{0, 0, 2, 2};
        renderer.DrawTga(destination, bounds, &image, 11, 0, 0);
        // Actual mode11 ignores the destination and draws at surface origin.
        expected[0] = expected[width + 1] = 0xffff;
        require(!renderer.pixels_locked && !renderer.native_back->locked(),
            "DrawTga mode11 did not unlock the actual back role");
    }
    const int icon = renderer.CreateSurface(2, 2, 0);
    require(icon == 1 && renderer.surface_storage[icon]->native_surface_owned,
        "real offscreen allocation did not own its word surface");
    renderer.PrepareSurface(icon);
    auto* entry = renderer.surface_storage[icon];
    // The keyed copies below observe the actual key through its pixel effects.
    require(entry->prepared, "real preparation did not mark the surface prepared");
    require(std::all_of(entry->native_surface->pixels().begin(),
                entry->native_surface->pixels().end(),
                [](auto p)
                {
                    return p == 0x07e0;
                }),
        "real preparation did not fill the offscreen words");
    renderer.current_surface = icon;
    auto* pixels = renderer.AccessPixels();
    const std::array<unsigned short, 4> input{0xf800, 0x07e0, 0x001f, 0xffff};
    std::copy(input.begin(), input.end(), pixels);
    renderer.UnlockPixels();
    renderer.BlitSurface(4, 4, icon);
    expected[4 * width + 4] = 0xf800;
    expected[5 * width + 4] = 0x001f;
    expected[5 * width + 5] = 0xffff;
    const ScreenRectangle destination{8, 8, 10, 9}, source{0, 0, 2, 1};
    renderer.BlitSurfaceRegion(destination, source, icon);
    expected[8 * width + 8] = 0xf800;
    const ScreenRectangle selected{4, 4, 6, 6};
    const int extracted = renderer.CreateRectangleSurface(selected);
    require(extracted == 2 && renderer.surface_storage[extracted]->native_surface_owned,
        "real rectangle extraction did not create its owner");
    const std::array<unsigned short, 4> copy{0xf800, 0, 0x001f, 0xffff};
    require(std::equal(copy.begin(),
                copy.end(),
                renderer.surface_storage[extracted]->native_surface->pixels().begin()),
        "plain rectangle extraction differs from literal pixels");
    renderer.CleanupSurface(icon);
    require(!entry->active && entry->native_surface == nullptr && !entry->native_surface_owned,
        "indexed cleanup failed to release the owned offscreen surface");
    require(renderer.CreateSurface(2, 2, 0) == icon && entry->native_surface_owned,
        "real CreateSurface did not reuse the inactive entry");
    renderer.PrepareSurface(icon);
    require(std::equal(expected.begin(), expected.end(), renderer.native_back->pixels().begin()),
        "keyed copies changed unexpected back-buffer words");
}

void present_and_capture(
    ScreenRequestRenderer& renderer, platform::Video& video, std::vector<unsigned short>& expected)
{
    render_effect_active = 1;
    render_effect_decreasing = 1;
    render_effect_mode = 3;
    render_effect_countdown = 7;
    render_effect_step = 2;
    render_effect_hold = 1;
    const Effects unchanged;
    auto* front_role = renderer.native_primary;
    auto* back_role = renderer.native_back;
    const auto* before_front = front_role->pixels().data();
    const auto* before_back = back_role->pixels().data();
    renderer.current_surface = -1;
    video.save_next("mode0-first.png");
    renderer.Present();
    // This is the single omit-Present control discriminator, checked before I/O.
    require(
        front_role->pixels().data() == before_back && back_role->pixels().data() == before_front,
        "mode0 Present did not exchange owned buffers");
    require(renderer.native_primary == front_role && renderer.native_back == back_role &&
                renderer.surface_storage[0]->native_surface == back_role &&
                renderer.current_surface == -1 && !renderer.pixels_locked && unchanged.same(),
        "mode0 Present changed a stable role, selection or software effect state");
    require(std::equal(expected.begin(), expected.end(), front_role->pixels().begin()),
        "first owned front image differs");
    png_pixels("mode0-first.png", expected);

    fill_back(renderer, 0x001f);
    expected.assign(width * height, 0x001f);
    constexpr char name[] = "mode0-second.jpg";
    static_assert(sizeof(name) <= sizeof(renderer.unknown_ad));
    std::memcpy(renderer.unknown_ad, name, sizeof(name));
    renderer.capture_pending_ac = 1;
    video.save_next("mode0-second.png");
    renderer.Present();
    require(renderer.capture_pending_ac == 0 && renderer.current_surface == 0 && unchanged.same() &&
                jpeg_export_result == 0,
        "capture-before-Present state differs");
    require(std::equal(expected.begin(), expected.end(), front_role->pixels().begin()),
        "second owned front image differs");
    png_pixels("mode0-second.png", expected);
    std::vector<unsigned char> blue(width * height * 3, 0);
    for (std::size_t i = 2; i < blue.size(); i += 3)
    {
        blue[i] = 255;
    }
    require(beachlife::runtime::jpeg::write_rgb420_file(
                "literal-blue.jpg", blue.data(), width, height, 100) ==
                beachlife::runtime::jpeg::Result::success,
        "literal RGB reference encode failed");
    require(bytes(name) == bytes("literal-blue.jpg"),
        "capture encoded the old front image instead of the pre-flip back image");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "usage: reconstructed_renderer_mode0_tests OUTPUT_DIRECTORY");
        Outputs output(argv[1]);
        renderer_owner_test::GeneratedHaze haze;
        renderer_owner_test::SdlRuntime sdl;
        ImGuiOwner imgui;
        platform::VideoSettings settings;
        settings.title = "Reconstructed renderer mode0 qualification";
        settings.width = width;
        settings.height = height;
        settings.api = platform::GraphicsApi::Vulkan;
        settings.vsync = false;
        settings.hidden = false;
        auto video = platform::create_video(settings);
        require(video && video->info().api == "vulkan", "real Vulkan Video was not selected");
        int mode_count = 0;
        auto** modes =
            SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(video->window()), &mode_count);
        bool exact_mode = false;
        for (int n = 0; modes && n < mode_count; ++n)
        {
            exact_mode |= modes[n]->w == int(width) && modes[n]->h == int(height) &&
                          modes[n]->pixel_density == 1.0f;
        }
        SDL_free(modes);
        // Environment gate before the constructor publishes its raw owners.
        // Unavailable-mode failure/rollback is not claimed by this capability.
        require(exact_mode, "actual density-1 800x600 fullscreen mode is unavailable");
        files::Session mount(haze.mount());
        require(screen_request_renderer == nullptr, "renderer registration was already bound");
        const renderer_owner_test::MaskGlobals prior_masks;
        const Effects prior_effects;
        {
            ScreenRequestRenderer renderer(*video, width, height, 0);
            RestoreBorrowed restore{prior_masks, prior_effects};
            int pixel_width = 0, pixel_height = 0;
            const auto* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(video->window()));
            require(renderer.window_34 == video.get() &&
                        (SDL_GetWindowFlags(video->window()) & SDL_WINDOW_FULLSCREEN) &&
                        SDL_GetWindowFullscreenMode(video->window()) && mode &&
                        mode->w == int(width) && mode->h == int(height) &&
                        mode->pixel_density == 1.0f &&
                        SDL_GetWindowSizeInPixels(video->window(), &pixel_width, &pixel_height) &&
                        pixel_width == int(width) && pixel_height == int(height),
                "actual Video/window/fullscreen mode differs from the exact requested pixels");
            std::vector<unsigned short> expected;
            startup_and_surfaces(renderer, expected);
            present_and_capture(renderer, *video, expected);
        }
        require(screen_request_renderer == nullptr, "genuine registration cleanup did not clear");
        require((SDL_GetWindowFlags(video->window()) & SDL_WINDOW_FULLSCREEN) == 0,
            "real destructor did not restore the windowed state");
        std::printf(
            "mode0: 3 groups, 1 renderer, 6 TIFF owners, 2 GPU presents, %u checks\n", checks);
        std::printf(
            "renderer bytes=%zu entry bytes=%zu pointer=%zu owned=%zu; Game/Intro/menu untested\n",
            sizeof(ScreenRequestRenderer),
            sizeof(ScreenSurfacePosition),
            offsetof(ScreenSurfacePosition, native_surface),
            offsetof(ScreenSurfacePosition, native_surface_owned));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "mode0 qualification: %s\n", error.what());
        return 2;
    }
}
