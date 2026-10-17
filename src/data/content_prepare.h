#pragma once

#include "content_manifest.h"

#include <functional>
#include <stdexcept>

namespace beachlife::data {

struct PrepareProgress {
    // fingerprint, checking, extracting, confirming, ready; bytes are per phase.
    std::string phase, path;
    std::uint64_t completed_bytes = 0, total_bytes = 0;
    std::size_t completed_files = 0, total_files = 0;
};

struct PrepareOptions {
    // Called synchronously on the preparing thread. Run preparation on a
    // worker and publish progress snapshots for the loading screen.
    std::function<void(const PrepareProgress&)> progress;
    std::function<bool()> cancelled;
};

class PreparationCancelled : public std::runtime_error {
public:
    PreparationCancelled() : std::runtime_error("Content preparation cancelled.") {}
};

struct PrepareResult {
    std::filesystem::path root;
    ContentManifest manifest;
    std::size_t files_reused = 0, files_written = 0;
};

// No installer is run. Completed assets survive interruption; only a fully
// verified set receives manifest.json. Rerunning verifies/reuses or repairs it.
// A mounted PreparedSource prevents preparation of that same identity.
PrepareResult prepare_content(const std::filesystem::path& image,
    const std::filesystem::path& cache = default_content_directory(),
    const PrepareOptions& options = {});

} // namespace beachlife::data
