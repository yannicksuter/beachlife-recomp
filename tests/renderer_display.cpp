#include "runtime/renderer_software_display.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace display = beachlife::runtime::renderer_display;
namespace platform = beachlife::platform;

namespace
{
std::size_t checks = 0;
std::size_t rejections = 0;
std::size_t observed_modes = 0;

struct Failure
{
    const char* message;
};

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw Failure{message};
    }
}

template <class Exception, class Call> void rejects(Call call, const char* prefix)
{
    try
    {
        call();
    }
    catch (const Exception& error)
    {
        require(std::string_view(error.what()).starts_with(prefix), "wrong rejection diagnostic");
        ++rejections;
        return;
    }
    throw Failure{"operation unexpectedly succeeded"};
}

class SdlVideo
{
  public:
    SdlVideo()
    {
        require(SDL_WasInit(0) == 0, "SDL was already initialized");
        if (!SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE))
        {
            SDL_Quit();
            throw Failure{"cannot select SDL dummy video"};
        }
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            const std::string error = SDL_GetError();
            SDL_Quit();
            throw std::runtime_error("SDL video initialization: " + error);
        }
    }

    ~SdlVideo()
    {
        SDL_Quit();
    }

    SdlVideo(const SdlVideo&) = delete;
    SdlVideo& operator=(const SdlVideo&) = delete;
};

using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;

// This records only the existing platform interface. It supplies no game method,
// renderer owner, GPU, fake SDL window or DirectDraw object.
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
        throw std::logic_error("fixture has no overlay");
    }
    void save_next(const std::filesystem::path&) override
    {
        throw std::logic_error("fixture does not save images");
    }
    void present(const platform::Frame& frame, ImDrawData* overlay) override
    {
        require(overlay == nullptr, "unexpected overlay");
        ++attempts;
        if (reject_next)
        {
            reject_next = false;
            throw std::runtime_error("recorder rejected present");
        }
        last_presented = frame;
        ++presents;
    }
    void bind_window(SDL_Window* window)
    {
        window_ = window;
    }

    std::size_t presents = 0;
    std::size_t attempts = 0;
    bool reject_next = false;
    platform::Frame last_presented;

  private:
    SDL_Window* window_;
    platform::VideoInfo info_{"recorder", "none"};
};

constexpr std::array<std::uint32_t, 4> expected{0xff0000ffu, 0xff00ff00u, 0xffff0000u, 0xffffffffu};

void literal_frame(const platform::Frame& frame)
{
    require(frame.width == 2 && frame.height == 2, "wrong frame dimensions");
    require(frame.pixels.size() == expected.size() &&
                std::equal(frame.pixels.begin(), frame.pixels.end(), expected.begin()),
        "RGB555 literal pixels differ");
}

void platform_geometry(display::SoftwareDisplay& service, SDL_Window* window)
{
    service.use_windowed_mode();
    service.clear_topmost();
    service.resize_client(416, 288);
    require(SDL_SetWindowPosition(window, 23, 37), "cannot set real dummy position");
    require(SDL_SyncWindow(window), "cannot synchronize dummy position");
    const auto client = service.client_rectangle_in_screen();
    require(client.left == 23 && client.top == 37 && client.right == 439 && client.bottom == 325,
        "real client geometry differs");
    require((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0, "window is fullscreen");

    const SDL_DisplayID id = SDL_GetDisplayForWindow(window);
    require(id != 0, "real display is unavailable");
    SDL_Rect area{};
    require(SDL_GetDisplayUsableBounds(id, &area), "real work-area query failed");
    const auto actual = service.work_area();
    require(actual.left == area.x && actual.top == area.y && actual.right == area.x + area.w &&
                actual.bottom == area.y + area.h,
        "service invented work-area geometry");

    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(id, &count);
    const std::unique_ptr<void, decltype(&SDL_free)> mode_storage(modes, SDL_free);
    require(modes != nullptr && count >= 0, "real mode enumeration failed");
    const auto reported = service.enumerate_modes();
    require(reported.size() >= static_cast<std::size_t>(count), "physical display modes were lost");
    observed_modes = reported.size();
    for (int index = 0; index < count; ++index)
    {
        const auto& mode = reported[static_cast<std::size_t>(index)];
        require(mode.width == modes[index]->w && mode.height == modes[index]->h &&
                    mode.pixel_format == modes[index]->format &&
                    mode.refresh_rate == modes[index]->refresh_rate,
            "service changed an actual display mode");
    }
    for (const auto size :
        std::array<std::array<int, 2>, 4>{{{640, 480}, {800, 600}, {1024, 768}, {1280, 960}}})
    {
        require(std::any_of(reported.begin(),
                    reported.end(),
                    [&](const display::DisplayMode& mode)
                    {
                        return mode.width == size[0] && mode.height == size[1];
                    }),
            "a supported logical framebuffer is missing from the resolution selector");
        const std::vector<std::byte> pixels(static_cast<std::size_t>(size[0]) * size[1] * 2);
        const auto frame = display::convert_rgb555(pixels, size[0], size[1], size[0] * 2);
        require(frame.width == size[0] && frame.height == size[1] &&
                    frame.pixels.size() == static_cast<std::size_t>(size[0]) * size[1],
            "advertised logical framebuffer cannot be converted for presentation");
    }

    // This pinned SDL dummy backend has no border callback. The service must
    // propagate that real failure, not use SDL's zeroed outputs as a fallback.
    int top, left, bottom, right;
    require(!SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right),
        "dummy border capability changed");
    rejects<std::runtime_error>(
        [&]
        {
            service.outer_rectangle();
        },
        "Cannot query renderer window borders:");
    rejects<std::runtime_error>(
        [&]
        {
            service.move_outer(5, 7);
        },
        "Cannot query renderer window borders:");
    rejects<std::runtime_error>(
        [&]
        {
            service.resize_client(0, 100);
        },
        "Cannot resize renderer client area:");
    const auto after = service.client_rectangle_in_screen();
    require(after.left == client.left && after.top == client.top && after.right == client.right &&
                after.bottom == client.bottom,
        "failed geometry operation changed the window");
}

