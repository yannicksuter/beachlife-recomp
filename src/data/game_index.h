#pragma once

#include "cabinet.h"
#include "wise.h"

#include <string_view>

namespace beachlife::data
{

struct GameAsset
{
    std::string path;
    std::uint64_t size = 0;
    std::size_t cabinet_index = 0;
    // Cabinet members are addressed by index; external files by their ISO path.
    bool external = false;
    std::string source_path;
    // A loose disc file that the installer descriptor does not describe (the
    // copy-protected executable of a retail pressing): it is copied from the
    // ISO as it is, without the descriptor's size and MD5 checks.
    bool loose = false;
    // A file inside a Wise installer (source_path): its raw-deflate stream
    // offset and the CRC-32 of its inflated bytes.
    bool wise = false;
    std::uint64_t wise_offset = 0;
    std::uint32_t wise_crc = 0;
};

struct GameIndex
{
    std::string profile = "uk-english-v1";
    std::string language = "en";
    // Every game language this disc offers, in the game's own Language order.
    std::vector<std::string> languages;
    std::vector<GameAsset> files;
    std::uint64_t total_bytes = 0;
};

// Supported installation layouts: the UK disc (English), the European disc
// (French, Italian, German, Spanish and Polish) and the US disc (English, its
// "_us" files also under the names the UK code opens). `language` selects one
// of the disc's languages; empty selects the first one in the game's Language
// order. Rejects unknown layouts, unoffered languages, traversal,
// file/directory conflicts and ambiguous aliases.
GameIndex map_game_files(
    const CabinetIndex& cabinet, const IsoIndex& iso, std::string_view language = {});
// The PLAY 2006 cover disc installs the UK 1.1 game files with one Wise
// installer and writes no BeachLife.ini (the game then uses its defaults).
// Profile "uk-wise-v1"; `language` must be empty or "en".
GameIndex map_wise_game_files(const WiseInstaller& installer, std::string_view language = {});
void write_game_index_json(std::ostream& output, const GameIndex& index);

// Whether a prepared profile/language pair is supported. Language codes are
// "en" (UK and US discs) and "fr", "it", "de", "es" or "pl" (European disc).
bool supported_game_profile(std::string_view profile, std::string_view language);
// English display name of a language code, such as "Polish" for "pl".
std::string_view language_name(std::string_view language);

} // namespace beachlife::data
