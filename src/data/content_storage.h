#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <cstdint>
#include <span>
#include <string_view>

namespace beachlife::data {

// Lifetime OS locks recover automatically when a process exits or crashes.
class ContentLock {
public:
    ContentLock(const std::filesystem::path& root, bool shared = false);
    ~ContentLock();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class StagedFile {
public:
    StagedFile(const std::filesystem::path& staging, std::string_view name);
    ~StagedFile();
    void write(std::span<const unsigned char> bytes);
    void commit(const std::filesystem::path& destination);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

void ensure_content_directory(const std::filesystem::path& root, std::string_view relative);
void remove_content_file(const std::filesystem::path& path);
// Missing paths return nullopt. Existing links/special files are errors.
std::optional<std::uint64_t> content_file_size(const std::filesystem::path& root,
                                              std::string_view relative);
std::filesystem::path default_content_directory();

} // namespace beachlife::data
