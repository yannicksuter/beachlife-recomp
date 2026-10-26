#include "cabinet.h"

#include <libunshield.h>
extern "C"
{
#include <md5/global.h>
#include <md5/md5.h>
}

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <ostream>
#include <set>
#include <stdexcept>

namespace beachlife::data
{
namespace
{
constexpr std::uint64_t max_header = 16 * 1024 * 1024;
constexpr std::uint64_t max_entries = 100000;

std::string hex_digest(std::span<const unsigned char> bytes)
{
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : bytes)
    {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
}

class Header
{
  public:
    explicit Header(std::span<const unsigned char> bytes)
        : bytes_(bytes)
    {
    }
    void range(std::uint64_t offset, std::uint64_t count) const
    {
        if (offset > bytes_.size() || count > bytes_.size() - offset)
        {
            throw std::runtime_error("Cabinet metadata points outside data1.hdr.");
        }
    }
    std::uint64_t number(std::uint64_t offset, unsigned count = 4) const
    {
        range(offset, count);
        std::uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i)
        {
            value |= std::uint64_t(bytes_[offset + i]) << (8 * i);
        }
        return value;
    }
    void string(std::uint64_t offset) const
    {
        range(offset, 1);
        const auto count = std::min<std::uint64_t>(4097, bytes_.size() - offset);
        const auto text =
            bytes_.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(count));
        if (std::find(text.begin(), text.end(), 0) == text.end())
        {
            throw std::runtime_error("Cabinet name is unterminated or exceeds 4096 bytes.");
        }
    }

  private:
    std::span<const unsigned char> bytes_;
};

// The pinned library assumes valid in-memory offsets and can abort on corrupt
// metadata. Validate the supported v6 layout before giving it a stable snapshot.
CabinetIndex validate_header(std::span<const unsigned char> bytes)
{
    const Header h(bytes);
    h.range(0, 20);
    const auto version = h.number(4);
    if (h.number(0) != 0x28635349 || version >> 24 != 1 || ((version >> 12) & 15) != 6)
    {
        throw std::runtime_error("Expected InstallShield 6 data1.hdr metadata.");
    }
    const auto descriptor = h.number(12), descriptor_size = h.number(16);
    if (descriptor < 20 || descriptor_size < 0x276)
    {
        throw std::runtime_error("Invalid cabinet descriptor.");
    }
    h.range(descriptor, descriptor_size);
    const auto table = descriptor + h.number(descriptor + 0xc);
    const auto table_size = h.number(descriptor + 0x14);
    if (table_size != h.number(descriptor + 0x18))
    {
        throw std::runtime_error("Cabinet file-table sizes disagree.");
    }
    h.range(table, table_size);
    const auto directories = h.number(descriptor + 0x1c), files = h.number(descriptor + 0x28);
    if (directories + files > max_entries || (files && !directories))
    {
        throw std::runtime_error("Cabinet directory/file count is unsupported.");
    }
    // unshield reads this combined table even though v6 uses fixed file records.
    if ((directories + files) * 4 > table_size)
    {
        throw std::runtime_error("Truncated cabinet directory table.");
    }
    const auto records_offset = h.number(descriptor + 0x2c);
    if (records_offset > table_size || files * 0x57 > table_size - records_offset)
    {
        throw std::runtime_error("Truncated cabinet file descriptors.");
    }
    for (std::uint64_t i = 0; i < directories; ++i)
    {
        h.string(table + h.number(table + i * 4));
    }

    CabinetIndex result;
    result.header_bytes = bytes.size();
    result.directories.resize(static_cast<std::size_t>(directories));
    result.files.resize(static_cast<std::size_t>(files));
    for (std::size_t i = 0; i < result.files.size(); ++i)
    {
        const auto record = table + records_offset + i * 0x57;
        const auto flags = h.number(record, 2), name = h.number(record + 0x3a);
        if (h.number(record + 0x3e, 2) >= directories)
        {
            throw std::runtime_error("Cabinet file has an invalid directory index.");
        }
        if (name)
        {
            h.string(table + name);
        }
        auto& entry = result.files[i];
        entry.size = h.number(record + 2, 8);
        entry.compressed_size = h.number(record + 10, 8);
        entry.data_offset = h.number(record + 18, 8);
        entry.volume = static_cast<unsigned>(h.number(record + 0x55, 2));
        entry.flags = static_cast<unsigned>(flags);
        entry.link_flags = static_cast<unsigned>(h.number(record + 0x54, 1));
        entry.link_previous = static_cast<std::size_t>(h.number(record + 0x4c));
        entry.link_next = static_cast<std::size_t>(h.number(record + 0x50));
        entry.md5 = hex_digest(bytes.subspan(static_cast<std::size_t>(record + 26), 16));
        entry.kind = (flags & 8) || !name ? "placeholder"
                     : entry.data_offset  ? "cabinet"
                                          : "external";
    }

    for (const bool component : {false, true})
    {
        std::set<std::uint64_t> visited;
        for (unsigned bucket = 0; bucket < 71; ++bucket)
        {
            auto next = h.number(descriptor + (component ? 0x15a : 0x3e) + bucket * 4);
            while (next)
            {
                if (!visited.insert(next).second || visited.size() > 4096)
                {
                    throw std::runtime_error(
                        "Cyclic, repeated or excessive cabinet metadata links.");
                }
                const auto node = descriptor + next;
                h.range(node, 12);
                const auto offset = h.number(node + 4);
                if (!offset)
                {
                    throw std::runtime_error("Missing cabinet group/component record.");
                }
                const auto record = descriptor + offset;
                h.range(record, component ? 0x75 : 0x1e);
                const auto name = h.number(record);
                if (!name)
                {
                    throw std::runtime_error("Missing cabinet group/component name.");
                }
                h.string(descriptor + name);
                if (component)
                {
                    const auto count = h.number(record + 0x6f, 2);
                    const auto names_offset = h.number(record + 0x71);
                    if (count > 71 || (count && !names_offset))
                    {
                        throw std::runtime_error("Invalid cabinet component group table.");
                    }
                    const auto names = descriptor + names_offset;
                    h.range(names, count * 4);
                    for (std::uint64_t i = 0; i < count; ++i)
                    {
                        const auto group_name = h.number(names + i * 4);
                        if (!group_name)
                        {
                            throw std::runtime_error("Missing component group name.");
                        }
                        h.string(descriptor + group_name);
                    }
                }
                else
                {
                    const auto first = h.number(record + 0x16), last = h.number(record + 0x1a);
                    if (first > last || last >= files)
                    {
                        throw std::runtime_error("Cabinet group file range is invalid.");
                    }
                }
                next = h.number(node + 8);
            }
        }
    }
    return result;
}

