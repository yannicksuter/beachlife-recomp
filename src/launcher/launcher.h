#pragma once
#include "settings.h"

namespace beachlife::launcher {

enum Page { PagePlay, PageFiles, PageDisplay, PageAppearance, PageAdvanced, PageAbout, PageCount };

struct Options {
    std::filesystem::path config;
    std::filesystem::path screenshot;
    std::string source;
    int page = PagePlay;
    std::string update_preview; // a made-up update state for screenshots (UpdateService::preview)
    int argc = 0;               // the command line, to restart after an update
    char** argv = nullptr;
    bool smoke = false;
};

// How the launcher was left: Play asks the caller to start the game with `settings`.
struct Outcome {
    int exit_code = 0;
    bool play = false;
    Settings settings;
};

Outcome run(const Options& options);

} // namespace beachlife::launcher
