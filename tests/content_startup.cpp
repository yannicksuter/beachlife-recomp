#include "runtime/content_startup.h"
#include "data/content_prepare.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <source_location>

using namespace beachlife;
using namespace std::chrono_literals;

void expect(bool value, const std::source_location& where = std::source_location::current()) {
    if (!value) throw std::runtime_error("VFS startup check failed at line " + std::to_string(where.line()));
}
template<class F> void wait_for(F ready) {
    const auto deadline = std::chrono::steady_clock::now()+10s;
    while (!ready()) { expect(std::chrono::steady_clock::now() < deadline); std::this_thread::sleep_for(1ms); }
}
void settle(runtime::StartupWork& work) {
    wait_for([&] { const auto status = work.status(); return status.ready || status.failed; });
}

// Controlled reads model slow storage, partial reads and damaged files without
// sleeps in production code or dependence on a particular disk/GPU.
struct Probe {
    std::uint64_t declared = 9000, available = 9000;
    std::size_t max_read = std::numeric_limits<std::size_t>::max();
    std::vector<unsigned char> body;
    std::string path = "BeachLife.ini";
    bool block_second = false, fail_second = false, fail_open = false;
    std::atomic<unsigned> opens{0}, reads{0};
    std::atomic_bool entered{false}, released{false}, closed{false};
};
class ProbeFile final : public data::SourceFile {
public:
    explicit ProbeFile(std::shared_ptr<Probe> probe) : probe_(std::move(probe)) {}
    ~ProbeFile() override { probe_->closed = true; }
    std::uint64_t size() const override { return probe_->declared; }
    std::uint64_t tell() const override { return position_; }
    void seek(std::uint64_t position) override { position_ = position; }
    std::size_t read(std::span<unsigned char> output) override {
        const auto count = ++probe_->reads;
        if (count == 2 && probe_->block_second) {
            probe_->entered = true;
            wait_for([&] { return probe_->released.load(); });
        }
        if (count == 2 && probe_->fail_second) throw std::runtime_error("Generated storage read failure");
        const auto bytes = static_cast<std::size_t>(std::min<std::uint64_t>(
            std::min(output.size(), probe_->max_read), probe_->available-position_));
        if (probe_->body.empty()) std::fill_n(output.begin(), bytes, 'x');
        else std::copy_n(probe_->body.begin()+static_cast<std::ptrdiff_t>(position_), bytes, output.begin());
        position_ += bytes;
        return bytes;
    }
private:
    std::shared_ptr<Probe> probe_;
    std::uint64_t position_ = 0;
};
class ProbeSource final : public data::FileSource {
public:
    explicit ProbeSource(std::shared_ptr<Probe> probe) : probe_(std::move(probe)) {}
    std::unique_ptr<data::SourceFile> open(std::string_view name) const override {
        expect(name == probe_->path);
        ++probe_->opens;
        if (probe_->fail_open) throw std::runtime_error("Generated missing file");
        return std::make_unique<ProbeFile>(probe_);
    }
private:
    std::shared_ptr<Probe> probe_;
};

