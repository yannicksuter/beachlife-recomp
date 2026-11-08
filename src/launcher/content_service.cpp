#include "content_service.h"
#include "data/game_disc.h"
#include "data/game_index.h"
#include "platform/path.h"
#include "platform/sha256.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <utility>

namespace beachlife::launcher
{
namespace
{
struct Stamp
{
    std::uint64_t size;
    std::int64_t modified;
    bool operator==(const Stamp&) const = default;
};

// None when the ISO cannot be reached now: removed media, a drive without a disc
// or an unreadable folder are a disconnected ISO, so a remembered set still plays.
std::optional<Stamp> stamp(const std::filesystem::path& source)
{
    std::error_code error;
    const auto status = std::filesystem::status(source, error);
    if (error || !std::filesystem::exists(status))
    {
        return {};
    }
    if (!std::filesystem::is_regular_file(status))
    {
        throw std::runtime_error("The selected ISO is not a file.");
    }
    const auto size = std::filesystem::file_size(source, error);
    if (error)
    {
        return {};
    }
    const auto modified = std::filesystem::last_write_time(source, error);
    if (error)
    {
        return {};
    }
    return Stamp{size, static_cast<std::int64_t>(modified.time_since_epoch().count())};
}

// Whether a prepared set carries the selected 1.1 patch: none for none, else
// the same file. Like the ISO, a patch file that cannot be reached now keeps
// the set it was prepared with.
bool patch_matches(const std::string& selected, const std::string& applied)
{
    if (selected.empty() || applied.empty())
    {
        return selected.empty() == applied.empty();
    }
    std::ifstream file(platform::utf8_path(selected), std::ios::binary);
    if (!file)
    {
        return true;
    }
    platform::Sha256 hash;
    std::array<char, 65536> buffer{};
    while (file.read(buffer.data(), buffer.size()) || file.gcount())
    {
        hash.update(buffer.data(), static_cast<std::size_t>(file.gcount()));
    }
    return hash.finish_hex() == applied;
}
} // namespace

ContentService::ContentService(
    std::filesystem::path cache, std::function<void(const data::PrepareProgress&)> observer)
    : cache_(std::move(cache))
    , observer_(std::move(observer))
    , thread_(
          [this]
          {
              worker();
          })
{
}

ContentService::~ContentService()
{
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
        if (cancellation_)
        {
            *cancellation_ = true;
        }
        pending_.reset();
    }
    changed_.notify_one();
    thread_.join();
}

void ContentService::enqueue(const Settings& settings, bool prepare)
{
    const std::lock_guard lock(mutex_);
    if (cancellation_)
    {
        *cancellation_ = true;
    }
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    selected_ = settings;
    ready_.reset(); // release the shared mount lock before repair starts
    status_ = {};
    status_.state = prepare ? State::Preparing : State::Checking;
    status_.source.selected = !settings.source.empty();
    status_.source.title = "Checking game files";
    pending_ = Job{settings, prepare, ++generation_, cancellation_};
    changed_.notify_one();
}
void ContentService::select(const Settings& settings)
{
    enqueue(settings, false);
}
void ContentService::prepare()
{
    Settings selected;
    {
        const std::lock_guard lock(mutex_);
        selected = selected_;
    }
    enqueue(selected, true);
}
void ContentService::cancel()
{
    const std::lock_guard lock(mutex_);
    if (cancellation_)
    {
        *cancellation_ = true;
    }
    ++generation_;
    pending_.reset();
    ready_.reset();
    status_.state = State::Cancelled;
    status_.binding = {};
    status_.language.clear();
    status_.error.clear();
}
ContentService::Status ContentService::snapshot() const
{
    const std::lock_guard lock(mutex_);
    return status_;
}
void ContentService::publish(
    const Job& job, Status status, std::shared_ptr<const data::PreparedSource> source)
{
    const std::lock_guard lock(mutex_);
    if (stopping_ || job.generation != generation_ || *job.cancelled)
    {
        return;
    }
    status_ = std::move(status);
    ready_ = std::move(source);
}
void ContentService::worker()
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock,
                [&]
                {
                    return stopping_ || pending_.has_value();
                });
            if (stopping_)
            {
                return;
            }
            job = std::move(*pending_);
            pending_.reset();
        }
        try
        {
            run(job);
        }
        catch (const data::PreparationCancelled&)
        {
        } // cancel/select already replaced visible state
        catch (const std::exception& error)
        {
            auto status = snapshot();
            status.state = State::Failed;
            status.source.selected = !job.settings.source.empty();
            status.source.title = "Game files need attention";
            status.source.detail = status.error = error.what();
            publish(job, std::move(status));
        }
    }
}

