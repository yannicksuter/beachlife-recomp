#include <stdio.h>

typedef char CheckFilePositionSize[sizeof(fpos_t) == 8 ? 1 : -1];
volatile long fixture_result;

// Generated-toolchain fixture only. This is not a reconstructed game function.
extern "C" void fixture_entry()
{
    FILE* stream = fopen("generated.bin", "rb");
    if (stream)
    {
        fpos_t position = 0;
        if (fseek(stream, 0, SEEK_END) == 0 && fgetpos(stream, &position) == 0)
            fixture_result = (long)position;
        fclose(stream);
    }
}
