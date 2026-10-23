#pragma once

#include "data/folder_source.h"
#include "render_mask_setup.h"
#include "runtime/renderer_software_display.h"
#include "runtime/retail_files.h"
#include "startup_screen_renderer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Fixture-only ownership support. Every renderer is the genuine constructor /
// destructor and loads six real generated TIFF files. No game body is supplied.
namespace renderer_owner_test
{
namespace fs = std::filesystem;
namespace files = beachlife::runtime::retail_files;
namespace platform = beachlife::platform;

class GeneratedHaze
{
  public:
    GeneratedHaze()
    {
        static std::atomic<unsigned long long> serial{0};
        for (unsigned attempt = 0; attempt < 100; ++attempt)
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            root_ = fs::temp_directory_path() / ("beachlife-renderer-" + std::to_string(stamp) +
                                                    "-" + std::to_string(serial.fetch_add(1)));
            if (fs::create_directory(root_))
            {
                created_ = true;
                break;
            }
        }
        if (!created_)
        {
            throw std::runtime_error("cannot create generated renderer directory");
        }
        try
        {
            fs::create_directories(root_ / "Data/Haze");
            for (unsigned image = 0; image < 6; ++image)
            {
                std::array<unsigned char, 516> bytes{};
                const auto word = [&](std::size_t at, unsigned value)
                {
                    bytes.at(at) = static_cast<unsigned char>(value);
                    bytes.at(at + 1) = static_cast<unsigned char>(value >> 8);
                };
                const auto dword = [&](std::size_t at, unsigned value)
                {
                    for (unsigned n = 0; n < 4; ++n)
                    {
                        bytes.at(at + n) = static_cast<unsigned char>(value >> (8 * n));
                    }
                };
                word(0, 0x4949);
                word(2, 42);
                dword(4, 8);
                constexpr unsigned tags[][4] = {{256, 4, 1, 2},
                    {257, 4, 1, 2},
                    {258, 3, 1, 8},
                    {259, 3, 1, 1},
                    {277, 3, 1, 1},
                    {273, 4, 1, 512},
                    {278, 4, 1, 2},
                    {279, 4, 1, 4}};
                word(8, std::size(tags));
                std::size_t at = 10;
                for (const auto& tag : tags)
                {
                    word(at, tag[0]);
                    word(at + 2, tag[1]);
                    dword(at + 4, tag[2]);
                    dword(at + 8, tag[3]);
                    at += 12;
                }
                dword(at, 0);
                for (unsigned pixel = 0; pixel < 4; ++pixel)
                {
                    bytes[512 + pixel] = static_cast<unsigned char>(image * 4 + pixel + 1);
                }
                const auto name = "Data/Haze/wibble_0" + std::to_string(image) + ".tif";
                std::ofstream stream(root_ / name, std::ios::binary);
                stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                stream.close();
                if (!stream)
                {
                    throw std::runtime_error("cannot write generated renderer TIFF");
                }
                entries_.push_back({name, bytes.size()});
            }
        }
        catch (...)
        {
            cleanup();
            throw;
        }
    }
    ~GeneratedHaze()
    {
        cleanup();
    }
    std::shared_ptr<const beachlife::data::FileSource> mount() const
    {
        return std::make_shared<beachlife::data::FolderSource>(root_, entries_);
    }

  private:
    void cleanup() noexcept
    {
        if (created_)
        {
            std::error_code ignored;
            fs::remove_all(root_, ignored);
            created_ = false;
        }
    }
    fs::path root_;
    bool created_ = false;
    std::vector<beachlife::data::SourceEntry> entries_;
};

class SdlRuntime
{
  public:
    SdlRuntime()
    {
        if (SDL_WasInit(SDL_INIT_VIDEO) != 0)
        {
            throw std::runtime_error(
                "renderer method fixture needs an unbound SDL video subsystem");
        }
        if (!SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "x11", SDL_HINT_OVERRIDE) ||
            !SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            const std::string error = SDL_GetError();
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            throw std::runtime_error("renderer fixture x11 initialization: " + error);
        }
        if (!SDL_GetCurrentVideoDriver() || std::string_view(SDL_GetCurrentVideoDriver()) != "x11")
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            throw std::runtime_error("renderer fixture did not select actual x11");
        }
    }
    ~SdlRuntime()
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};

