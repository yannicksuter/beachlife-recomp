// Descriptive name; the original symbol and exact declaration are unknown.
// FUNCTION: BEACHLIFE 0x004ea400
unsigned char* GetTgaPixelData(unsigned char* buffer, unsigned char id_length,
    unsigned short palette_length, unsigned char has_color_map)
{
    unsigned char* pixels = buffer + id_length + 18;
    if (has_color_map)
        pixels += palette_length * 3;
    return pixels;
}
