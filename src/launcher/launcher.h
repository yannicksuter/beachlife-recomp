#pragma once
#include "settings.h"
#include "data/content_manifest.h"

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
    bool auto_play = false; // --play: show setup progress, then leave for the runtime
};

// How the launcher was left: Play asks the caller to start the game with `settings`.
struct Outcome {
    int exit_code = 0;
    bool play = false;
    Settings settings;
    std::shared_ptr<const data::PreparedSource> content;
};

Outcome run(const Options& options);

} // namespace beachlife::launcher
