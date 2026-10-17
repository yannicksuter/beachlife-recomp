#include "runtime/content_startup.h"
#include "launcher/content_service.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace beachlife::runtime {

struct ContentStartup::State {
    launcher::Settings settings;
    std::filesystem::path cache, config;
    bool repair;
    std::shared_ptr<const data::PreparedSource> content;
};

ContentStartup::ContentStartup(launcher::Settings settings, std::filesystem::path cache,
                             std::filesystem::path config, bool repair)
    : state_(std::make_shared<State>(State{std::move(settings), std::move(cache), std::move(config), repair, {}})) {}

StartupStep ContentStartup::prepare(std::function<void(const data::PrepareProgress&)> observer) const {
    return {"Checking game files", [state = state_, observer = std::move(observer)](StartupProgress& progress) {
        data::PrepareOptions options;
        options.cancelled = [&] { return progress.cancelled(); };
        options.progress = [&](const data::PrepareProgress& update) {
            // Each preparation phase has its own byte total. Map those measured
            // counts into successive quarters, so changing phases cannot reset it.
            constexpr std::uint64_t units = 1000000;
            std::uint64_t phase = 0;
            const char* label = "Reading disc image";
            if (update.phase == "checking") { phase = 1; label = "Checking local files"; }
            else if (update.phase == "extracting") { phase = 2; label = "Preparing game files"; }
            else if (update.phase == "confirming") { phase = 3; label = "Verifying disc image"; }
            else if (update.phase == "ready") { phase = 4; label = "Opening game files"; }
            const auto within = phase == 4 || !update.total_bytes ? 0 :
                static_cast<std::uint64_t>(static_cast<long double>(std::min(update.completed_bytes, update.total_bytes)) /
                                           static_cast<long double>(update.total_bytes) * units);
            progress.report(label, phase*units + within, 4*units);
            if (observer) observer(update);
        };
        try {
            auto result = launcher::open_content(state->settings, state->cache, state->repair, options);
            if (result.status.state == launcher::ContentService::State::NeedsPreparation)
                result = launcher::open_content(state->settings, state->cache, true, options);
            if (progress.cancelled()) return;
            if (!result.source)
                throw std::runtime_error(result.status.error.empty() ? "Choose a readable Beach Life ISO to prepare your game files."
                                                                    : result.status.error);
            auto settings = state->settings;
            settings.content = result.status.binding;
            if (settings != state->settings && !state->config.empty()) launcher::save_settings(state->config, settings);
            state->settings = std::move(settings);
            state->content = std::move(result.source);
            std::printf("Game data: %zu files mounted, language %s.\n", state->content->manifest().files.size(),
                        state->content->manifest().language.c_str());
        } catch (const data::PreparationCancelled&) {
            if (!progress.cancelled()) throw;
        }
    }};
}

StartupStep ContentStartup::read() const {
    return {"Loading game files", [state = state_](StartupProgress& progress) {
        read_game_files(state->content).run(progress);
    }};
}

StartupStep read_game_files(std::shared_ptr<const data::FileSource> content) {
    if (!content) throw std::runtime_error("Game data is not mounted.");
    return {"Loading game files", [content = std::move(content)](StartupProgress& progress) {
        if (progress.cancelled()) return;
        auto ini = content->open("BeachLife.ini");
        const auto size = ini->size();
        if (size > 1024*1024) throw std::runtime_error("BeachLife.ini exceeds the startup read limit.");
        progress.total(size);
        std::array<unsigned char, 4096> buffer{};
        std::uint64_t read = 0;
        while (read < size) {
            if (progress.cancelled()) return;
            const auto remaining = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), size-read));
            const auto count = ini->read(std::span(buffer).first(remaining));
            if (progress.cancelled()) return;
            if (!count || count > remaining) throw std::runtime_error("Cannot read BeachLife.ini from prepared game data.");
            read += count;
            progress.advance(count);
        }
        if (progress.cancelled()) return;
        std::printf("Game data: BeachLife.ini %llu bytes read through VFS.\n", static_cast<unsigned long long>(read));
    }};
}

} // namespace beachlife::runtime