struct HeaderFile
{
    std::span<const unsigned char> bytes;
    std::size_t position = 0;
};

void* open_header(const char* name, const char* mode, void* context) noexcept
{
    if (std::strcmp(name, "data1.hdr") || std::strcmp(mode, "rb"))
    {
        return nullptr;
    }
    try
    {
        return new HeaderFile{*static_cast<std::vector<unsigned char>*>(context), 0};
    }
    catch (...)
    {
        return nullptr;
    }
}
int seek_header(void* opaque, long offset, int whence, void*) noexcept
{
    auto& file = *static_cast<HeaderFile*>(opaque);
    std::uint64_t base = 0;
    if (whence == SEEK_CUR)
    {
        base = file.position;
    }
    else if (whence == SEEK_END)
    {
        base = file.bytes.size();
    }
    else if (whence != SEEK_SET)
    {
        return -1;
    }
    const auto distance = offset < 0 ? std::uint64_t(-(offset + 1)) + 1 : std::uint64_t(offset);
    if ((offset < 0 && distance > base) || (offset >= 0 && distance > file.bytes.size() - base))
    {
        return -1;
    }
    file.position = static_cast<std::size_t>(offset < 0 ? base - distance : base + distance);
    return 0;
}
long tell_header(void* opaque, void*) noexcept
{
    return static_cast<long>(static_cast<HeaderFile*>(opaque)->position);
}
std::size_t read_header(
    void* output, std::size_t size, std::size_t count, void* opaque, void*) noexcept
{
    if (!size || !count || count > std::numeric_limits<std::size_t>::max() / size)
    {
        return 0;
    }
    auto& file = *static_cast<HeaderFile*>(opaque);
    const auto bytes = std::min(size * count, file.bytes.size() - file.position);
    if (bytes)
    {
        std::memcpy(output, file.bytes.data() + file.position, bytes);
    }
    file.position += bytes;
    return bytes / size;
}
std::size_t reject_write(const void*, std::size_t, std::size_t, void*, void*) noexcept
{
    return 0;
}
int close_header(void* file, void*) noexcept
{
    delete static_cast<HeaderFile*>(file);
    return 0;
}
void* no_directory(const char*, void*) noexcept
{
    return nullptr;
}
int close_directory(void*, void*) noexcept
{
    return 0;
}
struct dirent* no_entry(void*, void*) noexcept
{
    return nullptr;
}
const UnshieldIoCallbacks callbacks = {open_header,
    seek_header,
    tell_header,
    read_header,
    reject_write,
    close_header,
    no_directory,
    close_directory,
    no_entry};

