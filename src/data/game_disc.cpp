#include "game_disc.h"

#include "content_prepare.h"
#include "game_path.h"

#include <algorithm>
#include <stdexcept>

namespace beachlife::data
{

GameDisc::GameDisc(const IsoSource& source)
    : source_(source)
{
    const auto& entries = source.index().entries;
    const bool installshield = std::any_of(entries.begin(),
        entries.end(),
        [](const IsoEntry& entry)
        {
            return !entry.directory && normalize_game_path(entry.path).key == "data1.hdr";
        });
    if (installshield)
    {
        cabinet_ = std::make_unique<CabinetReader>(source);
        return;
    }
    wise_ = index_wise(source);
    if (!wise_)
    {
        throw std::runtime_error(
            "No supported Beach Life installer (InstallShield or Wise) on this disc.");
    }
}

GameIndex GameDisc::map(std::string_view language) const
{
    return cabinet_ ? map_game_files(cabinet_->index(), source_.index(), language)
                    : map_wise_game_files(*wise_, language);
}

std::string GameDisc::expected_md5(const GameAsset& asset) const
{
    if (asset.loose || asset.wise || !cabinet_)
    {
        return {};
    }
    return cabinet_->index().files.at(asset.cabinet_index).md5;
}

void GameDisc::extract(
    const GameAsset& asset, const std::function<void(std::span<const unsigned char>)>& sink)
{
    if (asset.wise)
    {
        if (!wise_ || asset.size > 0xffffffffu)
        {
            throw std::runtime_error("A Wise asset does not belong to this disc.");
        }
        extract_wise(source_,
            *wise_,
            {asset.path, asset.wise_offset, static_cast<std::uint32_t>(asset.size), asset.wise_crc},
            sink);
    }
    else if (asset.loose)
    {
        copy_loose_file(source_, asset, sink);
    }
    else if (cabinet_)
    {
        cabinet_->extract(asset.cabinet_index, sink);
    }
    else
    {
        throw std::runtime_error("A cabinet asset does not belong to this disc.");
    }
}

} // namespace beachlife::data
