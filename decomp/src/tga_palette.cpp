// Descriptive name; the original symbol and exact declaration are unknown.
// FUNCTION: BEACHLIFE 0x004ea3e0
unsigned char* GetTgaPaletteData(unsigned char* buffer, unsigned char id_length,
    unsigned char has_color_map)
{
    if (!has_color_map)
        return 0;
    return buffer + id_length + 18;
}
