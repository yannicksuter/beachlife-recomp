extern "C" void __cdecl free(void* memory);

// LIBRARY: BEACHLIFE 0x004ee6d6
// ??3@YAXPAX@Z
void operator delete(void* memory)
{
    free(memory);
}
