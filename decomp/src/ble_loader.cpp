#include <stdio.h>

long GetFileSize(const char* filename);

// Descriptive name; the original symbol and allocation spelling are unknown.
// FUNCTION: BEACHLIFE 0x004ec220
char* LoadBleFile(const char* filename)
{
    long size = GetFileSize(filename);
    if (size) {
        char* buffer = new char[size + 1];
        FILE* file = fopen(filename, "rb");
        fread(buffer, 1, size, file);
        fclose(file);

        for (int index = 0; index < size; ++index)
            buffer[index] = ~buffer[index];
        buffer[size] = '.';
        return buffer;
    }
    return 0;
}
