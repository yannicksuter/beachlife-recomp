#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace beachlife::data {

struct IsoEntry {
    std::string path;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    bool directory = false;
};

struct IsoIndex {
    std::string volume;
    bool joliet = false;
    std::uint64_t image_bytes = 0;
    std::uint64_t file_bytes = 0;
    std::size_t files = 0;
    std::size_t directories = 0;
    std::vector<IsoEntry> entries;
};

// Reads directory metadata only. No mounting, extraction or retail execution.
// Supports single-volume, 2048-byte-sector ISO 9660 and Joliet images.
IsoIndex index_iso(const std::filesystem::path& path);
void write_iso_json(std::ostream& output, const IsoIndex& index);

} // namespace beachlife::data
