#include "data/folder_source.h"
#include "render_mask_setup.h"
#include "runtime/renderer_bitmap.h"
#include "runtime/renderer_software_display.h"
#include "runtime/retail_files.h"
#include "startup_renderer_sea_state.h"
#include "startup_screen_renderer.h"
#include "tga_image.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace platform = beachlife::platform;
namespace display = beachlife::runtime::renderer_display;
namespace fs = std::filesystem;

namespace
{
static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
std::size_t checks = 0;

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

class SdlVideo
{
  public:
    SdlVideo()
    {
        require(SDL_WasInit(0) == 0, "SDL already initialized");
        require(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "x11", SDL_HINT_OVERRIDE),
            "cannot request the real x11 backend");
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            const std::string error = SDL_GetError();
            SDL_Quit();
            throw std::runtime_error("x11 initialization: " + error);
        }
    }
    ~SdlVideo()
    {
        SDL_Quit();
    }
};
using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;

// Only the established platform boundary is observed. SDL supplies the real
// window/modes/geometry; this object supplies no retail method or GPU claim.
class RecorderVideo final : public platform::Video
{
  public:
    explicit RecorderVideo(SDL_Window* value)
        : window_(value)
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
        throw std::logic_error("no overlay");
    }
    void save_next(const fs::path&) override
    {
        throw std::logic_error("no image capture");
    }
    void present(const platform::Frame& frame, ImDrawData* overlay) override
    {
        require(overlay == nullptr, "unexpected overlay");
        last = frame;
        ++presents;
    }
    platform::Frame last;
    std::size_t presents = 0;

  private:
    SDL_Window* window_;
    platform::VideoInfo info_{"recorder", "none"};
};

class GeneratedHaze
{
  public:
    explicit GeneratedHaze(const fs::path& path)
        : root_(path)
    {
        require(!fs::exists(root_), "generated haze directory already exists");
        fs::create_directories(root_ / "Data/Haze");
        for (unsigned index = 0; index < 6; ++index)
        {
            // Complete generated little-endian 2x2 eight-bit raw-strip TIFF.
            // These are six real files for the original six loader calls.
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
                    bytes.at(at + n) = static_cast<unsigned char>(value >> (n * 8));
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
            word(8, static_cast<unsigned>(std::size(tags)));
            std::size_t offset = 10;
            for (const auto& tag : tags)
            {
                word(offset, tag[0]);
                word(offset + 2, tag[1]);
                dword(offset + 4, tag[2]);
                dword(offset + 8, tag[3]);
                offset += 12;
            }
            dword(offset, 0);
            for (unsigned pixel = 0; pixel < 4; ++pixel)
            {
                bytes[512 + pixel] = static_cast<unsigned char>(index * 4 + pixel + 1);
            }
            char name[64];
            std::snprintf(name, sizeof(name), "Data/Haze/wibble_%02u.tif", index);
            std::ofstream stream(root_ / name, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            stream.close();
            require(static_cast<bool>(stream), "cannot write generated TIFF");
            entries_.push_back({name, bytes.size()});
        }
    }
    ~GeneratedHaze()
    {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }
    std::shared_ptr<const beachlife::data::FileSource> mount() const
    {
        return std::make_shared<beachlife::data::FolderSource>(root_, entries_);
    }

  private:
    fs::path root_;
    std::vector<beachlife::data::SourceEntry> entries_;
};

constexpr unsigned image_width = 8;
constexpr unsigned image_height = 4;
struct GuardedOwner
{
    std::uint64_t before = 0x123456789abcdef0ULL;
    ScreenRequestRenderer owner;
    std::uint64_t after = 0xfedcba9876543210ULL;

