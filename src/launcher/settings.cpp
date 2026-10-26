#include "settings.h"

#include <array>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace beachlife::launcher
{
namespace fs = std::filesystem;

fs::path path_from_utf8(const std::string& value)
{
    return fs::path(std::u8string(value.begin(), value.end()));
}

std::string path_utf8(const fs::path& value)
{
    const auto encoded = value.u8string();
    return {encoded.begin(), encoded.end()};
}

namespace
{
// Each choice as it is spelled in the settings file.
template <typename T> struct Name
{
    T value;
    const char* text;
};
constexpr Name<WindowMode> modes[] = {
    {WindowMode::Windowed, "windowed"}, {WindowMode::Fullscreen, "fullscreen"}};
constexpr Name<Scaling> scalings[] = {{Scaling::Fit, "fit"}, {Scaling::Stretch, "stretch"}};
constexpr Name<GraphicsApi> apis[] = {{GraphicsApi::Automatic, "auto"},
    {GraphicsApi::Direct3D12, "d3d12"},
    {GraphicsApi::Vulkan, "vulkan"},
    {GraphicsApi::Metal, "metal"}};
constexpr Name<OverlayCorner> corners[] = {{OverlayCorner::TopLeft, "top-left"},
    {OverlayCorner::TopRight, "top-right"},
    {OverlayCorner::BottomLeft, "bottom-left"},
    {OverlayCorner::BottomRight, "bottom-right"}};
constexpr Name<bool> switches[] = {{false, "off"}, {true, "on"}};
constexpr Name<UpdateChoice> channels[] = {{UpdateChoice::Automatic, "auto"},
    {UpdateChoice::Release, "release"},
    {UpdateChoice::Test, "test"}};

template <typename T, size_t N> const char* name_of(const Name<T> (&names)[N], T value)
{
    for (const auto& name : names)
    {
        if (name.value == value)
        {
            return name.text;
        }
    }
    throw std::runtime_error("Unsupported launcher setting.");
}

template <typename T, size_t N> T value_of(const Name<T> (&names)[N], const std::string& text)
{
    for (const auto& name : names)
    {
        if (text == name.text)
        {
            return name.value;
        }
    }
    throw std::runtime_error("The launcher settings format is invalid.");
}

void validate_source(const Settings& settings)
{
    if (settings.source.size() > 32768 ||
        settings.source.find_first_of("\r\n\0", 0, 3) != std::string::npos)
    {
        throw std::runtime_error(
            "The source path contains an unsupported character or is too long.");
    }
}

void validate(const Settings& settings)
{
    validate_source(settings);
    const auto& content = settings.content;
    if ((!content.identity.empty() &&
            (content.identity.size() != 64 ||
                content.identity.find_first_not_of("0123456789abcdef") != std::string::npos ||
                content.source.empty())) ||
        (content.identity.empty() &&
            (!content.source.empty() || content.size || content.modified)) ||
        content.source.size() > 32768 ||
        content.source.find_first_of("\r\n\0", 0, 3) != std::string::npos)
    {
        throw std::runtime_error("The prepared-content reference is invalid.");
    }
    if (settings.scale != 100 && settings.scale != 125 && settings.scale != 150)
    {
        throw std::runtime_error("Unsupported interface scale.");
    }
    const auto& display = settings.display;
    if (display.width < 640 || display.width > 16384 || display.height < 480 ||
        display.height > 16384)
    {
        throw std::runtime_error("Unsupported window size.");
    }
    if (display.monitor < 0 || display.monitor > 64)
    {
        throw std::runtime_error("Unsupported display number.");
    }
    // Unknown enumerators have no file spelling and are rejected here.
    name_of(modes, display.mode);
    name_of(scalings, display.scaling);
    name_of(apis, display.api);
    name_of(corners, display.fps_overlay_corner);
    name_of(channels, settings.updates.channel);
}

// Versions 2-4: one "key value" line per setting; every key at most once, none unknown.
// Version 3 added the content keys, version 4 the performance overlay.
void read_keyed(std::istream& file, Settings& result, int version)
{
    const bool content_fields = version >= 3, overlay_fields = version >= 4;
    const std::runtime_error invalid("The launcher settings format is invalid.");
    std::set<std::string> seen;
    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream in(line);
        std::string key, word;
        if (!(in >> key) || !seen.insert(key).second)
        {
            throw invalid;
        }
        auto& display = result.display;
        if (key == "source.path")
        {
            in >> std::quoted(result.source);
        }
        else if (content_fields && key == "content.identity")
        {
            in >> std::quoted(result.content.identity);
        }
        else if (content_fields && key == "content.source")
        {
            in >> std::quoted(result.content.source);
        }
        else if (content_fields && key == "content.size")
        {
            in >> word;
            const auto parsed =
                std::from_chars(word.data(), word.data() + word.size(), result.content.size);
            if (parsed.ec != std::errc{} || parsed.ptr != word.data() + word.size())
            {
                throw invalid;
            }
        }
        else if (content_fields && key == "content.modified")
        {
            in >> result.content.modified;
        }
        else if (key == "launcher.scale")
        {
            in >> result.scale;
        }
        else if (key == "display.mode" && in >> word)
        {
            display.mode = value_of(modes, word);
        }
        else if (key == "display.width")
        {
            in >> display.width;
        }
        else if (key == "display.height")
        {
            in >> display.height;
        }
        else if (key == "display.monitor")
        {
            in >> display.monitor;
        }
        else if (key == "display.scaling" && in >> word)
        {
            display.scaling = value_of(scalings, word);
        }
        else if (key == "display.vsync" && in >> word)
        {
            display.vsync = value_of(switches, word);
        }
        else if (key == "display.api" && in >> word)
        {
            display.api = value_of(apis, word);
        }
        else if (overlay_fields && key == "display.fps_overlay" && in >> word)
        {
            display.fps_overlay = value_of(switches, word);
        }
        else if (overlay_fields && key == "display.fps_overlay_corner" && in >> word)
        {
            display.fps_overlay_corner = value_of(corners, word);
        }
        else if (key == "diagnostics.log_window" && in >> word)
        {
            result.diagnostics.log_window = value_of(switches, word);
        }
        else if (key == "tweaks.skip_intro" && in >> word)
        {
            result.tweaks.skip_intro = value_of(switches, word);
        }
        else if (key == "tweaks.wheel_zoom" && in >> word)
        {
            result.tweaks.wheel_zoom = value_of(switches, word);
        }
        else if (key == "updates.check" && in >> word)
        {
            result.updates.check = value_of(switches, word);
        }
        else if (key == "updates.channel" && in >> word)
        {
            result.updates.channel = value_of(channels, word);
        }
        else
        {
            throw invalid;
        }
        if (!in || !(in >> std::ws).eof())
        {
            throw invalid;
        }
    }
}
} // namespace

