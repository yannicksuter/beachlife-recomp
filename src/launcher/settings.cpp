#include "settings.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace beachlife::launcher {
namespace fs = std::filesystem;

fs::path path_from_utf8(const std::string& value) {
    return fs::path(std::u8string(value.begin(), value.end()));
}

std::string path_utf8(const fs::path& value) {
    const auto encoded = value.u8string();
    return {encoded.begin(), encoded.end()};
}

static void validate(const Settings& settings) {
    if (settings.source.size() > 32768 || settings.source.find_first_of("\r\n\0", 0, 3) != std::string::npos)
        throw std::runtime_error("The source path contains an unsupported character or is too long.");
    if (settings.scale != 100 && settings.scale != 125 && settings.scale != 150)
        throw std::runtime_error("Unsupported interface scale.");
    if (settings.kind != SourceKind::Iso && settings.kind != SourceKind::Folder)
        throw std::runtime_error("Unknown source type.");
}

Settings load_settings(const fs::path& path) {
    std::error_code error;
    const bool exists = fs::exists(path, error);
    if (error) throw std::runtime_error("Cannot access launcher settings: " + error.message());
    if (!exists) return {};
    if (!fs::is_regular_file(path, error) || error || fs::file_size(path, error) > 65536 || error)
        throw std::runtime_error("The settings file is unreadable or invalid.");
    std::ifstream file(path, std::ios::binary);
    std::string magic, kind;
    Settings result;
    if (!std::getline(file, magic) || magic != "beachlife-launcher 1" ||
        !(file >> kind >> result.scale >> std::quoted(result.source)))
        throw std::runtime_error("Cannot read launcher settings. The existing file has been preserved.");
    file >> std::ws;
    if (!file.eof() || (kind != "iso" && kind != "folder"))
        throw std::runtime_error("The launcher settings format is invalid.");
    result.kind = kind == "iso" ? SourceKind::Iso : SourceKind::Folder;
    validate(result);
    return result;
}

void save_settings(const fs::path& path, const Settings& settings) {
    validate(settings);
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    fs::path temporary = path;
    temporary += ".tmp";
    try {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << "beachlife-launcher 1\n" << (settings.kind == SourceKind::Iso ? "iso" : "folder")
             << '\n' << settings.scale << '\n' << std::quoted(settings.source) << '\n';
        file.close();
        if (!file) throw std::runtime_error("Cannot write launcher settings.");
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace launcher settings.");
#else
        fs::rename(temporary, path);
#endif
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}

SourceStatus inspect_source(const Settings& settings) {
    SourceStatus status;
    if (settings.source.empty()) return status;
    status.selected = true;
    try {
        validate(settings);
        const auto path = path_from_utf8(settings.source);
        std::error_code error;
        if (!fs::exists(path, error) || error) {
            status.title = "Game files not found";
            status.detail = "Reconnect the drive or choose another source.";
            return status;
        }
        if (settings.kind == SourceKind::Folder) {
            if (!fs::is_directory(path, error) || error)
                throw std::runtime_error("Choose a folder, or switch the source type to Disc image.");
            fs::directory_iterator probe(path, error);
            if (error) throw std::runtime_error("The selected folder cannot be read.");
            status.title = "Game folder selected";
        } else {
            if (!fs::is_regular_file(path, error) || error)
                throw std::runtime_error("Choose an ISO file, or switch the source type to Installed folder.");
            status.bytes = fs::file_size(path, error);
            if (error) throw std::runtime_error("The selected file cannot be read.");
            std::ifstream file(path, std::ios::binary);
            std::array<char, 7> descriptor{};
            file.seekg(16 * 2048);
            file.read(descriptor.data(), static_cast<std::streamsize>(descriptor.size()));
            if (!file || std::string_view(descriptor.data() + 1, 5) != "CD001" || descriptor[6] != 1 ||
                status.bytes < 17 * 2048)
                throw std::runtime_error("This is not a supported ISO 9660 image. Choose your original Beach Life disc image.");
            status.title = "ISO image selected";
        }
        status.readable = true;
        status.detail = "Game content and languages have not been inspected yet.";
    } catch (const std::exception& error) {
        status.title = "Source needs attention";
        status.detail = error.what();
    }
    return status;
}

} // namespace beachlife::launcher
