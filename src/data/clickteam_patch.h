#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace beachlife::data
{

// An official Beach Life 1.1 patch (UK, European or Polish): a Clickteam Patch
// Maker executable. It is read as data and never run. Each entry either patches
// an installed file from one of its known original versions or adds a new file.
class ClickteamPatch
{
  public:
    struct Original
    {
        std::uint32_t size = 0;
        std::uint32_t offset = 0; // of its patch data in the data area
        std::uint32_t length = 0;
    };
    struct Entry
    {
        std::string path; // as stored ('\' separators)
        std::uint32_t size = 0;
        std::uint32_t checksum = 0; // Clickteam checksum of the patched file
        bool added = false;         // a new file instead of a patch
        bool supported = true;      // operation 0 (install or patch)
        std::vector<Original> originals;
    };

    // Throws when the file is not a Clickteam patch this reader understands.
    explicit ClickteamPatch(const std::filesystem::path& path);

    const std::string& sha256() const
    {
        return sha256_;
    }
    const std::vector<Entry>& entries() const
    {
        return entries_;
    }

    // The patched file. The original must be one of the entry's original
    // versions or already the patched file; otherwise this returns nullopt.
    // Throws when the patch data is damaged or its result fails the checksum.
    std::optional<std::vector<unsigned char>> apply(
        const Entry& entry, std::span<const unsigned char> original) const;
    // The contents of an added file, checked against its checksum.
    std::vector<unsigned char> added(const Entry& entry) const;

  private:
    std::vector<unsigned char> image_;
    std::size_t data_ = 0, data_size_ = 0;
    std::string sha256_;
    std::vector<Entry> entries_;
};

// The patch's rotating 32-bit checksum of a whole file.
std::uint32_t clickteam_checksum(std::span<const unsigned char> bytes);

// Patch Maker's deflate variant: each block starts with a 3-bit type (5 fixed,
// 6 dynamic, 7 stored) before the final-block bit, code-length codes are sent
// in the order 18, 17, 16, 0..15 and stored blocks carry no complement length.
// Throws unless the stream inflates to exactly `size` bytes.
std::vector<unsigned char> clickteam_inflate(
    std::span<const unsigned char> input, std::size_t size);

} // namespace beachlife::data
