#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace beachlife::runtime
{

class StartupWork;

// What the loading screen shows of the startup work.
struct StartupStatus
{
    std::string label;                 // the running step
    std::uint64_t done = 0, total = 0; // of the running step; total is 0 while its size is unknown
    std::size_t step = 0, steps = 0;   // finished steps, all steps
    bool ready = false, failed = false;
    // The finished share of all steps. None while nothing is measurable yet: the
    // bar sweeps only at the start, later steps of unknown size hold the bar.
    std::optional<float> share() const;
};

// A step's view of the work: reports its progress, and asks it to return early
// once the player has left.
class StartupProgress
{
  public:
    void total(std::uint64_t units); // the step's size, once known
    void advance(std::uint64_t units);
    void report(std::string label, std::uint64_t done, std::uint64_t total);
    bool cancelled() const;

  private:
    friend class StartupWork;
    explicit StartupProgress(StartupWork& work)
        : work_(work)
    {
    }
    StartupWork& work_;
};

// Work between the window opening and the game's first frame, such as reading
// game files through the VFS or preparing shaders.
struct StartupStep
{
    std::string label; // "Loading game files"
    std::function<void(StartupProgress&)> run;
};

// Runs the steps in order on one worker thread. An exception ends the work and is
// kept for the caller. cancel() asks the running step to return; destruction also
// cancels, then waits for it.
class StartupWork
{
  public:
    explicit StartupWork(std::vector<StartupStep> steps);
    ~StartupWork();
    StartupWork(const StartupWork&) = delete;
    StartupWork& operator=(const StartupWork&) = delete;

    StartupStatus status() const;
    void cancel();
    // Wait for the worker to exit without cancelling it. The owning caller may
    // join more than once; errors remain available through status()/rethrow().
    void join();
    // Throws the error of the failed step, if there is one.
    void rethrow() const;

  private:
    friend class StartupProgress;
    void run();

    const std::vector<StartupStep> steps_;
    mutable std::mutex mutex_;
    StartupStatus status_;
    std::exception_ptr error_;
    std::atomic_bool cancelled_{false};
    std::thread thread_;
};

} // namespace beachlife::runtime
