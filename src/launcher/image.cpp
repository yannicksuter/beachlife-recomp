#include "image.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <vector>

namespace beachlife::launcher
{
namespace
{
SDL_Texture* upload(SDL_Renderer* renderer, int width, int height, const unsigned char* pixels)
{
    auto* texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
    if (!texture)
    {
        return nullptr;
    }
    if (!SDL_UpdateTexture(texture, nullptr, pixels, width * 4))
    {
        SDL_DestroyTexture(texture);
        return nullptr;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return texture;
}

// Premultiplied RGBA to straight alpha. Transparent pixels take the colour of their visible
// neighbours, so filtering at an edge blends towards the edge's colour rather than black.
std::vector<unsigned char> straight(
    const std::vector<unsigned char>& premultiplied, int width, int height)
{
    std::vector<unsigned char> result(premultiplied.size(), 0);
    auto at = [&](int x, int y)
    {
        return (static_cast<size_t>(y) * width + x) * 4;
    };
    for (size_t i = 0; i < result.size(); i += 4)
    {
        const int alpha = premultiplied[i + 3];
        for (int c = 0; alpha && c < 3; ++c)
        {
            result[i + c] =
                static_cast<unsigned char>(std::min(255, premultiplied[i + c] * 255 / alpha));
        }
        result[i + 3] = static_cast<unsigned char>(alpha);
    }
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (premultiplied[at(x, y) + 3])
            {
                continue;
            }
            int sum[3] = {}, count = 0;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height ||
                        !premultiplied[at(nx, ny) + 3])
                    {
                        continue;
                    }
                    for (int c = 0; c < 3; ++c)
                    {
                        sum[c] += result[at(nx, ny) + c];
                    }
                    ++count;
                }
            }
            for (int c = 0; count && c < 3; ++c)
            {
                result[at(x, y) + c] = static_cast<unsigned char>(sum[c] / count);
            }
        }
    }
    return result;
}

// Area-averages premultiplied RGBA to a smaller size: each output pixel is the mean of the
// source under it, partly covered pixels weighted by their coverage, one axis at a time.
std::vector<unsigned char> shrink(
    const std::vector<unsigned char>& pixels, int width, int height, int out_width, int out_height)
{
    auto cover = [](int index, float scale, int limit, auto&& visit)
    {
        const float begin = index * scale, end = begin + scale;
        for (int i = static_cast<int>(begin); i < std::min(limit, static_cast<int>(std::ceil(end)));
            ++i)
        {
            visit(i, std::min(end, i + 1.0f) - std::max(begin, static_cast<float>(i)));
        }
    };
    const float scale_x = static_cast<float>(width) / out_width,
                scale_y = static_cast<float>(height) / out_height;
    std::vector<float> rows(static_cast<size_t>(out_width) * height * 4, 0.0f);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < out_width; ++x)
        {
            cover(x,
                scale_x,
                width,
                [&](int i, float weight)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        rows[(static_cast<size_t>(y) * out_width + x) * 4 + c] +=
                            pixels[(static_cast<size_t>(y) * width + i) * 4 + c] * weight;
                    }
                });
        }
    }
    std::vector<unsigned char> result(static_cast<size_t>(out_width) * out_height * 4);
    for (int y = 0; y < out_height; ++y)
    {
        for (int x = 0; x < out_width; ++x)
        {
            float sum[4] = {};
            cover(y,
                scale_y,
                height,
                [&](int i, float weight)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        sum[c] += rows[(static_cast<size_t>(i) * out_width + x) * 4 + c] * weight;
                    }
                });
            for (int c = 0; c < 4; ++c)
            {
                result[(static_cast<size_t>(y) * out_width + x) * 4 + c] =
                    static_cast<unsigned char>(
                        std::clamp(sum[c] / (scale_x * scale_y) + 0.5f, 0.0f, 255.0f));
            }
        }
    }
    return result;
}

// Averages blocks of `factor` pixels, then runs three box passes each way
// (close to a Gaussian) over the small copy.
std::vector<unsigned char> blur(
    const unsigned char* pixels, int width, int height, int factor, int& out_width, int& out_height)
{
    out_width = std::max(1, width / factor);
    out_height = std::max(1, height / factor);
    std::vector<float> small(static_cast<size_t>(out_width) * out_height * 3);
    for (int y = 0; y < out_height; ++y)
    {
        for (int x = 0; x < out_width; ++x)
        {
            float sum[3] = {};
            for (int dy = 0; dy < factor; ++dy)
            {
                for (int dx = 0; dx < factor; ++dx)
                {
                    const auto* p =
                        pixels +
                        (static_cast<size_t>(y * factor + dy) * width + x * factor + dx) * 4;
                    for (int c = 0; c < 3; ++c)
                    {
                        sum[c] += p[c];
                    }
                }
            }
            for (int c = 0; c < 3; ++c)
            {
                small[(static_cast<size_t>(y) * out_width + x) * 3 + c] =
                    sum[c] / (factor * factor);
            }
        }
    }
    constexpr int radius = 4;
    std::vector<float> line;
    auto pass = [&](int count, int length, size_t stride, auto offset)
    {
        line.resize(static_cast<size_t>(length) * 3);
        for (int i = 0; i < count; ++i)
        {
            for (int j = 0; j < length; ++j)
            {
                for (int c = 0; c < 3; ++c)
                {
                    line[j * 3 + c] = small[offset(i) + j * stride + c];
                }
            }
            for (int j = 0; j < length; ++j)
            {
                for (int c = 0; c < 3; ++c)
                {
                    float sum = 0;
                    for (int k = -radius; k <= radius; ++k)
                    {
                        sum += line[std::clamp(j + k, 0, length - 1) * 3 + c];
                    }
                    small[offset(i) + j * stride + c] = sum / (2 * radius + 1);
                }
            }
        }
    };
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        pass(out_height,
            out_width,
            3,
            [&](int row)
            {
                return static_cast<size_t>(row) * out_width * 3;
            });
        pass(out_width,
            out_height,
            static_cast<size_t>(out_width) * 3,
            [](int column)
            {
                return static_cast<size_t>(column) * 3;
            });
    }
    std::vector<unsigned char> result(static_cast<size_t>(out_width) * out_height * 4, 255);
    for (size_t i = 0; i < static_cast<size_t>(out_width) * out_height; ++i)
    {
        for (int c = 0; c < 3; ++c)
        {
            result[i * 4 + c] =
                static_cast<unsigned char>(std::clamp(small[i * 3 + c] + 0.5f, 0.0f, 255.0f));
        }
    }
    return result;
}
} // namespace

