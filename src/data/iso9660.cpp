#include "iso9660.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <span>
#include <stdexcept>

namespace beachlife::data {
namespace {
constexpr std::uint64_t sector = 2048;
constexpr std::uint64_t metadata_limit = 64 * 1024 * 1024;
using Bytes = std::span<const unsigned char>;

void check(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}

std::uint32_t both(const unsigned char* p, int count) {
    std::uint32_t little = 0, big = 0;
    for (int i = 0; i < count; ++i) {
        little |= static_cast<std::uint32_t>(p[i]) << (8*i);
        big = (big << 8) | p[count+i];
    }
    check(little == big, "The ISO contains inconsistent byte-order fields.");
    return little;
}

std::string decode(Bytes bytes, bool joliet) {
    std::string result;
    check(!joliet || bytes.size() % 2 == 0, "The ISO contains an invalid Joliet name.");
    for (std::size_t i = 0; i < bytes.size(); i += joliet ? 2 : 1) {
        const auto code = joliet ? (static_cast<unsigned>(bytes[i]) << 8) | bytes[i+1] : bytes[i];
        check(code >= 32 && code != 127 && !(code >= 0xd800 && code <= 0xdfff),
              "The ISO contains an invalid text character.");
        check(joliet || code < 128, "The ISO uses an unsupported filename encoding.");
        if (code < 0x80) result += static_cast<char>(code);
        else if (code < 0x800) {
            result += static_cast<char>(0xc0 | (code >> 6));
            result += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            result += static_cast<char>(0xe0 | (code >> 12));
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (code & 0x3f));
        }
    }
    return result;
}

struct Record {
    std::uint64_t offset;
    std::uint64_t size;
    bool directory;
    Bytes name;
};

Record record(Bytes bytes, std::uint64_t volume_bytes) {
    check(bytes.size() >= 34 && bytes[0] == bytes.size(), "The ISO contains a truncated directory record.");
    const auto length = bytes[32];
    check(length != 0 && 33u + length + (length % 2 == 0 ? 1u : 0u) <= bytes.size(),
          "The ISO contains a truncated filename.");
    check(!(bytes[25] & 0x80), "Multi-extent ISO files are not supported yet.");
    check(!(bytes[25] & 0x64), "The ISO uses unsupported file flags.");
    check(bytes[26] == 0 && bytes[27] == 0, "Interleaved ISO files are not supported yet.");
    check(both(bytes.data()+28, 2) == 1, "Multi-volume ISO images are not supported.");
    const auto offset = (static_cast<std::uint64_t>(both(bytes.data()+2, 4)) + bytes[1]) * sector;
    const auto size = both(bytes.data()+10, 4);
    check(offset <= volume_bytes && size <= volume_bytes-offset, "An ISO file extent is outside the image.");
    const bool directory = (bytes[25] & 2) != 0;
    check(!directory || (size != 0 && size % sector == 0), "The ISO contains an invalid directory size.");
    return {offset, size, directory, bytes.subspan(33, length)};
}

class Reader {
public:
    explicit Reader(const std::filesystem::path& path) : file_(path, std::ios::binary) {
        check(file_.is_open(), "Cannot open the selected ISO image.");
        file_.seekg(0, std::ios::end);
        const auto end = file_.tellg();
        check(end >= 0, "Cannot determine the ISO image size.");
        size = static_cast<std::uint64_t>(end);
    }
    void read(std::uint64_t offset, std::span<unsigned char> target) {
        check(offset <= size && target.size() <= size-offset, "The ISO image is truncated.");
        file_.seekg(static_cast<std::streamoff>(offset));
        file_.read(reinterpret_cast<char*>(target.data()), static_cast<std::streamsize>(target.size()));
        check(static_cast<bool>(file_), "Cannot read the selected ISO image.");
    }
    std::uint64_t size = 0;
private:
    std::ifstream file_;
};

void quote(std::ostream& out, const std::string& value) {
    out << '"';
    for (char ch : value) {
        if (ch == '"' || ch == '\\') out << '\\';
        out << ch;
    }
    out << '"';
}
} // namespace

