#pragma once

#include "cabinet.h"

namespace beachlife::data {

struct GameAsset {
    std::string path;
    std::uint64_t size = 0;
    std::size_t cabinet_index = 0;
    // Cabinet members are addressed by index; external files by their ISO path.
    bool external = false;
    std::string source_path;
};

struct GameIndex {
    std::string profile = "uk-english-v1";
    std::string language = "en";
    std::vector<GameAsset> files;
    std::uint64_t total_bytes = 0;
};

// Supported installation layout, not installer-language inference. Rejects
// unknown layouts, traversal, file/directory conflicts and ambiguous aliases.
GameIndex map_game_files(const CabinetIndex& cabinet, const IsoIndex& iso);
void write_game_index_json(std::ostream& output, const GameIndex& index);

} // namespace beachlife::data
