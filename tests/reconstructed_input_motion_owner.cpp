#include "data/folder_source.h"
#include "renderer_queue_test_state.h"
#include "runtime/retail_cursor.h"
#include "runtime/retail_files.h"
#include "startup_input_motion.h"
#include "startup_input_state.h"
#include "startup_screen_item.h"
#include "tga_image.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>

namespace
{
namespace data = beachlife::data;
namespace files = beachlife::runtime::retail_files;
namespace cursor = beachlife::runtime::retail_cursor;

static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && sizeof(float) == 4);
static_assert(sizeof(void*) == 8);
static_assert(std::is_standard_layout_v<InputMotionState>);
static_assert(!std::is_polymorphic_v<InputMotionState>);
static_assert(std::is_empty_v<InputMotionRegistration>);
static_assert(sizeof(InputMotionState) == 48 && alignof(InputMotionState) == 8);
static_assert(offsetof(InputMotionState, window_00) == 0);
static_assert(offsetof(InputMotionState, cursor_04) == 8);
static_assert(offsetof(InputMotionState, value_134) == 16);
static_assert(offsetof(InputMotionState, value_138) == 20);
static_assert(offsetof(InputMotionState, word_10) == 24);
static_assert(offsetof(InputMotionState, unknown_14) == 28);
static_assert(offsetof(InputMotionState, value_18) == 32);
static_assert(offsetof(InputMotionState, scale_1c) == 36);
static_assert(offsetof(InputMotionState, byte_20) == 40);
static_assert(offsetof(InputMotionState, byte_21) == 41);
static_assert(offsetof(InputMotionState, unknown_22) == 42);

struct Failure
{
    const char* message;
};
struct Counts
{
    unsigned int groups = 0;
    unsigned int constructions = 0;
    unsigned int destructions = 0;
    unsigned int image_checks = 0;
    unsigned int loads = 0;
    unsigned int sets = 0;
    unsigned int checks = 0;
} counts;

void require(bool value, const char* message)
{
    ++counts.checks;
    if (!value)
    {
        throw Failure{message};
    }
}

using Guard = std::array<unsigned char, 16>;
constexpr Guard before_guard{1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31};
constexpr Guard after_guard{32, 30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2};

struct Owner
{
    Guard before = before_guard;
    alignas(InputMotionState) unsigned char storage[sizeof(InputMotionState)];
    Guard after = after_guard;
    InputMotionState* value = nullptr;

    Owner(const char* path, void* window)
    {
        // Observable pre-lifetime stores establish only the two opaque bytes
        // checked below. No live object or vptr is overwritten; padding is ignored.
        volatile unsigned char* bytes = storage;
        for (std::size_t index = 0; index != sizeof(storage); ++index)
        {
            bytes[index] = 0xa7;
        }
        value = ::new (static_cast<void*>(storage)) InputMotionState(path, window);
        ++counts.constructions;
    }
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
    ~Owner()
    {
        close();
    }
    void close()
    {
        if (value)
        {
            value->~InputMotionState();
            value = nullptr;
            ++counts.destructions;
        }
    }
    void guards() const
    {
        require(before == before_guard && after == after_guard, "outside owner guards changed");
    }
};
static_assert(offsetof(Owner, storage) == 16);
static_assert(offsetof(Owner, after) == 16 + sizeof(InputMotionState));

// Only the already-qualified platform boundary is observed here. No retail
// method, image loader, input constructor or renderer owner is supplied.
struct CursorObservation final : cursor::Provider
{
    InputState* input = nullptr;
    void* window = nullptr;
    unsigned char token = 0;
    bool correct = true;

    void* load_image(void* instance,
        std::uint16_t resource,
        std::uint32_t type,
        std::int32_t width,
        std::int32_t height,
        std::uint32_t flags) noexcept override
    {
        ++counts.loads;
        auto* owner = startup_input_motion_state;
        // These members have already been assigned at the actual call site.
        // cursor_04 and the later initialized members must not be read yet.
        correct = correct && owner && owner->window_00 == window && owner->word_10 == 0 &&
                  instance == input->resource_instance && resource == 106 && type == 2 &&
                  width == 32 && height == 32 && flags == 0x8000 && input->cursor_identifier == 0;
        return &token;
    }
    void* set_cursor(void* handle) noexcept override
    {
        ++counts.sets;
        correct = correct && handle == &token && startup_input_motion_state &&
                  input->cursor_identifier == 0;
        return nullptr;
    }
};

