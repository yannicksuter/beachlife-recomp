#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <string>

class CompatProfileEnvironment
{
  public:
    CompatProfileEnvironment()
    {
        if (const char* value = SDL_getenv("BEACHLIFE_BOOT_USER_DIR"))
        {
            previous_ = value;
        }
    }
    ~CompatProfileEnvironment()
    {
        if (previous_)
        {
            SDL_setenv_unsafe("BEACHLIFE_BOOT_USER_DIR", previous_->c_str(), 1);
        }
        else
        {
            SDL_unsetenv_unsafe("BEACHLIFE_BOOT_USER_DIR");
        }
    }

  private:
    std::optional<std::string> previous_;
};
