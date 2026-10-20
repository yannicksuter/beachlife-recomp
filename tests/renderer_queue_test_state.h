#pragma once

#include "startup_screen_renderer.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <iterator>
#include <type_traits>

// Test-only typed state support for the experimental renderer declaration.
// This header supplies no reconstructed method, allocator or retail lifetime.
namespace renderer_queue_test
{
static_assert(!std::is_trivially_copyable_v<ScreenRequestRenderer>);

inline bool same_rectangle(const ScreenRectangle& a, const ScreenRectangle& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

template <std::size_t N> bool same_bytes(const unsigned char (&a)[N], const unsigned char (&b)[N])
{
    return std::equal(std::begin(a), std::end(a), std::begin(b));
}

inline bool equal(const ScreenRequestRenderer& a, const ScreenRequestRenderer& b)
{
#if defined(BEACHLIFE_DECOMP_SURFACE_PREPARE)
    if (a.fill_color != b.fill_color)
    {
        return false;
    }
#endif
#if defined(BEACHLIFE_DECOMP_RENDERER_MARKER_BUFFER)
    if (!same_bytes(a.unknown_a8, b.unknown_a8) || a.buffer_130 != b.buffer_130)
    {
        return false;
    }
#endif
    if (!(a.software_mode == b.software_mode && same_bytes(a.unknown_01, b.unknown_01) &&
            a.width == b.width && a.height == b.height &&
            a.clip_rectangle.x == b.clip_rectangle.x && a.clip_rectangle.y == b.clip_rectangle.y &&
            a.clip_rectangle.width == b.clip_rectangle.width &&
            a.clip_rectangle.height == b.clip_rectangle.height && a.pixels_1c == b.pixels_1c &&
            same_bytes(a.unknown_20, b.unknown_20) && a.format_44 == b.format_44 &&
            same_bytes(a.unknown_48, b.unknown_48) &&
            a.rectangle_suppressed_4d == b.rectangle_suppressed_4d &&
            a.rectangle_enabled_4e == b.rectangle_enabled_4e &&
            a.rectangle_ready_4f == b.rectangle_ready_4f &&
            same_bytes(a.unknown_50, b.unknown_50) && a.clip_mode_54 == b.clip_mode_54 &&
            same_bytes(a.unknown_58, b.unknown_58) && a.unscaled_clip_5c == b.unscaled_clip_5c &&
            a.clip_flag_5d == b.clip_flag_5d && same_bytes(a.unknown_5e, b.unknown_5e) &&
            a.word_60 == b.word_60 && same_bytes(a.unknown_64, b.unknown_64) &&
            a.surfaces == b.surfaces && same_bytes(a.unknown_6c, b.unknown_6c) &&
            a.current_surface == b.current_surface && a.pixels_locked == b.pixels_locked &&
            same_bytes(a.unknown_79, b.unknown_79) &&
            a.dirty_rectangle_count == b.dirty_rectangle_count &&
            a.dirty_rectangles.values.size() == b.dirty_rectangles.values.size()))
    {
        return false;
    }
    return std::equal(a.dirty_rectangles.values.begin(),
        a.dirty_rectangles.values.end(),
        b.dirty_rectangles.values.begin(),
        same_rectangle);
}

// Logical bytes represented by explicit member/value comparisons. Neither
// renderer padding nor host deque internals contribute to fixture counters.
inline std::size_t compared_bytes(const ScreenRequestRenderer& r)
{
    std::size_t tail_bytes = 0;
#if defined(BEACHLIFE_DECOMP_SURFACE_PREPARE)
    tail_bytes += sizeof(r.fill_color);
#endif
#if defined(BEACHLIFE_DECOMP_RENDERER_MARKER_BUFFER)
    tail_bytes += sizeof(r.unknown_a8) + sizeof(r.buffer_130);
#endif
    return sizeof(r.software_mode) + sizeof(r.unknown_01) + sizeof(r.width) + sizeof(r.height) +
           sizeof(r.clip_rectangle.x) + sizeof(r.clip_rectangle.y) +
           sizeof(r.clip_rectangle.width) + sizeof(r.clip_rectangle.height) + sizeof(r.pixels_1c) +
           sizeof(r.unknown_20) + sizeof(r.format_44) + sizeof(r.unknown_48) +
           sizeof(r.rectangle_suppressed_4d) + sizeof(r.rectangle_enabled_4e) +
           sizeof(r.rectangle_ready_4f) + sizeof(r.unknown_50) + sizeof(r.clip_mode_54) +
           sizeof(r.unknown_58) + sizeof(r.unscaled_clip_5c) + sizeof(r.clip_flag_5d) +
           sizeof(r.unknown_5e) + sizeof(r.word_60) + sizeof(r.unknown_64) + sizeof(r.surfaces) +
           sizeof(r.unknown_6c) + sizeof(r.current_surface) + sizeof(r.pixels_locked) +
           sizeof(r.unknown_79) + sizeof(r.dirty_rectangle_count) +
           r.dirty_rectangles.values.size() * sizeof(ScreenRectangle) + tail_bytes;
}

template <std::size_t N> void pattern(unsigned char (&bytes)[N], unsigned int seed)
{
    for (std::size_t n = 0; n < N; ++n)
    {
        bytes[n] = static_cast<unsigned char>(seed + n * 37U);
    }
}

inline void initialize(ScreenRequestRenderer& r, unsigned int seed)
{
    // Every pointer and scalar has a typed value. Only actual opaque arrays are
    // patterned; the real deque has already been constructed by its owner.
    r.software_mode = static_cast<unsigned char>(seed);
    pattern(r.unknown_01, seed + 1U);
    r.width = 800;
    r.height = 600;
    r.clip_rectangle = {-13, 17, 37, 41};
    r.pixels_1c = nullptr;
    pattern(r.unknown_20, seed + 2U);
    r.format_44 = 0x12345678U;
    pattern(r.unknown_48, seed + 3U);
    r.rectangle_suppressed_4d = 0x45;
    r.rectangle_enabled_4e = 0x56;
    r.rectangle_ready_4f = 0x67;
    pattern(r.unknown_50, seed + 4U);
    r.clip_mode_54 = -29;
    pattern(r.unknown_58, seed + 5U);
    r.unscaled_clip_5c = 0x9a;
    r.clip_flag_5d = 0xab;
    pattern(r.unknown_5e, seed + 6U);
    // Preserve the old ten-byte canary using an actual unsigned scalar.
    std::array<unsigned char, 4> word_bytes{};
    for (std::size_t n = 0; n < word_bytes.size(); ++n)
    {
        word_bytes[n] = static_cast<unsigned char>(seed + 6U + (n + 2U) * 37U);
    }
    r.word_60 = std::bit_cast<unsigned int>(word_bytes);
    pattern(r.unknown_64, seed + 6U + 6U * 37U);
    r.surfaces = nullptr;
    pattern(r.unknown_6c, seed + 7U);
    r.current_surface = -37;
    r.pixels_locked = 0xde;
    pattern(r.unknown_79, seed + 8U);
#if defined(BEACHLIFE_DECOMP_SURFACE_PREPARE)
    r.fill_color = std::bit_cast<unsigned short>(std::array<unsigned char, 2>{
        static_cast<unsigned char>(seed + 8U + 37U), static_cast<unsigned char>(seed + 8U + 74U)});
#endif
    r.dirty_rectangles.values = {{-7, -3, -2, 1}, {13, 17, 19, 23}};
    r.dirty_rectangle_count = 2;
#if defined(BEACHLIFE_DECOMP_RENDERER_MARKER_BUFFER)
    pattern(r.unknown_a8, seed + 9U);
    r.buffer_130 = nullptr;
#endif
}
} // namespace renderer_queue_test