void json_bytes(std::ostream& output, const std::string& text)
{
    constexpr char hex[] = "0123456789abcdef";
    output << '"';
    for (const unsigned char ch : text)
    {
        if (ch == '"' || ch == '\\')
        {
            output << '\\' << static_cast<char>(ch);
        }
        else if (ch >= 32 && ch < 127)
        {
            output << static_cast<char>(ch);
        }
        else
        {
            output << "\\u00" << hex[ch >> 4] << hex[ch & 15];
        }
    }
    output << '"';
}
} // namespace

namespace
{
std::vector<unsigned char> load_header(const IsoSource& source)
{
    auto file = source.open("data1.hdr");
    if (file->size() > max_header)
    {
        throw std::runtime_error("Cabinet metadata exceeds 16 MiB.");
    }
    std::vector<unsigned char> header(static_cast<std::size_t>(file->size()));
    if (file->read(header) != header.size())
    {
        throw std::runtime_error("Truncated data1.hdr.");
    }
    return header;
}
CabinetIndex parse_index(std::vector<unsigned char>& header)
{
    auto result = validate_header(header);
    static const bool logging_configured = []
    {
        unshield_set_log_level(UNSHIELD_LOG_LEVEL_ERROR);
        return true;
    }();
    (void)logging_configured;
    std::unique_ptr<Unshield, decltype(&unshield_close)> cabinet(
        unshield_open2("data1.cab", &callbacks, &header), unshield_close);
    if (!cabinet)
    {
        throw std::runtime_error("Cannot parse the cabinet metadata.");
    }
    auto* handle = cabinet.get();
    if (unshield_file_count(handle) != static_cast<int>(result.files.size()) ||
        unshield_directory_count(handle) != static_cast<int>(result.directories.size()))
    {
        throw std::runtime_error("Cabinet metadata counts disagree.");
    }
    for (int i = 0; i < unshield_component_count(handle); ++i)
    {
        result.components.emplace_back(unshield_component_name(handle, i));
    }
    for (int i = 0; i < unshield_file_group_count(handle); ++i)
    {
        const auto* group = unshield_file_group_get(handle, i);
        result.groups.push_back({group->name, group->first_file, group->last_file});
    }
    for (std::size_t i = 0; i < result.directories.size(); ++i)
    {
        result.directories[i] = unshield_directory_name(handle, static_cast<int>(i));
    }
    for (std::size_t i = 0; i < result.files.size(); ++i)
    {
        auto& entry = result.files[i];
        const auto index = static_cast<int>(i);
        if (entry.kind != "placeholder")
        {
            entry.name = unshield_file_name(handle, index);
        }
        entry.directory =
            result.directories.at(static_cast<std::size_t>(unshield_file_directory(handle, index)));
        if (entry.size != unshield_file_size(handle, index) ||
            (entry.kind == "cabinet") != unshield_file_is_valid(handle, index))
        {
            throw std::runtime_error("Cabinet file descriptor interpretations disagree.");
        }
    }
    return result;
}
} // namespace

CabinetIndex index_cabinet(const IsoSource& source)
{
    auto header = load_header(source);
    return parse_index(header);
}

struct CabinetReader::Impl
{
    struct File
    {
        HeaderFile header{};
        std::unique_ptr<SourceFile> input;
        bool output = false;
    };
    const IsoSource& source;
    std::vector<unsigned char> header;
    CabinetIndex metadata;
    std::string volume;
    std::function<void(std::span<const unsigned char>)> sink;
    std::uint64_t written = 0, expected = 0;
    std::exception_ptr error;
    std::unique_ptr<Unshield, decltype(&unshield_close)> archive{nullptr, unshield_close};

