#include <stdio.h>

// Descriptive name; the original symbol and signed return declaration are unknown.
// FUNCTION: BEACHLIFE 0x004ec150
long GetFileSize(const char* filename)
{
    FILE* file = fopen(filename, "rb");
    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END) == 0)
    {
        fpos_t position;
        if (fgetpos(file, &position) == 0)
        {
            long result = (long)position;
            fclose(file);
            return result;
        }
    }
    fclose(file);
    return 0;
}
