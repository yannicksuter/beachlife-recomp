#include "launcher/content_service.h"

#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <source_location>

using namespace beachlife::launcher;
namespace fs = std::filesystem;
using State = ContentService::State;
using namespace std::chrono_literals;

void expect(bool value, const std::source_location& where = std::source_location::current())
{
    if (!value)
    {
        throw std::runtime_error(
            "Launcher content check failed at line " + std::to_string(where.line()));
    }
}
ContentService::Status settled(ContentService& service)
{
    const auto until = std::chrono::steady_clock::now() + 15s;
    while (service.snapshot().busy() && std::chrono::steady_clock::now() < until)
    {
        std::this_thread::sleep_for(1ms);
    }
    auto status = service.snapshot();
    expect(!status.busy());
    return status;
}

// Pause inside real extraction so cancellation/source changes do not depend on
// scheduling luck or the speed of the filesystem.
struct Gate
{
    std::mutex mutex;
    std::condition_variable changed;
    bool armed = false, entered = false;
    void arm()
    {
        const std::lock_guard lock(mutex);
        armed = true;
        entered = false;
    }
    void observe(const beachlife::data::PrepareProgress& progress)
    {
        std::unique_lock lock(mutex);
        if (!armed || progress.phase != "extracting" || progress.completed_files != 1)
        {
            return;
        }
        entered = true;
        changed.notify_all();
        if (!changed.wait_for(lock,
                15s,
                [&]
                {
                    return !armed;
                }))
        {
            throw std::runtime_error("Extraction gate timed out");
        }
    }
    void wait()
    {
        std::unique_lock lock(mutex);
        expect(changed.wait_for(lock,
            10s,
            [&]
            {
                return entered;
            }));
    }
    void release()
    {
        const std::lock_guard lock(mutex);
        armed = false;
        changed.notify_all();
    }
};

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        return 2;
    }
    try
    {
        const fs::path image(argv[1]), cache(argv[2]);
        Settings settings;
        settings.source = path_utf8(fs::absolute(image).lexically_normal());
        Gate gate;
        ContentService service(cache,
            [&](const auto& progress)
            {
                gate.observe(progress);
            });
        service.select(settings);
        expect(settled(service).state == State::NeedsPreparation);
        expect(!fs::exists(cache)); // choosing a source does not extract
        gate.arm();
        service.prepare();
        gate.wait();
        expect(service.snapshot().state == State::Preparing);
        service.cancel(); // must return while the worker is still held
        expect(service.snapshot().state == State::Cancelled);
        expect(!fs::exists(fs::directory_iterator(cache)->path() / "manifest.json"));
        gate.release();
        service.prepare(); // resumes verified files, after the cancelled job unwinds
        auto ready = settled(service);
        expect(ready.state == State::Ready && ready.files == 6 && ready.language == "en");
        settings.content = ready.binding;
        const auto root = cache / ready.binding.identity;
        {
            // The game window's mount, held while the launcher is asked to repair.
            auto mount = std::make_shared<beachlife::data::PreparedSource>(root);
            auto file = mount->open("BEACHLIFE.INI");
            std::array<unsigned char, 10> body{};
            expect(file->read(body) == body.size() && body[0] == '[');
            service.prepare(); // a runtime mount prevents repair
            expect(settled(service).state == State::Failed);
        }
        service.select(settings);
        expect(settled(service).state == State::Ready);
        // A missing ISO is fine once the prepared identity has been remembered.
        const auto offline = image.parent_path() / "offline.iso";
        fs::rename(image, offline);
        service.select(settings);
        expect(settled(service).state == State::Ready);
        // So is one the system cannot reach (here a file where its folder was,
        // like a drive without its disc): the remembered set still plays.
        {
            const auto blocker = image.parent_path() / "not-a-folder";
            {
                std::ofstream file(blocker);
            }
            auto unreachable = settings;
            unreachable.source = path_utf8(blocker / "source.iso");
            unreachable.content.source = unreachable.source;
            service.select(unreachable);
            expect(settled(service).state == State::Ready);
            fs::remove(blocker);
        }
        service.select(settings);
        expect(settled(service).state == State::Ready);
        service.cancel(); // release the mount before damaging a prepared file
        fs::remove(root / "files/Data/loose.txt");
        service.select(settings);
        expect(settled(service).state == State::Failed);
        fs::rename(offline, image);
        service.select(settings);
        expect(settled(service).state == State::NeedsPreparation);
        service.prepare();
        expect(settled(service).state == State::Ready);
        settings.content = service.snapshot().binding;
        // A replaced image at the same path must not inherit cached readiness.
        fs::last_write_time(image, fs::last_write_time(image) + 2s);
        service.select(settings);
        expect(settled(service).state == State::NeedsPreparation);
        service.prepare();
        expect(settled(service).state == State::Ready);

        // Switch sources while a new import is inside extraction. Its result
        // cannot become readiness for the newly selected source.
        const auto other_image = image.parent_path() / "other.iso";
        fs::copy_file(image, other_image);
        {
            std::fstream changed(other_image, std::ios::binary | std::ios::in | std::ios::out);
            changed.seekp(-1, std::ios::end);
            changed.put('\x5a');
        }
        Settings other;
        other.source = path_utf8(other_image);
        service.select(other);
        expect(settled(service).state == State::NeedsPreparation);
        gate.arm();
        service.prepare();
        gate.wait();
        service.select(settings); // must return before releasing the worker
        expect(service.snapshot().state == State::Checking);
        gate.release();
        expect(settled(service).state == State::NeedsPreparation); // old timestamp binding
        service.prepare();
        ready = settled(service);
        expect(ready.state == State::Ready && ready.binding.source == settings.source);
        expect(ready.binding.identity == root.filename().string());
        for (const auto& entry : fs::directory_iterator(cache))
        {
            if (entry.path() != root)
            {
                expect(!fs::exists(entry.path() / "manifest.json"));
            }
        }
        service.cancel();
        {
            std::ofstream broken(image, std::ios::binary | std::ios::trunc);
            broken << "not an ISO";
        }
        service.select(settings);
        expect(settled(service).state == State::Failed);
        service.select({});
        expect(settled(service).state == State::Idle);
        {
            // Without a folder for prepared files, a selected ISO reports why.
            ContentService nowhere{{}};
            nowhere.select(settings);
            const auto status = settled(nowhere);
            expect(status.state == State::Failed &&
                   status.error.find("Cannot locate the folder") != std::string::npos);
        }
        std::cout << "Setup worker cancellation, resumption, stale results, offline readiness, "
                     "repair and mount lifetime passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