    static void* open(const char* name, const char* mode, void* context) noexcept
    {
        auto& self = *static_cast<Impl*>(context);
        try
        {
            auto file = std::make_unique<File>();
            if (!std::strcmp(mode, "rb") && !std::strcmp(name, "data1.hdr"))
            {
                file->header.bytes = self.header;
            }
            else if (!std::strcmp(mode, "rb") && name == self.volume)
            {
                file->input = self.source.open(self.volume);
            }
            else if (!std::strcmp(mode, "wb") && !std::strcmp(name, "vfs-output") && self.sink)
            {
                file->output = true;
            }
            else
            {
                return nullptr;
            }
            return file.release();
        }
        catch (...)
        {
            self.error = std::current_exception();
            return nullptr;
        }
    }
    static int seek(void* opaque, long offset, int whence, void* context) noexcept
    {
        auto& self = *static_cast<Impl*>(context);
        auto& file = *static_cast<File*>(opaque);
        try
        {
            if (file.output)
            {
                return -1;
            }
            if (!file.input)
            {
                return seek_header(&file.header, offset, whence, nullptr);
            }
            const auto base = whence == SEEK_SET   ? 0
                              : whence == SEEK_CUR ? file.input->tell()
                                                   : file.input->size();
            if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
            {
                return -1;
            }
            const auto distance =
                offset < 0 ? std::uint64_t(-(offset + 1)) + 1 : std::uint64_t(offset);
            if ((offset < 0 && distance > base) ||
                (offset >= 0 && distance > file.input->size() - base))
            {
                return -1;
            }
            file.input->seek(offset < 0 ? base - distance : base + distance);
            return 0;
        }
        catch (...)
        {
            self.error = std::current_exception();
            return -1;
        }
    }
    static long tell(void* opaque, void*) noexcept
    {
        auto& file = *static_cast<File*>(opaque);
        if (file.output)
        {
            return -1;
        }
        return file.input ? static_cast<long>(file.input->tell())
                          : tell_header(&file.header, nullptr);
    }
    static std::size_t read(
        void* output, std::size_t size, std::size_t count, void* opaque, void* context) noexcept
    {
        auto& self = *static_cast<Impl*>(context);
        auto& file = *static_cast<File*>(opaque);
        if (!size || !count || count > std::numeric_limits<std::size_t>::max() / size ||
            file.output)
        {
            return 0;
        }
        try
        {
            if (!file.input)
            {
                return read_header(output, size, count, &file.header, nullptr);
            }
            return file.input->read({static_cast<unsigned char*>(output), size * count}) / size;
        }
        catch (...)
        {
            self.error = std::current_exception();
            return 0;
        }
    }
    static std::size_t write(const void* bytes,
        std::size_t size,
        std::size_t count,
        void* opaque,
        void* context) noexcept
    {
        auto& self = *static_cast<Impl*>(context);
        if (!static_cast<File*>(opaque)->output || !size ||
            count > std::numeric_limits<std::size_t>::max() / size)
        {
            return 0;
        }
        try
        {
            const auto length = size * count;
            if (length > self.expected - self.written)
            {
                throw std::runtime_error("Cabinet output exceeds its declared size.");
            }
            self.sink({static_cast<const unsigned char*>(bytes), length});
            self.written += length;
            return count;
        }
        catch (...)
        {
            self.error = std::current_exception();
            return 0;
        }
    }
    static int close(void* opaque, void*) noexcept
    {
        delete static_cast<File*>(opaque);
        return 0;
    }

    explicit Impl(const IsoSource& input)
        : source(input)
        , header(load_header(input))
        , metadata(parse_index(header))
    {
        static const UnshieldIoCallbacks io{
            open, seek, tell, read, write, close, no_directory, close_directory, no_entry};
        archive.reset(unshield_open2("data1.cab", &io, this));
        if (!archive)
        {
            throw std::runtime_error("Cannot open validated cabinet metadata.");
        }
    }
};

CabinetReader::CabinetReader(const IsoSource& source)
    : impl_(std::make_unique<Impl>(source))
{
}
CabinetReader::~CabinetReader() = default;
const CabinetIndex& CabinetReader::index() const
{
    return impl_->metadata;
}

