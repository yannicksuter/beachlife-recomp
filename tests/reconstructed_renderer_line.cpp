#include "renderer_queue_test_state.h"
#include "startup_renderer_line_raster.h"
#include "startup_screen_frame_draw.h"
#include "startup_screen_item.h"
#include "startup_screen_renderer.h"

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(sizeof(int) == 4 && INT_MAX == 2147483647 && sizeof(SurfaceResult) == 4);
static_assert(std::endian::native == std::endian::little);
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Lock) == 25 * sizeof(void*));
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Unlock) == 32 * sizeof(void*));

namespace
{
enum class Kernel
{
    DirectX,
    DirectY,
    PackedX,
    PackedY
};
struct Point
{
    int x, y;
};
struct ColourPoint
{
    int x, y;
    unsigned short colour;
};
struct RasterCase
{
    Kernel kind;
    const char* name;
    std::array<int, 5> arguments;
    unsigned int colour, width, height, stride;
    std::vector<Point> points;
};
struct LineCase
{
    const char* name;
    std::array<int, 4> arguments;
    unsigned int colour;
    unsigned char unscaled, software;
    RenderRectangle clip;
    unsigned int width, height, pitch;
    ScreenPoint position;
    unsigned char initial_locked, expected_locked;
    bool access;
    std::vector<Point> points;
};
struct StyleCase
{
    const char* name;
    ScreenRectangle input;
    unsigned int format;
    std::vector<ColourPoint> points;
};
struct LiveStyleCase
{
    const char* name;
    unsigned int initial_format;
    ScreenRectangle after_first;
    unsigned int first_format;
    ScreenRectangle after_second;
    unsigned int second_format;
    std::array<std::vector<ColourPoint>, 3> stages;
};

// Fixed expected points, expanded from the accepted literal ledgers.
// No reference raster implementation runs in the native fixture.
const std::array<RasterCase, 72> raster_cases{{
    {Kernel::DirectX,
        "tie_positive",
        {2, 2, 2, 1, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{2, 2}, {3, 3}, {4, 3}}},
    {Kernel::DirectX,
        "tie_negative",
        {6, 2, 2, 1, -1},
        3405665912u,
        10u,
        10u,
        12u,
        {{6, 2}, {5, 3}, {4, 3}}},
    {Kernel::DirectX,
        "horizontal",
        {2, 2, 4, 0, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}}},
    {Kernel::DirectX,
        "shallow",
        {1, 1, 5, 2, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{1, 1}, {2, 1}, {3, 2}, {4, 2}, {5, 3}, {6, 3}}},
    {Kernel::DirectX,
        "equal_axes",
        {5, 1, 3, 3, -1},
        3405665912u,
        10u,
        10u,
        12u,
        {{5, 1}, {4, 2}, {3, 3}, {2, 4}}},
    {Kernel::DirectX, "point", {3, 3, 0, 0, -1}, 3405665912u, 10u, 10u, 12u, {{3, 3}}},
    {Kernel::DirectX,
        "zero_major_nonzero_minor",
        {3, 3, 0, 2, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{3, 3}}},
    {Kernel::DirectX,
        "enter_from_left",
        {-2, 2, 5, 1, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{0, 2}, {1, 3}, {2, 3}, {3, 3}}},
    {Kernel::DirectX,
        "enter_from_top",
        {2, -1, 4, 2, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{3, 0}, {4, 0}, {5, 1}, {6, 1}}},
    {Kernel::DirectX, "fully_clipped", {12, 2, 3, 1, 1}, 3405665912u, 10u, 10u, 12u, {}},
    {Kernel::DirectX,
        "cross_right_edge",
        {6, 2, 4, 0, 1},
        3405665912u,
        8u,
        8u,
        12u,
        {{6, 2}, {7, 2}}},
    {Kernel::DirectX,
        "start_on_last_row",
        {2, 7, 3, 0, 1},
        3405665912u,
        8u,
        8u,
        12u,
        {{2, 7}, {3, 7}, {4, 7}, {5, 7}}},
    {Kernel::DirectX,
        "last_column_moves_inward",
        {7, 2, 3, 0, -1},
        3405665912u,
        8u,
        8u,
        12u,
        {{7, 2}, {6, 2}, {5, 2}, {4, 2}}},
    {Kernel::DirectX, "zero_width", {2, 2, 3, 0, 1}, 3405665912u, 0u, 8u, 12u, {}},
    {Kernel::DirectX, "zero_height", {2, 2, 3, 0, 1}, 3405665912u, 8u, 0u, 12u, {}},
    {Kernel::DirectX, "zero_bounds", {2, 2, 3, 0, 1}, 3405665912u, 0u, 0u, 12u, {}},
    {Kernel::DirectX, "zero_low_colour", {3, 3, 0, 0, -1}, 4294901760u, 10u, 10u, 12u, {{3, 3}}},
    {Kernel::DirectX, "all_low_colour", {3, 3, 0, 0, -1}, 4294967295u, 10u, 10u, 12u, {{3, 3}}},
    {Kernel::DirectY,
        "tie_positive",
        {2, 2, 1, 2, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{2, 2}, {3, 3}, {3, 4}}},
    {Kernel::DirectY,
        "tie_negative",
        {6, 2, 1, 2, -1},
        3405665912u,
        10u,
        10u,
        12u,
        {{6, 2}, {5, 3}, {5, 4}}},
    {Kernel::DirectY,
        "vertical",
        {3, 2, 0, 4, -1},
        3405665912u,
        10u,
        10u,
        12u,
        {{3, 2}, {3, 3}, {3, 4}, {3, 5}, {3, 6}}},
    {Kernel::DirectY,
        "steep",
        {1, 1, 2, 5, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{1, 1}, {1, 2}, {2, 3}, {2, 4}, {3, 5}, {3, 6}}},
    {Kernel::DirectY,
        "equal_axes",
        {5, 1, 3, 3, -1},
        3405665912u,
        10u,
        10u,
        12u,
        {{5, 1}, {4, 2}, {3, 3}, {2, 4}}},
    {Kernel::DirectY, "point", {3, 3, 0, 0, -1}, 3405665912u, 10u, 10u, 12u, {{3, 3}}},
    {Kernel::DirectY,
        "zero_major_nonzero_minor",
        {3, 3, 2, 0, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{3, 3}}},
    {Kernel::DirectY,
        "enter_from_left",
        {-1, 2, 2, 4, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{0, 3}, {0, 4}, {1, 5}, {1, 6}}},
    {Kernel::DirectY,
        "enter_from_top",
        {2, -2, 1, 5, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{2, 0}, {3, 1}, {3, 2}, {3, 3}}},
    {Kernel::DirectY, "fully_clipped", {12, 2, 1, 3, 1}, 3405665912u, 10u, 10u, 12u, {}},
    {Kernel::DirectY,
        "cross_right_edge",
        {6, 2, 2, 4, 1},
        3405665912u,
        8u,
        8u,
        12u,
        {{6, 2}, {7, 3}, {7, 4}}},
    {Kernel::DirectY, "start_on_last_row", {2, 7, 0, 3, -1}, 3405665912u, 8u, 8u, 12u, {{2, 7}}},
    {Kernel::DirectY,
        "last_column_moves_inward",
        {7, 2, 2, 4, -1},
        3405665912u,
        8u,
        8u,
        12u,
        {{7, 2}, {6, 3}, {6, 4}, {5, 5}, {5, 6}}},
    {Kernel::DirectY, "zero_width", {2, 2, 0, 3, -1}, 3405665912u, 0u, 8u, 12u, {}},
    {Kernel::DirectY, "zero_height", {2, 2, 0, 3, -1}, 3405665912u, 8u, 0u, 12u, {}},
    {Kernel::DirectY, "zero_bounds", {2, 2, 0, 3, -1}, 3405665912u, 0u, 0u, 12u, {}},
    {Kernel::DirectY, "zero_low_colour", {3, 3, 0, 0, -1}, 4294901760u, 10u, 10u, 12u, {{3, 3}}},
    {Kernel::DirectY, "all_low_colour", {3, 3, 0, 0, -1}, 4294967295u, 10u, 10u, 12u, {{3, 3}}},
    {Kernel::PackedX,
        "tie_positive",
        {2, 2, 2, 1, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{2, 2}, {3, 3}, {4, 3}}},
    {Kernel::PackedX,
        "tie_negative",
        {6, 2, 2, 1, -1},
        3405665912u,
        10u,
        10u,
        12u,
        {{6, 2}, {5, 3}, {4, 3}}},
    {Kernel::PackedX,
        "horizontal",
        {2, 2, 4, 0, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}}},
    {Kernel::PackedX,
        "shallow",
        {1, 1, 5, 2, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{1, 1}, {2, 1}, {3, 2}, {4, 2}, {5, 3}, {6, 3}}},
    {Kernel::PackedX,
        "equal_axes",
        {5, 1, 3, 3, -1},
        3405665912u,
        10u,
        10u,
        12u,
        {{5, 1}, {4, 2}, {3, 3}, {2, 4}}},
    {Kernel::PackedX, "point", {3, 3, 0, 0, -1}, 3405665912u, 10u, 10u, 12u, {{3, 3}}},
    {Kernel::PackedX,
        "zero_major_nonzero_minor",
        {3, 3, 0, 2, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{3, 3}}},
    {Kernel::PackedX,
        "enter_from_left",
        {-2, 2, 5, 1, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{0, 2}, {1, 3}, {2, 3}, {3, 3}}},
    {Kernel::PackedX,
        "enter_from_top",
        {2, -1, 4, 2, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{3, 0}, {4, 0}, {5, 1}, {6, 1}}},
    {Kernel::PackedX, "fully_clipped", {12, 2, 3, 1, 1}, 3405665912u, 10u, 10u, 12u, {}},
    {Kernel::PackedX, "cross_right_edge", {6, 2, 4, 0, 1}, 3405665912u, 8u, 8u, 12u, {{6, 2}}},
    {Kernel::PackedX, "start_on_last_row", {2, 7, 3, 0, 1}, 3405665912u, 8u, 8u, 12u, {{2, 7}}},
    {Kernel::PackedX,
        "last_column_moves_inward",
        {7, 2, 3, 0, -1},
        3405665912u,
        8u,
        8u,
        12u,
        {{7, 2}, {6, 2}, {5, 2}, {4, 2}}},
    {Kernel::PackedX,
        "zero_width",
        {2, 2, 3, 0, 1},
        3405665912u,
        0u,
        8u,
        12u,
        {{3, 2}, {4, 2}, {5, 2}}},
    {Kernel::PackedX,
        "zero_height",
        {2, 2, 3, 0, 1},
        3405665912u,
        8u,
        0u,
        12u,
        {{3, 2}, {4, 2}, {5, 2}}},
    {Kernel::PackedX,
        "zero_bounds",
        {2, 2, 3, 0, 1},
        3405665912u,
        0u,
        0u,
        12u,
        {{3, 2}, {4, 2}, {5, 2}}},
    {Kernel::PackedX, "zero_low_colour", {3, 3, 0, 0, -1}, 4294901760u, 10u, 10u, 12u, {{3, 3}}},
    {Kernel::PackedX, "all_low_colour", {3, 3, 0, 0, -1}, 4294967295u, 10u, 10u, 12u, {{3, 3}}},
    {Kernel::PackedY,
        "tie_positive",
        {2, 2, 1, 2, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{2, 2}, {3, 3}, {3, 4}}},
    {Kernel::PackedY,
        "tie_negative",
        {6, 2, 1, 2, -1},
        3405665912u,
        10u,
        10u,
        12u,
        {{6, 2}, {5, 3}, {5, 4}}},
    {Kernel::PackedY,
        "vertical",
        {3, 2, 0, 4, -1},
        3405665912u,
        10u,
        10u,
        12u,
        {{3, 2}, {3, 3}, {3, 4}, {3, 5}, {3, 6}}},
    {Kernel::PackedY,
        "steep",
        {1, 1, 2, 5, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{1, 1}, {1, 2}, {2, 3}, {2, 4}, {3, 5}, {3, 6}}},
    {Kernel::PackedY,
        "equal_axes",
        {5, 1, 3, 3, -1},
        3405665912u,
        10u,
        10u,
        12u,
        {{5, 1}, {4, 2}, {3, 3}, {2, 4}}},
    {Kernel::PackedY, "point", {3, 3, 0, 0, -1}, 3405665912u, 10u, 10u, 12u, {{3, 3}}},
    {Kernel::PackedY,
        "zero_major_nonzero_minor",
        {3, 3, 2, 0, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{3, 3}}},
    {Kernel::PackedY,
        "enter_from_left",
        {-1, 2, 2, 4, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{0, 3}, {0, 4}, {1, 5}, {1, 6}}},
    {Kernel::PackedY,
        "enter_from_top",
        {2, -2, 1, 5, 1},
        3405665912u,
        10u,
        10u,
        12u,
        {{2, 0}, {3, 1}, {3, 2}, {3, 3}}},
    {Kernel::PackedY, "fully_clipped", {12, 2, 1, 3, 1}, 3405665912u, 10u, 10u, 12u, {}},
    {Kernel::PackedY,
        "cross_right_edge",
        {6, 2, 2, 4, 1},
        3405665912u,
        8u,
        8u,
        12u,
        {{6, 2}, {7, 3}, {7, 4}}},
    {Kernel::PackedY, "start_on_last_row", {2, 7, 0, 3, -1}, 3405665912u, 8u, 8u, 12u, {{2, 7}}},
    {Kernel::PackedY,
        "last_column_moves_inward",
        {7, 2, 2, 4, -1},
        3405665912u,
        8u,
        8u,
        12u,
        {{7, 2}, {6, 3}, {6, 4}, {5, 5}, {5, 6}}},
    {Kernel::PackedY, "zero_width", {2, 2, 0, 3, -1}, 3405665912u, 0u, 8u, 12u, {}},
    {Kernel::PackedY, "zero_height", {2, 2, 0, 3, -1}, 3405665912u, 8u, 0u, 12u, {}},
    {Kernel::PackedY, "zero_bounds", {2, 2, 0, 3, -1}, 3405665912u, 0u, 0u, 12u, {}},
    {Kernel::PackedY, "zero_low_colour", {3, 3, 0, 0, -1}, 4294901760u, 10u, 10u, 12u, {{3, 3}}},
    {Kernel::PackedY, "all_low_colour", {3, 3, 0, 0, -1}, 4294967295u, 10u, 10u, 12u, {{3, 3}}},
}};

