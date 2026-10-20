#include "runtime/resource_files.h"

#include <algorithm>
#include <bit>
#include <climits>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace beachlife::runtime::resource_files
{
static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && sizeof(unsigned int) == 4);

struct Stream
{
    std::unique_ptr<data::SourceFile> file;
    std::uint64_t cursor;
};

struct SessionState
{
    std::shared_ptr<const data::FileSource> source;
    SessionState* previous;
    std::thread::id thread;
    int first_descriptor;
    std::vector<std::unique_ptr<Stream>> streams;
    std::vector<Stream*> descriptors;
};

namespace
{
thread_local SessionState* current = nullptr;
constexpr std::size_t read_chunk = 256 * 1024;

// Validate a token's identity before accessing it. In particular, an outer
// Session's token cannot accidentally address the current inner Session.
Stream* owned(Stream* token) noexcept
{
    if (current)
    {
        for (const auto& stream : current->streams)
        {
            if (stream.get() == token)
            {
                return token;
            }
        }
    }
    return nullptr;
}

Stream* descriptor_stream(int descriptor) noexcept
{
    if (!current || descriptor < current->first_descriptor ||
        static_cast<std::size_t>(descriptor - current->first_descriptor) >=
            current->descriptors.size())
    {
        return nullptr;
    }
    return current->descriptors[static_cast<std::size_t>(descriptor - current->first_descriptor)];
}

struct ReadResult
{
    std::size_t bytes = 0;
    bool error = false;
};

ReadResult read_bytes(Stream& stream, void* buffer, std::size_t bytes) noexcept
{
    ReadResult result;
    if (!bytes)
    {
        return result;
    }
    auto* destination = static_cast<unsigned char*>(buffer);
    try
    {
        const auto length = stream.file->size();
        while (result.bytes < bytes && stream.cursor < length)
        {
            const auto amount = static_cast<std::size_t>(std::min<std::uint64_t>(
                {bytes - result.bytes, read_chunk, length - stream.cursor}));
            const auto received = stream.file->read({destination + result.bytes, amount});
            if (received > amount)
            {
                result.error = true;
                break;
            }
            if (!received)
            {
                break;
            }
            result.bytes += received;
            stream.cursor += received;
        }
    }
    catch (...)
    {
        // A throwing provider may already have modified the destination. Do
        // not roll back bytes or fabricate a successful raw-read result.
        result.error = true;
    }
    return result;
}

int close_owned(Stream* stream) noexcept
{
    if (!stream)
    {
        return -1;
    }
    if (!stream->file)
    {
        return -1;
    }
    stream->file.reset();
    return 0;
}
} // namespace

Session::Session(std::shared_ptr<const data::FileSource> source, int first_descriptor)
{
    if (!source || first_descriptor < 0)
    {
        throw std::invalid_argument(
            "Resource files require a VFS and nonnegative descriptor base.");
    }
    state_ = std::make_unique<SessionState>(SessionState{
        std::move(source), current, std::this_thread::get_id(), first_descriptor, {}, {}});
    current = state_.get();
}

Session::~Session()
{
    if (current != state_.get() || state_->thread != std::this_thread::get_id())
    {
        std::terminate();
    }
    current = state_->previous;
    // State destruction releases any streams the game left open, then the
    // stable tokens and mount. It does not change a game field or skip a call.
}

Stream* open_stream(const char* path, const char* mode) noexcept
{
    if (!current || !path || !mode || std::strcmp(mode, "rb") != 0)
    {
        return nullptr;
    }
    try
    {
        auto file = current->source->open(path);
        if (!file)
        {
            return nullptr;
        }
        const auto cursor = file->tell();
        auto stream = std::make_unique<Stream>(Stream{std::move(file), cursor});
        Stream* result = stream.get();
        current->streams.push_back(std::move(stream));
        return result;
    }
    catch (...)
    {
        return nullptr;
    }
}

std::size_t read_stream(void* buffer, std::size_t size, std::size_t count, Stream* token) noexcept
{
    Stream* stream = owned(token);
    if (!stream || !stream->file || !buffer || !size || !count ||
        count > std::numeric_limits<std::size_t>::max() / size)
    {
        return 0;
    }
    // fread reports complete elements, including after a partial final element
    // or an I/O failure. The copied partial bytes are retained.
    return read_bytes(*stream, buffer, size * count).bytes / size;
}

int close_stream(Stream* stream) noexcept
{
    return close_owned(owned(stream));
}

bool stream_position(Stream* token, std::uint64_t& position) noexcept
{
    Stream* stream = owned(token);
    if (!stream || !stream->file)
    {
        return false;
    }
    position = stream->cursor;
    return true;
}

int open_descriptor(const char* path, int flags) noexcept
{
    if (!current || (flags != 0 && flags != 0x8000) ||
        current->descriptors.size() > static_cast<std::size_t>(INT_MAX - current->first_descriptor))
    {
        return -1;
    }
    Stream* stream = open_stream(path, "rb");
    if (!stream)
    {
        return -1;
    }
    try
    {
        const int descriptor =
            current->first_descriptor + static_cast<int>(current->descriptors.size());
        current->descriptors.push_back(stream);
        return descriptor;
    }
    catch (...)
    {
        close_owned(stream);
        return -1;
    }
}

int read_descriptor(int descriptor, void* buffer, unsigned int bytes) noexcept
{
    Stream* stream = descriptor_stream(descriptor);
    if (!stream || !stream->file || (!buffer && bytes))
    {
        return -1;
    }
    const auto result = read_bytes(*stream, buffer, bytes);
    return result.error ? -1 : std::bit_cast<int>(static_cast<unsigned int>(result.bytes));
}

int seek_descriptor(int descriptor, unsigned int displacement, int origin) noexcept
{
    Stream* stream = descriptor_stream(descriptor);
    if (!stream || !stream->file || origin < 0 || origin > 2)
    {
        return -1;
    }
    try
    {
        const auto length = stream->file->size();
        const std::uint64_t base = origin == 0 ? 0 : origin == 1 ? stream->cursor : length;
        const int signed_displacement = std::bit_cast<int>(displacement);
        const auto distance = static_cast<std::uint64_t>(
            signed_displacement < 0 ? -std::int64_t(signed_displacement) : signed_displacement);
        if ((signed_displacement < 0 && base < distance) ||
            (signed_displacement >= 0 &&
                base > std::numeric_limits<std::uint64_t>::max() - distance))
        {
            return -1;
        }
        const auto destination = signed_displacement < 0 ? base - distance : base + distance;
        stream->file->seek(std::min(destination, length));
        stream->cursor = destination;
        return std::bit_cast<int>(static_cast<unsigned int>(destination));
    }
    catch (...)
    {
        return -1;
    }
}

int close_descriptor(int descriptor) noexcept
{
    return close_owned(descriptor_stream(descriptor));
}

} // namespace beachlife::runtime::resource_files
