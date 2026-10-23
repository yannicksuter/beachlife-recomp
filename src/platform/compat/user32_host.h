#pragma once

#include "windows.h"

namespace beachlife::compat
{
// Play owns the Video represented by this HWND. An entry point that uses the
// retail message loop can attach its window procedure without creating a window.
void bind_window_procedure(HWND window, WNDPROC procedure);
// Makes Play's window the game's window before any retail call names it, so
// calls without a window argument (ClipCursor, ShowCursor) apply to it.
void bind_window(HWND window);
} // namespace beachlife::compat