const std::array<LineCase, 23> line_cases{{
    {"packed_control_case",
        {2, 3, 8, 4},
        305419896u,
        128,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        0u,
        true,
        {{2, 3}, {3, 3}, {4, 3}, {5, 4}, {6, 4}, {7, 4}, {8, 4}}},
    {"direct_positive_x",
        {2, 3, 8, 4},
        305419896u,
        0,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {{2, 3}, {3, 3}, {4, 3}, {5, 4}, {6, 4}, {7, 4}, {8, 4}}},
    {"direct_positive_y",
        {2, 3, 3, 9},
        305419896u,
        0,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {{2, 3}, {2, 4}, {2, 5}, {3, 6}, {3, 7}, {3, 8}, {3, 9}}},
    {"direct_equal_axes",
        {2, 3, 7, 8},
        305419896u,
        0,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {{2, 3}, {3, 4}, {4, 5}, {5, 6}, {6, 7}, {7, 8}}},
    {"direct_negative_x",
        {12, 3, 4, 5},
        305419896u,
        0,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {{12, 3}, {11, 3}, {10, 4}, {9, 4}, {8, 4}, {7, 4}, {6, 5}, {5, 5}, {4, 5}}},
    {"direct_negative_y",
        {12, 3, 10, 9},
        305419896u,
        0,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {{12, 3}, {12, 4}, {11, 5}, {11, 6}, {11, 7}, {10, 8}, {10, 9}}},
    {"direct_vertical",
        {5, 2, 5, 8},
        305419896u,
        0,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {{5, 2}, {5, 3}, {5, 4}, {5, 5}, {5, 6}, {5, 7}, {5, 8}}},
    {"direct_point",
        {5, 5, 5, 5},
        305419896u,
        0,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {{5, 5}}},
    {"direct_swap_both",
        {4, 12, 10, 3},
        305419896u,
        0,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {{10, 3}, {9, 4}, {9, 5}, {8, 6}, {7, 7}, {7, 8}, {6, 9}, {5, 10}, {5, 11}, {4, 12}}},
    {"packed_positive_y",
        {3, 4, 5, 10},
        305419896u,
        1,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        0u,
        true,
        {{3, 4}, {3, 5}, {4, 6}, {4, 7}, {4, 8}, {5, 9}, {5, 10}}},
    {"packed_negative_x",
        {12, 3, 4, 5},
        305419896u,
        1,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        0u,
        true,
        {{12, 3}, {11, 3}, {10, 4}, {9, 4}, {8, 4}, {7, 4}, {6, 5}, {5, 5}, {4, 5}}},
    {"packed_negative_y",
        {12, 3, 10, 9},
        305419896u,
        1,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        0u,
        true,
        {{12, 3}, {12, 4}, {11, 5}, {11, 6}, {11, 7}, {10, 8}, {10, 9}}},
    {"packed_equal_axes",
        {2, 3, 7, 8},
        305419896u,
        255,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        0u,
        true,
        {{2, 3}, {3, 4}, {4, 5}, {5, 6}, {6, 7}, {7, 8}}},
    {"negative_start_not_clamped",
        {-2, -3, 5, 2},
        305419896u,
        0,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {{2, 0}, {3, 1}, {4, 1}, {5, 2}}},
    {"end_x_past_clip_not_clamped",
        {2, 3, 24, 4},
        305419896u,
        0,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {{2, 3},
            {3, 3},
            {4, 3},
            {5, 3},
            {6, 3},
            {7, 3},
            {8, 3},
            {9, 3},
            {10, 3},
            {11, 3},
            {12, 3},
            {13, 4},
            {14, 4},
            {15, 4},
            {16, 4},
            {17, 4},
            {18, 4},
            {19, 4},
            {20, 4},
            {21, 4},
            {22, 4},
            {23, 4},
            {24, 4}}},
    {"end_y_past_clip_not_clamped",
        {2, 3, 4, 24},
        305419896u,
        0,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {{2, 3},
            {2, 4},
            {2, 5},
            {2, 6},
            {2, 7},
            {2, 8},
            {3, 9},
            {3, 10},
            {3, 11},
            {3, 12},
            {3, 13},
            {3, 14},
            {3, 15},
            {3, 16},
            {3, 17},
            {3, 18},
            {4, 19},
            {4, 20},
            {4, 21},
            {4, 22},
            {4, 23},
            {4, 24}}},
    {"clip_origin_ignored",
        {2, 3, 8, 4},
        305419896u,
        0,
        1,
        {9, 9, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {{2, 3}, {3, 3}, {4, 3}, {5, 4}, {6, 4}, {7, 4}, {8, 4}}},
    {"start_x_at_clip",
        {20, 3, 24, 4},
        305419896u,
        1,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {}},
    {"negative_end_x",
        {2, 3, -1, 4},
        305419896u,
        1,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {}},
    {"start_y_at_clip",
        {2, 20, 8, 21},
        305419896u,
        1,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {}},
    {"end_y_zero",
        {2, 3, 8, 0},
        305419896u,
        1,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {}},
    {"end_y_negative",
        {2, 3, 8, -1},
        305419896u,
        1,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {}},
    {"reject_before_swap",
        {20, 12, 3, 2},
        305419896u,
        1,
        1,
        {0, 0, 20, 20},
        40u,
        40u,
        40u,
        {0, 0},
        165u,
        165u,
        false,
        {}},
}};

const std::array<std::vector<Point>, 5> interface_points{{
    {{3, 4}, {4, 4}, {5, 5}, {6, 5}, {7, 5}, {8, 5}, {9, 6}, {10, 6}, {11, 6}},
    {{3, 4}, {3, 5}, {4, 6}, {4, 7}, {4, 8}, {5, 9}, {5, 10}},
    {},
    {},
    {},
}};

const std::array<StyleCase, 13> style_cases{{
    {"normal_565",
        {4, 6, 14, 16},
        0u,
        {{5, 6, 8727},
            {6, 6, 8727},
            {7, 6, 8727},
            {8, 6, 8727},
            {9, 6, 8727},
            {10, 6, 8727},
            {11, 6, 8727},
            {12, 6, 8727},
            {5, 7, 38303},
            {6, 7, 38303},
            {7, 7, 38303},
            {8, 7, 38303},
            {9, 7, 38303},
            {10, 7, 38303},
            {11, 7, 38303},
            {12, 7, 38303},
            {5, 8, 48799},
            {6, 8, 48799},
            {7, 8, 48799},
            {8, 8, 48799},
            {9, 8, 48799},
            {10, 8, 48799},
            {11, 8, 48799},
            {12, 8, 48799}}},
    {"normal_555",
        {4, 6, 14, 16},
        1u,
        {{5, 6, 4375},
            {6, 6, 4375},
            {7, 6, 4375},
            {8, 6, 4375},
            {9, 6, 4375},
            {10, 6, 4375},
            {11, 6, 4375},
            {12, 6, 4375},
            {5, 7, 19167},
            {6, 7, 19167},
            {7, 7, 19167},
            {8, 7, 19167},
            {9, 7, 19167},
            {10, 7, 19167},
            {11, 7, 19167},
            {12, 7, 19167},
            {5, 8, 24415},
            {6, 8, 24415},
            {7, 8, 24415},
            {8, 8, 24415},
            {9, 8, 24415},
            {10, 8, 24415},
            {11, 8, 24415},
            {12, 8, 24415}}},
    {"format_two",
        {4, 6, 14, 16},
        2u,
        {{5, 6, 4375},
            {6, 6, 4375},
            {7, 6, 4375},
            {8, 6, 4375},
            {9, 6, 4375},
            {10, 6, 4375},
            {11, 6, 4375},
            {12, 6, 4375},
            {5, 7, 19167},
            {6, 7, 19167},
            {7, 7, 19167},
            {8, 7, 19167},
            {9, 7, 19167},
            {10, 7, 19167},
            {11, 7, 19167},
            {12, 7, 19167},
            {5, 8, 24415},
            {6, 8, 24415},
            {7, 8, 24415},
            {8, 8, 24415},
            {9, 8, 24415},
            {10, 8, 24415},
            {11, 8, 24415},
            {12, 8, 24415}}},
    {"format_all_bits",
        {4, 6, 14, 16},
        4294967295u,
        {{5, 6, 4375},
            {6, 6, 4375},
            {7, 6, 4375},
            {8, 6, 4375},
            {9, 6, 4375},
            {10, 6, 4375},
            {11, 6, 4375},
            {12, 6, 4375},
            {5, 7, 19167},
            {6, 7, 19167},
            {7, 7, 19167},
            {8, 7, 19167},
            {9, 7, 19167},
            {10, 7, 19167},
            {11, 7, 19167},
            {12, 7, 19167},
            {5, 8, 24415},
            {6, 8, 24415},
            {7, 8, 24415},
            {8, 8, 24415},
            {9, 8, 24415},
            {10, 8, 24415},
            {11, 8, 24415},
            {12, 8, 24415}}},
    {"bottom_negative",
        {4, 6, 14, -100},
        0u,
        {{5, 6, 8727},
            {6, 6, 8727},
            {7, 6, 8727},
            {8, 6, 8727},
            {9, 6, 8727},
            {10, 6, 8727},
            {11, 6, 8727},
            {12, 6, 8727},
            {5, 7, 38303},
            {6, 7, 38303},
            {7, 7, 38303},
            {8, 7, 38303},
            {9, 7, 38303},
            {10, 7, 38303},
            {11, 7, 38303},
            {12, 7, 38303},
            {5, 8, 48799},
            {6, 8, 48799},
            {7, 8, 48799},
            {8, 8, 48799},
            {9, 8, 48799},
            {10, 8, 48799},
            {11, 8, 48799},
            {12, 8, 48799}}},
    {"zero_height",
        {4, 6, 14, 6},
        0u,
        {{5, 6, 8727},
            {6, 6, 8727},
            {7, 6, 8727},
            {8, 6, 8727},
            {9, 6, 8727},
            {10, 6, 8727},
            {11, 6, 8727},
            {12, 6, 8727},
            {5, 7, 38303},
            {6, 7, 38303},
            {7, 7, 38303},
            {8, 7, 38303},
            {9, 7, 38303},
            {10, 7, 38303},
            {11, 7, 38303},
            {12, 7, 38303},
            {5, 8, 48799},
            {6, 8, 48799},
            {7, 8, 48799},
            {8, 8, 48799},
            {9, 8, 48799},
            {10, 8, 48799},
            {11, 8, 48799},
            {12, 8, 48799}}},
    {"one_cell",
        {4, 6, 5, 7},
        0u,
        {{3, 6, 8727},
            {4, 6, 8727},
            {5, 6, 8727},
            {3, 7, 38303},
            {4, 7, 38303},
            {5, 7, 38303},
            {3, 8, 48799},
            {4, 8, 48799},
            {5, 8, 48799}}},
    {"zero_width",
        {10, 10, 10, 10},
        0u,
        {{8, 10, 8727},
            {9, 10, 8727},
            {10, 10, 8727},
            {11, 10, 8727},
            {8, 11, 38303},
            {9, 11, 38303},
            {10, 11, 38303},
            {11, 11, 38303},
            {8, 12, 48799},
            {9, 12, 48799},
            {10, 12, 48799},
            {11, 12, 48799}}},
    {"inverted_horizontal",
        {14, 6, 4, 16},
        0u,
        {{2, 6, 8727},
            {3, 6, 8727},
            {4, 6, 8727},
            {5, 6, 8727},
            {6, 6, 8727},
            {7, 6, 8727},
            {8, 6, 8727},
            {9, 6, 8727},
            {10, 6, 8727},
            {11, 6, 8727},
            {12, 6, 8727},
            {13, 6, 8727},
            {14, 6, 8727},
            {15, 6, 8727},
            {2, 7, 38303},
            {3, 7, 38303},
            {4, 7, 38303},
            {5, 7, 38303},
            {6, 7, 38303},
            {7, 7, 38303},
            {8, 7, 38303},
            {9, 7, 38303},
            {10, 7, 38303},
            {11, 7, 38303},
            {12, 7, 38303},
            {13, 7, 38303},
            {14, 7, 38303},
            {15, 7, 38303},
            {2, 8, 48799},
            {3, 8, 48799},
            {4, 8, 48799},
            {5, 8, 48799},
            {6, 8, 48799},
            {7, 8, 48799},
            {8, 8, 48799},
            {9, 8, 48799},
            {10, 8, 48799},
            {11, 8, 48799},
            {12, 8, 48799},
            {13, 8, 48799},
            {14, 8, 48799},
            {15, 8, 48799}}},
    {"negative_coordinates", {-3, -5, 5, 7}, 0u, {}},
    {"point_endpoints", {4, 6, 7, 8}, 0u, {{5, 6, 8727}, {5, 7, 38303}, {5, 8, 48799}}},
    {"top_upper_representable", {100, 2147483645, 120, 0}, 0u, {}},
    {"left_lower_representable", {-2147483646, 20, -2147483636, 30}, 0u, {}},
}};

const std::array<LiveStyleCase, 2> live_style_cases{{
    {"input_and_global_replaced_after_first_line",
        0u,
        {100, 200, 300, 400},
        1u,
        {100, 200, 300, 400},
        1u,
        {{{{5, 6, 8727},
              {6, 6, 8727},
              {7, 6, 8727},
              {8, 6, 8727},
              {9, 6, 8727},
              {10, 6, 8727},
              {11, 6, 8727},
              {12, 6, 8727}},
            {{5, 7, 19167},
                {6, 7, 19167},
                {7, 7, 19167},
                {8, 7, 19167},
                {9, 7, 19167},
                {10, 7, 19167},
                {11, 7, 19167},
                {12, 7, 19167}},
            {{5, 8, 24415},
                {6, 8, 24415},
                {7, 8, 24415},
                {8, 8, 24415},
                {9, 8, 24415},
                {10, 8, 24415},
                {11, 8, 24415},
                {12, 8, 24415}}}}},
    {"captured_format_changes_twice",
        1u,
        {-10, -20, -30, -40},
        0u,
        {1, 1, 1, 1},
        2u,
        {{{{5, 6, 4375},
              {6, 6, 4375},
              {7, 6, 4375},
              {8, 6, 4375},
              {9, 6, 4375},
              {10, 6, 4375},
              {11, 6, 4375},
              {12, 6, 4375}},
            {{5, 7, 38303},
                {6, 7, 38303},
                {7, 7, 38303},
                {8, 7, 38303},
                {9, 7, 38303},
                {10, 7, 38303},
                {11, 7, 38303},
                {12, 7, 38303}},
            {{5, 8, 24415},
                {6, 8, 24415},
                {7, 8, 24415},
                {8, 8, 24415},
                {9, 8, 24415},
                {10, 8, 24415},
                {11, 8, 24415},
                {12, 8, 24415}}}}},
}};

template <class T> struct Guarded
{
    std::array<unsigned char, 16> before{};
    T value{};
    std::array<unsigned char, 16> after{};
};
using DirectPixels = std::array<unsigned short, 3200>;
using DensePixels = std::array<unsigned short, 1600>;
using SurfacePointers = std::array<ScreenSurfacePosition*, 3>;
struct Fixture
{
    Guarded<ScreenRequestRenderer> renderer;
    Guarded<ScreenRequestRenderer> other_renderer;
    Guarded<ScreenRectangle> bounds;
    std::array<Guarded<ScreenSurfacePosition>, 3> entries;
    std::array<Guarded<DirectDrawSurface7Prefix>, 3> devices;
    Guarded<SurfacePointers> table;
    Guarded<SurfacePointers> other_table;
    Guarded<DirectPixels> direct;
    Guarded<DensePixels> dense;
    Guarded<DensePixels> other_pixels;
};
struct Snapshot
{
    ScreenRequestRenderer renderer;
    ScreenRequestRenderer other_renderer;
    ScreenRectangle bounds;
    std::array<ScreenSurfacePosition, 3> entries;
    DirectPixels direct;
    DensePixels dense;
    DensePixels other_pixels;
};
enum class Interface
{
    PostSelection,
    ToSoftware,
    NullSuccess,
    Error,
    Style
};
struct Context
{
    Fixture* fixture = nullptr;
    Interface kind = Interface::Error;
    ScreenRectangle* input = nullptr;
    const LiveStyleCase* style = nullptr;
    DensePixels initial{};
    std::array<DensePixels, 3> expected_after{};
    std::size_t count = 0, locks = 0, unlocks = 0;
};
Context* active;
const char* domain = "initial";
std::size_t current_case;
std::size_t checks, logical_bytes, raw_calls, line_calls, style_calls, locks, unlocks;

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "%s case %zu: %s\n", domain, current_case, message);
        std::exit(2);
    }
}

