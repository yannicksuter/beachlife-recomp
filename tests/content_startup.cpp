#include "runtime/content_startup.h"
#include "data/content_prepare.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
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
        const auto bytes = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), probe_->available-position_));
        std::fill_n(output.begin(), bytes, 'x');
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
        expect(name == "BeachLife.ini");
        ++probe_->opens;
        if (probe_->fail_open) throw std::runtime_error("Generated missing file");
        return std::make_unique<ProbeFile>(probe_);
    }
private:
    std::shared_ptr<Probe> probe_;
};

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try {
        const std::filesystem::path image(argv[1]), cache(argv[2]);
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
        std::cout << "Queued VFS ownership, offline reads, byte progress, cancellation, limits and read errors passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