void check_preview(const std::filesystem::path& image, const std::filesystem::path& cache) {
    const std::string path = "Data/Screens/FrontScreen.tga";
    const auto prepared = data::prepare_content(image, cache);
    const auto offline = image.parent_path()/"preview-offline.iso";
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
        expect(result->pixels == std::vector<std::uint32_t>{
            0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff,
            0xffffffff, 0xffff0000, 0xff00ff00, 0xff0000ff});
        std::filesystem::rename(offline, image);
        bool locked = false;
        try { data::prepare_content(image, cache); } catch (const std::runtime_error&) { locked = true; }
        expect(locked);
    }
    expect(weak.expired());

    // Exercise the actual queued prepare -> INI -> image handoff used by the runtime.
    {
        launcher::Settings settings;
        settings.source = launcher::path_utf8(std::filesystem::absolute(image));
        runtime::ContentStartup content(settings, cache, {});
        result = std::make_shared<data::TgaImage>();
        runtime::StartupWork work({content.prepare(), content.read(), content.preview(path, result)});
        settle(work);
        work.rethrow();
        expect(work.status().ready && work.status().step == 3 && result->pixels.size() == 8);
    }
    for (const auto* invalid : {"Data/Screens/missing.tga", "Data/loose.txt"}) {
        result = std::make_shared<data::TgaImage>();
        runtime::StartupWork work({runtime::read_screen_preview(
            std::make_shared<data::PreparedSource>(prepared.root), invalid, result)});
        settle(work);
        expect(work.status().failed && !work.status().ready && result->pixels.empty());
        bool raised = false;
        try { work.rethrow(); } catch (const std::runtime_error&) { raised = true; }
        expect(raised);
    }

    // Pause real decoder input after its header, then cancel. No partial image
    // can reach the renderer, and cancellation must release the open VFS file.
    auto probe = std::make_shared<Probe>();
    std::ifstream input(prepared.root/"files"/path, std::ios::binary);
    probe->body.assign(std::istreambuf_iterator<char>(input), {});
    expect(probe->body.size() > 18);
    probe->declared = probe->available = probe->body.size();
    probe->path = path;
    probe->max_read = 18;
    probe->block_second = true;
    result = std::make_shared<data::TgaImage>();
    auto work = std::make_unique<runtime::StartupWork>(std::vector<runtime::StartupStep>{
        runtime::read_screen_preview(std::make_shared<ProbeSource>(probe), path, result)});
    wait_for([&] { return probe->entered.load(); });
    expect(work->status().label == "Loading image" && work->status().done == 18);
    work->cancel();
    probe->released = true;
    wait_for([&] { return probe->closed.load(); });
    expect(!work->status().ready && !work->status().failed);
    work.reset();
    expect(result->pixels.empty() && probe->reads == 2);
}