template <class T> void initialize_guard(Guarded<T>& x)
{
    x.before.fill(0x3c);
    x.after.fill(0xc3);
}

template <class T> void check_guard(const Guarded<T>& x)
{
    for (auto byte : x.before)
    {
        ++logical_bytes;
        require(byte == 0x3c, "leading guard differs");
    }
    for (auto byte : x.after)
    {
        ++logical_bytes;
        require(byte == 0xc3, "trailing guard differs");
    }
}

template <std::size_t N>
void check_pixels(const std::array<unsigned short, N>& actual,
    const std::array<unsigned short, N>& expected,
    const char* message)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        logical_bytes += sizeof(actual[i]);
        require(actual[i] == expected[i], message);
    }
}

void check_renderer(const ScreenRequestRenderer& actual, const ScreenRequestRenderer& expected)
{
    logical_bytes += renderer_queue_test::compared_bytes(actual);
    require(renderer_queue_test::equal(actual, expected), "renderer members/queue differ");
}

bool equal_entry(const ScreenSurfacePosition& a, const ScreenSurfacePosition& b)
{
    return a.active == b.active && a.software == b.software &&
           renderer_queue_test::same_bytes(a.unknown_02, b.unknown_02) && a.pixels == b.pixels &&
           a.surface == b.surface && renderer_queue_test::same_entry_extension(a, b) &&
           a.width == b.width && a.height == b.height && a.pitch == b.pitch &&
           a.position.x == b.position.x && a.position.y == b.position.y && a.prepared == b.prepared;
}

