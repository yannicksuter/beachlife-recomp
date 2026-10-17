// Descriptive name; the original symbol and declaration are unknown.
// FUNCTION: BEACHLIFE 0x004d2280
unsigned int SoundNameHash(const char* name)
{
    unsigned int hash = 0;
    while (*name) {
        hash = hash * 5u + static_cast<signed char>(*name);
        ++name;
    }
    return hash;
}
