#pragma once

#include "launcher/settings.h"
#include "data/content_manifest.h"

#include <filesystem>

namespace beachlife::runtime {

struct Options {
    launcher::Settings settings;      // the game files and the Display settings
    std::shared_ptr<const data::PreparedSource> content; // mounted once, held for the entire game session
    std::filesystem::path resources;  // assets/launcher: the overlay's font
    std::filesystem::path screenshot; // saves the last frame of a `frames` run as PNG
    int frames = 0;                   // stops after this many frames (checks); 0 runs until the player leaves
    bool hidden = false;              // checks: no visible window, so nothing is presented
};

// Reads prepared files through the VFS and opens the chosen graphics API, then runs
// the game loop until the player leaves (Esc or closing the window). The game
// itself is not reconstructed yet: its back buffer stays black and a small
// overlay says so. Returns 0, or 1 after reporting an error.
int run(const Options& options);

} // namespace beachlife::runtime
