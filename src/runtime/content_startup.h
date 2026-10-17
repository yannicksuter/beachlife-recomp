#pragma once

#include "data/source.h"
#include "runtime/startup.h"

namespace beachlife::runtime {

// The current startup read, queued behind the game window's loading screen.
// Retains the source (and prepared mount lock) for the queued work's lifetime.
StartupStep read_game_files(std::shared_ptr<const data::FileSource> content);

} // namespace beachlife::runtime