template <std::size_t N> bool same(const unsigned char (&a)[N], const unsigned char (&b)[N])
{
    return std::memcmp(a, b, N) == 0;
}

bool same_input_except_cursor(const InputState& a, const InputState& b)
{
    return same(a.unknown_00, b.unknown_00) && a.keyboard_device == b.keyboard_device &&
           same(a.unknown_08, b.unknown_08) && a.resource_instance == b.resource_instance &&
           same(a.unknown_14, b.unknown_14) && same(a.states, b.states) &&
           same(a.unknown_11d, b.unknown_11d) && a.unknown_120 == b.unknown_120 &&
           a.unknown_124 == b.unknown_124 && same(a.unknown_128, b.unknown_128) &&
           a.unknown_134 == b.unknown_134 && a.unknown_138 == b.unknown_138 &&
           same(a.unknown_13c, b.unknown_13c) && same(a.unknown_148, b.unknown_148) &&
           a.unknown_610 == b.unknown_610 && a.unknown_611 == b.unknown_611;
}

struct Environment
{
    // InputState remains its qualified borrowed read model. The renderer now
    // has a genuine owner and a scoped method view restored before cleanup.
    InputState input{};
    renderer_owner_test::Bootstrap renderer_bootstrap;
    ScreenRequestRenderer renderer{renderer_bootstrap.video(), 8, 4, 1};
    renderer_owner_test::RestoreOwnedState renderer_restore{renderer, renderer_bootstrap};
    unsigned char instance = 0;
    unsigned char window = 0;
    InputState* old_input = startup_input_state;
    ScreenRequestRenderer* old_renderer = screen_request_renderer;
    CursorObservation observation;
    cursor::Session cursor_session{observation};

    explicit Environment(unsigned int format, int identifier)
    {
        input.resource_instance = &instance;
        input.cursor_identifier = identifier;
        input.unknown_134 = -47;
        input.unknown_138 = 71;
        input.states[19] = 2;
        renderer_queue_test::initialize(renderer, 29);
        renderer.format_44 = format;
        observation.input = &input;
        observation.window = &window;
        startup_input_state = &input;
        screen_request_renderer = &renderer;
    }
    ~Environment()
    {
        startup_input_state = old_input;
        screen_request_renderer = old_renderer;
    }
};

// Complete generated indexed TGA: 2x2 pixels and four BGR palette entries.
// Expected decoded RGB bytes and packed words are literal, independent values.
constexpr std::array<unsigned char, 34> file_bytes{0,
    1,
    1,
    0,
    0,
    4,
    0,
    24,
    0,
    0,
    0,
    0,
    2,
    0,
    2,
    0,
    8,
    0,
    0,
    0,
    0,
    0,
    0,
    255,
    0,
    255,
    0,
    255,
    0,
    0,
    0,
    1,
    2,
    3};
