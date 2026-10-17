#include "folder_source.h"
#include "game_path.h"
#include "local_files.h"

#include <stdexcept>
#include <utility>

namespace beachlife::data {

FolderSource::FolderSource(std::filesystem::path root, std::vector<SourceEntry> entries)
    : root_(canonical_content_root(root)), entries_(std::move(entries)) {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto path = normalize_game_path(entries_[i].path);
        if (path.display != entries_[i].path || !lookup_.emplace(path.key, i).second)
            throw std::runtime_error("Prepared content has noncanonical or ambiguous paths.");
    }
    for (const auto& [key, index] : lookup_) {
        (void)index;
        for (auto slash = key.find('/'); slash != std::string::npos; slash = key.find('/', slash+1))
            if (lookup_.contains(key.substr(0, slash)))
                throw std::runtime_error("Prepared content uses a file as a directory.");
    }
}

std::unique_ptr<SourceFile> FolderSource::open(std::string_view logical_path) const {
    const auto found = lookup_.find(normalize_game_path(logical_path).key);
    if (found == lookup_.end()) throw std::runtime_error("The file is not present in prepared content.");
    const auto& entry = entries_[found->second];
    return open_local_file(root_, entry.path, entry.size);
}

} // namespace beachlife::data