// This painter accepts fixed final point data only. It performs no clipping,
// endpoint normalization, line traversal or incremental raster error updates.
template <std::size_t N>
void paint(std::array<unsigned short, N>& pixels,
    const std::vector<Point>& points,
    unsigned int stride,
    std::size_t words_per_pixel,
    unsigned int colour)
{
    for (const Point& point : points)
    {
        require(point.x >= 0 && point.y >= 0, "invalid literal point");
        const auto offset =
            (static_cast<std::size_t>(point.y) * stride + static_cast<std::size_t>(point.x)) *
            words_per_pixel;
        require(offset < pixels.size(), "literal point outside allocated buffer");
        pixels.at(offset) = static_cast<unsigned short>(colour);
    }
}

template <std::size_t N>
void paint_colours(std::array<unsigned short, N>& pixels,
    const std::vector<ColourPoint>& points,
    unsigned int stride,
    std::size_t words_per_pixel)
{
    for (const ColourPoint& point : points)
    {
        require(point.x >= 0 && point.y >= 0, "invalid literal colour point");
        const auto offset =
            (static_cast<std::size_t>(point.y) * stride + static_cast<std::size_t>(point.x)) *
            words_per_pixel;
        require(offset < pixels.size(), "literal colour point outside buffer");
        pixels.at(offset) = point.colour;
    }
}

