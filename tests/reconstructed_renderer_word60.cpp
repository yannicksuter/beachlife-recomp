#include "renderer_queue_test_state.h"
#include "startup_screen_renderer.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <type_traits>

namespace
{
static_assert(CHAR_BIT == 8 && sizeof(unsigned int) == 4);
static_assert(sizeof(void*) == 8 && alignof(void*) == 8);
static_assert(std::is_aggregate_v<ScreenRequestRenderer>);
static_assert(!std::is_polymorphic_v<ScreenRequestRenderer>);
static_assert(!std::is_trivially_copyable_v<ScreenRequestRenderer>);
static_assert(std::is_same_v<decltype(&ScreenRequestRenderer::SetWord60),
    void (ScreenRequestRenderer::*)(unsigned int)>);

#if defined(BEACHLIFE_DECOMP_SURFACE_TABLE)
constexpr std::size_t native_word_offset = 108;
constexpr std::size_t native_table_offset = 120;
constexpr std::size_t native_current_offset = 144;
constexpr std::size_t native_queue_offset = 152;
#else
constexpr std::size_t native_word_offset = 104;
constexpr std::size_t native_table_offset = 112;
constexpr std::size_t native_current_offset = 128;
constexpr std::size_t native_queue_offset = 136;
#endif
const char* current_case = "layout";
unsigned calls = 0;
unsigned checks = 0;
std::size_t logical_bytes = 0;

struct CheckFailure
{
    const char* reason;
};

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        throw CheckFailure{reason};
    }
}

struct Frame
{
    std::array<unsigned char, 16> before{};
    ScreenRequestRenderer renderer{};
    std::array<unsigned char, 16> after{};
};

std::uintptr_t offset(const ScreenRequestRenderer& renderer, const void* member)
{
    return reinterpret_cast<std::uintptr_t>(member) - reinterpret_cast<std::uintptr_t>(&renderer);
}

void check_layout(const ScreenRequestRenderer& r)
{
    // Check actual member addresses without offsetof on a possibly non-standard
    // library-containing type. The x86 field name does not impose its x86 offset.
    require(offset(r, r.unknown_5e) == native_word_offset - 2 && sizeof(r.unknown_5e) == 2,
        "leading opaque extent differs");
    require(offset(r, &r.word_60) == native_word_offset && sizeof(r.word_60) == 4 &&
                reinterpret_cast<std::uintptr_t>(&r.word_60) % alignof(unsigned int) == 0,
        "native word location or alignment differs");
    require(offset(r, r.unknown_64) == native_word_offset + 4 && sizeof(r.unknown_64) == 4,
        "trailing opaque extent differs");
    require(offset(r, &r.surfaces) == native_table_offset &&
                offset(r, &r.current_surface) == native_current_offset &&
                offset(r, &r.dirty_rectangles) == native_queue_offset,
        "retained suffix moved");
    require(
        offset(r, &r.dirty_rectangle_count) == native_queue_offset + sizeof(RendererRectangleQueue),
        "native queue or count extent differs");
    const std::size_t prefix_end =
        native_queue_offset + sizeof(RendererRectangleQueue) + sizeof(r.dirty_rectangle_count);
    std::size_t end = prefix_end;
#if defined(BEACHLIFE_DECOMP_RENDERER_MARKER_BUFFER)
    require(offset(r, r.unknown_a8) == prefix_end && sizeof(r.unknown_a8) == 0x88,
        "marker tail opaque interval differs");
    const std::size_t pointer_offset =
        (prefix_end + 0x88 + alignof(void*) - 1) / alignof(void*) * alignof(void*);
    require(offset(r, &r.buffer_130) == pointer_offset, "marker pointer alignment differs");
    end = pointer_offset + sizeof(r.buffer_130);
#endif
#if defined(BEACHLIFE_DECOMP_RENDERER_SEA_STATE)
    require(offset(r, &r.sea_state_134) == end, "sea-state pointer offset differs");
    end += sizeof(r.sea_state_134);
#endif
    const std::size_t alignment = alignof(ScreenRequestRenderer);
    require(alignment == alignof(void*) &&
                sizeof(ScreenRequestRenderer) == (end + alignment - 1) / alignment * alignment,
        "native renderer size differs");
}

void exercise()
{
    std::array<unsigned short, 4> pixels{0x1357, 0x2468, 0xabcd, 0x9876};
    std::array<ScreenSurfacePosition, 2> surfaces{};
    std::array<ScreenSurfacePosition*, 2> table{&surfaces[0], &surfaces[1]};
    Frame frame{}; // Real aggregate and deque lifetimes, not a retail owner ctor.
    frame.before.fill(0xa5);
    frame.after.fill(0x5a);
    renderer_queue_test::initialize(frame.renderer, 23);
    auto& r = frame.renderer;
    r.pixels_1c = pixels.data();
    r.surfaces = table.data();
    r.current_surface = 1;
    r.word_60 = 0x12345678U;
    check_layout(r);
    const ScreenRequestRenderer before = r;

    struct Literal
    {
        const char* name;
        unsigned int value;
    };
    constexpr std::array<Literal, 3> literals{
        {{"zero", 0U}, {"full-bits", 0xffffffffU}, {"high-bit-repeat", 0x80000000U}}};
    for (const Literal& literal : literals)
    {
        current_case = literal.name;
        ScreenRequestRenderer expected = before;
        expected.word_60 = literal.value;
        const unsigned repetitions = literal.value == 0x80000000U ? 2U : 1U;
        for (unsigned repetition = 0; repetition < repetitions; ++repetition)
        {
            if (repetition == 0)
            {
                r.SetWord60(literal.value);
            }
            else
            {
                r.SetWord60(r.word_60); // By-value self input after the high-bit store.
            }
            ++calls;
            require(r.word_60 == literal.value, "stored word differs");
            logical_bytes += renderer_queue_test::compared_bytes(expected);
            require(renderer_queue_test::equal(r, expected), "unrelated renderer state changed");
            for (std::size_t i = 0; i < frame.before.size(); ++i)
            {
                logical_bytes += 2;
                require(frame.before[i] == 0xa5 && frame.after[i] == 0x5a, "outer guard changed");
            }
            require(pixels == std::array<unsigned short, 4>{0x1357, 0x2468, 0xabcd, 0x9876} &&
                        table[0] == &surfaces[0] && table[1] == &surfaces[1],
                "borrowed storage changed");
            logical_bytes += sizeof(pixels) + sizeof(table);
        }
    }
    require(calls == 4, "literal call total differs");
}
} // namespace

int main()
{
    try
    {
        exercise();
        std::printf("renderer-word60 groups=3 calls=%u checks=%u logical-bytes=%zu "
                    "renderer-size=%zu renderer-align=%zu queue-size=%zu word-offset=%zu pass\n",
            calls,
            checks,
            logical_bytes,
            sizeof(ScreenRequestRenderer),
            alignof(ScreenRequestRenderer),
            sizeof(RendererRectangleQueue),
            native_word_offset);
        return 0;
    }
    catch (const CheckFailure& failure)
    {
        std::fprintf(stderr, "renderer-word60 %s: %s\n", current_case, failure.reason);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "renderer-word60 host failure: %s\n", failure.what());
        return 3;
    }
}
