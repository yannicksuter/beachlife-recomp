#include "runtime/content_startup.h"
#include "data/game_path.h"
#include "launcher/content_service.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace beachlife::runtime
{
namespace
{
struct PreviewCancelled
{
};

// The decoder only sees the VFS contract. This adapter keeps actual read progress
// and cooperative cancellation on the startup worker, away from the renderer.
class PreviewFile final : public data::SourceFile
{
  public:
    PreviewFile(std::unique_ptr<data::SourceFile> file, StartupProgress& progress)
        : file_(std::move(file))
        , progress_(progress)
    {
        if (!file_)
        {
            throw std::runtime_error("Cannot open the image from prepared game data.");
        }
        progress_.total(file_->size());
    }
    std::uint64_t size() const override
    {
        return file_->size();
    }
    std::uint64_t tell() const override
    {
        return file_->tell();
    }
    void seek(std::uint64_t position) override
    {
        check_cancelled();
        file_->seek(position);
    }
    std::size_t read(std::span<unsigned char> output) override
    {
        check_cancelled();
        const auto count = file_->read(output);
        if (count > output.size())
        {
            throw std::runtime_error("Invalid image read from prepared game data.");
        }
        progress_.advance(count);
        check_cancelled();
        return count;
    }

  private:
    void check_cancelled() const
    {
        if (progress_.cancelled())
        {
            throw PreviewCancelled{};
        }
    }
    std::unique_ptr<data::SourceFile> file_;
    StartupProgress& progress_;
};

class PreviewSource final : public data::FileSource
{
  public:
    PreviewSource(const data::FileSource& source, StartupProgress& progress)
        : source_(source)
        , progress_(progress)
    {
    }
    std::unique_ptr<data::SourceFile> open(std::string_view path) const override
    {
        if (progress_.cancelled())
        {
            throw PreviewCancelled{};
        }
        return std::make_unique<PreviewFile>(source_.open(path), progress_);
    }

  private:
    const data::FileSource& source_;
    StartupProgress& progress_;
};
} // namespace

struct ContentStartup::State
{
    launcher::Settings settings;
    std::filesystem::path cache, config;
    bool repair;
    std::shared_ptr<const data::PreparedSource> content;
};

ContentStartup::ContentStartup(launcher::Settings settings,
    std::filesystem::path cache,
    std::filesystem::path config,
    bool repair)
    : state_(std::make_shared<State>(
          State{std::move(settings), std::move(cache), std::move(config), repair, {}}))
{
}

StartupStep ContentStartup::prepare(
    std::function<void(const data::PrepareProgress&)> observer) const
{
    return {"Checking game files",
        [state = state_, observer = std::move(observer)](StartupProgress& progress)
        {
            data::PrepareOptions options;
            options.cancelled = [&]
            {
                return progress.cancelled();
            };
            options.progress = [&](const data::PrepareProgress& update)
            {
                // Each preparation phase has its own byte total. Map those measured
                // counts into successive quarters, so changing phases cannot reset it.
                constexpr std::uint64_t units = 1000000;
                std::uint64_t phase = 0;
                const char* label = "Reading disc image";
                if (update.phase == "checking")
                {
                    phase = 1;
                    label = "Checking local files";
                }
                else if (update.phase == "extracting")
                {
                    phase = 2;
                    label = "Preparing game files";
                }
                else if (update.phase == "confirming")
                {
                    phase = 3;
                    label = "Verifying disc image";
                }
                else if (update.phase == "ready")
                {
                    phase = 4;
                    label = "Opening game files";
                }
                const auto within = phase == 4 || !update.total_bytes
                                        ? 0
                                        : static_cast<std::uint64_t>(
                                              static_cast<long double>(std::min(
                                                  update.completed_bytes, update.total_bytes)) /
                                              static_cast<long double>(update.total_bytes) * units);
                progress.report(label, phase * units + within, 4 * units);
                if (observer)
                {
                    observer(update);
                }
            };
            try
            {
                auto result =
                    launcher::open_content(state->settings, state->cache, state->repair, options);
                if (result.status.state == launcher::ContentService::State::NeedsPreparation)
                {
                    result = launcher::open_content(state->settings, state->cache, true, options);
                }
                if (!result.source)
                {
                    if (progress.cancelled())
                    {
                        return;
                    }
                    throw std::runtime_error(
                        result.status.error.empty()
                            ? "Choose a readable Beach Life ISO to prepare your game files."
                            : result.status.error);
                }
                // Remember the ready set for offline play, also when the player left
                // just after it was published. Failing to save only costs that memory.
                auto settings = state->settings;
                settings.content = result.status.binding;
                if (settings != state->settings && !state->config.empty())
                {
                    try
                    {
                        launcher::save_settings(state->config, settings);
                    }
                    catch (const std::exception& error)
                    {
                        std::fprintf(stderr,
                            "Beach Life: the prepared game files are not remembered for next time: "
                            "%s\n",
                            error.what());
                    }
                }
                state->settings = std::move(settings);
                if (progress.cancelled())
                {
                    return;
                }
                state->content = std::move(result.source);
                std::printf("Game data: %zu files mounted, language %s.\n",
                    state->content->manifest().files.size(),
                    state->content->manifest().language.c_str());
            }
            catch (const data::PreparationCancelled&)
            {
                if (!progress.cancelled())
                {
                    throw;
                }
            }
        }};
}

namespace
{
// The game's startup read through the VFS, in chunks so progress and cancel stay live.
void read_startup_files(const data::FileSource& content, StartupProgress& progress)
{
    if (progress.cancelled())
    {
        return;
    }
    auto ini = content.open("BeachLife.ini");
    const auto size = ini->size();
    if (size > 1024 * 1024)
    {
        throw std::runtime_error("BeachLife.ini exceeds the startup read limit.");
    }
    progress.total(size);
    std::array<unsigned char, 4096> buffer{};
    std::uint64_t read = 0;
    while (read < size)
    {
        if (progress.cancelled())
        {
            return;
        }
        const auto remaining =
            static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), size - read));
        const auto count = ini->read(std::span(buffer).first(remaining));
        if (progress.cancelled())
        {
            return;
        }
        if (!count || count > remaining)
        {
            throw std::runtime_error("Cannot read BeachLife.ini from prepared game data.");
        }
        read += count;
        progress.advance(count);
    }
    if (progress.cancelled())
    {
        return;
    }
    std::printf("Game data: BeachLife.ini %llu bytes read through VFS.\n",
        static_cast<unsigned long long>(read));
}
} // namespace