SurfaceResult SURFACE_STDCALL lock_surface(DirectDrawSurface7Prefix* receiver,
    SurfaceApiRectangle* rectangle,
    SurfaceDescription* description,
    unsigned int flags,
    void* event)
{
    require(active != nullptr, "unexpected Lock");
    auto& c = *active;
    auto& f = *c.fixture;
    auto& r = f.renderer.value;
    require(c.locks == c.unlocks && c.locks < c.count, "Lock order differs");
    require(
        receiver == &f.devices[0].value && rectangle == nullptr && flags == 0 && event == nullptr,
        "captured renderer/Lock ABI differs");
    require(r.pixels_locked == (c.locks == 0 ? 0xa5 : 0), "locked state before Lock differs");
    require(f.entries[0].value.prepared == 0, "prepared store must precede Lock");
    check_pixels(f.dense.value,
        c.locks == 0 ? c.initial : c.expected_after[c.locks - 1],
        "pixels before Lock differ");
    require(screen_request_renderer == (c.locks == 0 ? &f.renderer.value : &f.other_renderer.value),
        "global mutation/order differs");
    std::array<unsigned char, sizeof(SurfaceDescription)> expected{};
    const unsigned int size = sizeof(SurfaceDescription);
    std::memcpy(expected.data(), &size, sizeof(size));
    require(description != nullptr &&
                std::memcmp(
                    static_cast<const void*>(description), expected.data(), expected.size()) == 0,
        "Lock descriptor differs");
    description->pixels = c.kind == Interface::NullSuccess ? nullptr : f.dense.value.data();
    description->pitch = 1234;
    if (c.kind == Interface::PostSelection)
    {
        r.current_surface = 1;
        f.entries[1].value.pitch = 43;
        f.entries[1].value.position = {99, 101};
        f.other_renderer.value.current_surface = 2;
        f.entries[2].value.position = {2, 3};
        f.entries[2].value.pitch = 200;
        screen_request_renderer = &f.other_renderer.value;
    }
    if (c.kind == Interface::ToSoftware)
    {
        r.software_mode = 7;
    }
    ++c.locks;
    ++locks;
    return c.kind == Interface::Error ? -37 : 0;
}

