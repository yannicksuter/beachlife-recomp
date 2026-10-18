#include "tga_image.h"

// Descriptive class name; the original symbol and declaration are unknown.
// FUNCTION: BEACHLIFE 0x004ea2b0
TgaImage::TgaImage(int image_width, int image_height)
{
    width = image_width;
    height = image_height;
    bits_per_pixel = 16;
    bytes_per_pixel = 2;
    pixels = 0;
    palette = 0;
    converted_palette = 0;
}