void ContentService::run(const Job& job)
{
    data::PrepareOptions options;
    options.cancelled = [cancel = job.cancelled]
    {
        return cancel->load();
    };
    options.progress = [&](const auto& progress)
    {
        {
            const std::lock_guard lock(mutex_);
            if (job.generation == generation_ && !*job.cancelled)
            {
                status_.progress = progress;
            }
        }
        if (observer_)
        {
            observer_(progress);
        }
    };
    auto result = open_content(job.settings, cache_, job.prepare, options);
    publish(job, std::move(result.status), std::move(result.source));
}

ContentResult open_content(const Settings& settings,
    const std::filesystem::path& cache,
    bool prepare,
    const data::PrepareOptions& options)
{
    using State = ContentService::State;
    const auto check_cancelled = [&]
    {
        if (options.cancelled && options.cancelled())
        {
            throw data::PreparationCancelled();
        }
    };
    check_cancelled();
    ContentResult result;
    auto& status = result.status;
    status.source = inspect_source(settings);
    if (settings.source.empty())
    {
        return result;
    }
    if (cache.empty())
    {
        throw std::runtime_error("Cannot locate the folder for prepared game files.");
    }
    const auto image =
        std::filesystem::absolute(platform::utf8_path(settings.source)).lexically_normal();
    const auto path = platform::utf8(image);
    const auto before = stamp(image);
    check_cancelled();
    // The disc's layout and languages come from its small installer metadata;
    // no game data is read. The saved language is a preference: a disc that
    // does not offer it plays in its first language.
    std::string layout_error;
    if (before && status.source.readable)
    {
        try
        {
            const data::IsoSource iso(image);
            status.languages = data::GameDisc(iso).map().languages;
        }
        catch (const std::exception& error)
        {
            layout_error = error.what();
        }
    }
    const auto& offered = status.languages;
    const std::string language =
        std::find(offered.begin(), offered.end(), settings.language) != offered.end()
            ? settings.language
        : offered.empty() ? std::string{}
                          : offered.front();
    check_cancelled();

    // After publication the prepared set is committed: a late cancel still returns
    // it, so the caller can remember it instead of preparing it again.
    auto mount = [&](const ContentBinding& binding, bool committed = false)
    {
        auto source = std::make_shared<data::PreparedSource>(cache / binding.identity);
        if (!committed)
        {
            check_cancelled();
        }
        status.state = State::Ready;
        status.binding = binding;
        status.language = source->manifest().language;
        status.files = source->manifest().files.size();
        status.bytes = source->manifest().total_bytes;
        status.source.title = before ? "ISO linked to prepared files" : "Playing from local files";
        status.source.detail =
            before ? "Game data has been prepared from this image."
                   : "The original ISO is disconnected. Your prepared game files are ready.";
        status.error.clear();
        result.source = std::move(source);
    };
    if (!prepare)
    {
        const auto& binding = settings.content;
        // An absent source is an offline launch, not permission to use an old
        // cache after selecting another path or replacing the ISO in place.
        if (!binding.identity.empty() && binding.identity.size() == 64 &&
            binding.identity.find_first_not_of("0123456789abcdef") == std::string::npos &&
            binding.source == path && (!before || *before == Stamp{binding.size, binding.modified}))
        {
            const auto inspected = status.source;
            try
            {
                mount(binding);
                // A set prepared for another language or patch is not this selection.
                if ((language.empty() || status.language == language) &&
                    patch_matches(settings.patch, result.source->manifest().patch_sha256))
                {
                    return result;
                }
                result.source.reset();
                ContentService::Status unbound;
                unbound.source = inspected;
                unbound.languages = std::move(status.languages);
                status = std::move(unbound);
            }
            catch (const data::PreparationCancelled&)
            {
                throw;
            }
            catch (const std::exception& error)
            {
                status.error = "Prepared game data needs repair: " + std::string(error.what());
            }
        }
        status.state = status.source.readable && layout_error.empty() ? State::NeedsPreparation
                                                                      : State::Failed;
        if (status.state == State::Failed && status.error.empty())
        {
            status.error = layout_error.empty() ? status.source.detail : layout_error;
        }
        return result;
    }
    if (!before || !status.source.readable)
    {
        throw std::runtime_error("Preparation needs the original ISO. " + status.source.detail);
    }
    if (!layout_error.empty())
    {
        throw std::runtime_error(layout_error);
    }
    auto selected = options;
    selected.language = language;
    selected.patch = platform::utf8_path(settings.patch);
    const auto prepared = data::prepare_content(image, cache, selected);
    if (stamp(image) != before)
    {
        throw std::runtime_error("The ISO changed during setup. Select it again to prepare it.");
    }
    mount({prepared.manifest.identity(), path, before->size, before->modified}, true);
    return result;
}
} // namespace beachlife::launcher
