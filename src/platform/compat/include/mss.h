// The retail game's Miles imports, implemented by the SDL3 software mixer.
// Keep the C signatures shared with the decompiled tree's miles_imports.h;
// several original return registers are deliberately unused by that tree.
#pragma once

#include <stdint.h>

#if defined(_WIN32)
#define MILES_CALL __stdcall
#else
#define MILES_CALL
#endif

typedef int32_t S32;
typedef uint32_t U32;
typedef float F32;
typedef void* HDIGDRIVER;
typedef void* HSAMPLE;
typedef void* H3DSAMPLE;
typedef void* HSTREAM;
typedef void* HPROVIDER;
typedef void* H3DPOBJECT;
typedef void* H3DLISTENER;

enum
{
    SMP_FREE = 1,
    SMP_DONE = 2,
    SMP_PLAYING = 4,
    SMP_STOPPED = 8
};

#ifdef __cplusplus
extern "C"
{
#endif
    void MILES_CALL AIL_set_redist_directory(const char* directory);
    int MILES_CALL AIL_startup(void);
    void MILES_CALL AIL_shutdown(void);
    HDIGDRIVER MILES_CALL AIL_open_digital_driver(
        unsigned int frequency, int bits, int channels, unsigned int flags);
    void MILES_CALL AIL_close_digital_driver(HDIGDRIVER driver);
    void MILES_CALL AIL_set_digital_master_volume(HDIGDRIVER driver, int volume);

    HSAMPLE MILES_CALL AIL_allocate_sample_handle(HDIGDRIVER driver);
    void MILES_CALL AIL_init_sample(HSAMPLE sample);
    void MILES_CALL AIL_set_named_sample_file(
        HSAMPLE sample, const char* extension, const void* data, unsigned int bytes, int block);
    void MILES_CALL AIL_set_sample_loop_count(HSAMPLE sample, unsigned int count);
    void MILES_CALL AIL_start_sample(HSAMPLE sample);
    void MILES_CALL AIL_end_sample(HSAMPLE sample);
    void MILES_CALL AIL_set_sample_volume(HSAMPLE sample, int volume);
    int MILES_CALL AIL_sample_volume(HSAMPLE sample);
    int MILES_CALL AIL_sample_status(HSAMPLE sample);

    int MILES_CALL AIL_enumerate_3D_providers(
        unsigned int* cursor, HPROVIDER* provider, const char** name);
    int MILES_CALL AIL_open_3D_provider(HPROVIDER provider);
    void MILES_CALL AIL_close_3D_provider(HPROVIDER provider);
    H3DLISTENER MILES_CALL AIL_open_3D_listener(HPROVIDER provider);
    H3DSAMPLE MILES_CALL AIL_allocate_3D_sample_handle(HPROVIDER provider);
    int MILES_CALL AIL_set_3D_sample_file(H3DSAMPLE sample, const void* file);
    void MILES_CALL AIL_set_3D_sample_loop_count(H3DSAMPLE sample, unsigned int count);
    void MILES_CALL AIL_set_3D_sample_distances(H3DSAMPLE sample, float maximum, float minimum);
    void MILES_CALL AIL_set_3D_position(H3DPOBJECT object, float x, float y, float z);
    void MILES_CALL AIL_start_3D_sample(H3DSAMPLE sample);
    void MILES_CALL AIL_end_3D_sample(H3DSAMPLE sample);
    void MILES_CALL AIL_set_3D_sample_volume(H3DSAMPLE sample, int volume);
    int MILES_CALL AIL_3D_sample_volume(H3DSAMPLE sample);
    int MILES_CALL AIL_3D_sample_status(H3DSAMPLE sample);

    // WAV/MP3 streams are decoded from the caller thread's prepared VFS into owned
    // PCM. The audio callback never uses the VFS or retains game-owned buffers.
    HSTREAM MILES_CALL AIL_open_stream(HDIGDRIVER driver, const char* filename, int memory);
    void MILES_CALL AIL_close_stream(HSTREAM stream);
    void MILES_CALL AIL_start_stream(HSTREAM stream);
    void MILES_CALL AIL_pause_stream(HSTREAM stream, int pause);
    void MILES_CALL AIL_set_stream_loop_count(HSTREAM stream, unsigned int count);
    void MILES_CALL AIL_set_stream_volume(HSTREAM stream, int volume);
    int MILES_CALL AIL_stream_status(HSTREAM stream);
#ifdef __cplusplus
}
#endif

#undef MILES_CALL
