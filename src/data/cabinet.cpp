#include "cabinet.h"

#include <libunshield.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <ostream>
#include <set>
#include <stdexcept>

namespace beachlife::data {
namespace {
constexpr std::uint64_t max_header = 16 * 1024 * 1024;
constexpr std::uint64_t max_entries = 100000;

class Header {
public:
    explicit Header(std::span<const unsigned char> bytes) : bytes_(bytes) {}
    void range(std::uint64_t offset, std::uint64_t count) const {
        if (offset > bytes_.size() || count > bytes_.size()-offset)
            throw std::runtime_error("Cabinet metadata points outside data1.hdr.");
    }
    std::uint64_t number(std::uint64_t offset, unsigned count = 4) const {
        range(offset, count);
        std::uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i) value |= std::uint64_t(bytes_[offset+i]) << (8*i);
        return value;
    }
    void string(std::uint64_t offset) const {
        range(offset, 1);
        const auto count = std::min<std::uint64_t>(4097, bytes_.size()-offset);
        const auto text = bytes_.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(count));
        if (std::find(text.begin(), text.end(), 0) == text.end())
            throw std::runtime_error("Cabinet name is unterminated or exceeds 4096 bytes.");
    }
private:
    std::span<const unsigned char> bytes_;
};

// The pinned library assumes valid in-memory offsets and can abort on corrupt
// metadata. Validate the supported v6 layout before giving it a stable snapshot.
CabinetIndex validate_header(std::span<const unsigned char> bytes) {
    const Header h(bytes);
    h.range(0, 20);
    const auto version = h.number(4);
    if (h.number(0) != 0x28635349 || version >> 24 != 1 || ((version >> 12) & 15) != 6)
        throw std::runtime_error("Expected InstallShield 6 data1.hdr metadata.");
    const auto descriptor = h.number(12), descriptor_size = h.number(16);
    if (descriptor < 20 || descriptor_size < 0x276)
        throw std::runtime_error("Invalid cabinet descriptor.");
    h.range(descriptor, descriptor_size);
    const auto table = descriptor+h.number(descriptor+0xc);
    const auto table_size = h.number(descriptor+0x14);
    if (table_size != h.number(descriptor+0x18))
        throw std::runtime_error("Cabinet file-table sizes disagree.");
    h.range(table, table_size);
    const auto directories = h.number(descriptor+0x1c), files = h.number(descriptor+0x28);
    if (directories+files > max_entries || (files && !directories))
        throw std::runtime_error("Cabinet directory/file count is unsupported.");
    // unshield reads this combined table even though v6 uses fixed file records.
    if ((directories+files)*4 > table_size)
        throw std::runtime_error("Truncated cabinet directory table.");
    const auto records_offset = h.number(descriptor+0x2c);
    if (records_offset > table_size || files*0x57 > table_size-records_offset)
        throw std::runtime_error("Truncated cabinet file descriptors.");
    for (std::uint64_t i = 0; i < directories; ++i) h.string(table+h.number(table+i*4));

    CabinetIndex result;
    result.header_bytes = bytes.size();
    result.directories.resize(static_cast<std::size_t>(directories));
    result.files.resize(static_cast<std::size_t>(files));
    for (std::size_t i = 0; i < result.files.size(); ++i) {
        const auto record = table+records_offset+i*0x57;
        const auto flags = h.number(record, 2), name = h.number(record+0x3a);
        if (h.number(record+0x3e, 2) >= directories)
            throw std::runtime_error("Cabinet file has an invalid directory index.");
        if (name) h.string(table+name);
        auto& entry = result.files[i];
        entry.size = h.number(record+2, 8);
        entry.compressed_size = h.number(record+10, 8);
        entry.data_offset = h.number(record+18, 8);
        entry.volume = static_cast<unsigned>(h.number(record+0x55, 2));
        entry.kind = (flags & 8) || !name ? "placeholder" : entry.data_offset ? "cabinet" : "external";
    }

    for (const bool component : {false, true}) {
        std::set<std::uint64_t> visited;
        for (unsigned bucket = 0; bucket < 71; ++bucket) {
            auto next = h.number(descriptor+(component ? 0x15a : 0x3e)+bucket*4);
            while (next) {
                if (!visited.insert(next).second || visited.size() > 4096)
                    throw std::runtime_error("Cyclic, repeated or excessive cabinet metadata links.");
                const auto node = descriptor+next;
                h.range(node, 12);
                const auto offset = h.number(node+4);
                if (!offset) throw std::runtime_error("Missing cabinet group/component record.");
                const auto record = descriptor+offset;
                h.range(record, component ? 0x75 : 0x1e);
                const auto name = h.number(record);
                if (!name) throw std::runtime_error("Missing cabinet group/component name.");
                h.string(descriptor+name);
                if (component) {
                    const auto count = h.number(record+0x6f, 2);
                    const auto names_offset = h.number(record+0x71);
                    if (count > 71 || (count && !names_offset))
                        throw std::runtime_error("Invalid cabinet component group table.");
                    const auto names = descriptor+names_offset;
                    h.range(names, count*4);
                    for (std::uint64_t i = 0; i < count; ++i) {
                        const auto group_name = h.number(names+i*4);
                        if (!group_name) throw std::runtime_error("Missing component group name.");
                        h.string(descriptor+group_name);
                    }
                } else {
                    const auto first = h.number(record+0x16), last = h.number(record+0x1a);
                    if (first > last || last >= files)
                        throw std::runtime_error("Cabinet group file range is invalid.");
                }
                next = h.number(node+8);
            }
        }
    }
    return result;
}

