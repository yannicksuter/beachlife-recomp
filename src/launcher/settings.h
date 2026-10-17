#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace beachlife::launcher {

enum class WindowMode { Windowed, Fullscreen };
enum class Scaling { Fit, Stretch };
enum class GraphicsApi { Automatic, Direct3D12, Vulkan, Metal };
enum class OverlayCorner { TopLeft, TopRight, BottomLeft, BottomRight };

// How the native game will open its window. Saved now, applied once the game runs.
struct DisplaySettings {
    WindowMode mode = WindowMode::Windowed;
    int width = 1280, height = 960;
    int monitor = 0; // 0 is the primary display, otherwise its 1-based position
    Scaling scaling = Scaling::Fit;
    bool vsync = true;
    GraphicsApi api = GraphicsApi::Automatic;
    bool fps_overlay = false; // the performance overlay: FPS, GPU and CPU usage over the game picture
    OverlayCorner fps_overlay_corner = OverlayCorner::TopRight;
    bool operator==(const DisplaySettings&) const = default;
};

enum class UpdateChoice { Automatic, Release, Test };

// Where updates come from: releases or test builds (Automatic: test builds keep
// to test builds, everything else to releases), and whether the launcher looks at start.
struct UpdateSettings {
    bool check = true;
    UpdateChoice channel = UpdateChoice::Automatic;
    bool operator==(const UpdateSettings&) const = default;
};

// Local troubleshooting aids; no usage data is collected or sent.
struct DiagnosticsSettings {
    bool log_window = false; // a window that follows the game's log while it runs
    bool operator==(const DiagnosticsSettings&) const = default;
};

// The prepared identity belongs to this exact source selection. Size/time are
// a fast replacement check, not an integrity checksum; the manifest holds the
// full source fingerprint. An absent ISO can still use the last prepared set.
struct ContentBinding {
    std::string identity, source;
    std::uint64_t size = 0;
    std::int64_t modified = 0;
    bool operator==(const ContentBinding&) const = default;
};

struct Settings {
    std::string source; // the game's ISO disc image
    int scale = 100;
    DisplaySettings display;
    DiagnosticsSettings diagnostics;
    UpdateSettings updates;
    ContentBinding content;
    bool operator==(const Settings&) const = default;
};

struct SourceStatus {
    bool selected = false;
    bool readable = false;
    std::string title = "No game files selected";
    std::string detail = "Choose the ISO image of your original Beach Life CD.";
    std::uintmax_t bytes = 0;
};

std::filesystem::path path_from_utf8(const std::string& value);
std::string path_utf8(const std::filesystem::path& value);
Settings load_settings(const std::filesystem::path& path);
void save_settings(const std::filesystem::path& path, const Settings& settings);
SourceStatus inspect_source(const Settings& settings);

} // namespace beachlife::launcher
