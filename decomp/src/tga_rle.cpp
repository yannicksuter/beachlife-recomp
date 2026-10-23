#include "tga_image.h"

void CopyTgaPixels(const unsigned char* source, TgaImage* image);

// Descriptive name; the original symbol and exact declaration are unknown.
// FUNCTION: BEACHLIFE 0x004ea5c0
void ExpandTgaRle(const unsigned char* source, TgaImage* image)
{
    int remaining = image->height * image->width;
    int bytes_per_pixel = image->bytes_per_pixel;
    unsigned char* buffer = new unsigned char[bytes_per_pixel * remaining];
    unsigned char* pixels = buffer;
    unsigned char color[4];
    while (remaining)
    {
        int count = *source++;
        if (count & 0x80)
        {
            for (int byte = 0; byte < bytes_per_pixel; ++byte)
                color[byte] = *source++;
            count = (count & 0x7f) + 1;
            while (count--)
            {
                for (int byte = 0; byte < bytes_per_pixel; ++byte)
                    *pixels++ = color[byte];
                --remaining;
            }
        }
        else
        {
            count = (count & 0x7f) + 1;
            while (count--)
            {
                for (int byte = 0; byte < bytes_per_pixel; ++byte)
                    *pixels++ = *source++;
                --remaining;
            }
        }
    }
    CopyTgaPixels(buffer, image);
    delete[] buffer;
}
