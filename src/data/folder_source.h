#pragma once

#include "source.h"
#include <vector>

namespace beachlife::data {

struct SourceEntry {
    std::string path;
    std::uint64_t size = 0;
};

// Read-only mount of an explicit manifest, not an unrestricted host directory.
// Logical names use Windows separators/case rules on every supported system.
class FolderSource final : public FileSource {
public:
    FolderSource(std::filesystem::path root, std::vector<SourceEntry> entries);
    std::unique_ptr<SourceFile> open(std::string_view logical_path) const override;
    const std::vector<SourceEntry>& entries() const { return entries_; }

private:
    std::filesystem::path root_;
    std::vector<SourceEntry> entries_;
    std::map<std::string, std::size_t> lookup_;
};

} // namespace beachlife::data