// Only the existing native platform Video boundary is implemented. No retail
// Present, renderer/controller method or GPU driver is replaced by this recorder.
class RecorderVideo final : public platform::Video
{
  public:
    explicit RecorderVideo(SDL_Window* window)
        : window_(window)
    {
    }
    SDL_Window* window() const override
    {
        return window_;
    }
    const platform::VideoInfo& info() const override
    {
        return info_;
    }
    void new_overlay_frame() override
    {
        throw std::logic_error("no fixture overlay");
    }
    void save_next(const fs::path&) override
    {
        throw std::logic_error("no fixture capture");
    }
    void present(const platform::Frame& frame, ImDrawData* overlay) override
    {
        if (overlay)
        {
            throw std::logic_error("unexpected fixture overlay");
        }
        last_frame = frame;
    }
    platform::Frame last_frame;

  private:
    SDL_Window* window_;
    platform::VideoInfo info_{"recorder", "none"};
};

struct Environment
{
    GeneratedHaze haze;
    SdlRuntime sdl;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{
        SDL_CreateWindow("renderer method owner", 320, 240, SDL_WINDOW_HIDDEN), SDL_DestroyWindow};
    RecorderVideo video{window.get()};

    Environment()
    {
        if (!window)
        {
            throw std::runtime_error(std::string("renderer fixture window: ") + SDL_GetError());
        }
        // Prove this actual backend supplies the needed geometry before a game
        // owner publishes any raw allocation. Never invent dummy/Wayland values.
        beachlife::runtime::renderer_display::SoftwareDisplay probe(video);
        (void)probe.outer_rectangle();
        (void)probe.client_rectangle_in_screen();
        (void)probe.work_area();
    }
};

inline Environment& environment()
{
    // Constructed lazily on the fixture's main thread, destroyed after all local
    // renderer owners. It retains no VFS Session between game constructions.
    static Environment value;
    return value;
}

struct MaskGlobals
{
    unsigned format = render_mask_format;
    unsigned short quarter = render_quarter_mask;
    unsigned short half = render_half_mask;
    unsigned short* pixels = render_mask_pixels;
    unsigned short full = render_full_mask;
    unsigned short green = render_green_mask;
    unsigned short sum_bias = render_sum_bias;
    unsigned short quarter_bias = render_quarter_bias;
    unsigned short half_bias = render_half_bias;
    unsigned char green_levels = render_green_levels;
    unsigned short green_bits = render_green_bits;
    unsigned short red_shift = render_red_shift;
    unsigned short red = render_red_mask;
    unsigned width = render_mask_width;
    unsigned short blue = render_blue_mask;

    void restore() const noexcept
    {
        render_mask_format = format;
        render_quarter_mask = quarter;
        render_half_mask = half;
        render_mask_pixels = pixels;
        render_full_mask = full;
        render_green_mask = green;
        render_sum_bias = sum_bias;
        render_quarter_bias = quarter_bias;
        render_half_bias = half_bias;
        render_green_levels = green_levels;
        render_green_bits = green_bits;
        render_red_shift = red_shift;
        render_red_mask = red;
        render_mask_width = width;
        render_blue_mask = blue;
    }
};

// Declare before the genuine renderer. Its nested real VFS is retired just after
// construction, so current language/font/resource fixture mounts remain exact.
class Bootstrap
{
  public:
    Bootstrap()
        : previous_(screen_request_renderer)
        , session_(std::make_unique<files::Session>(environment().haze.mount()))
    {
    }
    ~Bootstrap()
    {
        session_.reset();
        // Fixture binding restoration occurs after the actual registration
        // destructor's unconditional clear; it is not a replacement game body.
        screen_request_renderer = previous_;
        masks_.restore();
    }
    Bootstrap(const Bootstrap&) = delete;
    Bootstrap& operator=(const Bootstrap&) = delete;
    platform::Video& video()
    {
        return environment().video;
    }
    void finish(const ScreenRequestRenderer& value)
    {
        if (screen_request_renderer != &value || value.software_mode != 1 ||
            value.initialized_168 != 1 || value.word_60 != 0x38 ||
            value.surface_storage.size() != 1 || !value.native_display || !value.device_context_64)
        {
            throw std::runtime_error("genuine renderer bootstrap differs");
        }
        session_.reset();
        screen_request_renderer = previous_;
        masks_.restore();
    }

