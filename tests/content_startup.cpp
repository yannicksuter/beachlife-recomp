#include "runtime/content_startup.h"
#include "data/content_prepare.h"
#include "runtime/retail_files.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <source_location>

using namespace beachlife;
using namespace std::chrono_literals;
namespace files = runtime::retail_files;
using BridgeFile = std::unique_ptr<files::File, decltype(&files::close)>;

void expect(bool value, const std::source_location& where = std::source_location::current())
{
    if (!value)
    {
        throw std::runtime_error(
            "VFS startup check failed at line " + std::to_string(where.line()));
    }
}
template <class F> void wait_for(F ready)
{
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!ready())
    {
        expect(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(1ms);
    }
}
void settle(runtime::StartupWork& work)
{
    wait_for(
        [&]
        {
            const auto status = work.status();
            return status.ready || status.failed;
        });
}

// Controlled reads model slow storage, partial reads and damaged files without
// sleeps in production code or dependence on a particular disk/GPU.
struct Probe
{
    std::uint64_t declared = 9000, available = 9000;
    std::size_t max_read = std::numeric_limits<std::size_t>::max();
    std::vector<unsigned char> body;
    std::string path = "BeachLife.ini";
    bool block_second = false, fail_second = false, fail_open = false;
    std::atomic<unsigned> opens{0}, reads{0};
    std::atomic_bool entered{false}, released{false}, closed{false};
};
class ProbeFile final : public data::SourceFile
{
  public:
    explicit ProbeFile(std::shared_ptr<Probe> probe)
        : probe_(std::move(probe))
    {
    }
    ~ProbeFile() override
    {
        probe_->closed = true;
    }
    std::uint64_t size() const override
    {
        return probe_->declared;
    }
    std::uint64_t tell() const override
    {
        return position_;
    }
    void seek(std::uint64_t position) override
    {
        position_ = position;
    }
    std::size_t read(std::span<unsigned char> output) override
    {
        const auto count = ++probe_->reads;
        if (count == 2 && probe_->block_second)
        {
            probe_->entered = true;
            wait_for(
                [&]
                {
                    return probe_->released.load();
                });
        }
        if (count == 2 && probe_->fail_second)
        {
            throw std::runtime_error("Generated storage read failure");
        }
        const auto bytes = static_cast<std::size_t>(std::min<std::uint64_t>(
            std::min(output.size(), probe_->max_read), probe_->available - position_));
        if (probe_->body.empty())
        {
            std::fill_n(output.begin(), bytes, 'x');
        }
        else
        {
            std::copy_n(probe_->body.begin() + static_cast<std::ptrdiff_t>(position_),
                bytes,
                output.begin());
        }
        position_ += bytes;
        return bytes;
    }

  private:
    std::shared_ptr<Probe> probe_;
    std::uint64_t position_ = 0;
};
class ProbeSource final : public data::FileSource
{
  public:
    explicit ProbeSource(std::shared_ptr<Probe> probe)
        : probe_(std::move(probe))
    {
    }
    std::unique_ptr<data::SourceFile> open(std::string_view name) const override
    {
        expect(name == probe_->path);
        ++probe_->opens;
        if (probe_->fail_open)
        {
            throw std::runtime_error("Generated missing file");
        }
        return std::make_unique<ProbeFile>(probe_);
    }

  private:
    std::shared_ptr<Probe> probe_;
};

// The worker stops at an explicit point on either side of the real handoff.
// Deadlines only bound a broken fixture; no outcome depends on elapsed time.
class HandoffGate
{
  public:
    void pause()
    {
        std::unique_lock lock(mutex_);
        entered_ = true;
        changed_.notify_all();
        expect(changed_.wait_for(lock,
            10s,
            [&]
            {
                return released_;
            }));
    }
    void wait()
    {
        std::unique_lock lock(mutex_);
        expect(changed_.wait_for(lock,
            10s,
            [&]
            {
                return entered_;
            }));
    }
    void release()
    {
        std::lock_guard lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }

  private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool entered_ = false, released_ = false;
};

