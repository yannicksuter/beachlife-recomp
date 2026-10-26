#pragma once

#include "windows.h"

namespace beachlife::compat
{
// Play owns the Video represented by this HWND. CreateWindowExA attaches the
// registered retail window procedure to it.
void bind_window_procedure(HWND window, WNDPROC procedure);
// Makes Play's window the game's window before any retail call names it, so
// calls without a window argument (ClipCursor, ShowCursor) apply to it.
void bind_window(HWND window);

// Retains Play's window for the retail entry, including its final render after
// WM_QUIT. Clears callbacks and queued messages before the host destroys Video.
class WindowSession
{
  public:
    WindowSession(HWND window, void (*prepare_frame)(void*), void* context, bool hidden);
    ~WindowSession();
    WindowSession(const WindowSession&) = delete;
    WindowSession& operator=(const WindowSession&) = delete;
};
} // namespace beachlife::compat