  private:
    ScreenRequestRenderer* previous_;
    MaskGlobals masks_;
    std::unique_ptr<files::Session> session_;
};

// Declare after the renderer and its trailing guard. Only already-constructed
// owners are captured. The actual owned table/queue are swapped out without
// destroying entries; method cases may then install explicitly borrowed views.
// Restore is allocation-free and runs before the genuine renderer destructor.
class RestoreOwnedState
{
  public:
    RestoreOwnedState(ScreenRequestRenderer& value, Bootstrap& bootstrap)
        : value_(value)
        , pixels_(value.pixels_1c)
        , primary_(value.primary_surface)
        , back_(value.back_surface)
        , context_(value.device_context_64)
        , bitmap_(value.bitmap_info)
        , markers_(value.buffer_130)
        , sea_(value.sea_state_134)
        , display_(value.native_display)
#if defined(BEACHLIFE_DECOMP_RENDERER_WORD_SURFACE)
        , native_fullscreen_(value.native_fullscreen)
        , native_chain_(value.native_chain)
        , native_primary_(value.native_primary)
        , native_back_(value.native_back)
#endif
        , window_(value.window_34)
        , draw_(value.draw)
        , width_(value.width)
        , height_(value.height)
    {
        bootstrap.finish(value);
        std::copy(
            std::begin(value.haze_images_138), std::end(value.haze_images_138), images_.begin());
        surfaces_.swap(value.surface_storage);
        queue_.values.swap(value.dirty_rectangles.values);
    }
    ~RestoreOwnedState()
    {
        value_.pixels_1c = pixels_;
        value_.primary_surface = primary_;
        value_.back_surface = back_;
        value_.device_context_64 = context_;
        value_.bitmap_info = bitmap_;
        value_.buffer_130 = markers_;
        value_.sea_state_134 = sea_;
        value_.native_display = display_;
#if defined(BEACHLIFE_DECOMP_RENDERER_WORD_SURFACE)
        value_.native_fullscreen = native_fullscreen_;
        value_.native_chain = native_chain_;
        value_.native_primary = native_primary_;
        value_.native_back = native_back_;
#endif
        value_.window_34 = window_;
        value_.draw = draw_;
        value_.width = width_;
        value_.height = height_;
        value_.software_mode = 1;
        value_.current_surface = -1;
        value_.pixels_locked = 0;
        value_.capture_pending_ac = 0;
        value_.dirty_rectangle_count = 0;
        std::copy(images_.begin(), images_.end(), value_.haze_images_138);
        surfaces_.swap(value_.surface_storage);
        queue_.values.swap(value_.dirty_rectangles.values);
        // Swapped-out method pointer slots are borrowed: vector destruction does
        // not delete their payloads. The fixture must retire any new owning
        // method allocations itself before this guard, exactly as before.
    }
    RestoreOwnedState(const RestoreOwnedState&) = delete;
    RestoreOwnedState& operator=(const RestoreOwnedState&) = delete;

  private:
    ScreenRequestRenderer& value_;
    unsigned short* pixels_;
    DirectDrawSurface7Prefix* primary_;
    DirectDrawSurface7Prefix* back_;
    beachlife::runtime::renderer_display::DrawingContext* context_;
    beachlife::runtime::renderer_display::BitmapInfo* bitmap_;
    unsigned char* markers_;
    RendererSeaStatePrefix* sea_;
    beachlife::runtime::renderer_display::SoftwareDisplay* display_;
#if defined(BEACHLIFE_DECOMP_RENDERER_WORD_SURFACE)
    decltype(ScreenRequestRenderer::native_fullscreen) native_fullscreen_;
    decltype(ScreenRequestRenderer::native_chain) native_chain_;
    decltype(ScreenRequestRenderer::native_primary) native_primary_;
    decltype(ScreenRequestRenderer::native_back) native_back_;
#endif
    platform::Video* window_;
    DirectDraw7Prefix* draw_;
    unsigned width_, height_;
    std::array<TgaImage*, 6> images_{};
    std::vector<ScreenSurfacePosition*> surfaces_;
    RendererRectangleQueue queue_;
};
} // namespace renderer_owner_test
