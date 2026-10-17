#pragma once

#include "data/content_prepare.h"
#include "settings.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace beachlife::launcher {

// One persistent worker. Selecting/cancelling never joins an in-flight import
// on the UI thread; superseded results cannot become the current ready source.
class ContentService {
public:
    enum class State { Idle, Checking, NeedsPreparation, Preparing, Ready, Cancelled, Failed };
    struct Status {
        State state = State::Idle;
        SourceStatus source;
        data::PrepareProgress progress;
        ContentBinding binding;
        std::string language, error;
        std::size_t files = 0;
        std::uint64_t bytes = 0;
        bool busy() const { return state == State::Checking || state == State::Preparing; }
    };

    // Optional observer is for diagnostics on the worker, never UI calls.
    explicit ContentService(std::filesystem::path cache,
        std::function<void(const data::PrepareProgress&)> observer = {});
    ~ContentService();
    ContentService(const ContentService&) = delete;
    ContentService& operator=(const ContentService&) = delete;

    void select(const Settings& settings);
    void prepare(); // also explicitly verifies/repairs an existing set
    void cancel();
    Status snapshot() const;
    std::shared_ptr<const data::PreparedSource> take_ready();

private:
    struct Job {
        Settings settings;
        bool prepare = false;
        std::uint64_t generation = 0;
        std::shared_ptr<std::atomic_bool> cancelled;
    };
    void enqueue(const Settings& settings, bool prepare);
    void worker();
    void run(const Job& job);
    void publish(const Job& job, Status status, std::shared_ptr<const data::PreparedSource> source = {});

    const std::filesystem::path cache_;
    const std::function<void(const data::PrepareProgress&)> observer_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    Settings selected_;
    Status status_;
    std::shared_ptr<const data::PreparedSource> ready_;
    std::optional<Job> pending_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    std::uint64_t generation_ = 0;
    bool stopping_ = false;
    std::thread thread_;
};

} // namespace beachlife::launcher