SurfaceResult SURFACE_STDCALL unlock_surface(
    DirectDrawSurface7Prefix* receiver, SurfaceApiRectangle* rectangle)
{
    require(active != nullptr, "unexpected Unlock");
    auto& c = *active;
    auto& f = *c.fixture;
    const auto selected = c.kind == Interface::PostSelection ? 1U : 0U;
    require(c.locks == c.unlocks + 1 && c.unlocks < c.count &&
                receiver == &f.devices[selected].value && rectangle == nullptr,
        "Unlock receiver/order differs");
    check_pixels(f.dense.value, c.expected_after[c.unlocks], "pixels before Unlock differ");
    require(f.renderer.value.pixels_locked == 1, "locked byte cleared before Unlock");
    if (c.kind == Interface::Style)
    {
        require(c.input != nullptr && c.style != nullptr, "missing live style context");
        if (c.unlocks == 0)
        {
            *c.input = c.style->after_first;
            f.renderer.value.format_44 = c.style->first_format;
            f.other_renderer.value.format_44 = 0;
            screen_request_renderer = &f.other_renderer.value;
        }
        else if (c.unlocks == 1)
        {
            *c.input = c.style->after_second;
            f.renderer.value.format_44 = c.style->second_format;
        }
    }
    f.renderer.value.pixels_locked = 0xa5;
    ++c.unlocks;
    ++unlocks;
    return -37;
}

const DirectDrawSurface7VtablePrefix surface_table = []
{
    DirectDrawSurface7VtablePrefix table{};
    table.Lock = lock_surface;
    table.Unlock = unlock_surface;
    return table;
}();