struct HeaderFile {
    std::span<const unsigned char> bytes;
    std::size_t position = 0;
};

void* open_header(const char* name, const char* mode, void* context) noexcept {
    if (std::strcmp(name, "data1.hdr") || std::strcmp(mode, "rb")) return nullptr;
    try { return new HeaderFile{*static_cast<std::vector<unsigned char>*>(context), 0}; }
    catch (...) { return nullptr; }
}
int seek_header(void* opaque, long offset, int whence, void*) noexcept {
    auto& file = *static_cast<HeaderFile*>(opaque);
    std::uint64_t base = 0;
    if (whence == SEEK_CUR) base = file.position;
    else if (whence == SEEK_END) base = file.bytes.size();
    else if (whence != SEEK_SET) return -1;
    const auto distance = offset < 0 ? std::uint64_t(-(offset+1))+1 : std::uint64_t(offset);
    if ((offset < 0 && distance > base) || (offset >= 0 && distance > file.bytes.size()-base)) return -1;
    file.position = static_cast<std::size_t>(offset < 0 ? base-distance : base+distance);
    return 0;
}
long tell_header(void* opaque, void*) noexcept {
    return static_cast<long>(static_cast<HeaderFile*>(opaque)->position);
}
std::size_t read_header(void* output, std::size_t size, std::size_t count, void* opaque, void*) noexcept {
    if (!size || !count || count > std::numeric_limits<std::size_t>::max()/size) return 0;
    auto& file = *static_cast<HeaderFile*>(opaque);
    const auto bytes = std::min(size*count, file.bytes.size()-file.position);
    if (bytes) std::memcpy(output, file.bytes.data()+file.position, bytes);
    file.position += bytes;
    return bytes/size;
}
std::size_t reject_write(const void*, std::size_t, std::size_t, void*, void*) noexcept { return 0; }
int close_header(void* file, void*) noexcept { delete static_cast<HeaderFile*>(file); return 0; }
void* no_directory(const char*, void*) noexcept { return nullptr; }
int close_directory(void*, void*) noexcept { return 0; }
struct dirent* no_entry(void*, void*) noexcept { return nullptr; }
const UnshieldIoCallbacks callbacks = {
    open_header, seek_header, tell_header, read_header, reject_write, close_header,
    no_directory, close_directory, no_entry
};

