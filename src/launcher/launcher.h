#pragma once
#include "settings.h"

namespace beachlife::launcher {

enum Page { PagePlay, PageFiles, PageDisplay, PageAppearance, PageAdvanced, PageAbout, PageCount };

struct Options {
    std::filesystem::path config;
    std::filesystem::path screenshot;
    std::filesystem::path cache; // empty uses the per-user content directory
    std::string source;
    int page = PagePlay;
    std::string update_preview; // a made-up update state for screenshots (UpdateService::preview)
    int argc = 0;               // the command line, to restart after an update
    char** argv = nullptr;
    bool smoke = false;
    bool auto_play = false; // --play: leave straight for the game's loading screen
};

// Play or explicit preparation asks the caller to open the game's loading screen.
struct Outcome {
    int exit_code = 0;
    bool play = false;
    bool prepare_only = false; // use the loading screen for explicit prepare/repair, then return
    Settings settings;
    std::filesystem::path config;
};

Outcome run(const Options& options);

} // namespace beachlife::launcher