bool load_artwork(SDL_Renderer* renderer, const char* path, Artwork& artwork)
{
    size_t length = 0;
    auto* bytes = static_cast<unsigned char*>(SDL_LoadFile(path, &length));
    if (!bytes)
    {
        return false;
    }
    int width = 0, height = 0, channels = 0;
    auto* pixels =
        length <= INT_MAX
            ? stbi_load_from_memory(bytes, static_cast<int>(length), &width, &height, &channels, 4)
            : nullptr;
    SDL_free(bytes);
    if (!pixels)
    {
        return SDL_SetError("Cannot decode the launcher artwork");
    }
    artwork.width = static_cast<float>(width);
    artwork.height = static_cast<float>(height);
    artwork.sharp = upload(renderer, width, height, pixels);
    int soft_width = 0, soft_height = 0;
    const auto soft = blur(pixels, width, height, 8, soft_width, soft_height);
    stbi_image_free(pixels);
    if (!artwork.sharp)
    {
        return false;
    }
    artwork.soft = upload(renderer, soft_width, soft_height, soft.data());
    return artwork.soft != nullptr;
}

bool load_picture(const char* path, Picture& picture)
{
    SDL_Surface* loaded = SDL_LoadPNG(path);
    SDL_Surface* surface = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
    SDL_DestroySurface(loaded);
    if (!surface)
    {
        return false;
    }
    const int width = surface->w, height = surface->h;
    picture.pixels.assign(static_cast<size_t>(width) * height * 4, 0);
    const bool premultiplied = SDL_PremultiplySurfaceAlpha(surface, false);
    for (int y = 0; premultiplied && y < height; ++y)
    {
        SDL_memcpy(picture.pixels.data() + static_cast<size_t>(y) * width * 4,
            static_cast<unsigned char*>(surface->pixels) + y * surface->pitch,
            static_cast<size_t>(width) * 4);
    }
    SDL_DestroySurface(surface);
    picture.width = static_cast<float>(width);
    picture.height = static_cast<float>(height);
    return premultiplied;
}

SDL_Texture* picture_texture(SDL_Renderer* renderer, Picture& picture, int width)
{
    const int source_width = static_cast<int>(picture.width),
              source_height = static_cast<int>(picture.height);
    width = std::clamp(width, 1, source_width);
    if (picture.texture && picture.texture_width == width)
    {
        return picture.texture;
    }
    const int height =
        std::max(1, static_cast<int>(std::lround(picture.height * width / picture.width)));
    const auto pixels = width == source_width
                            ? picture.pixels
                            : shrink(picture.pixels, source_width, source_height, width, height);
    auto* texture = upload(renderer, width, height, straight(pixels, width, height).data());
    if (!texture)
    {
        return picture.texture; // keep drawing the previous size
    }
    SDL_DestroyTexture(picture.texture);
    picture.texture = texture;
    picture.texture_width = width;
    return texture;
}

SDL_Texture* create_ramp(SDL_Renderer* renderer)
{
    std::vector<unsigned char> pixels(256 * 4, 255);
    for (int i = 0; i < 256; ++i)
    {
        const float t = i / 255.0f;
        pixels[i * 4 + 3] = static_cast<unsigned char>(
            std::lround(255 * t * t * t * (t * (t * 6 - 15) + 10))); // smootherstep
    }
    return upload(renderer, 256, 1, pixels.data());
}

SDL_Texture* create_spot(SDL_Renderer* renderer)
{
    constexpr int size = 128;
    std::vector<unsigned char> pixels(size * size * 4, 255);
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const float dx = (x + 0.5f) / size * 2 - 1, dy = (y + 0.5f) / size * 2 - 1;
            const float falloff = std::max(0.0f, 1 - (dx * dx + dy * dy));
            pixels[(y * size + x) * 4 + 3] =
                static_cast<unsigned char>(std::lround(255 * falloff * falloff * falloff));
        }
    }
    return upload(renderer, size, size, pixels.data());
}

} // namespace beachlife::launcher
