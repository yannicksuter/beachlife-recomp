#include "source.h"
#include "game_path.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace beachlife::data
{
namespace
{
class IsoFile final : public SourceFile
{
  public:
    IsoFile(const std::filesystem::path& image, const IsoEntry& entry, std::uint64_t image_bytes)
        : stream_(image, std::ios::binary)
        , base_(entry.offset)
        , size_(entry.size)
    {
        if (!stream_)
        {
            throw std::runtime_error("Cannot open the ISO image.");
        }
        stream_.seekg(0, std::ios::end);
        const auto end = stream_.tellg();
        if (end < 0 || static_cast<std::uint64_t>(end) != image_bytes || base_ > image_bytes ||
            size_ > image_bytes - base_)
        {
            throw std::runtime_error("The ISO image changed after indexing.");
        }
    }
    std::uint64_t size() const override
    {
        return size_;
    }
    std::uint64_t tell() const override
    {
        return position_;
    }
    void seek(std::uint64_t position) override
    {
        if (position > size_)
        {
            throw std::out_of_range("Seek is outside the ISO file.");
        }
        position_ = position;
    }
    std::size_t read(std::span<unsigned char> destination) override
    {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>({destination.size(),
            size_ - position_,
            static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())}));
        if (!count)
        {
            return 0;
        }
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(base_ + position_));
        stream_.read(
            reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(count));
        if (!stream_)
        {
            throw std::runtime_error("The ISO file is truncated or unreadable.");
        }
        position_ += count;
        return count;
    }

  private:
    std::ifstream stream_;
    std::uint64_t base_, size_, position_ = 0;
};
} // namespace

IsoSource::IsoSource(std::filesystem::path image)
    : image_(std::move(image))
    , index_(index_iso(image_))
{
    for (std::size_t i = 0; i < index_.entries.size(); ++i)
    {
        const auto key = normalize_game_path(index_.entries[i].path).key;
        if (!lookup_.emplace(key, i).second)
        {
            throw std::runtime_error("The ISO has ambiguous paths after case normalization.");
        }
    }
}

std::unique_ptr<SourceFile> IsoSource::open(std::string_view logical_path) const
{
    const auto key = normalize_game_path(logical_path).key;
    const auto found = lookup_.find(key);
    if (found == lookup_.end())
    {
        throw std::runtime_error("The file is not present in the ISO.");
    }
    const auto& entry = index_.entries[found->second];
    if (entry.directory)
    {
        throw std::runtime_error("The requested ISO path is a directory.");
    }
    return std::make_unique<IsoFile>(image_, entry, index_.image_bytes);
}
} // namespace beachlife::data
