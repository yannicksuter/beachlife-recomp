#pragma once
#include <SDL3/SDL.h>

#include <vector>

namespace beachlife::launcher
{

struct Artwork
{
    SDL_Texture* sharp = nullptr;
    SDL_Texture* soft = nullptr; // Blurred at load time for frosted panels; the file is unchanged.
    float width = 0, height = 0;
};

// Returns false with SDL's error set; textures created before a failure are returned for cleanup.
bool load_artwork(SDL_Renderer* renderer, const char* path, Artwork& artwork);

// A PNG drawn at its exact on-screen size. The renderer has no mipmaps, so the picture is
// area-averaged (with premultiplied alpha) to the pixel width it is shown at whenever that
// changes, then uploaded with straight alpha, which every renderer, software too, blends.
struct Picture
{
    std::vector<unsigned char> pixels; // full size, premultiplied RGBA
    float width = 0, height = 0;
    SDL_Texture* texture = nullptr; // `pixels` resampled to `texture_width`
    int texture_width = 0;
};
bool load_picture(const char* path, Picture& picture);
// The picture `width` pixels wide (at most its own width), kept until another width is asked for.
SDL_Texture* picture_texture(SDL_Renderer* renderer, Picture& picture, int width);
// White textures whose alpha carries an eased ramp (256 x 1) or a soft round spot.
SDL_Texture* create_ramp(SDL_Renderer* renderer);
SDL_Texture* create_spot(SDL_Renderer* renderer);

} // namespace beachlife::launcher
