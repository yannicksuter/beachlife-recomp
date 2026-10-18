#pragma once

#include "source.h"

namespace beachlife::data
{

// Reject links/reparse points in an existing directory chain. Used for owned
// data stores, never for interpreting archive paths as absolute host paths.
void require_real_directory(const std::filesystem::path& path);
std::filesystem::path canonical_content_root(const std::filesystem::path& path);
std::unique_ptr<SourceFile> open_local_file(const std::filesystem::path& root,
    std::string_view canonical_path,
    std::uint64_t expected_size);

} // namespace beachlife::data
