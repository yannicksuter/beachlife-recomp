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
static_assert(!std::is_aggregate_v<ScreenRequestRenderer>);
static_assert(!std::is_copy_constructible_v<ScreenRequestRenderer>);
static_assert(!std::is_polymorphic_v<ScreenRequestRenderer>);
static_assert(!std::is_trivially_copyable_v<ScreenRequestRenderer>);
static_assert(std::is_same_v<decltype(&ScreenRequestRenderer::SetWord60),
    void (ScreenRequestRenderer::*)(unsigned int)>);

std::size_t observed_word_offset = 0;
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
    renderer_owner_test::Bootstrap bootstrap;
    std::array<unsigned char, 16> before{};
    ScreenRequestRenderer renderer{bootstrap.video(), 8, 4, 1};
    std::array<unsigned char, 16> after{};
    renderer_owner_test::RestoreOwnedState restore{renderer, bootstrap};
};

std::uintptr_t offset(const ScreenRequestRenderer& renderer, const void* member)
{
    return reinterpret_cast<std::uintptr_t>(member) - reinterpret_cast<std::uintptr_t>(&renderer);
}

void check_layout(const ScreenRequestRenderer& r)
{
    const auto aligned = [](std::size_t at, std::size_t alignment)
    {
        return (at + alignment - 1) / alignment * alignment;
    };
    observed_word_offset = offset(r, &r.word_60);
    require(sizeof(r.unknown_5e) == 2 && sizeof(r.word_60) == 4 &&
                observed_word_offset == aligned(offset(r, r.unknown_5e) + 2, alignof(unsigned int)),
        "native word location or alignment differs");
    require(offset(r, &r.device_context_64) ==
                aligned(observed_word_offset + sizeof(r.word_60), alignof(void*)),
        "typed context alignment differs");
    require(offset(r, &r.surface_storage) ==
                    offset(r, &r.device_context_64) + sizeof(r.device_context_64) &&
                offset(r, &r.current_surface) ==
                    offset(r, &r.surface_storage) + sizeof(r.surface_storage),
        "actual vector/member order differs");
    require(
        offset(r, &r.dirty_rectangles) == aligned(offset(r, &r.fill_color) + sizeof(r.fill_color),
                                              alignof(RendererRectangleQueue)) &&
            offset(r, &r.dirty_rectangle_count) ==
                offset(r, &r.dirty_rectangles) + sizeof(RendererRectangleQueue),
        "actual deque/member order differs");
    require(
        offset(r, &r.sea_state_134) == offset(r, &r.buffer_130) + sizeof(r.buffer_130) &&
            offset(r, &r.haze_images_138) == offset(r, &r.sea_state_134) + sizeof(r.sea_state_134),
        "owned marker/sea/image order differs");
    require(offset(r, &r.native_display) ==
                    aligned(offset(r, r.unknown_169) + sizeof(r.unknown_169), alignof(void*)) &&
                sizeof(r) == aligned(offset(r, &r.native_display) + sizeof(r.native_display),
                                 alignof(ScreenRequestRenderer)),
        "actual owner tail or size differs");
}

void exercise()
{
    std::array<unsigned short, 4> pixels{0x1357, 0x2468, 0xabcd, 0x9876};
    std::array<ScreenSurfacePosition, 2> surfaces{};
    std::array<ScreenSurfacePosition*, 2> table{&surfaces[0], &surfaces[1]};
    Frame frame{}; // Genuine owner, with its owned storage held by the restoration guard.
    frame.before.fill(0xa5);
    frame.after.fill(0x5a);
    renderer_queue_test::initialize(frame.renderer, 23);
    auto& r = frame.renderer;
    r.pixels_1c = pixels.data();
    r.surface_storage.assign(table.begin(), table.end());
    r.current_surface = 1;
    r.word_60 = 0x12345678U;
    check_layout(r);
    const renderer_queue_test::Snapshot before = r;

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
        renderer_queue_test::Snapshot expected = before;
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
            observed_word_offset);
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
