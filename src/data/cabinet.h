#pragma once

#include "source.h"
#include <iosfwd>
#include <string>
#include <vector>

namespace beachlife::data {

struct CabinetEntry {
    std::string name, directory, kind;
    std::uint64_t size = 0, compressed_size = 0, data_offset = 0;
    unsigned volume = 0;
};

struct CabinetGroup {
    std::string name;
    int first_file = 0, last_file = 0;
};

struct CabinetIndex {
    std::uint64_t header_bytes = 0;
    std::vector<std::string> components, directories;
    std::vector<CabinetGroup> groups;
    std::vector<CabinetEntry> files;
};

// Inspect InstallShield 6 metadata in data1.hdr; never extracts cabinet payloads.
// Names retain their original bytes. Mapping/encoding belongs to the game source.
CabinetIndex index_cabinet(const IsoSource& source);
void write_cabinet_json(std::ostream& output, const CabinetIndex& index);

} // namespace beachlife::data
