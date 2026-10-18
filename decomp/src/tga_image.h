#pragma once

// Descriptive layout, not a recovered original class declaration.
// The x86 object is 0x20 bytes; the final field's meaning remains unknown.
struct TgaImage
{
    int width;
    int height;
    int bits_per_pixel;
    int bytes_per_pixel;
    unsigned char* pixels;
    unsigned char* palette;
    unsigned short* converted_palette;
    unsigned char unknown_1c[4];

    TgaImage(int image_width, int image_height);
    ~TgaImage();
};
