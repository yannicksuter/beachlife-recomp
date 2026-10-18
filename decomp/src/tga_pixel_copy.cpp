#include "tga_image.h"

// Descriptive name; the original symbol and exact declaration are unknown.
// FUNCTION: BEACHLIFE 0x004ea470
void CopyTgaPixels(const unsigned char* source, TgaImage* image)
{
    int width = image->width;
    int height = image->height;
    unsigned char* pixels = image->pixels;
    switch (image->bits_per_pixel)
    {
    case 8:
    {
        for (int row = 0; row < height; ++row)
        {
            for (int column = 0; column < width; ++column)
            {
                pixels[(height - row - 1) * width + column] = source[row * width + column];
            }
        }
        break;
    }
    case 24:
    {
        for (int row = 0; row < height; ++row)
        {
            for (int column = 0; column < width; ++column)
            {
                pixels[((height - row - 1) * width + column) * 3] = source[(row * width + column) * 3 + 2];
                pixels[((height - row - 1) * width + column) * 3 + 1] = source[(row * width + column) * 3 + 1];
                pixels[((height - row - 1) * width + column) * 3 + 2] = source[(row * width + column) * 3];
            }
        }
        break;
    }
    case 32:
    {
        for (int row = 0; row < height; ++row)
        {
            for (int column = 0; column < width; ++column)
            {
                pixels[((height - row - 1) * width + column) * 4] = source[(row * width + column) * 4 + 2];
                pixels[((height - row - 1) * width + column) * 4 + 1] = source[(row * width + column) * 4 + 1];
                pixels[((height - row - 1) * width + column) * 4 + 2] = source[(row * width + column) * 4];
                pixels[((height - row - 1) * width + column) * 4 + 3] = source[(row * width + column) * 4 + 3];
            }
        }
        break;
    }
    }
}
