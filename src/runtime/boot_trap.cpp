#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

// Called by the boot build's generated traps: the game reached a function that
// is not decompiled yet, or a platform call without a host service. Stop
// loudly with the name and original address; never continue past it.
namespace beachlife::boot
{
namespace
{
[[noreturn]] void stop(const std::string& message)
{
    std::fprintf(stderr, "Beach Life boot build: %s\n", message.c_str());
    std::fflush(stderr);
    if (!SDL_getenv("BEACHLIFE_BOOT_QUIET"))
    {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Beach Life boot build", message.c_str(), nullptr);
    }
    std::abort();
}
} // namespace

[[noreturn]] void trap(const char* name, std::uint32_t address)
{
    char where[32] = "not in the inventory";
    if (address)
    {
        std::snprintf(where, sizeof where, "0x%08x", static_cast<unsigned>(address));
    }
    stop(std::string("not decompiled yet: ") + name + " (" + where + ")");
}

[[noreturn]] void missing_service(const char* name)
{
    stop(std::string("no host service yet for ") + name);
}
} // namespace beachlife::boot