void reject_handoff(const runtime::ContentStartup& content,
    const std::shared_ptr<runtime::ContentStartup::Result>& result)
{
    bool rejected = false;
    try
    {
        content.handoff(result);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    expect(rejected);
}

void check_bytes(data::SourceFile& file, std::string_view expected)
{
    expect(file.size() == expected.size());
    std::string actual(expected.size(), '\0');
    std::size_t done = 0;
    while (done < actual.size())
    {
        const auto count = file.read(std::span(
            reinterpret_cast<unsigned char*>(actual.data()) + done, actual.size() - done));
        expect(count && count <= actual.size() - done);
        done += count;
    }
    expect(actual == expected && file.tell() == expected.size());
    unsigned char extra = 0;
    expect(file.read(std::span(&extra, 1)) == 0);
}

void check_repair_locked(const std::filesystem::path& image, const std::filesystem::path& cache)
{
    bool locked = false;
    try
    {
        data::prepare_content(image, cache);
    }
    catch (const std::runtime_error&)
    {
        locked = true;
    }
    expect(locked);
}

void check_bridge_bytes(files::File* file, std::string_view expected)
{
    expect(file != nullptr);
    files::Position position = 0;
    expect(files::seek(file, 0, files::SeekOrigin::end) == 0);
    expect(files::position(file, &position) == 0 && position == expected.size());
    expect(files::seek(file, 0, files::SeekOrigin::begin) == 0);
    std::string actual(expected.size(), '\0');
    expect(files::read(actual.data(), 1, actual.size(), file) == actual.size());
    expect(actual == expected);
    expect(files::position(file, &position) == 0 && position == expected.size());
    unsigned char extra = 0;
    expect(files::read(&extra, 1, 1, file) == 0);
}

void check_bound_bytes(const char* path, std::string_view expected)
{
    BridgeFile file(files::open(path, "rb"), files::close);
    check_bridge_bytes(file.get(), expected);
}

void check_handoff(
    const std::filesystem::path& image, const std::filesystem::path& cache, bool nested)
{
    namespace fs = std::filesystem;
    using Result = runtime::ContentStartup::Result;
    const auto caller = std::this_thread::get_id();
    const auto config = cache.parent_path() / "handoff.cfg";
    const auto offline = image.parent_path() / "handoff-offline.iso";
    launcher::Settings settings;
    settings.source = launcher::path_utf8(fs::absolute(image).lexically_normal());
    expect(files::open("BeachLife.ini", "rb") == nullptr);
    auto previous_probe = std::make_shared<Probe>();
    previous_probe->body = {'p', 'r', 'i', 'o', 'r'};
    previous_probe->declared = previous_probe->available = previous_probe->body.size();
    std::optional<files::Session> previous;
    if (nested)
    {
        previous.emplace(std::make_shared<ProbeSource>(previous_probe));
    }
    const auto check_previous = [&]
    {
        if (nested)
        {
            check_bound_bytes("BeachLife.ini", "prior");
        }
        else
        {
            expect(files::open("BeachLife.ini", "rb") == nullptr);
        }
    };
    check_previous();

    // Rejected destinations do not start work. Queuing consumes a fresh result
    // even if its eventual work fails, so it cannot carry a stale prior source.
    {
        runtime::ContentStartup content(settings, cache, {});
        reject_handoff(content, {});
        auto result = std::make_shared<Result>();
        result->source = std::make_shared<ProbeSource>(std::make_shared<Probe>());
        const auto supplied = result->source;
        reject_handoff(content, result);
        expect(result->source == supplied);
        result = std::make_shared<Result>();
        auto step = content.handoff(result);
        reject_handoff(content, result);
        {
            runtime::StartupWork work({std::move(step)});
            settle(work);
            expect(work.status().failed && !work.status().ready && work.status().step == 0);
            bool raised = false;
            try
            {
                work.rethrow();
            }
            catch (const std::runtime_error& error)
            {
                raised = std::string_view(error.what()) == "Game data is not mounted.";
            }
            expect(raised);
        }
        expect(!result->source);
        reject_handoff(content, result);
    }
    check_previous();

    const auto complete = [&](bool cached)
    {
        runtime::ContentStartup content(settings, cache, config);
        auto result = std::make_shared<Result>();
        auto step = content.handoff(result);
        std::thread::id worker;
        bool prepared = false;
        {
            runtime::StartupWork work({content.prepare(
                                           [&](const auto&)
                                           {
                                               prepared = true;
                                           }),
                content.read(),
                {step.label,
                    [run = std::move(step.run), &worker](runtime::StartupProgress& progress)
                    {
                        worker = std::this_thread::get_id();
                        expect(files::open("BeachLife.ini", "rb") == nullptr);
                        run(progress);
                        expect(files::open("BeachLife.ini", "rb") == nullptr);
                    }}});
            settle(work);
            work.rethrow();
            expect(work.status().ready && work.status().step == 3);
            check_previous(); // worker publication never changes the caller's binding
        } // Join before accessing the worker's result or observation fields.
        expect(worker != caller && worker != std::thread::id{});
        expect(prepared != cached);
        auto source = std::move(result->source);
        expect(source != nullptr);
        reject_handoff(content, result); // still single-use after adoption emptied it
        return source;
    };

    auto source = complete(false);
    std::weak_ptr<const data::FileSource> weak = source;
    // Only the adopted source remains: ContentStartup, its work and result died.
    expect(!weak.expired() && std::this_thread::get_id() == caller);
    {
        auto file = source->open("BeachLife.ini");
        check_bytes(*file, "[Fixture]\nLanguage=English\n");
    }
    check_repair_locked(image, cache);
    auto stream = source->open("Data/loose.txt");
    source.reset();
    expect(weak.expired());
    check_repair_locked(image, cache); // an open PreparedFile independently retains the lock
    check_bytes(*stream, std::string_view("loose file\0\xff", 12));
    stream.reset();
    expect(data::prepare_content(image, cache).files_reused == 6);
    settings = launcher::load_settings(config);
    expect(!settings.content.identity.empty());

    // Adopt only a joined, successful result. The binding retains its source;
    // an open bridge handle then retains both source and lock after the binding.
    source = complete(true);
    weak = source;
    BridgeFile retained(nullptr, files::close);
    {
        files::Session game_files(source);
        source.reset();
        expect(!weak.expired());
        check_bound_bytes("BeachLife.ini", "[Fixture]\nLanguage=English\n");
        retained.reset(files::open("Data/loose.txt", "rb"));
        expect(retained != nullptr);
        check_repair_locked(image, cache);
    }
    check_previous();
    expect(!weak.expired());
    check_repair_locked(image, cache);
    check_bridge_bytes(retained.get(), std::string_view("loose file\0\xff", 12));
    retained.reset();
    expect(weak.expired());
    expect(data::prepare_content(image, cache).files_reused == 6);

    // Repeated warm and disconnected sessions use fresh results and the saved
    // mount. Each survives the startup owner, and each releases its last lock.
    for (bool disconnected : {false, true})
    {
        if (disconnected)
        {
            fs::rename(image, offline);
        }
        source = complete(true);
        weak = source;
        {
            files::Session game_files(source);
            source.reset();
            expect(!weak.expired());
            check_bound_bytes("BeachLife.ini", "[Fixture]\nLanguage=English\n");
        }
        expect(weak.expired());
        check_previous();
        if (disconnected)
        {
            fs::rename(offline, image);
        }
        expect(data::prepare_content(image, cache).files_reused == 6);
    }

    // A required step failing before the handoff cannot publish its source.
    {
        runtime::ContentStartup content(settings, cache, config);
        auto result = std::make_shared<Result>();
        {
            runtime::StartupWork work({content.prepare(),
                content.read(),
                {"generated failure",
                    [](runtime::StartupProgress&)
                    {
                        throw std::runtime_error("Required startup step failed.");
                    }},
                content.handoff(result)});
            settle(work);
            expect(work.status().failed && !work.status().ready && work.status().step == 2);
            bool raised = false;
            try
            {
                work.rethrow();
            }
            catch (const std::runtime_error& error)
            {
                raised = std::string_view(error.what()) == "Required startup step failed.";
            }
            expect(raised);
        }
        expect(!result->source);
        reject_handoff(content, result);
    }
    check_previous();
    expect(data::prepare_content(image, cache).files_reused == 6);

    for (bool staged : {false, true})
    {
        auto result = std::make_shared<Result>();
        {
            runtime::ContentStartup content(settings, cache, config);
            auto step = content.handoff(result);
            HandoffGate gate;
            auto work = std::make_unique<runtime::StartupWork>(std::vector<runtime::StartupStep>{
                content.prepare(),
                content.read(),
                {step.label,
                    [run = std::move(step.run), &gate, staged](runtime::StartupProgress& progress)
                    {
                        expect(files::open("BeachLife.ini", "rb") == nullptr);
                        if (staged)
                        {
                            run(progress);
                        }
                        gate.pause();
                        if (!staged)
                        {
                            run(progress);
                        }
                        expect(files::open("BeachLife.ini", "rb") == nullptr);
                    }}});
            gate.wait();
            const auto status = work->status();
            expect(status.step == 2 && !status.ready && !status.failed);
            check_previous();
            work->cancel();
            gate.release();
            work.reset(); // join before reading or discarding the staged result
            expect(static_cast<bool>(result->source) == staged);
            reject_handoff(content, result);
            weak = result->source;
        }
        if (staged)
        {
            // Pointer presence is not successful completion: leave it staged,
            // never construct a consumer, and discard it after the joined cancel.
            expect(!weak.expired());
            check_repair_locked(image, cache);
        }
        result.reset();
        expect(weak.expired());
        expect(data::prepare_content(image, cache).files_reused == 6);
        expect(launcher::load_settings(config) == settings);
        check_previous();
    }

    // Leaving after work completes still discards the result without binding.
    source = complete(true);
    check_previous();
    source.reset();
    expect(data::prepare_content(image, cache).files_reused == 6);

    // A later game-thread exception tears down its binding and retained source.
    bool raised = false;
    try
    {
        auto adopted = complete(true);
        weak = adopted;
        files::Session game_files(std::move(adopted));
        check_bound_bytes("BeachLife.ini", "[Fixture]\nLanguage=English\n");
        throw std::runtime_error("Generated game-thread failure");
    }
    catch (const std::runtime_error& error)
    {
        raised = std::string_view(error.what()) == "Generated game-thread failure";
    }
    expect(raised && weak.expired());
    check_previous();
    source = complete(true); // cancellation/errors cannot poison the next Play
    source.reset();
    expect(data::prepare_content(image, cache).files_reused == 6);
    check_previous();
    previous.reset();
    expect(files::open("BeachLife.ini", "rb") == nullptr);
}

void check_preview(const std::filesystem::path& image, const std::filesystem::path& cache)
{
    const std::string path = "Data/Screens/FrontScreen.tga";
    const auto prepared = data::prepare_content(image, cache);
    const auto offline = image.parent_path() / "preview-offline.iso";
    std::filesystem::rename(image, offline);
    auto mount = std::make_shared<data::PreparedSource>(prepared.root);
    std::weak_ptr<const data::FileSource> weak = mount;
    auto result = std::make_shared<data::TgaImage>();
    std::vector<runtime::StartupStep> steps{runtime::read_screen_preview(mount, path, result)};
    mount.reset();
    expect(!weak.expired());
    {
        runtime::StartupWork work(std::move(steps));
        settle(work);
        work.rethrow();
        expect(work.status().ready && work.status().done == work.status().total);
        expect(result->width == 4 && result->height == 2);
        expect(result->pixels == std::vector<std::uint32_t>{0xff0000ff,
                                     0xff00ff00,
                                     0xffff0000,
                                     0xffffffff,
                                     0xffffffff,
                                     0xffff0000,
                                     0xff00ff00,
                                     0xff0000ff});
        std::filesystem::rename(offline, image);
        bool locked = false;
        try
        {
            data::prepare_content(image, cache);
        }
        catch (const std::runtime_error&)
        {
            locked = true;
        }
        expect(locked);
    }
    expect(weak.expired());

    // Exercise the actual queued prepare -> INI -> image handoff used by the runtime.
    {
        launcher::Settings settings;
        settings.source = launcher::path_utf8(std::filesystem::absolute(image));
        runtime::ContentStartup content(settings, cache, {});
        result = std::make_shared<data::TgaImage>();
        runtime::StartupWork work(
            {content.prepare(), content.read(), content.preview(path, result)});
        settle(work);
        work.rethrow();
        expect(work.status().ready && work.status().step == 3 && result->pixels.size() == 8);
    }
    for (const auto* invalid : {"Data/Screens/missing.tga", "Data/loose.txt"})
    {
        result = std::make_shared<data::TgaImage>();
        runtime::StartupWork work({runtime::read_screen_preview(
            std::make_shared<data::PreparedSource>(prepared.root), invalid, result)});
        settle(work);
        expect(work.status().failed && !work.status().ready && result->pixels.empty());
        bool raised = false;
        try
        {
            work.rethrow();
        }
        catch (const std::runtime_error&)
        {
            raised = true;
        }
        expect(raised);
    }

    // Pause real decoder input after its header, then cancel. No partial image
    // can reach the renderer, and cancellation must release the open VFS file.
    auto probe = std::make_shared<Probe>();
    std::ifstream input(prepared.root / "files" / path, std::ios::binary);
    probe->body.assign(std::istreambuf_iterator<char>(input), {});
    expect(probe->body.size() > 18);
    probe->declared = probe->available = probe->body.size();
    probe->path = path;
    probe->max_read = 18;
    probe->block_second = true;
    result = std::make_shared<data::TgaImage>();
    auto work = std::make_unique<runtime::StartupWork>(std::vector<runtime::StartupStep>{
        runtime::read_screen_preview(std::make_shared<ProbeSource>(probe), path, result)});
    wait_for(
        [&]
        {
            return probe->entered.load();
        });
    expect(work->status().label == "Loading image" && work->status().done == 18);
    work->cancel();
    probe->released = true;
    wait_for(
        [&]
        {
            return probe->closed.load();
        });
    expect(!work->status().ready && !work->status().failed);
    work.reset();
    expect(result->pixels.empty() && probe->reads == 2);
}

void check_setup(const std::filesystem::path& image, const std::filesystem::path& cache)
{
    namespace fs = std::filesystem;
    const auto config = cache.parent_path() / "launcher.cfg";
    launcher::Settings settings;
    settings.source = launcher::path_utf8(fs::absolute(image).lexically_normal());
    launcher::save_settings(config, settings); // the launcher's part of Play
    const auto offline = image.parent_path() / "setup-offline.iso";

    // Hold the actual extraction inside the loading step, then cancel it. No
    // saved readiness or later VFS read may escape the cancelled worker.
    {
        std::atomic_bool entered{false}, released{false}, later{false};
        runtime::ContentStartup content(settings, cache, config);
        auto prepare = content.prepare(
            [&](const data::PrepareProgress& update)
            {
                if (update.phase != "extracting" || update.completed_files != 1)
                {
                    return;
                }
                entered = true;
                wait_for(
                    [&]
                    {
                        return released.load();
                    });
            });
        auto work = std::make_unique<runtime::StartupWork>(
            std::vector<runtime::StartupStep>{std::move(prepare),
                {"later",
                    [&](runtime::StartupProgress&)
                    {
                        later = true;
                    }}});
        wait_for(
            [&]
            {
                return entered.load();
            });
        const auto status = work->status();
        expect(status.label == "Preparing game files" && !status.ready && !status.failed);
        expect(status.share().value() >= 0.25f && status.share().value() <= 0.375f);
        expect(launcher::load_settings(config).content.identity.empty());
        work->cancel();
        released = true;
        work.reset();
        expect(!later && launcher::load_settings(config).content.identity.empty());
        for (const auto& entry : fs::directory_iterator(cache))
        {
            if (entry.is_directory())
            {
                expect(!fs::exists(entry.path() / "manifest.json"));
            }
        }
    }

    // Resume all the way through saving the binding and reading from the VFS.
    fs::path root;
    {
        runtime::ContentStartup content(settings, cache, config);
        {
            runtime::StartupWork work({content.prepare(), content.read()});
            settle(work);
            work.rethrow();
            expect(work.status().ready && work.status().done == 27);
        }
        settings = launcher::load_settings(config);
        expect(!settings.content.identity.empty());
        root = cache / settings.content.identity;
        expect(fs::exists(root / "manifest.json"));
        bool locked = false;
        try
        {
            data::prepare_content(image, cache);
        }
        catch (const std::runtime_error&)
        {
            locked = true;
        }
        expect(locked); // the runtime retains its mount after loading finishes
    }

    const auto target = root / "files/Data/loose.txt";
    const auto modified = fs::last_write_time(target);
    const auto config_modified = fs::last_write_time(config);
    for (bool disconnected : {false, true})
    {
        if (disconnected)
        {
            fs::rename(image, offline);
        }
        runtime::ContentStartup content(settings, cache, config);
        runtime::StartupWork work({content.prepare(
                                       [](const auto&)
                                       {
                                           expect(false);
                                       }),
            content.read()});
        settle(work);
        work.rethrow();
        expect(work.status().ready);
        expect(fs::last_write_time(target) == modified &&
               fs::last_write_time(config) == config_modified);
    }
    fs::remove(target);
    {
        runtime::ContentStartup content(settings, cache, config);
        runtime::StartupWork work({content.prepare(), content.read()});
        settle(work);
        expect(work.status().failed && !work.status().ready);
        expect(launcher::load_settings(config) == settings);
    }
    fs::rename(offline, image);
    {
        runtime::ContentStartup content(settings, cache, config);
        runtime::StartupWork work({content.prepare(), content.read()});
        settle(work);
        work.rethrow();
        expect(work.status().ready && fs::is_regular_file(target));
    }
    // Explicit repair must verify even when the stamp and file sizes match.
    {
        std::fstream changed(target, std::ios::binary | std::ios::in | std::ios::out);
        changed.put('!');
    }
    {
        bool checked = false;
        runtime::ContentStartup content(settings, cache, config, true);
        runtime::StartupWork work({content.prepare(
            [&](const auto& update)
            {
                checked |= update.phase == "checking";
            })});
        settle(work);
        work.rethrow();
        expect(work.status().ready && checked);
        std::ifstream restored(target, std::ios::binary);
        expect(restored.get() != '!');
    }
    // An inaccessible settings destination only loses the offline memory: the
    // mounted files still start the game.
    {
        const auto bad_config = cache.parent_path() / "config-is-a-directory";
        fs::create_directory(bad_config);
        auto unbound = settings;
        unbound.content = {};
        runtime::ContentStartup content(unbound, cache, bad_config);
        runtime::StartupWork work({content.prepare(), content.read()});
        settle(work);
        expect(work.status().ready && !work.status().failed && fs::is_directory(bad_config));
    }
    // Replacing the selected source never inherits the old offline binding.
    fs::rename(image, offline);
    {
        std::ofstream broken(image, std::ios::binary);
        broken << "broken ISO";
    }
    {
        runtime::ContentStartup content(settings, cache, config);
        runtime::StartupWork work({content.prepare(), content.read()});
        settle(work);
        expect(work.status().failed && !work.status().ready);
        expect(launcher::load_settings(config) == settings);
    }
    fs::remove(image);
    fs::rename(offline, image);
}

// A cancel that arrives just after publication keeps the prepared set: its binding
// is remembered, so the next Play does not prepare (and hash) the files again.
void check_late_cancel(const std::filesystem::path& image, const std::filesystem::path& cache)
{
    namespace fs = std::filesystem;
    const auto config = cache.parent_path() / "late-cancel.cfg";
    launcher::Settings settings;
    settings.source = launcher::path_utf8(fs::absolute(image).lexically_normal());
    launcher::save_settings(config, settings);
    std::atomic_bool entered{false}, released{false};
    runtime::ContentStartup content(settings, cache, config);
    auto prepare = content.prepare(
        [&](const data::PrepareProgress& update)
        {
            if (update.phase != "ready")
            {
                return;
            }
            entered = true;
            wait_for(
                [&]
                {
                    return released.load();
                });
        });
    auto work = std::make_unique<runtime::StartupWork>(
        std::vector<runtime::StartupStep>{std::move(prepare), content.read()});
    wait_for(
        [&]
        {
            return entered.load();
        });
    work->cancel();
    released = true;
    work.reset();
    const auto saved = launcher::load_settings(config).content;
    expect(saved.identity.size() == 64 && fs::exists(cache / saved.identity / "manifest.json"));
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        return 2;
    }
    try
    {
        const std::filesystem::path image(argv[1]), cache(argv[2]);
        check_setup(image, cache.parent_path() / "setup-cache");
        check_late_cancel(image, cache.parent_path() / "late-cancel-cache");
        expect(files::open("BeachLife.ini", "rb") == nullptr);
        check_handoff(image, cache.parent_path() / "handoff-cache", false);
        check_handoff(image, cache.parent_path() / "nested-handoff-cache", true);
        const auto prepared = data::prepare_content(image, cache);
        const auto offline = image.parent_path() / "offline.iso";
        std::filesystem::rename(image, offline);
        auto mount = std::make_shared<data::PreparedSource>(prepared.root);
        std::weak_ptr<const data::FileSource> weak = mount;
        std::vector<runtime::StartupStep> steps{runtime::read_game_files(mount)};
        mount.reset(); // the queued step owns the mount, even before the worker runs
        expect(!weak.expired());
        {
            runtime::StartupWork work(std::move(steps));
            settle(work);
            work.rethrow();
            const auto status = work.status();
            expect(
                status.ready && status.done == 27 && status.total == 27 && status.share() == 1.0f);
            std::filesystem::rename(offline, image);
            bool locked = false;
            try
            {
                data::prepare_content(image, cache);
            }
            catch (const std::runtime_error&)
            {
                locked = true;
            }
            expect(locked); // the queued work keeps the mount alive until destroyed
        }
        expect(weak.expired());
        expect(data::prepare_content(image, cache).files_reused == 6);
        check_preview(image, cache);
        expect(files::open("BeachLife.ini", "rb") == nullptr);

        // Observe real byte progress before the next controlled read completes.
        auto probe = std::make_shared<Probe>();
        probe->block_second = true;
        auto work = std::make_unique<runtime::StartupWork>(std::vector<runtime::StartupStep>{
            runtime::read_game_files(std::make_shared<ProbeSource>(probe))});
        wait_for(
            [&]
            {
                return probe->entered.load();
            });
        const auto reading = work->status();
        expect(
            reading.label == "Loading game files" && reading.done == 4096 && reading.total == 9000);
        expect(reading.share().value() > 0.45f && reading.share().value() < 0.46f);
        work->cancel();
        probe->released = true;
        wait_for(
            [&]
            {
                return probe->closed.load();
            });
        expect(!work->status().ready && !work->status().failed);
        work.reset(); // joins the read; cancellation never completes the step
        expect(probe->reads == 2 && probe->opens == 1);

        for (int scenario = 0; scenario < 6; ++scenario)
        {
            probe = std::make_shared<Probe>();
            if (scenario == 0)
            {
                probe->declared = probe->available = 0;
            }
            if (scenario == 1)
            {
                probe->declared = 1024 * 1024 + 1;
            }
            if (scenario == 2)
            {
                probe->available = 1200;
            }
            if (scenario == 3)
            {
                probe->fail_second = true;
            }
            if (scenario == 4)
            {
                probe->fail_open = true;
            }
            runtime::StartupWork check(
                {runtime::read_game_files(std::make_shared<ProbeSource>(probe))});
            settle(check);
            const auto status = check.status();
            if (scenario == 0)
            {
                expect(status.ready && status.done == 0 && probe->reads == 0);
            }
            else if (scenario == 5)
            {
                expect(status.ready && status.done == 9000 && probe->reads == 3);
            }
            else
            {
                expect(status.failed && !status.ready);
                bool raised = false;
                try
                {
                    check.rethrow();
                }
                catch (const std::runtime_error&)
                {
                    raised = true;
                }
                expect(raised);
                if (scenario == 1)
                {
                    expect(probe->reads == 0);
                }
                if (scenario == 2)
                {
                    expect(status.done == 1200);
                }
            }
        }
        std::cout
            << "Loading-screen preparation, cancellation, resume, saved/offline startup, repair "
               "and mount lifetime passed.\n"
            << "Queued VFS ownership, byte progress, limits and read errors passed.\n"
            << "Completed VFS handoff, single-use results, joined cancellation, offline sessions "
               "and source/stream lock ownership passed.\n"
            << "Caller-thread VFS binding, worker isolation, nested restoration, bridge reads "
               "and retained-handle teardown passed.\n"
            << "Screen preview pixels, offline mount lifetime, errors and cancellation passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
