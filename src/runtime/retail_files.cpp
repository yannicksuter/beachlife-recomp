#include "retail_files.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace beachlife::runtime::retail_files
{
namespace
{
thread_local Session* current_session = nullptr;
constexpr std::size_t read_chunk = 256 * 1024;
} // namespace

struct File
{
    std::shared_ptr<const data::FileSource> source;
    std::unique_ptr<data::SourceFile> stream;
    std::thread::id thread;
    Position cursor = 0;
};

namespace
{
bool usable(const File* file) noexcept
{
    return file && file->thread == std::this_thread::get_id();
}
} // namespace

Session::Session(
    std::shared_ptr<const data::FileSource> source, AllocationFailure allocation_failure)
    : source_(std::move(source))
    , previous_(current_session)
    , thread_(std::this_thread::get_id())
    , allocation_failure_(allocation_failure)
{
    if (!source_)
    {
        throw std::invalid_argument("A reconstructed file session requires a VFS.");
    }
    current_session = this;
}

Session::~Session()
{
    if (thread_ != std::this_thread::get_id() || current_session != this)
    {
        std::terminate();
    }
    current_session = previous_;
}

File* open(const char* filename, const char* mode) noexcept
{
    if (!current_session || !filename || !mode || std::strcmp(mode, "rb") != 0)
    {
        return nullptr;
    }
    try
    {
        auto stream = current_session->source_->open(filename);
        if (!stream)
        {
            return nullptr;
        }
        const auto cursor = stream->tell();
        return new File{
            current_session->source_, std::move(stream), std::this_thread::get_id(), cursor};
    }
    catch (const std::bad_alloc&)
    {
        if (current_session->allocation_failure_ == AllocationFailure::terminate_process)
        {
            std::abort();
        }
        return nullptr;
    }
    catch (...)
    {
        return nullptr;
    }
}

std::size_t read(void* buffer, std::size_t size, std::size_t count, File* file) noexcept
{
    if (!usable(file) || !size || !count || !buffer ||
        count > std::numeric_limits<std::size_t>::max() / size)
    {
        return 0;
    }
    const auto requested = size * count;
    auto* bytes = static_cast<unsigned char*>(buffer);
    std::size_t done = 0;
    try
    {
        const auto length = file->stream->size();
        while (done < requested && file->cursor < length)
        {
            const auto amount = static_cast<std::size_t>(
                std::min<Position>({requested - done, length - file->cursor, read_chunk}));
            const auto received = file->stream->read(std::span(bytes + done, amount));
            if (received == 0 || received > amount)
            {
                break;
            }
            done += received;
            file->cursor += received;
        }
    }
    catch (...)
    {
        // fread returns the completed elements; callers keep their original
        // choice of checking or ignoring that result.
    }
    return done / size;
}

int seek(File* file, std::int32_t offset, SeekOrigin origin) noexcept
{
    if (!usable(file))
    {
        return -1;
    }
    try
    {
        const auto length = file->stream->size();
        Position base;
        switch (origin)
        {
        case SeekOrigin::begin:
            base = 0;
            break;
        case SeekOrigin::current:
            base = file->cursor;
            break;
        case SeekOrigin::end:
            base = length;
            break;
        default:
            return -1;
        }
        const auto distance = static_cast<Position>(offset < 0 ? -std::int64_t(offset) : offset);
        if ((offset < 0 && base < distance) ||
            (offset >= 0 && base > std::numeric_limits<Position>::max() - distance))
        {
            return -1;
        }
        const auto destination = offset < 0 ? base - distance : base + distance;
        // stdio permits a read-only seek beyond EOF. Keep that logical cursor
        // while the bounded VFS stream remains at its end.
        file->stream->seek(std::min(destination, length));
        file->cursor = destination;
        return 0;
    }
    catch (...)
    {
        return -1;
    }
}

int position(File* file, Position* result) noexcept
{
    if (!usable(file) || !result)
    {
        return -1;
    }
    try
    {
        // Query the stream too so a VFS position failure remains observable.
        const auto actual = file->stream->tell();
        *result = file->cursor > file->stream->size() ? file->cursor : actual;
        return 0;
    }
    catch (...)
    {
        return -1;
    }
}

int close(File* file) noexcept
{
    if (!usable(file))
    {
        return -1;
    }
    delete file;
    return 0;
}

} // namespace beachlife::runtime::retail_files