void CabinetReader::extract(
    std::size_t index, const std::function<void(std::span<const unsigned char>)>& sink)
{
    auto& self = *impl_;
    // Retail font variants share a payload via backward links. Resolve these
    // ourselves with a bounded walk, rather than unshield's recursive lookup.
    const auto& requested = self.metadata.files.at(index);
    std::set<std::size_t> visited;
    for (;;)
    {
        const auto& linked = self.metadata.files.at(index);
        if (!visited.insert(index).second || (linked.link_flags & ~3u) ||
            ((linked.link_flags & 2) && linked.link_next >= self.metadata.files.size()) ||
            linked.size != requested.size || linked.md5 != requested.md5 ||
            linked.kind != requested.kind || (linked.flags & ~4u))
        {
            throw std::runtime_error("Invalid, cyclic or inconsistent cabinet payload link.");
        }
        if (!(linked.link_flags & 1))
        {
            break;
        }
        if (linked.link_previous >= self.metadata.files.size())
        {
            throw std::runtime_error("Cabinet payload link points outside the file table.");
        }
        index = linked.link_previous;
    }
    const auto& entry = self.metadata.files.at(index);
    if (!sink || entry.kind == "placeholder" || (entry.flags & ~4u))
    {
        throw std::runtime_error("Unsupported cabinet member flags or split member.");
    }
    // The pinned reader uses unsigned int byte counters and long I/O offsets.
    // Enforce a common cross-platform limit instead of allowing truncation.
    constexpr auto limit = std::uint64_t(std::numeric_limits<std::int32_t>::max());
    if (entry.size > limit || entry.compressed_size > limit || entry.data_offset > limit)
    {
        throw std::runtime_error("Cabinet member exceeds the supported 2 GiB limit.");
    }
    if (entry.kind == "external")
    {
        if (entry.flags || entry.size != entry.compressed_size)
        {
            throw std::runtime_error("Unsupported external member encoding.");
        }
        auto file = self.source.open(
            entry.directory.empty() ? entry.name : entry.directory + "/" + entry.name);
        if (file->size() != entry.size)
        {
            throw std::runtime_error("External member size differs from the cabinet descriptor.");
        }
        MD5_CTX md5;
        MD5Init(&md5);
        std::array<unsigned char, 65536> buffer{};
        while (const auto received = file->read(buffer))
        {
            MD5Update(&md5, buffer.data(), static_cast<unsigned>(received));
            sink(std::span(buffer).first(received));
        }
        std::array<unsigned char, 16> digest{};
        MD5Final(digest.data(), &md5);
        if (hex_digest(digest) != entry.md5)
        {
            throw std::runtime_error("External member checksum failed.");
        }
        return;
    }
    self.volume = "data" + std::to_string(entry.volume) + ".cab";
    auto volume = self.source.open(self.volume);
    if (volume->size() > limit || volume->size() < 84 || entry.data_offset < 84 ||
        entry.data_offset > volume->size() ||
        entry.compressed_size > volume->size() - entry.data_offset ||
        (!(entry.flags & 4) && entry.size != entry.compressed_size))
    {
        throw std::runtime_error(
            "Cabinet volume is truncated or has an unsupported member extent.");
    }
    std::array<unsigned char, 84> header{};
    if (volume->read(header) != header.size())
    {
        throw std::runtime_error("Truncated cabinet volume header.");
    }
    const Header h(header);
    const auto version = h.number(4);
    if (h.number(0) != 0x28635349 || version >> 24 != 1 || ((version >> 12) & 15) != 6 ||
        index < h.number(28) || index > h.number(32))
    {
        throw std::runtime_error("Cabinet volume header does not describe this member.");
    }
    self.error = nullptr;
    self.written = 0;
    self.expected = entry.size;
    self.sink = sink;
    const bool success =
        unshield_file_save(self.archive.get(), static_cast<int>(index), "vfs-output");
    self.sink = {};
    if (self.error)
    {
        std::rethrow_exception(self.error);
    }
    if (!success || self.written != entry.size)
    {
        throw std::runtime_error("Cabinet member decompression or checksum verification failed.");
    }
}

void write_cabinet_json(std::ostream& output, const CabinetIndex& index)
{
    output << "{\"format_version\":6,\"name_encoding\":\"byte-values\",\"header_bytes\":"
           << index.header_bytes;
    auto strings = [&](const char* name, const std::vector<std::string>& values)
    {
        output << ",\"" << name << "\":[";
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (i)
            {
                output << ',';
            }
            json_bytes(output, values[i]);
        }
        output << ']';
    };
    strings("components", index.components);
    strings("directories", index.directories);
    output << ",\"groups\":[";
    for (std::size_t i = 0; i < index.groups.size(); ++i)
    {
        if (i)
        {
            output << ',';
        }
        const auto& group = index.groups[i];
        output << "{\"name\":";
        json_bytes(output, group.name);
        output << ",\"first_file\":" << group.first_file << ",\"last_file\":" << group.last_file
               << '}';
    }
    output << "],\"files\":[";
    for (std::size_t i = 0; i < index.files.size(); ++i)
    {
        if (i)
        {
            output << ',';
        }
        const auto& file = index.files[i];
        output << "{\"index\":" << i << ",\"name\":";
        json_bytes(output, file.name);
        output << ",\"directory\":";
        json_bytes(output, file.directory);
        output << ",\"kind\":";
        json_bytes(output, file.kind);
        output << ",\"size\":" << file.size << ",\"compressed_size\":" << file.compressed_size
               << ",\"data_offset\":" << file.data_offset << ",\"volume\":" << file.volume << '}';
    }
    output << "]}\n";
}
} // namespace beachlife::data
