#pragma once

#include "windows.h"

namespace beachlife::platform
{
class Video;
}

// Internal to the compat layer: how retail handles map to the host.
namespace beachlife::compat
{
// In the boot build the game's main window handle is Play's Video: the
// compat CreateWindowExA returns &video where retail expects its HWND.
inline platform::Video* video(HWND window)
{
    return reinterpret_cast<platform::Video*>(window);
}

// A retail call reached a case the compat layer does not support yet. Stops
// loudly with the API name and detail; never pretends the call succeeded.
[[noreturn]] void unsupported(const char* api, const char* detail);
} // namespace beachlife::compat
