#include "io.h"
#include "kernel_files.h"
#include "msvcrt.h"
#include "native_kernel.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <memory>

namespace
{
using namespace beachlife::compat::kernel;
struct Stream
{
    int descriptor = -1;
    bool text = false;
    ~Stream()
    {
        if (descriptor >= 0)
        {
            _close(descriptor);
        }
    }
};

#ifndef _WIN32
ssize_t read_stream(void* cookie, char* buffer, std::size_t bytes)
{
    auto& stream = *static_cast<Stream*>(cookie);
    if (stream.text && bytes)
    {
        beachlife::compat::unsupported("fread", "MSVCRT text-mode input translation");
    }
    return _read(
        stream.descriptor, buffer, static_cast<unsigned>(std::min<std::size_t>(bytes, INT_MAX)));
}

ssize_t write_stream(void* cookie, const char* buffer, std::size_t bytes)
{
    auto& stream = *static_cast<Stream*>(cookie);
    // The current text-mode caller opens preferences with "wt" solely to
    // truncate it, then writes through WritePrivateProfileStringA.
    if (stream.text && bytes)
    {
        beachlife::compat::unsupported("fwrite", "MSVCRT text-mode output translation");
    }
    const auto written = _write(
        stream.descriptor, buffer, static_cast<unsigned>(std::min<std::size_t>(bytes, INT_MAX)));
    // GNU cookie write callbacks report an error with zero, never -1.
    return written < 0 ? 0 : written;
}

int seek_stream(void* cookie, off64_t* offset, int origin)
{
    return guard(
        [&]() -> int
        {
            const auto position =
                seek_file(static_cast<Stream*>(cookie)->descriptor, *offset, origin);
            if (!position || *position > INT64_MAX)
            {
                return -1;
            }
            *offset = static_cast<off64_t>(*position);
            return 0;
        },
        -1);
}

int close_stream(void* cookie)
{
    std::unique_ptr<Stream> stream(static_cast<Stream*>(cookie));
    const int result = _close(stream->descriptor);
    stream->descriptor = -1;
    return result;
}
#endif
} // namespace

namespace
{
// MSVCRT keeps the generator state per thread, starting at 1.
thread_local uint32_t random_state = 1;
} // namespace

extern "C"
{
    int beachlife_crt_rand(void) noexcept
    {
        random_state = random_state * 214013u + 2531011u;
        return static_cast<int>((random_state >> 16) & 0x7fff);
    }

    void beachlife_crt_srand(unsigned int seed) noexcept
    {
        random_state = seed;
    }

    FILE* beachlife_crt_fopen(const char* path, const char* mode)
    {
        return guard(
            [&]() -> FILE*
            {
                if (!mode || (*mode != 'r' && *mode != 'w' && *mode != 'a'))
                {
                    errno = EINVAL;
                    last_error = ERROR_INVALID_PARAMETER;
                    return nullptr;
                }
                bool update = false, binary = false, text = false;
                for (const char* flag = mode + 1; *flag; ++flag)
                {
                    if (*flag == '+' && !update)
                    {
                        update = true;
                    }
                    else if (*flag == 'b' && !binary && !text)
                    {
                        binary = true;
                    }
                    else if (*flag == 't' && !binary && !text)
                    {
                        text = true;
                    }
                    else
                    {
                        beachlife::compat::unsupported("fopen", "mode extensions");
                    }
                }
                int flags = update ? _O_RDWR : (*mode == 'r' ? _O_RDONLY : _O_WRONLY);
                flags |= _O_BINARY;
                if (*mode == 'w')
                {
                    flags |= _O_CREAT | _O_TRUNC;
                }
                if (*mode == 'a')
                {
                    flags |= _O_CREAT | _O_APPEND;
                }
                auto stream = std::make_unique<Stream>();
                stream->text = !binary;
                stream->descriptor = _open(path, flags);
                if (stream->descriptor < 0)
                {
                    return nullptr;
                }
#ifdef _WIN32
                FILE* result = stdio_stream(stream->descriptor);
                stream->descriptor = -1;
                if (result && stream->text && !beachlife::compat::native::text_mode(result))
                {
                    std::fclose(result);
                    return nullptr;
                }
                return result;
#else
                const char cookie_mode[] = {*mode, update ? '+' : '\0', '\0'};
                // A real host FILE preserves stdio buffering, element counts,
                // fgets/fgetc, seek, error and EOF semantics. Only opening is
                // redirected in game TUs; host stdio never gets interposed.
                FILE* result = ::fopencookie(stream.get(),
                    cookie_mode,
                    cookie_io_functions_t{read_stream, write_stream, seek_stream, close_stream});
                if (result)
                {
                    stream.release();
                }
                return result;
#endif
            },
            static_cast<FILE*>(nullptr));
    }

    int beachlife_crt_fseek(FILE* stream, int32_t offset, int origin)
    {
        return beachlife::compat::native::seek(stream, offset, origin);
    }

    int32_t beachlife_crt_ftell(FILE* stream)
    {
        const auto position = beachlife::compat::native::tell(stream);
        if (position < 0 || position > INT32_MAX)
        {
            if (position > INT32_MAX)
            {
                errno = EOVERFLOW;
            }
            return -1;
        }
        return static_cast<int32_t>(position);
    }

    int beachlife_crt_fgetpos(FILE* stream, void* output)
    {
        if (!output)
        {
            errno = EINVAL;
            return -1;
        }
        const auto position = beachlife::compat::native::tell(stream);
        if (position < 0)
        {
            return -1;
        }
        // VC6's fpos_t is a signed 64-bit byte offset, not glibc's mbstate.
        const int64_t value = position;
        std::memcpy(output, &value, sizeof(value));
        return 0;
    }
}