void initialize(Fixture& f)
{
    initialize_guard(f.renderer);
    initialize_guard(f.other_renderer);
    initialize_guard(f.bounds);
    initialize_guard(f.table);
    initialize_guard(f.other_table);
    initialize_guard(f.direct);
    initialize_guard(f.dense);
    initialize_guard(f.other_pixels);
    renderer_queue_test::initialize(f.renderer.value, 23);
    renderer_queue_test::initialize(f.other_renderer.value, 71);
    for (std::size_t i = 0; i < f.dense.value.size(); ++i)
    {
        f.direct.value[i * 2] = static_cast<unsigned short>(0x1357U ^ i);
        f.direct.value[i * 2 + 1] = static_cast<unsigned short>(0xa000U + i);
        f.dense.value[i] = static_cast<unsigned short>(0x2468U ^ i);
        f.other_pixels.value[i] = static_cast<unsigned short>(0x789aU ^ i);
    }
    for (std::size_t i = 0; i < f.entries.size(); ++i)
    {
        initialize_guard(f.entries[i]);
        initialize_guard(f.devices[i]);
        auto& e = f.entries[i].value;
        e.active = static_cast<unsigned char>(0x41 + i);
        e.software = static_cast<unsigned char>(0x51 + i);
        renderer_queue_test::pattern(e.unknown_02, static_cast<unsigned int>(71 + i));
        e.pixels = i == 0 ? f.dense.value.data() : f.other_pixels.value.data();
        e.surface = &f.devices[i].value;
        renderer_queue_test::initialize_entry_extension(e, static_cast<unsigned int>(91 + i));
        e.width = 40;
        e.height = 40;
        e.pitch = 40;
        e.position = {0, 0};
        e.prepared = static_cast<unsigned char>(0x31 + i);
        f.devices[i].value.vtable = &surface_table;
        f.table.value[i] = &e;
        f.other_table.value[i] = &e;
    }
    for (auto* r : {&f.renderer.value, &f.other_renderer.value})
    {
        r->software_mode = 1;
        r->width = 40;
        r->height = 40;
        r->clip_rectangle = {0, 0, 20, 20};
        r->format_44 = 0;
        r->unscaled_clip_5c = 0;
        r->current_surface = 0;
        r->pixels_locked = 0xa5;
    }
    f.renderer.value.pixels_1c = f.direct.value.data();
    f.renderer.value.surfaces = f.table.value.data();
    f.other_renderer.value.pixels_1c = f.other_pixels.value.data();
    f.other_renderer.value.surfaces = f.other_table.value.data();
    // Wrong receiver/global selection reaches valid typed objects and a failing
    // COM receiver assertion, never a fabricated game method or invalid vptr.
    f.other_renderer.value.unscaled_clip_5c = 1;
    f.other_renderer.value.software_mode = 0;
    f.other_renderer.value.current_surface = 2;
}

Snapshot snapshot(const Fixture& f)
{
    return {f.renderer.value,
        f.other_renderer.value,
        f.bounds.value,
        {f.entries[0].value, f.entries[1].value, f.entries[2].value},
        f.direct.value,
        f.dense.value,
        f.other_pixels.value};
}

void check(const Fixture& f, const Snapshot& expected)
{
    check_pixels(f.direct.value, expected.direct, "direct pixels differ");
    check_pixels(f.dense.value, expected.dense, "dense pixels differ");
    check_pixels(f.other_pixels.value, expected.other_pixels, "unselected pixels differ");
    require(
        f.renderer.value.pixels_locked == expected.renderer.pixels_locked, "locked byte differs");
    check_renderer(f.renderer.value, expected.renderer);
    check_renderer(f.other_renderer.value, expected.other_renderer);
    require(renderer_queue_test::same_rectangle(f.bounds.value, expected.bounds), "input differs");
    for (std::size_t i = 0; i < f.entries.size(); ++i)
    {
        require(f.table.value[i] == &f.entries[i].value &&
                    f.other_table.value[i] == &f.entries[i].value,
            "entry pointer table differs");
        require(equal_entry(f.entries[i].value, expected.entries[i]), "entry members differ");
        require(f.devices[i].value.vtable == &surface_table, "COM table pointer differs");
        check_guard(f.entries[i]);
        check_guard(f.devices[i]);
    }
    check_guard(f.renderer);
    check_guard(f.other_renderer);
    check_guard(f.bounds);
    check_guard(f.table);
    check_guard(f.other_table);
    check_guard(f.direct);
    check_guard(f.dense);
    check_guard(f.other_pixels);
}

void run_kernel(const RasterCase& c)
{
    Fixture f{};
    initialize(f);
    f.renderer.value.width = c.width;
    f.renderer.value.height = c.height;
    f.renderer.value.pixels_1c = f.other_pixels.value.data();
    auto expected = snapshot(f);
    const bool direct = c.kind == Kernel::DirectX || c.kind == Kernel::DirectY;
    if (direct)
    {
        paint(expected.direct, c.points, c.stride, 2, c.colour);
    }
    else
    {
        paint(expected.dense, c.points, c.stride, 1, c.colour);
    }
    using Method = void (*)(int, int, int, int, int, unsigned int, unsigned short*, unsigned int);
    constexpr std::array<Method, 4> methods{
        RasterLineDirectX, RasterLineDirectY, RasterLinePackedX, RasterLinePackedY};
    screen_request_renderer = &f.renderer.value;
    methods.at(static_cast<std::size_t>(c.kind))(c.arguments[0],
        c.arguments[1],
        c.arguments[2],
        c.arguments[3],
        c.arguments[4],
        c.colour,
        direct ? f.direct.value.data() : f.dense.value.data(),
        c.stride);
    ++raw_calls;
    check(f, expected);
    require(screen_request_renderer == &f.renderer.value, "kernel changed global");
    screen_request_renderer = nullptr;
}

void run_line(const LineCase& c)
{
    Fixture f{};
    initialize(f);
    auto& r = f.renderer.value;
    r.width = c.width;
    r.height = c.height;
    r.clip_rectangle = c.clip;
    r.unscaled_clip_5c = c.unscaled;
    r.software_mode = c.software;
    r.pixels_locked = c.initial_locked;
    f.entries[0].value.pitch = c.pitch;
    f.entries[0].value.position = c.position;
    auto expected = snapshot(f);
    if (c.unscaled)
    {
        paint(expected.dense, c.points, c.pitch >> 1, 1, c.colour);
    }
    else
    {
        paint(expected.direct, c.points, c.width, 2, c.colour);
    }
    if (c.access)
    {
        expected.entries[0].prepared = 0;
    }
    expected.renderer.pixels_locked = c.expected_locked;
    screen_request_renderer = &r;
    r.DrawLine(c.arguments[0], c.arguments[1], c.arguments[2], c.arguments[3], c.colour);
    ++line_calls;
    check(f, expected);
    require(screen_request_renderer == &r, "line changed global");
    screen_request_renderer = nullptr;
}

