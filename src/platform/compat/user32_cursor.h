#pragma once

#include "windows.h"

#include <memory>

namespace beachlife::compat
{
struct CursorResources;

// Owns executable resource cursors for one game run. Requires the retail file
// session and SDL video to outlive it. Nested scopes restore the previous cache
// and cursor; handles must not escape their scope. All calls stay on its thread.
class ResourceCursorSession
{
  public:
    ResourceCursorSession();
    ~ResourceCursorSession();
    ResourceCursorSession(const ResourceCursorSession&) = delete;
    ResourceCursorSession& operator=(const ResourceCursorSession&) = delete;

  private:
    std::unique_ptr<CursorResources> resources_;
};

// Internal USER32 bridge. The returned HCURSOR is an SDL_Cursor pointer.
HCURSOR load_resource_cursor(
    HINSTANCE instance, LPCSTR name, UINT type, int width, int height, UINT flags) noexcept;
HCURSOR current_cursor();
} // namespace beachlife::compat
