#pragma once

#include "data/source.h"

#include <array>
#include <memory>

namespace beachlife::runtime
{

// Internal snapshot implementation. The production owner supplies its fixed
// identity; a generated fixture may supply another digest through its private
// owner test seam, without changing parser bounds or the production entrypoint.
class ConfigSnapshot final : public data::FileSource
{
  public:
    static std::shared_ptr<const ConfigSnapshot> read(
        std::shared_ptr<const data::FileSource> source, std::string_view expected_sha256);
    std::unique_ptr<data::SourceFile> open(std::string_view path) const override;

    static constexpr std::string_view filename = "Data/Script/GameParameters.pis";
    static constexpr std::size_t supported_bytes = 1242;
    static constexpr std::string_view supported_sha256 =
        "e15c4fc9dfd4e940d9fcb28c63097585c382335e5815fcab4308ac3fb813e3a6";

  private:
    ConfigSnapshot(std::shared_ptr<const data::FileSource> source,
        std::array<unsigned char, supported_bytes> bytes);
    std::shared_ptr<const data::FileSource> source_;
    // Shared immutable bytes let an open stream retain the snapshot payload
    // independently of its source object and active Session.
    std::shared_ptr<const std::array<unsigned char, supported_bytes>> bytes_;
};

} // namespace beachlife::runtime
