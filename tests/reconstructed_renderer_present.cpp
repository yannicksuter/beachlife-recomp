#include "renderer_owner_test_data.h"
#include "runtime/jpeg_export.h"
#include "runtime/renderer_bitmap.h"
#include "startup_jpeg_export.h"
#include "startup_renderer_present_effect.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace
{
namespace fs = std::filesystem;
unsigned checks = 0;
unsigned presents = 0;

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

struct Effects
{
    unsigned char active, decreasing;
    unsigned mode, countdown, step;
    unsigned char hold;
    bool operator==(const Effects&) const = default;
};

Effects effects()
{
    return {render_effect_active,
        render_effect_decreasing,
        render_effect_mode,
        render_effect_countdown,
        render_effect_step,
        render_effect_hold};
}

void effects(Effects value)
{
    render_effect_active = value.active;
    render_effect_decreasing = value.decreasing;
    render_effect_mode = value.mode;
    render_effect_countdown = value.countdown;
    render_effect_step = value.step;
    render_effect_hold = value.hold;
}

struct RestoreEffects
{
    Effects value = effects();
    ~RestoreEffects()
    {
        effects(value);
    }
};

struct RestoreMasks
{
    renderer_owner_test::MaskGlobals value;
    ~RestoreMasks()
    {
        value.restore();
    }
};

struct ClearBorrowedExportPointers
{
    ~ClearBorrowedExportPointers()
    {
        // The original wrapper retains these borrowed addresses after image
        // cleanup. Clear observations before the fixture owner lifetime ends.
        jpeg_export_properties.dib_bytes = nullptr;
        jpeg_export_properties.jpg_file = nullptr;
    }
};

class OutputDirectory
{
  public:
    explicit OutputDirectory(const fs::path& requested)
        : previous_(fs::current_path())
        , root_(fs::absolute(requested))
    {
        require(!fs::exists(root_), "Present output directory already exists");
        fs::create_directories(root_);
        fs::current_path(root_);
    }
    ~OutputDirectory()
    {
        std::error_code ignored;
        fs::current_path(previous_, ignored);
        fs::remove_all(root_, ignored);
    }

  private:
    fs::path previous_, root_;
};

void frame_is(const beachlife::platform::Frame& frame, unsigned rgba)
{
    require(frame.width == 8 && frame.height == 4 && frame.pixels.size() == 32,
        "Present frame extent differs");
    for (auto pixel : frame.pixels)
    {
        require(pixel == rgba, "Present frame pixels differ");
    }
}

void present(ScreenRequestRenderer& owner)
{
    owner.Present();
    ++presents;
}

void effect_cases(ScreenRequestRenderer& owner, renderer_owner_test::RecorderVideo& video)
{
    struct Case
    {
        Effects before, after;
        unsigned short pixel;
        unsigned rgba;
    };
    // Literal scalar/color results, including the original signed old-step
    // comparison and the countdown performed before the decreasing-zero case.
    constexpr Case cases[] = {
        {{0, 1, 3, 8, 7, 1}, {0, 1, 3, 8, 7, 1}, 0x0000, 0xff000000},
        {{1, 0, 0, 3, 1, 0}, {1, 0, 0, 2, 2, 0}, 0x0000, 0xff000000},
        {{1, 0, 1, 8, 2, 0}, {1, 0, 2, 8, 0, 0}, 0x1ce7, 0xff393939},
        {{1, 0, 4, 8, 7, 0}, {1, 1, 4, 20, 7, 1}, 0xffff, 0xffffffff},
        {{1, 1, 2, 2, 6, 1}, {1, 1, 2, 1, 6, 1}, 0xffff, 0xffffffff},
        {{1, 1, 2, 1, 6, 1}, {1, 1, 2, 0, 6, 0}, 0xffff, 0xffffffff},
        {{1, 1, 2, 8, 2, 0}, {1, 1, 1, 8, 0, 0}, 0x3def, 0xff7b7b7b},
        {{1, 1, 0, 3, 2, 0}, {0, 0, 0, 2, 2, 0}, 0x0000, 0xff000000},
        {{1, 1, 3, 8, 1, 0}, {1, 1, 3, 8, 2, 0}, 0x5ad6, 0xffb5b5b5},
        {{1, 0, 1, 8, UINT_MAX, 0}, {1, 0, 1, 8, 0, 0}, 0x1ce7, 0xff393939},
    };
    auto& surface = *owner.surface_storage.at(0);
    for (const auto& value : cases)
    {
        effects(value.before);
        std::fill_n(surface.pixels, 32, 0);
        surface.prepared = 1;
        owner.current_surface = 37; // The real body must select zero before access.
        owner.pixels_locked = 99;
        present(owner);
        require(effects() == value.after, "Present effect state differs");
        require(owner.current_surface == 0 && surface.prepared == 0,
            "Present did not select/access surface zero");
        require(owner.pixels_locked == 0, "Present lock cleanup differs");
        for (unsigned n = 0; n < 32; ++n)
        {
            require(surface.pixels[n] == value.pixel, "Present effect word differs");
        }
        frame_is(video.last_frame, value.rgba);
    }
}

std::vector<char> read_file(const char* path)
{
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "Present capture file missing");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void capture_order(ScreenRequestRenderer& owner, renderer_owner_test::RecorderVideo& video)
{
    constexpr char filename[] = "before-effect.jpg";
    static_assert(sizeof(filename) <= sizeof(owner.unknown_ad));
    // This short terminated fixture is wholly inside retained opaque storage;
    // it establishes no general capture filename capacity or producer contract.
    std::copy(std::begin(filename), std::end(filename), owner.unknown_ad);
    std::fill_n(owner.surface_storage.at(0)->pixels, 32, 0x7c00);
    owner.capture_pending_ac = 1;
    effects({1, 0, 4, 2, 0, 0});
    present(owner);
    require(owner.capture_pending_ac == 0 && jpeg_export_result == 0,
        "Present did not complete real capture");
    require(effects() == Effects{1, 1, 4, 20, 0, 1}, "Present capture/effect state differs");
    frame_is(video.last_frame, 0xffffffff);

    // Independently supplied constant RGB represents the frame BEFORE the
    // white effect. Reuse the real qualified codec instead of duplicating it.
    std::array<unsigned char, 96> red{};
    for (std::size_t n = 0; n < red.size(); n += 3)
    {
        red[n] = 255;
    }
    require(beachlife::runtime::jpeg::write_rgb420_file("reference.jpg", red.data(), 8, 4, 100) ==
                beachlife::runtime::jpeg::Result::success,
        "Present capture reference encode failed");
    const auto actual = read_file(filename);
    require(!actual.empty() && actual == read_file("reference.jpg"),
        "Present captured pixels after applying the effect");
}

void boundary_cleanup(ScreenRequestRenderer& owner, renderer_owner_test::RecorderVideo& video)
{
    effects({0, 0, 0, 0, 0, 0});
    auto& surface = *owner.surface_storage.at(0);
    const auto previous = video.last_frame;
    const auto planes = owner.bitmap_info->bmiHeader.biPlanes;
    owner.bitmap_info->bmiHeader.biPlanes = 2;
    present(owner); // Real host validation failure follows the original cleanup tail.
    owner.bitmap_info->bmiHeader.biPlanes = planes;
    require(owner.pixels_locked == 0, "Present failed-boundary cleanup differs");
    require(video.last_frame.pixels == previous.pixels, "Invalid DIB was presented");

    struct RestorePixels
    {
        ScreenSurfacePosition& surface;
        unsigned short* saved;
        ~RestorePixels()
        {
            surface.pixels = saved;
        }
    } restore{surface, surface.pixels};
    surface.pixels = nullptr;
    owner.pixels_locked = 0;
    present(owner);
    // AccessPixels sets the lock byte even for null software pixels; the
    // original early return deliberately skips the ordinary tail cleanup.
    require(owner.pixels_locked == 1, "Present null-pixel early return differs");
    require(video.last_frame.pixels == previous.pixels, "Null pixels were presented");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Present output directory argument required");
        OutputDirectory output(argv[1]);
        auto& environment = renderer_owner_test::environment();
        RestoreEffects restore_effects;
        RestoreMasks restore_masks;
        require(screen_request_renderer == nullptr, "Renderer already registered");
        {
            beachlife::runtime::retail_files::Session mount(environment.haze.mount());
            ScreenRequestRenderer owner(environment.video, 8, 4, 1);
            ClearBorrowedExportPointers clear_borrowed;
            require(screen_request_renderer == &owner, "Genuine Present owner not registered");
            effect_cases(owner, environment.video);
            capture_order(owner, environment.video);
            boundary_cleanup(owner, environment.video);
        }
        require(screen_request_renderer == nullptr, "Genuine Present owner cleanup differs");
        std::printf(
            "{\"groups\":3,\"owners\":1,\"present_calls\":%u,\"checks\":%u}\n", presents, checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "renderer-present: %s\n", error.what());
        return 2;
    }
}
