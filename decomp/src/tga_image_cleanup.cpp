#include "tga_image.h"

// FUNCTION: BEACHLIFE 0x004ea2e0
TgaImage::~TgaImage()
{
    delete[] pixels;
    if (palette)
    {
        delete[] palette;
    }
    if (converted_palette)
    {
        delete[] converted_palette;
    }
}
