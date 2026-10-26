#pragma once
#include "settings.h"

namespace beachlife::launcher
{

enum Page
{
    PagePlay,
    PageFiles,
    PageGameplay,
    PageDisplay,
    PageAppearance,
    PageAdvanced,
    PageAbout,
    PageCount
};

struct Options
{
    std::filesystem::path config;
    std::filesystem::path screenshot;
    std::filesystem::path cache; // empty uses the per-user content directory
    std::string source;
    int page = PagePlay;
    std::string update_preview; // a made-up update state for screenshots (UpdateService::preview)
    int argc = 0;               // the command line, to restart after an update
    char** argv = nullptr;
    bool smoke = false;
};

// Play or explicit preparation asks the caller to open the game's loading screen.
struct Outcome
{
    int exit_code = 0;
    bool play = false;
    bool prepare_only = false;  // use the loading screen for explicit prepare/repair, then return
    std::string preview_screen; // selected artwork to show through the native VFS
    Settings settings;
    std::filesystem::path config;
};

Outcome run(const Options& options);
// --play: the saved settings (or --config) with --source applied and saved,
// without opening the launcher. Settings that cannot be read are an error and
// are never overwritten.
Outcome play_settings(const Options& options);

} // namespace beachlife::launcher
