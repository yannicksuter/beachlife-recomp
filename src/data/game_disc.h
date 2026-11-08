#pragma once

#include "cabinet.h"
#include "game_index.h"
#include "wise.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace beachlife::data
{

// A disc's installer: InstallShield 6 cabinets (data1.hdr) or one Wise
// installer executable. Borrows the ISO source; never runs an installer.
class GameDisc
{
  public:
    explicit GameDisc(const IsoSource& source);
    GameIndex map(std::string_view language = {}) const;
    // The installer's MD5 of an asset, or empty when it has none (loose and
    // Wise files are copied again rather than reused unchecked).
    std::string expected_md5(const GameAsset& asset) const;
    // Streams one mapped asset after the installer's own size and checksum checks.
    void extract(
        const GameAsset& asset, const std::function<void(std::span<const unsigned char>)>& sink);

  private:
    const IsoSource& source_;
    std::unique_ptr<CabinetReader> cabinet_;
    std::optional<WiseInstaller> wise_;
};

} // namespace beachlife::data