IsoIndex index_iso(const std::filesystem::path& path) {
    Reader image(path);
    std::array<unsigned char, sector> chosen{}, primary{};
    bool has_primary = false, terminated = false;
    std::uint64_t descriptor_end = 0;
    int joliet_level = 0;
    for (std::uint64_t block = 16; block < 80; ++block) {
        std::array<unsigned char, sector> descriptor{};
        image.read(block*sector, descriptor);
        check(std::equal(descriptor.begin()+1, descriptor.begin()+6, "CD001") && descriptor[6] == 1,
              "This is not a supported ISO 9660 image.");
        if (descriptor[0] == 255) { terminated = true; descriptor_end = (block+1)*sector; break; }
        if (descriptor[0] == 1) {
            check(!has_primary, "The ISO contains duplicate primary descriptors.");
            primary = descriptor;
            has_primary = true;
        }
        if (descriptor[0] == 2 && descriptor[7] == 0 && descriptor[88] == '%' && descriptor[89] == '/') {
            const int level = descriptor[90] == '@' ? 1 : descriptor[90] == 'C' ? 2 : descriptor[90] == 'E' ? 3 : 0;
            if (level > joliet_level) { chosen = descriptor; joliet_level = level; }
        }
    }
    check(terminated && has_primary, "The ISO volume descriptors are incomplete.");
    if (!joliet_level) chosen = primary;
    for (const auto* descriptor : {&primary, &chosen}) {
        check(both(descriptor->data()+128, 2) == sector, "Only 2048-byte ISO logical blocks are supported.");
        check(both(descriptor->data()+120, 2) == 1 && both(descriptor->data()+124, 2) == 1,
              "Multi-volume ISO images are not supported.");
        check((*descriptor)[881] == 1, "The ISO file structure version is unsupported.");
    }
    const auto volume_bytes = static_cast<std::uint64_t>(both(chosen.data()+80, 4)) * sector;
    check(volume_bytes >= descriptor_end && volume_bytes <= image.size, "The ISO volume size exceeds the image or is invalid.");
    check(both(primary.data()+80, 4) == both(chosen.data()+80, 4), "The ISO volume sizes disagree.");
    IsoIndex result;
    result.joliet = joliet_level != 0;
    result.image_bytes = image.size;
    auto label = Bytes(chosen).subspan(40, 32);
    const std::size_t unit = result.joliet ? 2 : 1;
    while (!label.empty()) {
        const auto last = result.joliet ? (static_cast<unsigned>(label[label.size()-2]) << 8) | label.back() : label.back();
        if (last != 0 && last != ' ') break;
        label = label.first(label.size()-unit);
    }
    result.volume = decode(label, result.joliet);
    const auto root = record(Bytes(chosen).subspan(156, chosen[156]), volume_bytes);
    check(root.directory && root.name.size() == 1 && root.name[0] == 0, "The ISO root directory is invalid.");
    struct Pending { std::string path; std::uint64_t offset; std::uint64_t size; unsigned depth; };
    std::vector<Pending> pending{{"", root.offset, root.size, 0}};
    std::set<std::uint64_t> directories;
    std::set<std::string> names;
    std::uint64_t metadata_bytes = 0, path_bytes = 0;
    while (!pending.empty()) {
        auto current = pending.back(); pending.pop_back();
        check(current.depth <= 32 && current.size <= 16*1024*1024 &&
              current.size <= metadata_limit-metadata_bytes, "The ISO directory index exceeds supported limits.");
        check(directories.insert(current.offset).second, "The ISO contains a repeated or cyclic directory.");
        metadata_bytes += current.size;
        std::vector<unsigned char> bytes(static_cast<std::size_t>(current.size));
        image.read(current.offset, bytes);
        for (std::size_t pos = 0; pos < bytes.size();) {
            const auto length = bytes[pos];
            if (!length) { pos += static_cast<std::size_t>(sector) - pos % sector; continue; }
            check(length <= bytes.size()-pos && length <= sector-pos%sector,
                  "An ISO directory record crosses its block boundary.");
            const auto item = record(Bytes(bytes).subspan(pos, length), volume_bytes);
            pos += length;
            if (item.name.size() == 1 && item.name[0] <= 1) continue;
            auto name = decode(item.name, result.joliet);
            if (!item.directory) {
                const auto version = name.rfind(';');
                if (version != std::string::npos) {
                    check(version+1 < name.size() && std::all_of(name.begin()+static_cast<std::ptrdiff_t>(version)+1,
                          name.end(), [](char ch) { return ch >= '0' && ch <= '9'; }), "The ISO filename version is invalid.");
                    name.resize(version);
                }
                if (!name.empty() && name.back() == '.') name.pop_back();
            }
            check(!name.empty() && name != "." && name != ".." && name.find_first_of("/\\:") == std::string::npos,
                  "The ISO contains an unsafe filename.");
            const auto full = current.path.empty() ? name : current.path + '/' + name;
            check(full.size() <= 4096 && result.entries.size() < 100000 && full.size() <= 16*1024*1024-path_bytes,
                  "The ISO directory index exceeds supported limits.");
            path_bytes += full.size();
            check(names.insert(full).second, "The ISO contains duplicate file paths.");
            result.entries.push_back({full, item.offset, item.size, item.directory});
            if (item.directory) {
                ++result.directories;
                pending.push_back({full, item.offset, item.size, current.depth+1});
            } else { ++result.files; result.file_bytes += item.size; }
        }
    }
    std::sort(result.entries.begin(), result.entries.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    return result;
}

void write_iso_json(std::ostream& out, const IsoIndex& index) {
    out << "{\"volume\":"; quote(out, index.volume);
    out << ",\"filesystem\":\"" << (index.joliet ? "Joliet" : "ISO 9660")
        << "\",\"image_bytes\":" << index.image_bytes << ",\"file_bytes\":" << index.file_bytes
        << ",\"files\":" << index.files << ",\"directories\":" << index.directories << ",\"entries\":[";
    bool first = true;
    for (const auto& entry : index.entries) {
        if (!first) out << ',';
        first = false;
        out << "{\"path\":"; quote(out, entry.path);
        out << ",\"offset\":" << entry.offset << ",\"size\":" << entry.size
            << ",\"directory\":" << (entry.directory ? "true" : "false") << '}';
    }
    out << "]}\n";
}
} // namespace beachlife::data