Settings load_settings(const fs::path& path)
{
    std::error_code error;
    const bool exists = fs::exists(path, error);
    if (error)
    {
        throw std::runtime_error("Cannot access launcher settings: " + error.message());
    }
    if (!exists)
    {
        return {};
    }
    if (!fs::is_regular_file(path, error) || error || fs::file_size(path, error) > 256 * 1024 ||
        error)
    {
        throw std::runtime_error("The settings file is unreadable or invalid.");
    }
    std::ifstream file(path, std::ios::binary);
    std::string magic, kind;
    Settings result;
    const std::string prefix = "beachlife-launcher ";
    const int version =
        std::getline(file, magic) && magic.size() == prefix.size() + 1 && magic.starts_with(prefix)
            ? magic.back() - '0'
            : 0;
    if (version < 1 || version > 4)
    {
        throw std::runtime_error(
            "Cannot read launcher settings. The existing file has been preserved.");
    }
    if (version > 1)
    {
        read_keyed(file, result, version);
    }
    else
    {
        // Version 1 (launcher 0.0.1): source kind, scale and quoted path; display defaults. Only
        // ISO images are supported now: a saved folder stays selected and asks for the image
        // instead.
        if (!(file >> kind >> result.scale >> std::quoted(result.source)))
        {
            throw std::runtime_error(
                "Cannot read launcher settings. The existing file has been preserved.");
        }
        file >> std::ws;
        if (!file.eof() || (kind != "iso" && kind != "folder"))
        {
            throw std::runtime_error("The launcher settings format is invalid.");
        }
    }
    validate(result);
    return result;
}

