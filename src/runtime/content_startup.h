#pragma once

#include "data/source.h"
#include "data/content_prepare.h"
#include "launcher/settings.h"
#include "runtime/startup.h"

namespace beachlife::runtime {

// The current startup read, queued behind the game window's loading screen.
// Retains the source (and prepared mount lock) for the queued work's lifetime.
StartupStep read_game_files(std::shared_ptr<const data::FileSource> content);

// Owns the setup result for a whole game session. Queued steps retain it too.
// All IO, including the saved offline binding, runs on the startup worker.
class ContentStartup {
public:
    ContentStartup(launcher::Settings settings, std::filesystem::path cache,
                   std::filesystem::path config, bool repair = false);
    // Optional observer runs on the worker for diagnostics, never UI calls.
    StartupStep prepare(std::function<void(const data::PrepareProgress&)> observer = {}) const;
    StartupStep read() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace beachlife::runtime