void run_interface(std::size_t which)
{
    Fixture f{};
    initialize(f);
    f.renderer.value.unscaled_clip_5c = 1;
    f.renderer.value.software_mode = which == 2 ? 1 : 0;
    if (which == 2)
    {
        f.entries[0].value.pixels = nullptr;
    }
    auto expected = snapshot(f);
    expected.entries[0].prepared = 0;
    expected.renderer.pixels_locked = which < 2 ? 0 : (which == 4 ? 0xa5 : 1);
    constexpr std::array<Interface, 5> kinds{Interface::PostSelection,
        Interface::ToSoftware,
        Interface::Error,
        Interface::NullSuccess,
        Interface::Error};
    Context context{};
    context.fixture = &f;
    context.kind = kinds.at(which);
    context.initial = f.dense.value;
    context.count = 1;
    context.expected_after[0] = f.dense.value;
    paint(context.expected_after[0],
        interface_points.at(which),
        which == 0 ? 21 : 20,
        1,
        0x12345678u);
    expected.dense = context.expected_after[0];
    if (which == 0)
    {
        expected.renderer.current_surface = 1;
        expected.entries[1].pitch = 43;
        expected.entries[1].position = {99, 101};
        expected.entries[2].pitch = 200;
        expected.entries[2].position = {2, 3};
    }
    if (which == 1)
    {
        expected.renderer.software_mode = 7;
    }
    constexpr std::array<std::array<int, 4>, 5> args{
        {{5, 7, 13, 9}, {3, 4, 5, 10}, {2, 3, 8, 4}, {2, 3, 8, 4}, {2, 3, 8, 4}}};
    screen_request_renderer = &f.renderer.value;
    active = which == 2 ? nullptr : &context;
    const auto& a = args.at(which);
    f.renderer.value.DrawLine(a[0], a[1], a[2], a[3], 0x12345678u);
    ++line_calls;
    active = nullptr;
    require(context.locks == (which == 2 ? 0U : 1U) && context.unlocks == (which == 0 ? 1U : 0U),
        "interface call counts differ");
    require(screen_request_renderer == (which == 0 ? &f.other_renderer.value : &f.renderer.value),
        "interface global differs");
    check(f, expected);
    screen_request_renderer = nullptr;
}

void run_style(const StyleCase& c)
{
    Fixture f{};
    initialize(f);
    f.bounds.value = c.input;
    f.renderer.value.format_44 = c.format;
    auto expected = snapshot(f);
    paint_colours(expected.direct, c.points, 40, 2);
    screen_request_renderer = &f.renderer.value;
    DrawFrameStyle6(f.bounds.value);
    ++style_calls;
    check(f, expected);
    require(screen_request_renderer == &f.renderer.value, "style changed global");
    screen_request_renderer = nullptr;
}

void run_live_style(const LiveStyleCase& c, bool queue_alias)
{
    Fixture f{};
    initialize(f);
    f.renderer.value.unscaled_clip_5c = 1;
    f.renderer.value.software_mode = 0;
    f.renderer.value.format_44 = c.initial_format;
    f.bounds.value = {4, 6, 14, 16};
    ScreenRectangle* input = &f.bounds.value;
    if (queue_alias)
    {
        f.renderer.value.dirty_rectangles.values.front() = f.bounds.value;
        input = &f.renderer.value.dirty_rectangles.values.front();
    }
    auto expected = snapshot(f);
    Context context{};
    context.fixture = &f;
    context.kind = Interface::Style;
    context.input = input;
    context.style = &c;
    context.initial = f.dense.value;
    context.count = 3;
    for (std::size_t i = 0; i < context.count; ++i)
    {
        context.expected_after[i] = i == 0 ? f.dense.value : context.expected_after[i - 1];
        paint_colours(context.expected_after[i], c.stages[i], 20, 1);
    }
    expected.dense = context.expected_after[2];
    expected.renderer.format_44 = c.second_format;
    expected.renderer.pixels_locked = 0;
    expected.entries[0].prepared = 0;
    if (queue_alias)
    {
        expected.renderer.dirty_rectangles.values.front() = c.after_second;
    }
    else
    {
        expected.bounds = c.after_second;
    }
    screen_request_renderer = &f.renderer.value;
    active = &context;
    DrawFrameStyle6(*input);
    ++style_calls;
    active = nullptr;
    require(context.locks == 3 && context.unlocks == 3, "style interface counts differ");
    require(screen_request_renderer == &f.other_renderer.value, "style callback global lost");
    check(f, expected);
    screen_request_renderer = nullptr;
}
} // namespace

int main()
{
    require(screen_request_renderer == nullptr, "actual renderer global not initially null");
    domain = "kernel";
    for (current_case = 0; current_case < raster_cases.size(); ++current_case)
    {
        run_kernel(raster_cases[current_case]);
    }
    domain = "line";
    for (current_case = 0; current_case < line_cases.size(); ++current_case)
    {
        run_line(line_cases[current_case]);
    }
    domain = "interface";
    for (current_case = 0; current_case < interface_points.size(); ++current_case)
    {
        run_interface(current_case);
    }
    domain = "style";
    for (current_case = 0; current_case < style_cases.size(); ++current_case)
    {
        run_style(style_cases[current_case]);
    }
    domain = "style interface";
    current_case = 0;
    for (bool alias : {false, true})
    {
        for (const auto& c : live_style_cases)
        {
            run_live_style(c, alias);
            ++current_case;
        }
    }
    require(
        raw_calls == 72 && line_calls == 28 && style_calls == 17 && locks == 16 && unlocks == 13,
        "final call counts differ");
    require(
        screen_request_renderer == nullptr && active == nullptr, "fixture globals not restored");
    std::printf("renderer line: 117 sessions, %zu kernels, %zu lines, %zu styles, "
                "%zu locks, %zu unlocks, %zu checks, %zu logical bytes\n",
        raw_calls,
        line_calls,
        style_calls,
        locks,
        unlocks,
        checks,
        logical_bytes);
    return 0;
}
