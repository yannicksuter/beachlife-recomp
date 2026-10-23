#include "tga_image.h"

// Descriptive name; the original symbol and exact declaration are unknown.
// FUNCTION: BEACHLIFE 0x004ea430
void CopyTgaPalette(const unsigned char* source, TgaImage* image, int count)
{
    unsigned char* palette = image->palette;
    for (int index = 0; index < count; ++index)
    {
        palette[index * 3] = source[index * 3 + 2];
        palette[index * 3 + 1] = source[index * 3 + 1];
        palette[index * 3 + 2] = source[index * 3];
    }
}
