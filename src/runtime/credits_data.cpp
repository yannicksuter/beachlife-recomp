#include "runtime/credits_data.h"

#include "platform/sha256.h"

#include <algorithm>
#include <bit>
#include <span>
#include <stdexcept>
#include <vector>

namespace beachlife::runtime
{
namespace
{
using Bytes = std::span<const unsigned char>;
constexpr std::uint32_t image_base = 0x00400000;
constexpr std::uint32_t rows_rva = 0x00180170;
constexpr std::size_t row_bytes = 20;
constexpr std::size_t payload_bytes =
    CreditsData::row_count * row_bytes + CreditsData::format_bytes;

[[noreturn]] void invalid_image()
{
    throw std::runtime_error("Credits data has an unsupported or truncated PE layout.");
}

void bounds(Bytes bytes, std::size_t at, std::size_t count)
{
    if (at > bytes.size() || count > bytes.size() - at)
    {
        invalid_image();
    }
}

std::uint16_t u16(Bytes bytes, std::size_t at)
{
    bounds(bytes, at, 2);
    return static_cast<std::uint16_t>(bytes[at] | (static_cast<unsigned>(bytes[at + 1]) << 8));
}

std::uint32_t u32(Bytes bytes, std::size_t at)
{
    bounds(bytes, at, 4);
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

std::size_t payload_offset(Bytes bytes)
{
    if (u16(bytes, 0) != 0x5a4d)
    {
        invalid_image();
    }
    const std::size_t pe = u32(bytes, 0x3c);
    bounds(bytes, pe, 24);
    if (u32(bytes, pe) != 0x00004550 || u16(bytes, pe + 4) != 0x014c)
    {
        invalid_image();
    }
    const auto sections = u16(bytes, pe + 6);
    const auto optional_size = u16(bytes, pe + 20);
    const std::size_t optional = pe + 24;
    bounds(bytes, optional, optional_size);
    if (!sections || optional_size < 96 || u16(bytes, optional) != 0x010b ||
        u32(bytes, optional + 28) != image_base)
    {
        invalid_image();
    }
    const auto image_size = u32(bytes, optional + 56);
    if (rows_rva > image_size || payload_bytes > image_size - rows_rva)
    {
        invalid_image();
    }
    const std::size_t table = optional + optional_size;
    bounds(bytes, table, static_cast<std::size_t>(sections) * 40);
    std::size_t found = 0;
    bool mapped = false;
    for (std::size_t index = 0; index < sections; ++index)
    {
        const std::size_t section = table + index * 40;
        const auto virtual_size = u32(bytes, section + 8);
        const auto rva = u32(bytes, section + 12);
        const std::uint64_t section_end = static_cast<std::uint64_t>(rva) + virtual_size;
        const std::uint64_t payload_end = static_cast<std::uint64_t>(rows_rva) + payload_bytes;
        if (rva >= payload_end || section_end <= rows_rva)
        {
            continue;
        }
        if (rows_rva < rva || payload_end > section_end)
        {
            invalid_image();
        }
        const auto delta = rows_rva - rva;
        const auto raw_size = u32(bytes, section + 16);
        const std::size_t raw = u32(bytes, section + 20);
        const auto flags = u32(bytes, section + 36);
        // The complete records and adjacent format must be initialized,
        // readable/writable data in one unambiguous, non-executable section.
        if (mapped || payload_bytes > virtual_size - delta || delta > raw_size ||
            payload_bytes > raw_size - delta || (flags & 0xc0000040) != 0xc0000040 ||
            (flags & 0x20000000))
        {
            invalid_image();
        }
        bounds(bytes, raw, raw_size);
        found = raw + delta;
        bounds(bytes, found, payload_bytes);
        mapped = true;
    }
    if (!mapped)
    {
        invalid_image();
    }
    return found;
}
} // namespace

CreditsData CreditsData::read(const data::FileSource& source)
{
    return read_verified(source, supported_sha256);
}

CreditsData CreditsData::read_verified(
    const data::FileSource& source, std::string_view expected_sha256)
{
    auto file = source.open(filename);
    if (!file || file->size() != supported_file_bytes)
    {
        throw std::runtime_error("The selected executable is not a supported credits-data source.");
    }
    file->seek(0);
    std::vector<unsigned char> bytes(supported_file_bytes);
    std::size_t done = 0;
    while (done != bytes.size())
    {
        const auto received = file->read(std::span(bytes).subspan(done));
        if (!received || received > bytes.size() - done)
        {
            throw std::runtime_error("The credits-data source could not be read completely.");
        }
        done += received;
    }
    if (file->size() != supported_file_bytes ||
        platform::sha256_hex(std::string_view(
            reinterpret_cast<const char*>(bytes.data()), bytes.size())) != expected_sha256)
    {
        throw std::runtime_error("The selected executable revision is not supported for credits.");
    }

    const auto offset = payload_offset(bytes);
    CreditsData result;
    for (std::size_t index = 0; index < row_count; ++index)
    {
        const auto at = offset + index * row_bytes;
        auto& row = result.rows_[index];
        row.font_index = std::bit_cast<std::int32_t>(u32(bytes, at));
        row.key_number = std::bit_cast<std::int32_t>(u32(bytes, at + 4));
        row.word_08 = std::bit_cast<std::int32_t>(u32(bytes, at + 8));
        row.flag_0c = bytes[at + 12];
        std::copy_n(bytes.begin() + at + 13, 3, row.unknown_0d.begin());
        row.word_10 = u32(bytes, at + 16);
    }
    const auto format = offset + row_count * row_bytes;
    for (std::size_t index = 0; index < format_bytes; ++index)
    {
        result.format_[index] = std::bit_cast<char>(bytes[format + index]);
    }
    // The supported format is exactly15 characters and its terminating NUL.
    if (result.format_.back() != 0 ||
        std::find(result.format_.begin(), result.format_.end() - 1, '\0') !=
            result.format_.end() - 1)
    {
        invalid_image();
    }
    return result;
}

} // namespace beachlife::runtime