    GuardedOwner(platform::Video& video, unsigned char mode)
        : owner(video, image_width, image_height, mode)
    {
    }
    void guards() const
    {
        require(before == 0x123456789abcdef0ULL && after == 0xfedcba9876543210ULL,
            "outside owner guard changed");
    }
};

void check_constructed(GuardedOwner& guarded, RecorderVideo& video)
{
    auto& owner = guarded.owner;
    guarded.guards();
    require(screen_request_renderer == &owner, "owner was not published");
    require(owner.software_mode == 1 && owner.width == image_width && owner.height == image_height,
        "constructor dimensions or software mode differ");
    require(owner.initialized_168 == 1 && owner.word_60 == 0x38,
        "constructor word60 or initialization result differs");
    require(
        owner.draw == nullptr && owner.primary_surface == nullptr && owner.back_surface == nullptr,
        "native software owner fabricated a COM object");
    require(owner.native_display && owner.device_context_64 && owner.window_34 == &video,
        "real display/context/window ownership missing");
    require(owner.format_44 == 1 && owner.fill_color == 0x3e0 && owner.clip_mode_54 == 1 &&
                owner.word_50 == 0 && owner.word_58 == 0x3f800000 && owner.current_surface == -1 &&
                owner.pixels_locked == 0 && owner.capture_pending_ac == 0 &&
                owner.unscaled_clip_5c == 0 && owner.clip_flag_5d == 0 && owner.flag_4c == 1 &&
                owner.rectangle_suppressed_4d == 0 && owner.rectangle_enabled_4e == 0 &&
                owner.rectangle_ready_4f == 0 && owner.dirty_rectangle_count == 0,
        "defined constructor member differs");
    require(owner.clip_rectangle.x == 0 && owner.clip_rectangle.y == 0 &&
                owner.clip_rectangle.width == static_cast<int>(image_width) &&
                owner.clip_rectangle.height == static_cast<int>(image_height),
        "clip rectangle differs");
    require(owner.dirty_rectangles.values.empty(), "new real deque is not empty");
    require(render_mask_pixels == owner.pixels_1c && render_mask_width == image_width &&
                render_mask_format == 1 && render_green_mask == 0x3e0,
        "real mask setup not selected");
    require(owner.pixels_1c && owner.buffer_130, "real drawing buffers missing");
    for (unsigned i = 0; i < image_width * image_height; ++i)
    {
        require(owner.buffer_130[i] == 0, "marker initialization differs");
    }
    require(owner.sea_state_134 && owner.sea_state_134->owned_data_00 == nullptr &&
                owner.sea_state_134->word_1007c == 0xffffffffu &&
                owner.sea_state_134->byte_10080 == 0 && owner.sea_state_134->sea_enabled_10081 == 1,
        "genuine sea owner initialization differs");
    for (unsigned i = 0; i < 6; ++i)
    {
        const auto* image = owner.haze_images_138[i];
        require(image && image->width == 2 && image->height == 2 && image->bits_per_pixel == 8 &&
                    image->bytes_per_pixel == 1 && image->pixels,
            "real haze image owner differs");
        for (unsigned pixel = 0; pixel < 4; ++pixel)
        {
            require(image->pixels[pixel] == i * 4 + pixel + 1, "real TIFF payload differs");
        }
    }
    require(owner.surface_storage.size() == 1, "constructor did not create one real surface");
    const auto* entry = owner.surface_storage[0];
    require(entry && entry->active == 1 && entry->software == 1 && entry->pixels &&
                entry->width == image_width && entry->height == image_height &&
                entry->pitch == image_width * 2 && entry->position.x == 0 &&
                entry->position.y == 0 && entry->prepared == 0,
        "real initial surface differs");
    const auto& bitmap = owner.bitmap_info->bmiHeader;
    require(bitmap.biSize == 40 && bitmap.biWidth == static_cast<int>(image_width) &&
                bitmap.biHeight == -static_cast<int>(image_height) && bitmap.biPlanes == 1 &&
                bitmap.biBitCount == 16 && bitmap.biCompression == 0 && bitmap.biSizeImage == 0 &&
                bitmap.biXPelsPerMeter == 0 && bitmap.biYPelsPerMeter == 0 &&
                bitmap.biClrUsed == 0 && bitmap.biClrImportant == 0,
        "bitmap header differs");
    // Never inspect descriptor tail, entry hardware-only members, opaque bytes,
    // initial drawing pixels or ABI padding; they have no initialized value.
    const auto rectangle = owner.native_display->client_rectangle_in_screen();
    require(owner.window_rectangle_24.left == rectangle.left &&
                owner.window_rectangle_24.top == rectangle.top &&
                owner.window_rectangle_24.right == rectangle.right &&
                owner.window_rectangle_24.bottom == rectangle.bottom,
        "stored client rectangle differs");
    const auto outer = owner.native_display->outer_rectangle();
    const auto work = owner.native_display->work_area();
    require(outer.left >= work.left && outer.top >= work.top, "left/top clamp differs");
    std::array<unsigned char, 4> flags{};
    for (const auto& mode : owner.native_display->enumerate_modes())
    {
        if (mode.width == 640 && mode.height == 480)
        {
            flags[0] = 1;
        }
        if (mode.width == 800 && mode.height == 600)
        {
            flags[1] = 1;
        }
        if (mode.width == 1024 && mode.height == 768)
        {
            flags[2] = 1;
        }
        if (mode.width == 1280 && mode.height == 960)
        {
            flags[3] = 1;
        }
    }
    require(std::equal(flags.begin(), flags.end(), owner.display_mode_flags_48),
        "real display enumeration/callback flags differ");
}

std::size_t run(const fs::path& files)
{
    GeneratedHaze generated(files);
    beachlife::runtime::retail_files::Session session(generated.mount());
    SdlVideo sdl;
    require(SDL_GetCurrentVideoDriver() && std::string_view(SDL_GetCurrentVideoDriver()) == "x11",
        "fixture did not select actual x11");
    Window window(
        SDL_CreateWindow("genuine renderer owner", 320, 240, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    require(window != nullptr, "cannot create real x11 window");
    RecorderVideo video(window.get());
    auto older = std::make_unique<GuardedOwner>(video, 1);
    check_constructed(*older, video);
    auto& owner = older->owner;
    const int appended = owner.CreateSurface(2, 2, 0);
    require(appended == 1 && owner.surface_storage.size() == 2, "surface append differs");
    ScreenSurfacePosition* retained = owner.surface_storage[1];
    owner.CleanupSurface(appended);
    require(retained->active == 0 && retained->pixels == nullptr, "real surface cleanup differs");
    const int reused = owner.CreateSurface(3, 2, 0);
    require(reused == 1 && owner.surface_storage.size() == 2 &&
                owner.surface_storage[1] == retained && retained->width == 3 &&
                retained->height == 2 && retained->pitch == 6,
        "inactive-entry reuse or publication differs");
    owner.dirty_rectangles.PushBack({1, 2, 3, 4});
    owner.dirty_rectangle_count = 1;
    older->guards();

    auto younger = std::make_unique<GuardedOwner>(video, 1);
    check_constructed(*younger, video);
#if !defined(BEACHLIFE_DECOMP_RENDERER_WORD_SURFACE)
    bool unsupported = false;
    try
    {
        GuardedOwner hardware(video, 0);
    }
    catch (const std::runtime_error& error)
    {
        unsupported = std::string_view(error.what()) ==
                      "Native hardware renderer construction is unsupported";
    }
    require(unsupported && screen_request_renderer == &younger->owner,
        "unsupported hardware entry mutated the published owner");
#endif
    // Mode0 startup now has a separate real-GPU/exact-mode ownership case.
    // Do not run it through this software RecorderVideo or an 8x4 fake mode.
    older.reset();
    require(screen_request_renderer == nullptr,
        "older owner did not unconditionally clear younger registration");
    younger->guards();

    // Observe the actual context the genuine constructor acquired. This is a
    // platform boundary check, not a call to the still-unlinked retail Present.
    std::array<unsigned short, image_width * image_height> pixels{};
    constexpr std::array<unsigned short, 4> colors{0xfc00, 0x03e0, 0x801f, 0xffff};
    constexpr std::array<std::uint32_t, 4> rgba{0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff};
    for (std::size_t i = 0; i < pixels.size(); ++i)
    {
        pixels[i] = colors[i % colors.size()];
    }
    auto separate_lease = younger->owner.native_display->acquire_context();
    younger->owner.current_surface = 0;
    unsigned short* locked = younger->owner.AccessPixels();
    require(
        locked == younger->owner.surface_storage[0]->pixels && younger->owner.pixels_locked == 1,
        "real pixel access differs");
    std::copy(pixels.begin(), pixels.end(), locked);
    younger->owner.device_context_64->present_rgb555(
        std::as_bytes(std::span(locked, pixels.size())),
        image_width,
        image_height,
        image_width * 2);
    younger->owner.UnlockPixels();
    require(younger->owner.pixels_locked == 0, "real unlock did not clear its flag");
    younger.reset();
    require(screen_request_renderer == nullptr, "final registration not cleared");
    separate_lease->present_rgb555(
        std::as_bytes(std::span(pixels)), image_width, image_height, image_width * 2);
    require(video.presents == 2 && video.last.width == static_cast<int>(image_width) &&
                video.last.height == static_cast<int>(image_height) &&
                video.last.pixels.size() == pixels.size(),
        "real independent context lifetime differs");
    for (std::size_t i = 0; i < pixels.size(); ++i)
    {
        require(video.last.pixels[i] == rgba[i % rgba.size()], "RGB555 transport literal differs");
    }
    return video.presents;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "supply an unused generated-file directory");
        const auto presents = run(argv[1]);
        require(SDL_WasInit(0) == 0, "SDL survived fixture lifetime");
        std::printf(
            "renderer-owner: owners=2 TIFFs=12 appends=1 reuses=1 presents=%zu checks=%zu\n",
            presents,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "renderer-owner: %s\n", failure.message);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "renderer-owner host failure: %s\n", failure.what());
        return 3;
    }
}
