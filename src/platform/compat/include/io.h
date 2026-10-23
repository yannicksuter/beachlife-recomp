#pragma once

#include <stddef.h>
#include <stdint.h>

// VC6 values, independent of the host's fcntl.h flags and 64-bit long/time_t.
#define _O_RDONLY 0x0000
#define _O_WRONLY 0x0001
#define _O_RDWR 0x0002
#define _O_APPEND 0x0008
#define _O_CREAT 0x0100
#define _O_TRUNC 0x0200
#define _O_EXCL 0x0400
#define _O_TEXT 0x4000
#define _O_BINARY 0x8000
#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_SUBDIR 0x10
#define _A_ARCH 0x20

struct _finddata_t
{
    unsigned attrib;
    int32_t time_create;
    int32_t time_access;
    int32_t time_write;
    uint32_t size;
    char name[260];
};

#ifdef __cplusplus
extern "C"
{
#endif
    int _open(const char* path, int flags, ...);
    int _read(int file, void* buffer, unsigned int bytes);
    int _write(int file, const void* buffer, unsigned int bytes);
    int32_t _lseek(int file, int32_t offset, int origin);
    int _close(int file);
    long _findfirst(const char* pattern, struct _finddata_t* result);
    int _findnext(long search, struct _finddata_t* result);
    int _findclose(long search);
#ifdef __cplusplus
}
#endif
