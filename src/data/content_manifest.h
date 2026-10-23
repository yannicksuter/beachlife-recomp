#pragma once

#include "content_storage.h"
#include "folder_source.h"

namespace beachlife::data
{

inline constexpr int content_format = 1;
inline constexpr std::uint64_t max_content_bytes = 16ull * 1024 * 1024 * 1024;
inline constexpr std::size_t max_content_manifest_bytes = 32 * 1024 * 1024;

struct PreparedEntry : SourceEntry
{
    std::string sha256;
};

struct ContentManifest
{
    std::string source_sha256, profile, language;
    std::vector<PreparedEntry> files;
    std::uint64_t total_bytes = 0;
    std::string identity() const;
};

ContentManifest parse_content_manifest(std::string_view text);
std::string content_manifest_json(const ContentManifest& manifest);

// A complete manifest is the readiness boundary. Opening checks existence and
// sizes; verify() additionally reads and hashes all bytes. No ISO is needed.
class PreparedSource final : public FileSource
{
  public:
    explicit PreparedSource(const std::filesystem::path& root);
    const ContentManifest& manifest() const
    {
        return manifest_;
    }
    std::unique_ptr<SourceFile> open(std::string_view path) const override;
    void verify() const;

  private:
    std::shared_ptr<ContentLock> lock_;
    ContentManifest manifest_;
    std::unique_ptr<FolderSource> files_;
};

std::string sha256_source_file(SourceFile& file);

} // namespace beachlife::data
