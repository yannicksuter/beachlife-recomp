#include "renderer_owner_test_data.h"
#include "runtime/jpeg_export.h"
#include "runtime/renderer_bitmap.h"
#include "startup_jpeg_export.h"
#include "startup_screen_item.h"
#include "tga_image.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace jpeg = beachlife::runtime::jpeg;
namespace owner_test = renderer_owner_test;

namespace
{
static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Release) == 2 * sizeof(void*));
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Blt) == 5 * sizeof(void*));
static_assert(offsetof(DirectDrawSurface7VtablePrefix, GetSurfaceDesc) == 22 * sizeof(void*));
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Lock) == 25 * sizeof(void*));
static_assert(offsetof(DirectDrawSurface7VtablePrefix, SetColorKey) == 29 * sizeof(void*));
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Unlock) == 32 * sizeof(void*));
static_assert(sizeof(DirectDrawSurface7VtablePrefix) == 33 * sizeof(void*));

std::size_t checks = 0;
std::size_t calls = 0;
void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

class OutputDirectory
{
  public:
    explicit OutputDirectory(const fs::path& path)
        : previous_(fs::current_path())
        , root_(fs::absolute(path))
    {
        require(!fs::exists(root_), "generated output directory already exists");
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
    fs::path previous_;
    fs::path root_;
};

// The complete owner remains naturally constructed. No pointer owner, deque,
// surface table, opaque member or vptr is overwritten by a synthetic prefix.
struct GuardedOwner
{
    std::uint64_t before = 0x123456789abcdef0ULL;
    ScreenRequestRenderer value;
    std::uint64_t after = 0xfedcba9876543210ULL;

