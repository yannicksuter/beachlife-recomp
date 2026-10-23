#pragma once

#include "data/folder_source.h"

#include <span>
#include <thread>
#include <vector>

namespace beachlife::compat
{
// Bind the prepared VFS manifest on the game thread, alongside the existing
// retail_files/resource_files sessions. Enumeration only uses this index;
// file contents still go through those services. Nested sessions restore the
// previous index, and an open find handle owns its snapshot independently.
class DirectorySession
{
  public:
    explicit DirectorySession(std::span<const data::SourceEntry> entries);
    ~DirectorySession();
    DirectorySession(const DirectorySession&) = delete;
    DirectorySession& operator=(const DirectorySession&) = delete;

  private:
    std::vector<data::SourceEntry> entries_;
    const std::vector<data::SourceEntry>* previous_;
    std::thread::id thread_;
};
} // namespace beachlife::compat
