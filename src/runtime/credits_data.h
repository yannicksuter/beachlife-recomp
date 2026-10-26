#pragma once

#include "data/source.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace beachlife::runtime
{

// Native values decoded from the original scalar records, never PE pointers.
struct CreditsRow
{
    std::int32_t font_index;
    std::int32_t key_number;
    std::int32_t word_08;
    std::uint8_t flag_0c;
    std::array<std::uint8_t, 3> unknown_0d;
    std::uint32_t word_10;
};

// An owned, verified input snapshot. No executable is mapped or loaded as code.
// Read through an explicit FileSource, including the prepared local game cache.
// Preparation retains BeachLife.exe as data; this reader still checks its revision.
class CreditsData
{
  public:
    static constexpr std::size_t row_count = 205;
    static constexpr std::size_t format_bytes = 16;
    static constexpr std::size_t supported_file_bytes = 1617920;
    static constexpr std::string_view filename = "BeachLife.exe";
    static constexpr std::string_view supported_sha256 =
        "983c098f69314c142ac8fd0d74fa34d098a49bd59f1aed88fb67a525c4b7f9e9";

    static CreditsData read(const data::FileSource& source);
    // Optional until the retail credits consumer requests its first row. An
    // unavailable source leaves the publication boundary unbound, never zero-filled.
    static std::optional<CreditsData> read_if_available(const data::FileSource& source);
    const std::array<CreditsRow, row_count>& rows() const noexcept
    {
        return rows_;
    }
    const std::array<char, format_bytes>& format() const noexcept
    {
        return format_;
    }

  private:
    // Generated fixtures substitute only their digest, retaining every size,
    // PE mapping, byte-decoding and publication check of the production route.
    friend struct CreditsDataTestAccess;
    static CreditsData read_verified(
        const data::FileSource& source, std::string_view expected_sha256);
    CreditsData() = default;
    std::array<CreditsRow, row_count> rows_{};
    std::array<char, format_bytes> format_{};
};

} // namespace beachlife::runtime