void save_settings(const fs::path& path, const Settings& settings)
{
    validate(settings);
    if (!path.parent_path().empty())
    {
        fs::create_directories(path.parent_path());
    }
    fs::path temporary = path;
    temporary += ".tmp";
    try
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        const auto& display = settings.display;
        file << "beachlife-launcher 4\n"
             << "source.path " << std::quoted(settings.source) << '\n'
             << "content.identity " << std::quoted(settings.content.identity) << '\n'
             << "content.source " << std::quoted(settings.content.source) << '\n'
             << "content.size " << settings.content.size << '\n'
             << "content.modified " << settings.content.modified << '\n'
             << "launcher.scale " << settings.scale << '\n'
             << "display.mode " << name_of(modes, display.mode) << '\n'
             << "display.width " << display.width << '\n'
             << "display.height " << display.height << '\n'
             << "display.monitor " << display.monitor << '\n'
             << "display.scaling " << name_of(scalings, display.scaling) << '\n'
             << "display.vsync " << name_of(switches, display.vsync) << '\n'
             << "display.api " << name_of(apis, display.api) << '\n'
             << "display.fps_overlay " << name_of(switches, display.fps_overlay) << '\n'
             << "display.fps_overlay_corner " << name_of(corners, display.fps_overlay_corner)
             << '\n'
             << "diagnostics.log_window " << name_of(switches, settings.diagnostics.log_window)
             << '\n'
             << "tweaks.skip_intro " << name_of(switches, settings.tweaks.skip_intro) << '\n'
             << "tweaks.wheel_zoom " << name_of(switches, settings.tweaks.wheel_zoom) << '\n'
             << "updates.check " << name_of(switches, settings.updates.check) << '\n'
             << "updates.channel " << name_of(channels, settings.updates.channel) << '\n';
        file.close();
        if (!file)
        {
            throw std::runtime_error("Cannot write launcher settings.");
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            throw std::runtime_error("Cannot replace launcher settings.");
        }
#else
        fs::rename(temporary, path);
#endif
    }
    catch (...)
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}

SourceStatus inspect_source(const Settings& settings)
{
    SourceStatus status;
    if (settings.source.empty())
    {
        return status;
    }
    status.selected = true;
    try
    {
        validate_source(settings);
        const auto path = path_from_utf8(settings.source);
        std::error_code error;
        if (!fs::exists(path, error) || error)
        {
            status.title = "Game files not found";
            status.detail = "Reconnect the drive or choose another source.";
            return status;
        }
        if (fs::is_directory(path, error))
        {
            throw std::runtime_error("This is a folder. Choose the disc image (.iso file) itself.");
        }
        if (!fs::is_regular_file(path, error) || error)
        {
            throw std::runtime_error("Choose your Beach Life disc image (.iso file).");
        }
        status.bytes = fs::file_size(path, error);
        if (error)
        {
            throw std::runtime_error("The selected file cannot be read.");
        }
        std::ifstream file(path, std::ios::binary);
        std::array<char, 7> descriptor{};
        file.seekg(16 * 2048);
        file.read(descriptor.data(), static_cast<std::streamsize>(descriptor.size()));
        if (!file || std::string_view(descriptor.data() + 1, 5) != "CD001" || descriptor[6] != 1 ||
            status.bytes < 17 * 2048)
        {
            throw std::runtime_error("This is not a supported ISO 9660 image. Choose your original "
                                     "Beach Life disc image.");
        }
        status.title = "ISO image selected";
        status.readable = true;
        status.detail = "Game content and languages have not been inspected yet.";
    }
    catch (const std::exception& error)
    {
        status.title = "Source needs attention";
        status.detail = error.what();
    }
    return status;
}

} // namespace beachlife::launcher