void json_bytes(std::ostream& output, const std::string& text) {
    constexpr char hex[] = "0123456789abcdef";
    output << '"';
    for (const unsigned char ch : text) {
        if (ch == '"' || ch == '\\') output << '\\' << static_cast<char>(ch);
        else if (ch >= 32 && ch < 127) output << static_cast<char>(ch);
        else output << "\\u00" << hex[ch >> 4] << hex[ch & 15];
    }
    output << '"';
}
} // namespace

CabinetIndex index_cabinet(const IsoSource& source) {
    auto file = source.open("data1.hdr");
    if (file->size() > max_header) throw std::runtime_error("Cabinet metadata exceeds 16 MiB.");
    std::vector<unsigned char> header(static_cast<std::size_t>(file->size()));
    if (file->read(header) != header.size()) throw std::runtime_error("Truncated data1.hdr.");
    auto result = validate_header(header);
    static const bool logging_configured = [] {
        unshield_set_log_level(UNSHIELD_LOG_LEVEL_ERROR);
        return true;
    }();
    (void)logging_configured;
    std::unique_ptr<Unshield, decltype(&unshield_close)> cabinet(
        unshield_open2("data1.cab", &callbacks, &header), unshield_close);
    if (!cabinet) throw std::runtime_error("Cannot parse the cabinet metadata.");
    auto* handle = cabinet.get();
    if (unshield_file_count(handle) != static_cast<int>(result.files.size()) ||
        unshield_directory_count(handle) != static_cast<int>(result.directories.size()))
        throw std::runtime_error("Cabinet metadata counts disagree.");
    for (int i = 0; i < unshield_component_count(handle); ++i)
        result.components.emplace_back(unshield_component_name(handle, i));
    for (int i = 0; i < unshield_file_group_count(handle); ++i) {
        const auto* group = unshield_file_group_get(handle, i);
        result.groups.push_back({group->name, group->first_file, group->last_file});
    }
    for (std::size_t i = 0; i < result.directories.size(); ++i)
        result.directories[i] = unshield_directory_name(handle, static_cast<int>(i));
    for (std::size_t i = 0; i < result.files.size(); ++i) {
        auto& entry = result.files[i];
        const auto index = static_cast<int>(i);
        if (entry.kind != "placeholder") entry.name = unshield_file_name(handle, index);
        entry.directory = result.directories.at(static_cast<std::size_t>(unshield_file_directory(handle, index)));
        if (entry.size != unshield_file_size(handle, index) ||
            (entry.kind == "cabinet") != unshield_file_is_valid(handle, index))
            throw std::runtime_error("Cabinet file descriptor interpretations disagree.");
    }
    return result;
}

void write_cabinet_json(std::ostream& output, const CabinetIndex& index) {
    output << "{\"format_version\":6,\"name_encoding\":\"byte-values\",\"header_bytes\":" << index.header_bytes;
    auto strings = [&](const char* name, const std::vector<std::string>& values) {
        output << ",\"" << name << "\":[";
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i) output << ',';
            json_bytes(output, values[i]);
        }
        output << ']';
    };
    strings("components", index.components);
    strings("directories", index.directories);
    output << ",\"groups\":[";
    for (std::size_t i = 0; i < index.groups.size(); ++i) {
        if (i) output << ',';
        const auto& group = index.groups[i];
        output << "{\"name\":"; json_bytes(output, group.name);
        output << ",\"first_file\":" << group.first_file << ",\"last_file\":" << group.last_file << '}';
    }
    output << "],\"files\":[";
    for (std::size_t i = 0; i < index.files.size(); ++i) {
        if (i) output << ',';
        const auto& file = index.files[i];
        output << "{\"index\":" << i << ",\"name\":"; json_bytes(output, file.name);
        output << ",\"directory\":"; json_bytes(output, file.directory);
        output << ",\"kind\":"; json_bytes(output, file.kind);
        output << ",\"size\":" << file.size << ",\"compressed_size\":" << file.compressed_size
               << ",\"data_offset\":" << file.data_offset << ",\"volume\":" << file.volume << '}';
    }
    output << "]}\n";
}
} // namespace beachlife::data
