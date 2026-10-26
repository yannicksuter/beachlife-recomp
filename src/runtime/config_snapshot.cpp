#include "config_snapshot.h"

#include "data/game_path.h"
#include "platform/sha256.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace beachlife::runtime
{
namespace
{
using Bytes = std::array<unsigned char, ConfigSnapshot::supported_bytes>;

class SnapshotFile final : public data::SourceFile
{
  public:
    explicit SnapshotFile(std::shared_ptr<const Bytes> bytes)
        : bytes_(std::move(bytes))
    {
    }
    std::uint64_t size() const override
    {
        return bytes_->size();
    }
    std::uint64_t tell() const override
    {
        return position_;
    }
    void seek(std::uint64_t position) override
    {
        // The bridge bounds every seek to the known stream length.
        if (position > bytes_->size())
        {
            throw std::out_of_range("Configuration snapshot seek exceeds its size.");
        }
        position_ = static_cast<std::size_t>(position);
    }
    std::size_t read(std::span<unsigned char> destination) override
    {
        const auto count = std::min(destination.size(), bytes_->size() - position_);
        if (count)
        {
            std::memcpy(destination.data(), bytes_->data() + position_, count);
            position_ += count;
        }
        return count;
    }

  private:
    std::shared_ptr<const Bytes> bytes_;
    std::size_t position_ = 0;
};
} // namespace

ConfigSnapshot::ConfigSnapshot(std::shared_ptr<const data::FileSource> source, Bytes bytes)
    : source_(std::move(source))
    , bytes_(std::make_shared<const Bytes>(std::move(bytes)))
{
}

std::shared_ptr<const ConfigSnapshot> ConfigSnapshot::read(
    std::shared_ptr<const data::FileSource> source, std::string_view expected_sha256)
{
    if (!source)
    {
        throw std::invalid_argument("Game configuration requires a completed VFS mount.");
    }
    auto file = source->open(filename);
    if (!file || file->size() != supported_bytes)
    {
        throw std::runtime_error("The selected game configuration is not supported.");
    }
    file->seek(0);
    Bytes bytes{};
    std::size_t count = 0;
    while (count != bytes.size())
    {
        const auto received = file->read(std::span(bytes).subspan(count));
        if (!received || received > bytes.size() - count)
        {
            throw std::runtime_error("The game configuration could not be read completely.");
        }
        count += received;
    }
    if (file->size() != supported_bytes ||
        platform::sha256_hex(std::string_view(
            reinterpret_cast<const char*>(bytes.data()), bytes.size())) != expected_sha256)
    {
        throw std::runtime_error("The selected game configuration is not supported.");
    }
    return std::shared_ptr<const ConfigSnapshot>(new ConfigSnapshot(std::move(source), bytes));
}

std::unique_ptr<data::SourceFile> ConfigSnapshot::open(std::string_view path) const
{
    if (data::normalize_game_path(path).key == "data/script/gameparameters.pis")
    {
        return std::make_unique<SnapshotFile>(bytes_);
    }
    return source_->open(path);
}

} // namespace beachlife::runtime
