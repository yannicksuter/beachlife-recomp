#pragma once

#include "source.h"
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace beachlife::data
{

struct CabinetEntry
{
    std::string name, directory, kind;
    std::uint64_t size = 0, compressed_size = 0, data_offset = 0;
    unsigned volume = 0;
    unsigned flags = 0, link_flags = 0;
    std::string md5;
    std::size_t link_previous = 0, link_next = 0;
};

struct CabinetGroup
{
    std::string name;
    int first_file = 0, last_file = 0;
};

struct CabinetIndex
{
    std::uint64_t header_bytes = 0;
    std::vector<std::string> components, directories;
    std::vector<CabinetGroup> groups;
    std::vector<CabinetEntry> files;
};

// Inspect InstallShield 6 metadata in data1.hdr; never extracts cabinet payloads.
// Names retain their original bytes. Mapping/encoding belongs to the game source.
CabinetIndex index_cabinet(const IsoSource& source);
void write_cabinet_json(std::ostream& output, const CabinetIndex& index);

// Borrows the ISO source. Streams bounded chunks and checks the original size
// and checksum. The sink may throw to cancel; partial output is the caller's
// staging data and must not be published until extract returns successfully.
class CabinetReader
{
  public:
    explicit CabinetReader(const IsoSource& source);
    ~CabinetReader();
    CabinetReader(const CabinetReader&) = delete;
    CabinetReader& operator=(const CabinetReader&) = delete;
    const CabinetIndex& index() const;
    void extract(
        std::size_t index, const std::function<void(std::span<const unsigned char>)>& sink);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace beachlife::data
