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
        settings.source = path_utf8(temporary);
        settings.kind = SourceKind::Folder;
        settings.scale = 150;
        save_settings(file, settings);
        check(load_settings(file) == settings, "Saving must replace the previous file");
        check(inspect_source(settings).readable, "Existing folder should be selectable");
        settings.source = path_utf8(temporary / "missing");
        check(!inspect_source(settings).readable, "Missing sources must be reported");
        settings.source = path_utf8(temporary / "fixture.iso");
        settings.kind = SourceKind::Iso;
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
