#pragma once

#include "data/content_prepare.h"
#include "data/source.h"
#include "data/tga.h"
#include "launcher/settings.h"
#include "runtime/startup.h"

namespace beachlife::runtime
{

// The current startup read, queued behind the game window's loading screen.
// Retains the source (and prepared mount lock) for the queued work's lifetime.
StartupStep read_game_files(std::shared_ptr<const data::FileSource> content);

// Reads a selected screen through the mounted VFS. The queued step owns both
// source and result. Use the result only after successful worker completion;
// partial images are never published, and cancelled work must not be presented.
StartupStep read_screen_preview(std::shared_ptr<const data::FileSource> content,
    std::string logical_path,
    std::shared_ptr<data::TgaImage> result);

// Owns the setup result for a whole game session. Queued steps retain it too.
// All IO, including the saved offline binding, runs on the startup worker.
class ContentStartup
{
  public:
    // A single-use destination for the worker's final handoff step. Only consume
    // source after successful loading and worker join. Late cancellation can
    // leave a staged source here; pointer presence does not authorize startup.
    struct Result
    {
        std::shared_ptr<const data::FileSource> source;

      private:
        friend class ContentStartup;
        bool queued_ = false;
    };

    ContentStartup(launcher::Settings settings,
        std::filesystem::path cache,
        std::filesystem::path config,
        bool repair = false);
    // Optional observer runs on the worker for diagnostics, never UI calls.
    StartupStep prepare(std::function<void(const data::PrepareProgress&)> observer = {}) const;
    StartupStep read() const;
    StartupStep preview(std::string logical_path, std::shared_ptr<data::TgaImage> result) const;
    // Queue last, using a fresh empty result. No result access is allowed while
    // the worker runs. The retained source outlives this object and queued work;
    // bind any thread-local consumer on its own thread after successful loading.
    StartupStep handoff(std::shared_ptr<Result> result) const;

  private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace beachlife::runtime
