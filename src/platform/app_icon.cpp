#include "app_icon.h"
#include "path.h"

#include <SDL3/SDL.h>
#include <stdexcept>

namespace beachlife::platform
{
void set_app_icon(SDL_Window* window, const std::filesystem::path& image)
{
    auto* surface = SDL_LoadPNG(utf8(image).c_str());
    if (!surface)
    {
        throw std::runtime_error("Cannot load the application icon. Restore the complete package.");
    }
    // Some window systems (including SDL's dummy driver) do not expose icons.
    SDL_SetWindowIcon(window, surface);
    SDL_DestroySurface(surface);
}
} // namespace beachlife::platform
