#include "wise.h"

#include "iso9660.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string_view>

namespace beachlife::data
{
namespace
{
constexpr std::string_view maindir = "%MAINDIR%\\";
// Bounds that keep a non-installer or damaged file cheap to reject.
constexpr std::uint64_t max_script_bytes = 4 * 1024 * 1024;
constexpr std::uint64_t max_installed_bytes = 2ull * 1024 * 1024 * 1024 - 1;
constexpr std::size_t max_streams = 512;
constexpr std::size_t scan_bytes = 8192;

struct Inflated
{
    std::uint64_t compressed = 0, size = 0;
    std::uint32_t crc = 0;
};

using Sink = std::function<void(std::span<const unsigned char>)>;

std::uint32_t u32(std::span<const unsigned char> bytes, std::size_t at)
{
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

// A seekable view of bytes already in memory (the scanned overlay start).
class MemoryFile final : public SourceFile
{
  public:
    explicit MemoryFile(std::vector<unsigned char> bytes)
        : bytes_(std::move(bytes))
    {
    }
    std::uint64_t size() const override
    {
        return bytes_.size();
    }
    std::uint64_t tell() const override
    {
        return position_;
    }
    void seek(std::uint64_t position) override
    {
        position_ = std::min<std::uint64_t>(position, bytes_.size());
    }
    std::size_t read(std::span<unsigned char> destination) override
    {
        const auto count = static_cast<std::size_t>(
            std::min<std::uint64_t>(destination.size(), bytes_.size() - position_));
        std::memcpy(destination.data(), bytes_.data() + position_, count);
        position_ += count;
        return count;
    }

  private:
    std::vector<unsigned char> bytes_;
    std::uint64_t position_ = 0;
};

// Inflates the raw-deflate stream at offset into sink and checks the CRC-32
// that follows it. Returns nullopt for anything that is not such a stream.
std::optional<Inflated> inflate_at(
    SourceFile& file, std::uint64_t offset, std::uint64_t limit, const Sink& sink)
{
    if (offset >= file.size())
    {
        return std::nullopt;
    }
    z_stream z{};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
    {
        throw std::runtime_error("Cannot start the Wise decompressor.");
    }
    struct End
    {
        z_stream& z;
        ~End()
        {
            inflateEnd(&z);
        }
    } end{z};
    std::array<unsigned char, 65536> input{}, output{};
    file.seek(offset);
    std::uint32_t crc = crc32(0, nullptr, 0);
    std::uint64_t total = 0;
    int status = Z_OK;
    while (status != Z_STREAM_END)
    {
        if (z.avail_in == 0)
        {
            const auto count = file.read(input);
            if (!count)
            {
                return std::nullopt;
            }
            z.next_in = input.data();
            z.avail_in = static_cast<uInt>(count);
        }
        z.next_out = output.data();
        z.avail_out = static_cast<uInt>(output.size());
        status = inflate(&z, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END)
        {
            return std::nullopt;
        }
        const auto produced = output.size() - z.avail_out;
        total += produced;
        if (total > limit)
        {
            return std::nullopt;
        }
        crc = crc32(crc, output.data(), static_cast<uInt>(produced));
        if (produced && sink)
        {
            sink(std::span(output).first(produced));
        }
    }
    std::array<unsigned char, 4> trailer{};
    file.seek(offset + z.total_in);
    if (file.read(trailer) != trailer.size() || u32(trailer, 0) != crc)
    {
        return std::nullopt;
    }
    return Inflated{z.total_in, total, crc};
}

std::optional<std::uint64_t> overlay_start(SourceFile& file)
{
    std::array<unsigned char, 4096> head{};
    file.seek(0);
    const auto count = file.read(head);
    if (count < 0x40 || head[0] != 'M' || head[1] != 'Z')
    {
        return std::nullopt;
    }
    const std::size_t pe = u32(head, 0x3c);
    if (pe > count - 24 || u32(head, pe) != 0x00004550)
    {
        return std::nullopt;
    }
    const std::size_t sections = head[pe + 6] | (head[pe + 7] << 8);
    const std::size_t optional = head[pe + 20] | (head[pe + 21] << 8);
    const std::size_t table = pe + 24 + optional;
    if (table + sections * 40 > count)
    {
        return std::nullopt;
    }
    std::uint64_t end = 0;
    for (std::size_t i = 0; i < sections; ++i)
    {
        const auto at = table + i * 40;
        end = std::max<std::uint64_t>(
            end, static_cast<std::uint64_t>(u32(head, at + 20)) + u32(head, at + 16));
    }
    if (!end || end >= file.size())
    {
        return std::nullopt;
    }
    return end;
}

struct Record
{
    std::string path;
    std::uint32_t start = 0, end = 0, size = 0, crc = 0;
};

// Install-file records: u32 start, u32 end, u32 DOS date/time, u32 inflated
// size, 20 zero bytes, u32 CRC-32, then the "%MAINDIR%\..." target and NUL.
// Start and end are relative to the installer's data area.
std::vector<Record> script_records(std::span<const unsigned char> script)
{
    std::vector<Record> records;
    const std::string_view text(reinterpret_cast<const char*>(script.data()), script.size());
    for (auto at = text.find(maindir); at != std::string_view::npos;
        at = text.find(maindir, at + 1))
    {
        if (at < 40)
        {
            continue;
        }
        const auto nul = text.find('\0', at);
        if (nul == std::string_view::npos)
        {
            break;
        }
        const auto zeros = script.subspan(at - 24, 20);
        Record record{std::string(text.substr(at + maindir.size(), nul - at - maindir.size())),
            u32(script, at - 40),
            u32(script, at - 36),
            u32(script, at - 28),
            u32(script, at - 4)};
        const bool printable = std::all_of(record.path.begin(),
            record.path.end(),
            [](char c)
            {
                return c >= 0x20 && c < 0x7f;
            });
        if (std::all_of(zeros.begin(),
                zeros.end(),
                [](unsigned char c)
                {
                    return c == 0;
                }) &&
            record.start < record.end && record.size && printable && !record.path.empty() &&
            record.path.back() != '\\')
        {
            records.push_back(std::move(record));
        }
    }
    return records;
}
} // namespace

std::optional<WiseInstaller> index_wise_file(SourceFile& file, std::string name)
{
    const auto overlay = overlay_start(file);
    if (!overlay)
    {
        return std::nullopt;
    }
    // The overlay header has no fixed size: the first stream is the first
    // offset that inflates to a complete stream with a matching CRC-32.
    std::vector<unsigned char> window(
        static_cast<std::size_t>(std::min<std::uint64_t>(256 * 1024, file.size() - *overlay)));
    file.seek(*overlay);
    window.resize(file.read(window));
    MemoryFile scan(window);
    std::optional<std::uint64_t> position;
    for (std::size_t skip = 0; skip < std::min(scan_bytes, window.size()) && !position; ++skip)
    {
        if (inflate_at(scan, skip, max_script_bytes, {}))
        {
            position = *overlay + skip;
        }
    }
    if (!position)
    {
        return std::nullopt;
    }
    std::vector<Record> records;
    std::vector<std::pair<std::uint64_t, Inflated>> streams;
    std::optional<std::uint64_t> base;
    const Record* first = nullptr;
    for (std::size_t count = 0; count < max_streams && !base; ++count)
    {
        std::vector<unsigned char> bytes;
        const bool script_pending = records.empty();
        const auto stream = inflate_at(file,
            *position,
            script_pending ? max_script_bytes : max_installed_bytes,
            [&](std::span<const unsigned char> chunk)
            {
                if (script_pending)
                {
                    bytes.insert(bytes.end(), chunk.begin(), chunk.end());
                }
            });
        if (!stream)
        {
            break;
        }
        if (script_pending)
        {
            records = script_records(bytes);
            if (!records.empty())
            {
                first = &*std::min_element(records.begin(),
                    records.end(),
                    [](const Record& left, const Record& right)
                    {
                        return left.start < right.start;
                    });
            }
        }
        else if (stream->size == first->size && stream->crc == first->crc &&
                 *position >= first->start)
        {
            base = *position - first->start;
        }
        *position += stream->compressed + 4;
    }
    if (!base)
    {
        return std::nullopt;
    }
    WiseInstaller result{std::move(name), {}};
    std::map<std::string, const Record*> seen;
    for (const auto& record : records)
    {
        if (!seen.emplace(record.path, &record).second)
        {
            throw std::runtime_error("The Wise script installs one path twice.");
        }
        result.files.push_back({record.path, *base + record.start, record.size, record.crc});
    }
    return result;
}

std::optional<WiseInstaller> index_wise(const IsoSource& source)
{
    for (const auto& entry : source.index().entries)
    {
        const auto& path = entry.path;
        const bool root = path.find('/') == std::string::npos;
        const bool executable =
            path.size() > 4 &&
            (path.ends_with(".exe") || path.ends_with(".EXE") || path.ends_with(".Exe"));
        if (entry.directory || !root || !executable || entry.size < 1024 * 1024)
        {
            continue;
        }
        auto file = source.open(path);
        if (auto installer = index_wise_file(*file, path))
        {
            return installer;
        }
    }
    return std::nullopt;
}

void extract_wise(const IsoSource& source,
    const WiseInstaller& installer,
    const WiseFile& file,
    const std::function<void(std::span<const unsigned char>)>& sink)
{
    auto stream = source.open(installer.installer);
    const auto result = inflate_at(*stream, file.offset, file.size, sink);
    if (!result || result->size != file.size || result->crc != file.crc)
    {
        throw std::runtime_error("A Wise installer file failed its size or CRC-32 check.");
    }
}

} // namespace beachlife::data
