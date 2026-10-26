#include "compat_host.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace beachlife::compat
{
[[noreturn]] void unsupported(const char* api, const char* detail)
{
    const std::string message = std::string("no host support yet for ") + api +
                                (detail && *detail ? ": " : "") + (detail ? detail : "");
    std::fprintf(stderr, "Beach Life boot build: %s\n", message.c_str());
    std::fflush(stderr);
    if (!SDL_getenv("BEACHLIFE_BOOT_QUIET"))
    {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Beach Life boot build", message.c_str(), nullptr);
    }
    std::abort();
}
} // namespace beachlife::compat
