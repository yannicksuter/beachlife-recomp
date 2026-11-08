#include "game_index.h"
#include "game_path.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace beachlife::data
{
namespace
{
struct Language
{
    const char* code;
    const char* name;
    const char* config_group;
};

// The game's INI Language values 0-5 (uk, french, italian, german, spanish,
// polish) and the installer group holding each language's BeachLife.ini.
constexpr std::array<Language, 6> languages{{{"en", "English", "Ess_English"},
    {"fr", "French", "Ess_French"},
    {"it", "Italian", "Ess_Italian"},
    {"de", "German", "Ess_German"},
    {"es", "Spanish", "Ess_Spanish"},
    {"pl", "Polish", "Ess_Polish"}}};

struct Layout
{
    const char* profile;
    const char* data_group;
    const char* loose_group;
    const char* executable;
};

constexpr Layout uk_layout{"uk-english-v1", "UK Comp", "UK Uncomp", "beachlife.exe"};
constexpr Layout euro_layout{"euro-v1", "EURO Comp", "EURO Uncomp", "beachlife.exe"};
constexpr Layout us_layout{"us-english-v1", "US Comp", "US Uncomp", "springbreak.exe"};
// The UK 1.1 game files installed by one Wise installer (PLAY 2006 cover disc).
constexpr std::string_view wise_profile = "uk-wise-v1";

// The US build opens "_us" names. Our code is the UK/European build, so the US
// disc keeps every file under its own name (its GUI scripts name the US screens)
// and the names that the UK code itself opens alias the US files.
constexpr std::array<std::pair<const char*, const char*>, 7> us_aliases{{
    {"Data/text_uk.ble", "data/text_us.ble"},
    {"Data/Script/GUI/MainMenu.ble", "data/script/gui/mainmenu_us.ble"},
    {"Data/Script/GUI/Intro.ble", "data/script/gui/intro_us.ble"},
    {"Data/Script/GUI/gamewon.ble", "data/script/gui/gamewon_us.ble"},
    {"Data/Screens/loading_1.tga", "data/screens/loading_us_1.tga"},
    {"Data/Screens/loading_2.tga", "data/screens/loading_us_2.tga"},
    {"Data/Screens/FrontScreen.tga", "data/screens/frontscreen_us.tga"},
}};

bool has_group(const CabinetIndex& cabinet, std::string_view name)
{
    return std::any_of(cabinet.groups.begin(),
        cabinet.groups.end(),
        [&](const CabinetGroup& group)
        {
            return group.name == name;
        });
}
// Rejects a file that another path uses as a directory, then publishes the
// assets in logical path order.
void move_assets(std::map<std::string, GameAsset>& assets, GameIndex& result)
{
    for (auto& [key, file] : assets)
    {
        for (auto slash = key.find('/'); slash != std::string::npos;
            slash = key.find('/', slash + 1))
        {
            if (assets.contains(key.substr(0, slash)))
            {
                throw std::runtime_error("Game content uses a file as a directory.");
            }
        }
        result.files.push_back(std::move(file));
    }
}
} // namespace

bool supported_game_profile(std::string_view profile, std::string_view language)
{
    if (profile == uk_layout.profile || profile == us_layout.profile || profile == wise_profile)
    {
        return language == "en";
    }
    return profile == euro_layout.profile && language != "en" &&
           std::any_of(languages.begin(),
               languages.end(),
               [&](const Language& item)
               {
                   return item.code == language;
               });
}

std::string_view language_name(std::string_view language)
{
    for (const auto& item : languages)
    {
        if (item.code == language)
        {
            return item.name;
        }
    }
    return {};
}

GameIndex map_game_files(
    const CabinetIndex& cabinet, const IsoIndex& iso, std::string_view language)
{
    const Layout* layout = has_group(cabinet, euro_layout.data_group) ? &euro_layout
                           : has_group(cabinet, us_layout.data_group) ? &us_layout
                                                                      : &uk_layout;
    GameIndex result;
    result.profile = layout->profile;
    for (const auto& item : languages)
    {
        const bool offered = layout != &euro_layout ? item.code == std::string_view("en")
                                                    : item.code != std::string_view("en") &&
                                                          has_group(cabinet, item.config_group);
        if (offered)
        {
            result.languages.emplace_back(item.code);
        }
    }
    if (result.languages.empty())
    {
        throw std::runtime_error("Unsupported game layout: no game language is offered.");
    }
    result.language = language.empty() ? result.languages.front() : std::string(language);
    if (std::find(result.languages.begin(), result.languages.end(), result.language) ==
        result.languages.end())
    {
        throw std::runtime_error("This disc does not offer the selected game language.");
    }
    const auto config_group = std::find_if(languages.begin(),
        languages.end(),
        [&](const Language& item)
        {
            return item.code == result.language;
        })->config_group;

    std::map<std::string, const IsoEntry*> loose;
    for (const auto& entry : iso.entries)
    {
        if (!loose.emplace(normalize_game_path(entry.path).key, &entry).second)
        {
            throw std::runtime_error("The ISO contains ambiguous source paths.");
        }
    }
    std::map<std::string, GameAsset> assets;
    std::set<std::string> groups;
    std::set<std::size_t> selected;
    bool data_found = false, ini_found = false, executable_found = false;
    for (const auto& group : cabinet.groups)
    {
        const bool data = group.name == layout->data_group || group.name == layout->loose_group;
        const bool config = group.name == config_group;
        if (!data && !config)
        {
            continue;
        }
        if (!groups.insert(group.name).second || group.first_file < 0 ||
            group.last_file < group.first_file ||
            static_cast<std::size_t>(group.last_file) >= cabinet.files.size())
        {
            throw std::runtime_error("Invalid or duplicate game file group.");
        }
        for (int i = group.first_file; i <= group.last_file; ++i)
        {
            const auto& file = cabinet.files[static_cast<std::size_t>(i)];
            if (file.kind == "placeholder")
            {
                continue;
            }
            const auto raw = file.directory.empty() ? file.name : file.directory + "/" + file.name;
            // These observed layouts use ASCII names for everything the game
            // reads. Do not guess a legacy code page and silently map the wrong
            // runtime path. The European disc's root readmes (L\xe9eme.htm) are
            // installer documents outside Data and are not game content.
            if (std::any_of(raw.begin(),
                    raw.end(),
                    [](unsigned char c)
                    {
                        return c >= 128;
                    }))
            {
                const bool root_document =
                    file.directory.empty() && std::find(raw.begin(), raw.end(), '/') == raw.end();
                if (!root_document)
                {
                    throw std::runtime_error("This game profile requires ASCII asset names.");
                }
                continue;
            }
            const auto path = normalize_game_path(raw);
            const bool ini = config && path.key == "beachlife.ini";
            const bool asset = data && path.key.starts_with("data/");
            // The one root executable is cached only as data for native constants.
            // Both existing cabinet and external descriptors keep their normal checks.
            const bool executable = data && path.key == layout->executable;
            if (!ini && !asset && !executable)
            {
                continue;
            }
            if (!selected.insert(static_cast<std::size_t>(i)).second)
            {
                throw std::runtime_error("Game file groups overlap.");
            }
            if (file.kind != "cabinet" && file.kind != "external")
            {
                throw std::runtime_error("Unsupported game file descriptor.");
            }
            // The runtime reads its module image as BeachLife.exe on every disc.
            GameAsset mapped{executable ? std::string("BeachLife.exe") : path.display,
                file.size,
                static_cast<std::size_t>(i),
                file.kind == "external",
                {}};
            if (mapped.external)
            {
                const auto found = loose.find(path.key);
                if (found == loose.end() || found->second->directory)
                {
                    throw std::runtime_error(
                        "An external game file is missing or has the wrong size.");
                }
                if (found->second->size != file.size)
                {
                    // Retail pressings wrap the executable in copy protection after
                    // the installer was built. Only the root executable may differ;
                    // it is read as data, never run.
                    if (!executable)
                    {
                        throw std::runtime_error(
                            "An external game file is missing or has the wrong size.");
                    }
                    mapped.loose = true;
                    mapped.size = found->second->size;
                }
                mapped.source_path = found->second->path;
            }
            if (!assets.emplace(path.key, std::move(mapped)).second)
            {
                throw std::runtime_error("Game content has ambiguous logical paths.");
            }
            const auto size = assets.at(path.key).size;
            if (size > std::numeric_limits<std::uint64_t>::max() - result.total_bytes)
            {
                throw std::runtime_error("Game data size overflows.");
            }
            result.total_bytes += size;
            data_found |= asset;
            ini_found |= ini;
            executable_found |= executable;
        }
    }
    if (!data_found || !ini_found || !executable_found || !groups.contains(layout->data_group))
    {
        throw std::runtime_error("Unsupported game layout: expected the UK, European or US game "
                                 "data, BeachLife.ini and BeachLife.exe.");
    }
    if (layout == &us_layout)
    {
        for (const auto& [alias, original] : us_aliases)
        {
            const auto found = assets.find(original);
            if (found == assets.end())
            {
                throw std::runtime_error("The US game data lacks an expected file.");
            }
            GameAsset copy = found->second;
            copy.path = normalize_game_path(alias).display;
            const auto size = copy.size;
            if (!assets.emplace(normalize_game_path(alias).key, std::move(copy)).second)
            {
                throw std::runtime_error("Game content has ambiguous logical paths.");
            }
            result.total_bytes += size;
        }
    }
    move_assets(assets, result);
    return result;
}

GameIndex map_wise_game_files(const WiseInstaller& installer, std::string_view language)
{
    if (!language.empty() && language != "en")
    {
        throw std::runtime_error("This disc does not offer the selected game language.");
    }
    GameIndex result;
    result.profile = wise_profile;
    result.language = "en";
    result.languages = {"en"};
    std::map<std::string, GameAsset> assets;
    bool data_found = false, executable_found = false;
    for (const auto& file : installer.files)
    {
        const auto path = normalize_game_path(file.path);
        const bool asset = path.key.starts_with("data/");
        const bool executable = path.key == "beachlife.exe";
        if (!asset && !executable)
        {
            continue;
        }
        GameAsset mapped{path.display, file.size, 0, false, installer.installer};
        mapped.wise = true;
        mapped.wise_offset = file.offset;
        mapped.wise_crc = file.crc;
        if (!assets.emplace(path.key, std::move(mapped)).second)
        {
            throw std::runtime_error("Game content has ambiguous logical paths.");
        }
        result.total_bytes += file.size;
        data_found |= asset;
        executable_found |= executable;
    }
    if (!data_found || !executable_found)
    {
        throw std::runtime_error("This installer does not contain the Beach Life game data.");
    }
    move_assets(assets, result);
    return result;
}

void write_game_index_json(std::ostream& output, const GameIndex& index)
{
    // normalize_game_path forbids quotes, backslashes and controls in display paths.
    output << "{\"profile\":\"" << index.profile << "\",\"language\":\"" << index.language
           << "\",\"languages\":[";
    for (std::size_t i = 0; i < index.languages.size(); ++i)
    {
        output << (i ? ",\"" : "\"") << index.languages[i] << '"';
    }
    output << "],\"total_bytes\":" << index.total_bytes << ",\"files\":[";
    for (std::size_t i = 0; i < index.files.size(); ++i)
    {
        if (i)
        {
            output << ',';
        }
        const auto& file = index.files[i];
        output << "{\"path\":\"" << file.path << "\",\"size\":" << file.size
               << ",\"cabinet_index\":" << file.cabinet_index << ",\"storage\":\""
               << (file.wise          ? "wise"
                      : file.loose    ? "iso-loose"
                      : file.external ? "iso"
                                      : "cabinet")
               << "\"}";
    }
    output << "]}\n";
}
} // namespace beachlife::data