    explicit GuardedOwner(beachlife::platform::Video& video)
        : value(video, 8, 4, 1)
    {
    }
    void check() const
    {
        require(before == 0x123456789abcdef0ULL && after == 0xfedcba9876543210ULL,
            "outside owner guards changed");
    }
};

// The real wrapper intentionally retains borrowed pointers after Capture frees
// its image. Do not evaluate the dangling pixel pointer. Fixture cleanup clears
// those two observations before the renderer/path storage lifetime ends.
struct ExportPointerCleanup
{
    ~ExportPointerCleanup()
    {
        jpeg_export_properties.dib_bytes = nullptr;
        jpeg_export_properties.jpg_file = nullptr;
    }
};

using Rgb = std::array<unsigned char, 3>;
constexpr Rgb red{255, 0, 0};
constexpr Rgb green{0, 255, 0};
constexpr Rgb blue{0, 0, 255};
constexpr Rgb white{255, 255, 255};
constexpr Rgb black{0, 0, 0};
constexpr Rgb grey{132, 132, 132};
constexpr Rgb mixed{33, 140, 165};
constexpr Rgb dim{16, 33, 24};
constexpr std::array<unsigned short, 32> words{0x7c00,
    0x03e0,
    0x001f,
    0x7fff,
    0x0000,
    0x4210,
    0x1234,
    0x0883,
    0x0883,
    0x1234,
    0x4210,
    0x0000,
    0x7fff,
    0x001f,
    0x03e0,
    0x7c00,
    0x0000,
    0x7c00,
    0x4210,
    0x03e0,
    0x1234,
    0x001f,
    0x0883,
    0x7fff,
    0x7fff,
    0x0883,
    0x001f,
    0x1234,
    0x03e0,
    0x4210,
    0x7c00,
    0x0000};
constexpr std::array<Rgb, 32> whole_rgb{red,
    green,
    blue,
    white,
    black,
    grey,
    mixed,
    dim,
    dim,
    mixed,
    grey,
    black,
    white,
    blue,
    green,
    red,
    black,
    red,
    grey,
    green,
    mixed,
    blue,
    dim,
    white,
    white,
    dim,
    blue,
    mixed,
    green,
    grey,
    red,
    black};
// Literal rows at x=[2,6), y=[1,4). No RGB555 conversion/crop implementation is
// copied into the oracle; the production codec receives these literal RGB bytes.
constexpr std::array<Rgb, 12> crop_rgb{
    grey, black, white, blue, grey, green, mixed, blue, blue, mixed, green, grey};

std::vector<unsigned char> read_file(const char* name)
{
    std::ifstream stream(name, std::ios::binary);
    require(stream.good(), "JPEG output cannot be opened");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::vector<unsigned char> reference(
    const char* name, std::span<const Rgb> rgb, unsigned width, unsigned height)
{
    std::vector<unsigned char> bytes;
    for (const auto& pixel : rgb)
    {
        bytes.insert(bytes.end(), pixel.begin(), pixel.end());
    }
    require(bytes.size() == width * height * 3, "literal RGB extent differs");
    require(
        jpeg::write_rgb420_file(name, bytes.data(), width, height, 100) == jpeg::Result::success,
        "real reference JPEG encoding failed");
    const auto encoded = read_file(name);
    require(encoded.size() > 4 && encoded[0] == 0xff && encoded[1] == 0xd8 &&
                encoded[encoded.size() - 2] == 0xff && encoded.back() == 0xd9,
        "reference JPEG markers differ");
    return encoded;
}

struct DefinedOwnerState
{
    // Only defined members/pointers are observed. No hardware-only entry member,
    // opaque owner byte, descriptor tail or ABI padding is read.
    ScreenSurfacePosition* entry;
    ScreenSurfacePosition* const* table;
    std::size_t capacity;
    unsigned short* pixels;
    unsigned short* default_pixels;
    beachlife::platform::Video* window;
    beachlife::runtime::renderer_display::SoftwareDisplay* display;
    beachlife::runtime::renderer_display::DrawingContext* context;
    beachlife::runtime::renderer_display::BitmapInfo* bitmap;
    RendererSeaStatePrefix* sea;
    unsigned char* markers;
    std::array<TgaImage*, 6> haze{};

    explicit DefinedOwnerState(const ScreenRequestRenderer& owner)
        : entry(owner.surface_storage.at(0))
        , table(owner.surface_storage.data())
        , capacity(owner.surface_storage.capacity())
        , pixels(entry->pixels)
        , default_pixels(owner.pixels_1c)
        , window(owner.window_34)
        , display(owner.native_display)
        , context(owner.device_context_64)
        , bitmap(owner.bitmap_info)
        , sea(owner.sea_state_134)
        , markers(owner.buffer_130)
    {
        std::copy(std::begin(owner.haze_images_138), std::end(owner.haze_images_138), haze.begin());
    }

    void check(const ScreenRequestRenderer& owner) const
    {
        require(screen_request_renderer == &owner && owner.software_mode == 1 &&
                    owner.format_44 == 1 && owner.width == 8 && owner.height == 4 &&
                    owner.word_60 == 0x38 && owner.initialized_168 == 1,
            "defined renderer state changed");
        require(owner.surface_storage.size() == 1 && owner.surface_storage.data() == table &&
                    owner.surface_storage.capacity() == capacity &&
                    owner.surface_storage[0] == entry && entry->pixels == pixels &&
                    owner.pixels_1c == default_pixels && owner.window_34 == window &&
                    owner.native_display == display && owner.device_context_64 == context &&
                    owner.bitmap_info == bitmap && owner.sea_state_134 == sea &&
                    owner.buffer_130 == markers &&
                    std::equal(haze.begin(), haze.end(), std::begin(owner.haze_images_138)),
            "owned storage identity changed");
        require(owner.draw == nullptr && owner.primary_surface == nullptr &&
                    owner.back_surface == nullptr,
            "software capture introduced a hardware object");
        require(entry->active == 1 && entry->software == 1 && entry->width == 8 &&
                    entry->height == 4 && entry->pitch == 16 && entry->position.x == 0 &&
                    entry->position.y == 0,
            "surface geometry changed");
        require(owner.dirty_rectangles.values.empty() && owner.dirty_rectangle_count == 0 &&
                    owner.clip_rectangle.x == 0 && owner.clip_rectangle.y == 0 &&
                    owner.clip_rectangle.width == 8 && owner.clip_rectangle.height == 4 &&
                    owner.fill_color == 0x3e0,
            "unrelated queue or clipping state changed");
        for (unsigned index = 0; index < 32; ++index)
        {
            require(markers[index] == 0 && default_pixels[index] == 0x1111,
                "unrelated owned buffer changed");
        }
        for (unsigned index = 0; index < 6; ++index)
        {
            require(haze[index] && haze[index]->width == 2 && haze[index]->height == 2 &&
                        haze[index]->pixels[0] == index * 4 + 1,
                "genuine TIFF ownership changed");
        }
    }
};

void capture(GuardedOwner& guarded,
    const DefinedOwnerState& before,
    const char* name,
    const ScreenRectangle& area,
    bool high_bit,
    jpeg::Result expected_result,
    const std::vector<unsigned char>* expected_file)
{
    auto& owner = guarded.value;
    require(
        std::string_view(name).size() < 24, "fixture filename no longer fits its bounded setup");
    // Only this 32-byte fixture window is initialized/observed. It does not
    // establish a retail filename capacity for the opaque +ad region.
    std::array<unsigned char, 32> path_bytes{};
    path_bytes.fill(0xa5);
    std::copy_n(reinterpret_cast<const unsigned char*>(name),
        std::string_view(name).size() + 1,
        path_bytes.begin());
    std::copy(path_bytes.begin(), path_bytes.end(), std::begin(owner.unknown_ad));
    for (std::size_t index = 0; index < words.size(); ++index)
    {
        before.pixels[index] = static_cast<unsigned short>(words[index] | (high_bit ? 0x8000 : 0));
    }
    std::array<unsigned short, 32> pixels_before{};
    std::copy_n(before.pixels, pixels_before.size(), pixels_before.begin());
    owner.current_surface = -1; // Capture must select the actual owned surface0.
    owner.pixels_locked = 0;
    owner.capture_pending_ac = 0x7f;
    before.entry->prepared = 1;
    jpeg_export_result = 123;
    owner.CaptureFrame(area);
    ++calls;
    require(jpeg_export_result == static_cast<int>(expected_result), "JPEG result differs");
    require(owner.current_surface == 0 && owner.pixels_locked == 0 && before.entry->prepared == 0,
        "capture access/unlock state differs");
    before.check(owner);
    guarded.check();
    require(std::equal(pixels_before.begin(), pixels_before.end(), before.pixels),
        "capture changed source pixels");
    require(std::equal(path_bytes.begin(), path_bytes.end(), std::begin(owner.unknown_ad)),
        "bounded filename or its guard changed");
    const auto& state = jpeg_export_properties;
    require(state.dib_width == static_cast<unsigned>(area.right - area.left) &&
                state.dib_height == area.bottom - area.top && state.dib_pad_bytes == 0 &&
                state.dib_channels == 3 && state.dib_color == 1 &&
                state.jpg_file == reinterpret_cast<const char*>(owner.unknown_ad) &&
                state.jpg_width == state.dib_width &&
                state.jpg_height == static_cast<unsigned>(state.dib_height) &&
                state.jpg_channels == 3 && state.jpg_color == 3 && state.jpg_subsampling == 1 &&
                state.quality == 100,
        "real export settings differ");
    // The image and pixel array have already been destroyed by the complete
    // Capture body. Never read/compare its retained dangling dib_bytes pointer.
    if (expected_file)
    {
        require(read_file(name) == *expected_file, "captured JPEG differs from literal RGB");
    }
    else
    {
        require(!fs::exists(name), "failed output unexpectedly exists");
    }
    require(owner.capture_pending_ac == 0, "CaptureFrame did not clear pending flag");
}

void run()
{
    require(screen_request_renderer == nullptr, "renderer global initially bound");
    auto& environment = owner_test::environment();
    {
        beachlife::runtime::retail_files::Session files(environment.haze.mount());
        GuardedOwner guarded(environment.video);
        ExportPointerCleanup export_cleanup;
        auto& owner = guarded.value;
        require(owner.surface_storage.size() == 1, "constructor surface missing");
        const DefinedOwnerState before(owner);
        require(before.pixels && before.default_pixels && before.pixels != before.default_pixels,
            "genuine separate drawing buffers missing");
        std::fill_n(before.default_pixels, 32, static_cast<unsigned short>(0x1111));
        before.check(owner);
        const auto whole = reference("whole-reference.jpg", whole_rgb, 8, 4);
        const auto cropped = reference("crop-reference.jpg", crop_rgb, 4, 3);
        capture(guarded, before, "whole.jpg", {0, 0, 8, 4}, false, jpeg::Result::success, &whole);
        capture(guarded, before, "high.jpg", {0, 0, 8, 4}, true, jpeg::Result::success, &whole);
        capture(guarded, before, "crop.jpg", {2, 1, 6, 4}, true, jpeg::Result::success, &cropped);
        require(!fs::exists("absent"), "missing-parent case is not missing");
        capture(guarded,
            before,
            "absent/fail.jpg",
            {2, 1, 6, 4},
            false,
            jpeg::Result::open_failed,
            nullptr);
    }
    require(screen_request_renderer == nullptr, "real registration cleanup did not clear global");
    require(calls == 4, "capture call count differs");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "expected generated output directory");
        OutputDirectory output(argv[1]);
        run();
        std::printf(
            "renderer-capture: groups=3 owners=1 TIFFs=6 calls=%zu references=2 checks=%zu\n",
            calls,
            checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "renderer-capture: %s\n", error.what());
        return 2;
    }
}