constexpr std::array<unsigned char, 4> decoded_pixels{2, 3, 0, 1};
constexpr std::array<unsigned char, 12> decoded_palette{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
constexpr std::array<unsigned short, 4> palette565{0x0000, 0xf800, 0x07e0, 0x001f};
constexpr std::array<unsigned short, 4> palette555{0x0000, 0x7c00, 0x03e0, 0x001f};

void initialized(const Owner& owner, void* window, bool rgb555)
{
    const auto& value = *owner.value;
    require(value.value_134 == 400 && value.value_138 == 300, "constructor coordinates differ");
    require(value.window_00 == window && value.word_10 == 0 && value.unknown_14 == 0 &&
                value.value_18 == 0.0f && value.scale_1c == 10.0f && value.byte_20 == 0 &&
                value.byte_21 == 0,
        "constructor initialized state differs");
    require(value.unknown_22[0] == 0xa7 && value.unknown_22[1] == 0xa7,
        "constructor changed opaque tail bytes");
    const auto* image = value.cursor_04;
    require(image && image->width == 2 && image->height == 2 && image->bits_per_pixel == 8 &&
                image->bytes_per_pixel == 1 && image->pixels && image->palette &&
                image->converted_palette,
        "real loaded image state differs");
    ++counts.image_checks;
    require(std::memcmp(image->pixels, decoded_pixels.data(), decoded_pixels.size()) == 0,
        "real decoded pixels differ");
    require(std::memcmp(image->palette, decoded_palette.data(), decoded_palette.size()) == 0,
        "real decoded palette differs");
    const auto& expected = rgb555 ? palette555 : palette565;
    for (std::size_t index = 0; index != 256; ++index)
    {
        require(image->converted_palette[index] == (index < 4 ? expected[index] : 0),
            "real converted palette differs");
    }
    for (std::size_t index = decoded_palette.size(); index != 768; ++index)
    {
        require(image->palette[index] == 0, "unused palette bytes changed");
    }
    owner.guards();
}

void single_owner(unsigned int format, int identifier)
{
    ++counts.groups;
    Environment environment(format, identifier);
    const InputState input_before = environment.input;
    const renderer_queue_test::Snapshot renderer_before = environment.renderer;
    const auto loads_before = counts.loads;
    const auto sets_before = counts.sets;
    Owner owner("cursor.tga", &environment.window);
    require(startup_input_motion_state == owner.value, "owner was not published");
    initialized(owner, &environment.window, format == 1);
    require(environment.observation.correct && environment.input.cursor_identifier == 1 &&
                counts.loads == loads_before + (identifier == 1 ? 0U : 1U) &&
                counts.sets == sets_before + (identifier == 1 ? 0U : 1U),
        "real cursor API order or setter behavior differs");
    require(same_input_except_cursor(environment.input, input_before) &&
                renderer_queue_test::equal(environment.renderer, renderer_before),
        "borrowed input or renderer state changed");
    owner.close();
    require(
        startup_input_motion_state == nullptr, "ordinary destructor did not clear registration");
    owner.guards();
}

void unconditional_clear()
{
    ++counts.groups;
    Environment environment(2, 0); // Every non-1 format selects the original 565 branch.
    const renderer_queue_test::Snapshot renderer_before = environment.renderer;
    Owner older("cursor.tga", &environment.window);
    Owner younger("cursor.tga", &environment.window);
    require(startup_input_motion_state == younger.value, "younger owner was not published");
    initialized(older, &environment.window, false);
    initialized(younger, &environment.window, false);
    require(older.value->cursor_04 != younger.value->cursor_04 &&
                older.value->cursor_04->pixels != younger.value->cursor_04->pixels,
        "separate owners share an image allocation");
    older.close();
    require(startup_input_motion_state == nullptr && younger.value != nullptr,
        "older destructor did not unconditionally clear younger publication");
    // Only the still-live younger image is inspected after the older lifetime ends.
    require(std::memcmp(younger.value->cursor_04->pixels, decoded_pixels.data(), 4) == 0,
        "older cleanup changed the younger image");
    younger.close();
    require(startup_input_motion_state == nullptr && environment.observation.correct &&
                renderer_queue_test::equal(environment.renderer, renderer_before),
        "final cleanup or borrowed renderer state differs");
    older.guards();
    younger.guards();
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 2;
    }
    try
    {
        require(startup_input_motion_state == nullptr, "real global initial state differs");
        auto* const input_before = startup_input_state;
        auto* const clock_before = startup_frame_clock;
        auto* const renderer_before = screen_request_renderer;
        const std::filesystem::path root(argv[1]);
        std::filesystem::create_directories(root);
        {
            std::ofstream output(root / "cursor.tga", std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(file_bytes.data()), file_bytes.size());
            require(bool(output), "cannot write generated TGA");
        }
        auto source = std::make_shared<data::FolderSource>(
            root, std::vector<data::SourceEntry>{{"cursor.tga", file_bytes.size()}});
        {
            files::Session session(source);
            single_owner(0, 0);
            single_owner(1, 1);
            unconditional_clear();
        }
        require(startup_input_motion_state == nullptr && startup_input_state == input_before &&
                    startup_frame_clock == clock_before &&
                    screen_request_renderer == renderer_before,
            "borrowed globals were not restored");
        require(counts.groups == 3 && counts.constructions == 4 && counts.destructions == 4 &&
                    counts.image_checks == 4 && counts.loads == 2 && counts.sets == 2,
            "focused protocol count differs");
        std::printf("{\"groups\":%u,\"constructions\":%u,\"destructions\":%u,"
                    "\"images\":%u,\"cursor_loads\":%u,\"cursor_sets\":%u,\"checks\":%u,"
                    "\"native_owner_bytes\":%zu,\"updates\":0}\n",
            counts.groups,
            counts.constructions,
            counts.destructions,
            counts.image_checks,
            counts.loads,
            counts.sets,
            counts.checks,
            sizeof(InputMotionState));
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "InputMotion owner fixture: %s\n", failure.message);
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "InputMotion owner fixture exception: %s\n", error.what());
        return 2;
    }
}
