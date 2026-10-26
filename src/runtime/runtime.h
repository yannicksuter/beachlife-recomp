#pragma once

#include "launcher/settings.h"

#include <filesystem>

namespace beachlife::runtime
{

struct Options
{
    launcher::Settings settings;      // the game files and the Display settings
    std::filesystem::path config;     // remember successful setup for offline play
    std::filesystem::path cache;      // empty uses the per-user content directory
    std::filesystem::path resources;  // assets/launcher: the overlay's font
    std::filesystem::path screenshot; // saves the last frame of a `frames` run as PNG
    std::string preview_screen;       // optional logical TGA path; an asset preview, not gameplay
    int frames = 0;        // stops after this many frames (checks); 0 runs until the player leaves
    bool hidden = false;   // checks: no visible window, so nothing is presented
    bool headless = false; // tests: offscreen window, no GPU, silent audio
    bool prepare_only = false; // explicit prepare/repair: return after the loading screen
};

// Opens the chosen graphics API, prepares/mounts data behind the loading screen, then runs
// the decompiled game when linked, or the independent runtime/preview in a
// launcher development build. Returns 0, or 1 after reporting a host error.
int run(const Options& options);

} // namespace beachlife::runtime
