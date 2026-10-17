#pragma once

// Descriptive partial layout, not a recovered original class declaration.
// On the x86 matching target, the palette pointer is at offset 0x14.
struct TgaImage
{
    unsigned char unknown[0x14];
    unsigned char* palette;
};