struct PresentCounts
{
    std::size_t successful, attempted;
};

PresentCounts run()
{
    SdlVideo sdl;
    require(SDL_GetCurrentVideoDriver() && std::string_view(SDL_GetCurrentVideoDriver()) == "dummy",
        "fixture did not select the real SDL dummy backend");
    Window window(SDL_CreateWindow("renderer display fixture", 320, 240, SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, "cannot create actual hidden SDL window");
    RecorderVideo video(window.get());

    // Prefix/suffix guards, row padding and high bits are intentional. The
    // actual input begins at an odd address and has little-endian RGB555 words.
    alignas(2) std::array<unsigned char, 14> storage{
        0xa5, 0x00, 0xfc, 0xe0, 0x03, 0x66, 0x77, 0x1f, 0x80, 0xff, 0xff, 0x88, 0x99, 0x5a};
    const auto before = storage;
    const auto pixels = std::as_bytes(std::span(storage)).subspan(1, 12);
    require((reinterpret_cast<std::uintptr_t>(pixels.data()) & 1) == 1, "input is not unaligned");
    literal_frame(display::convert_rgb555(pixels, 2, 2, 6));
    const std::array<std::byte, 2> middle{std::byte{0x10}, std::byte{0x42}};
    const auto gray = display::convert_rgb555(middle, 1, 1, 2);
    require(gray.pixels == std::vector<std::uint32_t>{0xff848484u}, "five-bit expansion differs");

    std::unique_ptr<display::DrawingContext> context;
    {
        display::SoftwareDisplay service(video);
        platform_geometry(service, window.get());
        context = service.acquire_context();
        context->present_rgb555(pixels, 2, 2, 6);
    }
    // The device wrapper is gone; its independent context still borrows a live
    // Video/window and must remain usable without any retail owner/provider.
    context->present_rgb555(pixels, 2, 2, 6);
    require(video.presents == 2, "independent drawing context did not present twice");
    literal_frame(context->last_frame());
    literal_frame(video.last_presented);

    // Conversion completes before Video::present. A platform presentation error
    // therefore retains the new converted frame, not the last accepted image.
    video.reject_next = true;
    rejects<std::runtime_error>(
        [&]
        {
            context->present_rgb555(middle, 1, 1, 2);
        },
        "recorder rejected present");
    const auto preserved = [&]
    {
        require(video.presents == 2 && video.attempts == 3,
            "pre-presentation rejection reached Video::present");
        const auto& converted = context->last_frame();
        require(converted.width == 1 && converted.height == 1 && converted.pixels == gray.pixels,
            "last converted frame was not retained");
        literal_frame(video.last_presented);
    };
    preserved();
    const auto bad_pixels =
        [&](std::span<const std::byte> input, int width, int height, std::size_t stride)
    {
        rejects<std::invalid_argument>(
            [&]
            {
                context->present_rgb555(input, width, height, stride);
            },
            "Renderer");
        preserved();
    };
    bad_pixels(pixels.first(9), 2, 2, 6);
    bad_pixels(pixels, 0, 2, 6);
    bad_pixels(pixels, 2, -1, 6);
    bad_pixels(pixels, 2, 2, 3);
    bad_pixels(pixels, 1, 3, std::numeric_limits<std::size_t>::max());

    std::exception_ptr worker_failure;
    std::thread worker(
        [&]
        {
            try
            {
                rejects<std::logic_error>(
                    [&]
                    {
                        context->present_rgb555(pixels, 2, 2, 6);
                    },
                    "Renderer display operation is on the wrong thread");
            }
            catch (...)
            {
                worker_failure = std::current_exception();
            }
        });
    worker.join();
    if (worker_failure)
    {
        std::rethrow_exception(worker_failure);
    }
    preserved();

    Window replacement(
        SDL_CreateWindow("other window", 20, 20, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    require(replacement != nullptr, "cannot create second actual window");
    video.bind_window(replacement.get());
    rejects<std::logic_error>(
        [&]
        {
            context->present_rgb555(pixels, 2, 2, 6);
        },
        "Renderer display window identity changed");
    preserved();
    video.bind_window(window.get());
    display::SoftwareDisplay expired(video);
    window.reset();
    video.bind_window(nullptr);
    rejects<std::runtime_error>(
        [&]
        {
            context->present_rgb555(pixels, 2, 2, 6);
        },
        "Renderer window is no longer available:");
    rejects<std::runtime_error>(
        [&]
        {
            expired.work_area();
        },
        "Renderer window is no longer available:");
    preserved();
    require(storage == before, "pixel source or guards changed");
    return {video.presents, video.attempts};
}
} // namespace

int main()
{
    try
    {
        const auto presents = run();
        require(SDL_WasInit(0) == 0, "SDL survived fixture teardown");
        std::printf("renderer-display: groups=3 checks=%zu presents=%zu attempts=%zu rejected=%zu "
                    "modes=%zu\n",
            checks,
            presents.successful,
            presents.attempted,
            rejections,
            observed_modes);
        return 0;
    }
    catch (const Failure& error)
    {
        std::fprintf(stderr, "renderer-display: %s\n", error.message);
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "renderer-display host failure: %s\n", error.what());
        return 3;
    }
}
