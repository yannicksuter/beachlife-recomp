#pragma once
#include <SDL3/SDL.h>

namespace beachlife::launcher {
SDL_Texture* load_artwork(SDL_Renderer* renderer, const char* path);
}
