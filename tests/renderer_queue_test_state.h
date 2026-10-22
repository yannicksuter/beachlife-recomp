#pragma once

#include "renderer_owner_test_data.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <iterator>
#include <type_traits>

// Test-only typed views of a genuine renderer owner. This is not another
// ScreenRequestRenderer definition, constructor, destructor or vtable.
namespace renderer_queue_test
{
static_assert(!std::is_aggregate_v<ScreenRequestRenderer>);
static_assert(!std::is_copy_constructible_v<ScreenRequestRenderer>);

inline bool same_rectangle(const ScreenRectangle& a, const ScreenRectangle& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

template <class A, class B> bool same_bytes(const A& a, const B& b)
{
    return std::size(a) == std::size(b) && std::equal(std::begin(a), std::end(a), std::begin(b));
}

template <class Bytes> void pattern(Bytes& bytes, unsigned int seed)
{
    std::size_t n = 0;
    for (auto& value : bytes)
    {
        value = static_cast<unsigned char>(seed + n++ * 37U);
    }
}

inline unsigned int patterned_word(unsigned int seed)
{
    std::array<unsigned char, 4> bytes{};
    pattern(bytes, seed);
    return std::bit_cast<unsigned int>(bytes);
}

// Non-owning observations only. Pointer slots never delete pointed-to data.
// Construct only after initialize() has given every previously opaque member
// a defined value; the natural-state owner fixture deliberately does not use it.
struct Snapshot
{
    decltype(ScreenRequestRenderer::software_mode) software_mode{};
    decltype(ScreenRequestRenderer::width) width{};
    decltype(ScreenRequestRenderer::height) height{};
    decltype(ScreenRequestRenderer::pixels_1c) pixels_1c{};
    decltype(ScreenRequestRenderer::window_34) window_34{};
    decltype(ScreenRequestRenderer::draw) draw{};
    decltype(ScreenRequestRenderer::primary_surface) primary_surface{};
    decltype(ScreenRequestRenderer::back_surface) back_surface{};
    decltype(ScreenRequestRenderer::format_44) format_44{};
    decltype(ScreenRequestRenderer::flag_4c) flag_4c{};
    decltype(ScreenRequestRenderer::rectangle_suppressed_4d) rectangle_suppressed_4d{};
    decltype(ScreenRequestRenderer::rectangle_enabled_4e) rectangle_enabled_4e{};
    decltype(ScreenRequestRenderer::rectangle_ready_4f) rectangle_ready_4f{};
    decltype(ScreenRequestRenderer::word_50) word_50{};
    decltype(ScreenRequestRenderer::clip_mode_54) clip_mode_54{};
    decltype(ScreenRequestRenderer::word_58) word_58{};
    decltype(ScreenRequestRenderer::unscaled_clip_5c) unscaled_clip_5c{};
    decltype(ScreenRequestRenderer::clip_flag_5d) clip_flag_5d{};
    decltype(ScreenRequestRenderer::word_60) word_60{};
    decltype(ScreenRequestRenderer::device_context_64) device_context_64{};
    decltype(ScreenRequestRenderer::current_surface) current_surface{};
    decltype(ScreenRequestRenderer::pixels_locked) pixels_locked{};
    decltype(ScreenRequestRenderer::fill_color) fill_color{};
    decltype(ScreenRequestRenderer::dirty_rectangle_count) dirty_rectangle_count{};
    decltype(ScreenRequestRenderer::bitmap_info) bitmap_info{};
    decltype(ScreenRequestRenderer::capture_pending_ac) capture_pending_ac{};
    decltype(ScreenRequestRenderer::buffer_130) buffer_130{};
    decltype(ScreenRequestRenderer::sea_state_134) sea_state_134{};
    decltype(ScreenRequestRenderer::initialized_168) initialized_168{};
    decltype(ScreenRequestRenderer::native_display) native_display{};
#if defined(BEACHLIFE_DECOMP_RENDERER_WORD_SURFACE)
    decltype(ScreenRequestRenderer::native_fullscreen) native_fullscreen{};
    decltype(ScreenRequestRenderer::native_chain) native_chain{};
    decltype(ScreenRequestRenderer::native_primary) native_primary{};
    decltype(ScreenRequestRenderer::native_back) native_back{};
#endif
    std::array<unsigned char, 3> unknown_01{};
    std::array<unsigned char, 4> unknown_20{};
    std::array<unsigned char, 4> display_mode_flags_48{};
    std::array<unsigned char, 2> unknown_5e{};
    std::array<unsigned char, 1> unknown_79{};
    std::array<unsigned char, 131> unknown_ad{};
    std::array<TgaImage*, 6> haze_images_138{};
    std::array<unsigned char, 24> unknown_150{};
    std::array<unsigned char, 3> unknown_169{};
    RenderRectangle clip_rectangle{};
    RendererWindowRectangle window_rectangle_24{};
    std::vector<ScreenSurfacePosition*> surface_storage;
    ScreenSurfacePosition* const* surface_data = nullptr;
    std::size_t surface_capacity = 0;
    RendererRectangleQueue dirty_rectangles;

    Snapshot() = default;
    Snapshot(const ScreenRequestRenderer& r)
    {
        software_mode = r.software_mode;
        width = r.width;
        height = r.height;
        pixels_1c = r.pixels_1c;
        window_34 = r.window_34;
        draw = r.draw;
        primary_surface = r.primary_surface;
        back_surface = r.back_surface;
        format_44 = r.format_44;
        flag_4c = r.flag_4c;
        rectangle_suppressed_4d = r.rectangle_suppressed_4d;
        rectangle_enabled_4e = r.rectangle_enabled_4e;
        rectangle_ready_4f = r.rectangle_ready_4f;
        word_50 = r.word_50;
        clip_mode_54 = r.clip_mode_54;
        word_58 = r.word_58;
        unscaled_clip_5c = r.unscaled_clip_5c;
        clip_flag_5d = r.clip_flag_5d;
        word_60 = r.word_60;
        device_context_64 = r.device_context_64;
        current_surface = r.current_surface;
        pixels_locked = r.pixels_locked;
        fill_color = r.fill_color;
        dirty_rectangle_count = r.dirty_rectangle_count;
        bitmap_info = r.bitmap_info;
        capture_pending_ac = r.capture_pending_ac;
        buffer_130 = r.buffer_130;
        sea_state_134 = r.sea_state_134;
        initialized_168 = r.initialized_168;
        native_display = r.native_display;
#if defined(BEACHLIFE_DECOMP_RENDERER_WORD_SURFACE)
        native_fullscreen = r.native_fullscreen;
        native_chain = r.native_chain;
        native_primary = r.native_primary;
        native_back = r.native_back;
#endif
        std::copy(std::begin(r.unknown_01), std::end(r.unknown_01), unknown_01.begin());
        std::copy(std::begin(r.unknown_20), std::end(r.unknown_20), unknown_20.begin());
        std::copy(std::begin(r.display_mode_flags_48),
            std::end(r.display_mode_flags_48),
            display_mode_flags_48.begin());
        std::copy(std::begin(r.unknown_5e), std::end(r.unknown_5e), unknown_5e.begin());
        std::copy(std::begin(r.unknown_79), std::end(r.unknown_79), unknown_79.begin());
        std::copy(std::begin(r.unknown_ad), std::end(r.unknown_ad), unknown_ad.begin());
        std::copy(
            std::begin(r.haze_images_138), std::end(r.haze_images_138), haze_images_138.begin());
        std::copy(std::begin(r.unknown_150), std::end(r.unknown_150), unknown_150.begin());
        std::copy(std::begin(r.unknown_169), std::end(r.unknown_169), unknown_169.begin());
        clip_rectangle = r.clip_rectangle;
        window_rectangle_24 = r.window_rectangle_24;
        surface_storage = r.surface_storage;
        surface_data = r.surface_storage.data();
        surface_capacity = r.surface_storage.capacity();
        dirty_rectangles.values = r.dirty_rectangles.values;
    }
};

// Guard values and observations only; never an owning renderer copy.
template <class Guard> struct FrameSnapshot
{
    Guard before;
    Snapshot value;
    Guard after;
    template <class Frame>
    explicit FrameSnapshot(const Frame& frame)
        : before(frame.before)
        , value(frame.value)
        , after(frame.after)
    {
    }
};

inline bool equal(const ScreenRequestRenderer& a, const Snapshot& b)
{
#if defined(BEACHLIFE_DECOMP_RENDERER_WORD_SURFACE)
    if (a.native_fullscreen != b.native_fullscreen || a.native_chain != b.native_chain ||
        a.native_primary != b.native_primary || a.native_back != b.native_back)
    {
        return false;
    }
#endif
    if (a.software_mode != b.software_mode || a.width != b.width || a.height != b.height ||
        a.pixels_1c != b.pixels_1c || a.window_34 != b.window_34 || a.draw != b.draw ||
        a.primary_surface != b.primary_surface || a.back_surface != b.back_surface ||
        a.format_44 != b.format_44 || a.flag_4c != b.flag_4c ||
        a.rectangle_suppressed_4d != b.rectangle_suppressed_4d ||
        a.rectangle_enabled_4e != b.rectangle_enabled_4e ||
        a.rectangle_ready_4f != b.rectangle_ready_4f || a.word_50 != b.word_50 ||
        a.clip_mode_54 != b.clip_mode_54 || a.word_58 != b.word_58 ||
        a.unscaled_clip_5c != b.unscaled_clip_5c || a.clip_flag_5d != b.clip_flag_5d ||
        a.word_60 != b.word_60 || a.device_context_64 != b.device_context_64 ||
        a.current_surface != b.current_surface || a.pixels_locked != b.pixels_locked ||
        a.fill_color != b.fill_color || a.dirty_rectangle_count != b.dirty_rectangle_count ||
        a.bitmap_info != b.bitmap_info || a.capture_pending_ac != b.capture_pending_ac ||
        a.buffer_130 != b.buffer_130 || a.sea_state_134 != b.sea_state_134 ||
        a.initialized_168 != b.initialized_168 || a.native_display != b.native_display ||
        !same_bytes(a.unknown_01, b.unknown_01) || !same_bytes(a.unknown_20, b.unknown_20) ||
        !same_bytes(a.display_mode_flags_48, b.display_mode_flags_48) ||
        !same_bytes(a.unknown_5e, b.unknown_5e) || !same_bytes(a.unknown_79, b.unknown_79) ||
        !same_bytes(a.unknown_ad, b.unknown_ad) ||
        !same_bytes(a.haze_images_138, b.haze_images_138) ||
        !same_bytes(a.unknown_150, b.unknown_150) || !same_bytes(a.unknown_169, b.unknown_169) ||
        a.clip_rectangle.x != b.clip_rectangle.x || a.clip_rectangle.y != b.clip_rectangle.y ||
        a.clip_rectangle.width != b.clip_rectangle.width ||
        a.clip_rectangle.height != b.clip_rectangle.height ||
        a.window_rectangle_24.left != b.window_rectangle_24.left ||
        a.window_rectangle_24.top != b.window_rectangle_24.top ||
        a.window_rectangle_24.right != b.window_rectangle_24.right ||
        a.window_rectangle_24.bottom != b.window_rectangle_24.bottom ||
        a.surface_storage != b.surface_storage || a.surface_storage.data() != b.surface_data ||
        a.surface_storage.capacity() != b.surface_capacity ||
        a.dirty_rectangles.values.size() != b.dirty_rectangles.values.size())
    {
        return false;
    }
    return std::equal(a.dirty_rectangles.values.begin(),
        a.dirty_rectangles.values.end(),
        b.dirty_rectangles.values.begin(),
        same_rectangle);
}

// Logical observed members/values, never ABI padding or vector/deque internals.
template <class State> std::size_t compared_bytes(const State& r)
{
    return
#if defined(BEACHLIFE_DECOMP_RENDERER_WORD_SURFACE)
        sizeof(r.native_fullscreen) + sizeof(r.native_chain) + sizeof(r.native_primary) +
        sizeof(r.native_back) +
#endif
        sizeof(r.software_mode) + sizeof(r.width) + sizeof(r.height) + sizeof(r.pixels_1c) +
        sizeof(r.window_34) + sizeof(r.draw) + sizeof(r.primary_surface) + sizeof(r.back_surface) +
        sizeof(r.format_44) + sizeof(r.flag_4c) + sizeof(r.rectangle_suppressed_4d) +
        sizeof(r.rectangle_enabled_4e) + sizeof(r.rectangle_ready_4f) + sizeof(r.word_50) +
        sizeof(r.clip_mode_54) + sizeof(r.word_58) + sizeof(r.unscaled_clip_5c) +
        sizeof(r.clip_flag_5d) + sizeof(r.word_60) + sizeof(r.device_context_64) +
        sizeof(r.current_surface) + sizeof(r.pixels_locked) + sizeof(r.fill_color) +
        sizeof(r.dirty_rectangle_count) + sizeof(r.bitmap_info) + sizeof(r.capture_pending_ac) +
        sizeof(r.buffer_130) + sizeof(r.sea_state_134) + sizeof(r.initialized_168) +
        sizeof(r.native_display) + sizeof(r.unknown_01) + sizeof(r.unknown_20) +
        sizeof(r.display_mode_flags_48) + sizeof(r.unknown_5e) + sizeof(r.unknown_79) +
        sizeof(r.unknown_ad) + sizeof(r.haze_images_138) + sizeof(r.unknown_150) +
        sizeof(r.unknown_169) + sizeof(r.clip_rectangle) + sizeof(r.window_rectangle_24) +
        r.surface_storage.size() * sizeof(ScreenSurfacePosition*) + sizeof(void*) +
        sizeof(std::size_t) + r.dirty_rectangles.values.size() * sizeof(ScreenRectangle);
}

// Only for scoped method views with RestoreOwnedState already active. No whole
// object pattern, owning copy or mutation of the preserved genuine allocations.
inline void initialize(ScreenRequestRenderer& r, unsigned int seed)
{
    r.software_mode = static_cast<unsigned char>(seed);
    pattern(r.unknown_01, seed + 1U);
    r.width = 800;
    r.height = 600;
    r.clip_rectangle = {-13, 17, 37, 41};
    r.pixels_1c = nullptr;
    pattern(r.unknown_20, seed + 2U);
    r.window_rectangle_24 = {-9, -7, 23, 31};
    r.window_34 = nullptr;
    r.draw = nullptr;
    r.primary_surface = nullptr;
    r.back_surface = nullptr;
    r.format_44 = 0x12345678U;
    pattern(r.display_mode_flags_48, seed + 3U);
    r.flag_4c = 0x34;
    r.rectangle_suppressed_4d = 0x45;
    r.rectangle_enabled_4e = 0x56;
    r.rectangle_ready_4f = 0x67;
    r.word_50 = patterned_word(seed + 4U);
    r.clip_mode_54 = -29;
    r.word_58 = patterned_word(seed + 5U);
    r.unscaled_clip_5c = 0x9a;
    r.clip_flag_5d = 0xab;
    pattern(r.unknown_5e, seed + 6U);
    r.word_60 = patterned_word(seed + 6U + 2U * 37U);
    r.device_context_64 = nullptr;
    r.surface_storage.clear();
    r.current_surface = -37;
    r.pixels_locked = 0xde;
    pattern(r.unknown_79, seed + 8U);
    r.fill_color = std::bit_cast<unsigned short>(std::array<unsigned char, 2>{
        static_cast<unsigned char>(seed + 8U + 37U), static_cast<unsigned char>(seed + 8U + 74U)});
    r.dirty_rectangles.values = {{-7, -3, -2, 1}, {13, 17, 19, 23}};
    r.dirty_rectangle_count = 2;
    r.bitmap_info = nullptr;
    r.capture_pending_ac = 0;
    pattern(r.unknown_ad, seed + 9U);
    r.buffer_130 = nullptr;
    r.sea_state_134 = nullptr;
    std::fill(std::begin(r.haze_images_138), std::end(r.haze_images_138), nullptr);
    pattern(r.unknown_150, seed + 10U);
    r.initialized_168 = 0x7b;
    pattern(r.unknown_169, seed + 11U);
    r.native_display = nullptr;
#if defined(BEACHLIFE_DECOMP_RENDERER_WORD_SURFACE)
    r.native_fullscreen = nullptr;
    r.native_chain = nullptr;
    r.native_primary = nullptr;
    r.native_back = nullptr;
#endif
}

inline bool same_entry_extension(const ScreenSurfacePosition& a, const ScreenSurfacePosition& b)
{
    return
#if defined(BEACHLIFE_DECOMP_RENDERER_WORD_SURFACE)
        a.native_surface == b.native_surface && a.native_surface_owned == b.native_surface_owned &&
#endif
        a.video_memory == b.video_memory && same_bytes(a.unknown_0d, b.unknown_0d);
}

inline void initialize_entry_extension(ScreenSurfacePosition& entry, unsigned int seed)
{
#if defined(BEACHLIFE_DECOMP_RENDERER_WORD_SURFACE)
    entry.native_surface = nullptr;
    entry.native_surface_owned = false;
#endif
    entry.video_memory = static_cast<unsigned char>(seed);
    pattern(entry.unknown_0d, seed + 37U);
}
} // namespace renderer_queue_test
