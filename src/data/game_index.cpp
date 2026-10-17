#include "game_index.h"
#include "game_path.h"

#include <algorithm>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <stdexcept>

namespace beachlife::data {

GameIndex map_game_files(const CabinetIndex& cabinet, const IsoIndex& iso) {
    std::map<std::string, const IsoEntry*> loose;
    for (const auto& entry : iso.entries)
        if (!loose.emplace(normalize_game_path(entry.path).key, &entry).second)
            throw std::runtime_error("The ISO contains ambiguous source paths.");
    std::map<std::string, GameAsset> assets;
    std::set<std::string> groups;
    std::set<std::size_t> selected;
    bool data_found = false, ini_found = false;
    GameIndex result;
    for (const auto& group : cabinet.groups) {
        const bool data = group.name == "UK Comp" || group.name == "UK Uncomp";
        const bool config = group.name == "Ess_English";
        if (!data && !config) continue;
        if (!groups.insert(group.name).second || group.first_file < 0 ||
            group.last_file < group.first_file || static_cast<std::size_t>(group.last_file) >= cabinet.files.size())
            throw std::runtime_error("Invalid or duplicate game file group.");
        for (int i = group.first_file; i <= group.last_file; ++i) {
            const auto& file = cabinet.files[static_cast<std::size_t>(i)];
            if (file.kind == "placeholder") continue;
            const auto raw = file.directory.empty() ? file.name : file.directory+"/"+file.name;
            // This observed layout uses ASCII names. Do not guess a legacy code
            // page for another edition and silently map the wrong runtime path.
            if (std::any_of(raw.begin(), raw.end(), [](unsigned char c) { return c >= 128; }))
                throw std::runtime_error("This game profile requires ASCII asset names.");
            const auto path = normalize_game_path(raw);
            const bool ini = config && path.key == "beachlife.ini";
            const bool asset = data && path.key.starts_with("data/");
            if (!ini && !asset) continue;
            if (!selected.insert(static_cast<std::size_t>(i)).second)
                throw std::runtime_error("Game file groups overlap.");
            if (file.kind != "cabinet" && file.kind != "external")
                throw std::runtime_error("Unsupported game file descriptor.");
            GameAsset mapped{path.display, file.size, static_cast<std::size_t>(i), file.kind == "external", {}};
            if (mapped.external) {
                const auto found = loose.find(path.key);
                if (found == loose.end() || found->second->directory || found->second->size != file.size)
                    throw std::runtime_error("An external game file is missing or has the wrong size.");
                mapped.source_path = found->second->path;
            }
            if (!assets.emplace(path.key, std::move(mapped)).second)
                throw std::runtime_error("Game content has ambiguous logical paths.");
            if (file.size > std::numeric_limits<std::uint64_t>::max()-result.total_bytes)
                throw std::runtime_error("Game data size overflows.");
            result.total_bytes += file.size;
            data_found |= asset;
            ini_found |= ini;
        }
    }
    if (!data_found || !ini_found || !groups.contains("UK Comp"))
        throw std::runtime_error("Unsupported game layout: expected English game data and BeachLife.ini.");
    for (auto& [key, file] : assets) {
        for (auto slash = key.find('/'); slash != std::string::npos; slash = key.find('/', slash+1))
            if (assets.contains(key.substr(0, slash)))
                throw std::runtime_error("Game content uses a file as a directory.");
        result.files.push_back(std::move(file));
    }
    return result;
}

void write_game_index_json(std::ostream& output, const GameIndex& index) {
    // normalize_game_path forbids quotes, backslashes and controls in display paths.
    output << "{\"profile\":\"" << index.profile << "\",\"language\":\"" << index.language
           << "\",\"total_bytes\":" << index.total_bytes << ",\"files\":[";
    for (std::size_t i = 0; i < index.files.size(); ++i) {
        if (i) output << ',';
        const auto& file = index.files[i];
        output << "{\"path\":\"" << file.path << "\",\"size\":" << file.size
               << ",\"cabinet_index\":" << file.cabinet_index << ",\"storage\":\""
               << (file.external ? "iso" : "cabinet") << "\"}";
    }
    output << "]}\n";
}
} // namespace beachlife::data