StartupStep ContentStartup::read() const
{
    return {"Loading game files",
        [state = state_](StartupProgress& progress)
        {
            if (!state->content)
            {
                throw std::runtime_error("Game data is not mounted.");
            }
            read_startup_files(*state->content, progress);
        }};
}

StartupStep ContentStartup::preview(
    std::string logical_path, std::shared_ptr<data::TgaImage> result) const
{
    if (!result)
    {
        throw std::invalid_argument("A screen preview needs an image result.");
    }
    logical_path = data::normalize_game_path(logical_path).display;
    return {"Loading image",
        [state = state_, path = std::move(logical_path), result = std::move(result)](
            StartupProgress& progress)
        {
            read_screen_preview(state->content, path, result).run(progress);
        }};
}

StartupStep read_screen_preview(std::shared_ptr<const data::FileSource> content,
    std::string logical_path,
    std::shared_ptr<data::TgaImage> result)
{
    if (!content)
    {
        throw std::runtime_error("Game data is not mounted.");
    }
    if (!result)
    {
        throw std::invalid_argument("A screen preview needs an image result.");
    }
    logical_path = data::normalize_game_path(logical_path).display;
    return {"Loading image",
        [content = std::move(content), path = std::move(logical_path), result = std::move(result)](
            StartupProgress& progress)
        {
            try
            {
                PreviewSource source(*content, progress);
                auto image = data::read_tga(source, path);
                if (progress.cancelled())
                {
                    return;
                }
                std::printf("Screen preview: %s, %d x %d pixels read through VFS.\n",
                    path.c_str(),
                    image.width,
                    image.height);
                *result = std::move(image);
            }
            catch (const PreviewCancelled&)
            {
                // StartupWork observes the cancellation and never marks the step ready.
            }
        }};
}

StartupStep read_game_files(std::shared_ptr<const data::FileSource> content)
{
    if (!content)
    {
        throw std::runtime_error("Game data is not mounted.");
    }
    return {"Loading game files",
        [content = std::move(content)](StartupProgress& progress)
        {
            read_startup_files(*content, progress);
        }};
}

} // namespace beachlife::runtime
