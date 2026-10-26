#include "jpeg_export.h"

#include <climits>
#include <cstdio>

// A private implementation keeps stb's mutable orientation setting isolated
// from other users. Its initial zero value is never changed: rows are top-down.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#define STBIW_JPEG_SUBSAMPLING 1
#include "stb_image_write.h"

namespace beachlife::runtime::jpeg
{
namespace
{
OpenFunction open_function = nullptr;

struct Output
{
    std::FILE* file;
    bool failed;
};

void write_bytes(void* context, void* bytes, int count)
{
    auto& output = *static_cast<Output*>(context);
    if (!output.failed && count > 0 &&
        std::fwrite(bytes, 1, static_cast<std::size_t>(count), output.file) !=
            static_cast<std::size_t>(count))
    {
        output.failed = true;
    }
}
} // namespace

void set_open_function(OpenFunction open) noexcept
{
    open_function = open;
}

Result write_rgb420_file(const char* path,
    const unsigned char* pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t quality) noexcept
{
    if (!path || !pixels || width == 0 || height == 0 || width > 65535 || height > 65535 ||
        quality == 0 || quality > 100 ||
        static_cast<std::uint64_t>(width) * height > static_cast<std::uint64_t>(INT_MAX) / 3)
    {
        return Result::invalid_properties;
    }

    // The original writer requests read/write access and CREATE_ALWAYS. This
    // is the corresponding host stdio boundary; sharing/path rules differ.
    std::FILE* file = open_function ? open_function(path, "w+b") : std::fopen(path, "w+b");
    if (!file)
    {
        return Result::open_failed;
    }

    Output output{file, false};
    const int encoded = stbi_write_jpg_to_func(write_bytes,
        &output,
        static_cast<int>(width),
        static_cast<int>(height),
        3,
        pixels,
        static_cast<int>(quality));
    // stb's callback has no return value. Observe real short writes and delayed
    // stdio failures instead of treating encoder completion as file success.
    if (std::fflush(file) != 0)
    {
        output.failed = true;
    }
    const int closed = std::fclose(file);
    if (output.failed)
    {
        return Result::write_failed;
    }
    if (closed != 0)
    {
        return Result::close_failed;
    }
    return encoded ? Result::success : Result::encode_failed;
}

} // namespace beachlife::runtime::jpeg
