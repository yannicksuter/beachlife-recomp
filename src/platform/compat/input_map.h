#pragma once

#include "platform/video.h"
#include "windows.h"

namespace beachlife::compat::input
{
using Point = platform::PresentationTransform::Point;

// A virtual Windows screen: fullscreen starts at zero even when emulated in
// a host window. Windowed clients retain the host's desktop origin so retail
// GetWindowRect/frame-metric subtraction still gives logical client pixels.
struct Mapping
{
    platform::PresentationTransform presentation;
    POINT origin{};
    RECT clip{}; // client coordinates, exclusive right/bottom

    Point clamp(Point point) const;
    Point from_window(double x, double y) const;
    Point delta(double x, double y, double dx, double dy) const;
    SDL_Rect window_clip() const;
};

void bind(HWND window);
bool mapping(HWND window, Mapping& result);
bool cursor_position(POINT& point);
bool set_cursor_position(int x, int y);
bool clip_cursor(const RECT* rectangle);
} // namespace beachlife::compat::input
