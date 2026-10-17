#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace beachlife::launcher {

enum class SourceKind { Iso, Folder };

struct Settings {
    std::string source;
    SourceKind kind = SourceKind::Iso;
    int scale = 100;
    bool operator==(const Settings&) const = default;
};

struct SourceStatus {
    bool selected = false;
    bool readable = false;
    std::string title = "No game files selected";
    std::string detail = "Choose your original disc image or installed game folder.";
    std::uintmax_t bytes = 0;
};

std::filesystem::path path_from_utf8(const std::string& value);
std::string path_utf8(const std::filesystem::path& value);
Settings load_settings(const std::filesystem::path& path);
void save_settings(const std::filesystem::path& path, const Settings& settings);
SourceStatus inspect_source(const Settings& settings);

} // namespace beachlife::launcher