void check_setup(const std::filesystem::path& image, const std::filesystem::path& cache) {
    namespace fs = std::filesystem;
    const auto config = cache.parent_path()/"launcher.cfg";
    launcher::Settings settings;
    settings.source = launcher::path_utf8(fs::absolute(image).lexically_normal());
    launcher::save_settings(config, settings); // the launcher's part of Play
    const auto offline = image.parent_path()/"setup-offline.iso";

    // Hold the actual extraction inside the loading step, then cancel it. No
    // saved readiness or later VFS read may escape the cancelled worker.
    {
        std::atomic_bool entered{false}, released{false}, later{false};
        runtime::ContentStartup content(settings, cache, config);
        auto prepare = content.prepare([&](const data::PrepareProgress& update) {
            if (update.phase != "extracting" || update.completed_files != 1) return;
            entered = true;
            wait_for([&] { return released.load(); });
        });
        auto work = std::make_unique<runtime::StartupWork>(std::vector<runtime::StartupStep>{
            std::move(prepare), {"later", [&](runtime::StartupProgress&) { later = true; }}});
        wait_for([&] { return entered.load(); });
        const auto status = work->status();
        expect(status.label == "Preparing game files" && !status.ready && !status.failed);
        expect(status.share().value() >= 0.25f && status.share().value() <= 0.375f);
        expect(launcher::load_settings(config).content.identity.empty());
        work->cancel();
        released = true;
        work.reset();
        expect(!later && launcher::load_settings(config).content.identity.empty());
        for (const auto& entry : fs::directory_iterator(cache))
            if (entry.is_directory()) expect(!fs::exists(entry.path()/"manifest.json"));
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
        root = cache/settings.content.identity;
        expect(fs::exists(root/"manifest.json"));
        bool locked = false;
        try { data::prepare_content(image, cache); } catch (const std::runtime_error&) { locked = true; }
        expect(locked); // the runtime retains its mount after loading finishes
    }

    const auto target = root/"files/Data/loose.txt";
    const auto modified = fs::last_write_time(target);
    const auto config_modified = fs::last_write_time(config);
    for (bool disconnected : {false, true}) {
        if (disconnected) fs::rename(image, offline);
        runtime::ContentStartup content(settings, cache, config);
        runtime::StartupWork work({content.prepare([](const auto&) { expect(false); }), content.read()});
        settle(work);
        work.rethrow();
        expect(work.status().ready);
        expect(fs::last_write_time(target) == modified && fs::last_write_time(config) == config_modified);
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
    { std::fstream changed(target, std::ios::binary|std::ios::in|std::ios::out); changed.put('!'); }
    {
        bool checked = false;
        runtime::ContentStartup content(settings, cache, config, true);
        runtime::StartupWork work({content.prepare([&](const auto& update) { checked |= update.phase == "checking"; })});
        settle(work);
        work.rethrow();
        expect(work.status().ready && checked);
        std::ifstream restored(target, std::ios::binary);
        expect(restored.get() != '!');
    }
    // An inaccessible settings destination only loses the offline memory: the
    // mounted files still start the game.
    {
        const auto bad_config = cache.parent_path()/"config-is-a-directory";
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
    { std::ofstream broken(image, std::ios::binary); broken << "broken ISO"; }
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
void check_late_cancel(const std::filesystem::path& image, const std::filesystem::path& cache) {
    namespace fs = std::filesystem;
    const auto config = cache.parent_path()/"late-cancel.cfg";
    launcher::Settings settings;
    settings.source = launcher::path_utf8(fs::absolute(image).lexically_normal());
    launcher::save_settings(config, settings);
    std::atomic_bool entered{false}, released{false};
    runtime::ContentStartup content(settings, cache, config);
    auto prepare = content.prepare([&](const data::PrepareProgress& update) {
        if (update.phase != "ready") return;
        entered = true;
        wait_for([&] { return released.load(); });
    });
    auto work = std::make_unique<runtime::StartupWork>(std::vector<runtime::StartupStep>{std::move(prepare), content.read()});
    wait_for([&] { return entered.load(); });
    work->cancel();
    released = true;
    work.reset();
    const auto saved = launcher::load_settings(config).content;
    expect(saved.identity.size() == 64 && fs::exists(cache/saved.identity/"manifest.json"));
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try {
        const std::filesystem::path image(argv[1]), cache(argv[2]);
        check_setup(image, cache.parent_path()/"setup-cache");
        check_late_cancel(image, cache.parent_path()/"late-cancel-cache");
        const auto prepared = data::prepare_content(image, cache);
        const auto offline = image.parent_path()/"offline.iso";
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
            expect(status.ready && status.done == 27 && status.total == 27 && status.share() == 1.0f);
            std::filesystem::rename(offline, image);
            bool locked = false;
            try { data::prepare_content(image, cache); } catch (const std::runtime_error&) { locked = true; }
            expect(locked); // the queued work keeps the mount alive until destroyed
        }
        expect(weak.expired());
        expect(data::prepare_content(image, cache).files_reused == 5);
        check_preview(image, cache);

        // Observe real byte progress before the next controlled read completes.
        auto probe = std::make_shared<Probe>();
        probe->block_second = true;
        auto work = std::make_unique<runtime::StartupWork>(std::vector<runtime::StartupStep>{
            runtime::read_game_files(std::make_shared<ProbeSource>(probe))});
        wait_for([&] { return probe->entered.load(); });
        const auto reading = work->status();
        expect(reading.label == "Loading game files" && reading.done == 4096 && reading.total == 9000);
        expect(reading.share().value() > 0.45f && reading.share().value() < 0.46f);
        work->cancel();
        probe->released = true;
        wait_for([&] { return probe->closed.load(); });
        expect(!work->status().ready && !work->status().failed);
        work.reset(); // joins the read; cancellation never completes the step
        expect(probe->reads == 2 && probe->opens == 1);

        for (int scenario = 0; scenario < 6; ++scenario) {
            probe = std::make_shared<Probe>();
            if (scenario == 0) probe->declared = probe->available = 0;
            if (scenario == 1) probe->declared = 1024*1024+1;
            if (scenario == 2) probe->available = 1200;
            if (scenario == 3) probe->fail_second = true;
            if (scenario == 4) probe->fail_open = true;
            runtime::StartupWork check({runtime::read_game_files(std::make_shared<ProbeSource>(probe))});
            settle(check);
            const auto status = check.status();
            if (scenario == 0) expect(status.ready && status.done == 0 && probe->reads == 0);
            else if (scenario == 5) expect(status.ready && status.done == 9000 && probe->reads == 3);
            else {
                expect(status.failed && !status.ready);
                bool raised = false;
                try { check.rethrow(); } catch (const std::runtime_error&) { raised = true; }
                expect(raised);
                if (scenario == 1) expect(probe->reads == 0);
                if (scenario == 2) expect(status.done == 1200);
            }
        }
        std::cout << "Loading-screen preparation, cancellation, resume, saved/offline startup, repair and mount lifetime passed.\n"
                  << "Queued VFS ownership, byte progress, limits and read errors passed.\n"
                  << "Screen preview pixels, offline mount lifetime, errors and cancellation passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
