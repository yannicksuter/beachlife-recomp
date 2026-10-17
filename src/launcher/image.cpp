#include "image.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"
#include <climits>

namespace beachlife::launcher {
SDL_Texture* load_artwork(SDL_Renderer* renderer, const char* path) {
    size_t length = 0;
    auto* bytes = static_cast<unsigned char*>(SDL_LoadFile(path, &length));
    if (!bytes) return nullptr;
    int width = 0, height = 0, channels = 0;
    auto* pixels = length <= INT_MAX ? stbi_load_from_memory(bytes, static_cast<int>(length),
        &width, &height, &channels, 4) : nullptr;
    SDL_free(bytes);
    if (!pixels) {
        SDL_SetError("Cannot decode the launcher artwork");
        return nullptr;
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels, width * 4);
    SDL_Texture* texture = surface ? SDL_CreateTextureFromSurface(renderer, surface) : nullptr;
    SDL_DestroySurface(surface);
    stbi_image_free(pixels);
    if (texture) SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return texture;
}
}
