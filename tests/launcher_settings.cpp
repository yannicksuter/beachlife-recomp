#include "launcher/settings.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace beachlife::launcher;
namespace fs = std::filesystem;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main() {
    const auto temporary = fs::temp_directory_path() / ("beachlife-settings-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(temporary);
    try {
        const auto file = temporary / "preferences" / "launcher.cfg";
        check(load_settings(file) == Settings{}, "Missing settings should use defaults");
        Settings settings;
        settings.source = path_utf8(temporary / path_from_utf8("Grüsse / \"quoted\" \\ source.iso"));
        settings.scale = 125;
        save_settings(file, settings);
        check(load_settings(file) == settings, "Unicode/quoted paths must survive saving");
        settings.source = path_utf8(temporary / "Beach Life.iso");
        settings.scale = 150;
        save_settings(file, settings);
        check(load_settings(file) == settings, "Saving must replace the previous file");
        settings.display = {WindowMode::Fullscreen, 1920, 1080, 2, Scaling::Stretch, false, GraphicsApi::Vulkan};
        settings.diagnostics.log_window = true;
        settings.updates = {false, UpdateChoice::Test};
        save_settings(file, settings);
        check(load_settings(file) == settings, "Display, diagnostics and update settings must survive saving");
        auto write = [&](const std::string& text) { std::ofstream out(file, std::ios::binary); out << text; };
        auto rejects = [&](const std::string& text) {
            write(text);
            try { (void)load_settings(file); } catch (const std::runtime_error&) { return true; }
            return false;
        };
        write("beachlife-launcher 1\nfolder\n125\n\"/games/Beach Life\"\n");
        const auto migrated = load_settings(file);
        check(migrated.scale == 125 && migrated.source == "/games/Beach Life" &&
              migrated.display == DisplaySettings{} && !migrated.diagnostics.log_window &&
              migrated.updates == UpdateSettings{},
              "Version 1 settings must load with default display and diagnostics settings");
        check(load_settings(file) == migrated && rejects("beachlife-launcher 2\nlauncher.scale 100\nlauncher.scale 125\n"),
              "Repeated settings must be rejected");
        check(rejects("beachlife-launcher 2\nsource.kind folder\n"), "The source type is no longer a setting");
        check(rejects("beachlife-launcher 2\ndisplay.colour red\n"), "Unknown settings must be rejected");
        check(rejects("beachlife-launcher 2\ndisplay.mode borderless\n"), "Unknown display modes must be rejected");
        check(rejects("beachlife-launcher 2\ndisplay.width 320\n"), "Window sizes below 640 x 480 must be rejected");
        check(rejects("beachlife-launcher 2\ndisplay.vsync on extra\n"), "Trailing values must be rejected");
        check(rejects("beachlife-launcher 2\ndiagnostics.log_window yes\n"), "Switches must be on or off");
        check(rejects("beachlife-launcher 2\nupdates.channel nightly\n"), "Unknown update channels must be rejected");
        write("beachlife-launcher 2\ndisplay.vsync off\n");
        auto partial = Settings{};
        partial.display.vsync = false;
        check(load_settings(file) == partial, "Missing keys must keep their defaults");
        settings.display.width = 100;
        bool too_small = false;
        try { save_settings(file, settings); } catch (const std::runtime_error&) { too_small = true; }
        check(too_small && load_settings(file) == partial, "Invalid display settings must not be written");
        settings.display = {};
        settings.diagnostics = {};
        settings.updates = {};
        save_settings(file, settings);
        settings.source = path_utf8(temporary);
        const auto folder = inspect_source(settings);
        check(folder.selected && !folder.readable, "A folder is not a disc image and must ask for one");
        settings.source = path_utf8(temporary / "missing");
        check(!inspect_source(settings).readable, "Missing sources must be reported");
        settings.source = path_utf8(temporary / "fixture.iso");
        std::vector<char> data(17*2048);
        const std::string signature = "CD001";
        data[16*2048] = 1;
        std::copy(signature.begin(), signature.end(), data.begin()+16*2048+1);
        data[16*2048+6] = 1;
        std::ofstream image(path_from_utf8(settings.source), std::ios::binary);
        image.write(data.data(), static_cast<std::streamsize>(data.size())); image.close();
        const auto status = inspect_source(settings);
        check(status.readable && status.bytes == data.size(), "ISO descriptor should be recognized");
        check(status.detail.find("not been inspected") != std::string::npos, "Container recognition must not claim game/language verification");
        fs::resize_file(path_from_utf8(settings.source), 12);
        check(!inspect_source(settings).readable, "Truncated images must be rejected");
        const auto before = load_settings(file);
        settings.source = "bad\npath";
        bool rejected = false;
        try { save_settings(file, settings); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected && load_settings(file) == before, "Invalid writes must preserve saved preferences");
        std::ofstream corrupt(file); corrupt << "not settings"; corrupt.close();
        rejected = false;
        try { (void)load_settings(file); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected && fs::file_size(file) == 12, "Malformed settings must be reported and preserved");
        fs::remove_all(temporary);
        std::cout << "Launcher persistence and source checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        fs::remove_all(temporary);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
