#include "runtime/startup.h"

#include <algorithm>

namespace beachlife::runtime {

std::optional<float> StartupStatus::share() const {
    if (ready || !steps) return 1.0f;
    if (!total && !step) return std::nullopt;
    const double step_share = total ? static_cast<double>(std::min(done, total))/static_cast<double>(total) : 0.0;
    return static_cast<float>((static_cast<double>(step) + step_share)/static_cast<double>(steps));
}

void StartupProgress::total(std::uint64_t units) {
    std::lock_guard lock(work_.mutex_);
    work_.status_.total = units;
}

void StartupProgress::advance(std::uint64_t units) {
    std::lock_guard lock(work_.mutex_);
    work_.status_.done += units;
}

void StartupProgress::report(std::string label, std::uint64_t done, std::uint64_t total) {
    std::lock_guard lock(work_.mutex_);
    work_.status_.label = std::move(label);
    work_.status_.done = done;
    work_.status_.total = total;
}

bool StartupProgress::cancelled() const { return work_.cancelled_; }

StartupWork::StartupWork(std::vector<StartupStep> steps) : steps_(std::move(steps)) {
    status_.steps = steps_.size();
    if (!steps_.empty()) status_.label = steps_.front().label;
    thread_ = std::thread([this] { run(); });
}

StartupWork::~StartupWork() {
    cancel();
    thread_.join();
}

StartupStatus StartupWork::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

void StartupWork::cancel() { cancelled_ = true; }

void StartupWork::rethrow() const {
    std::lock_guard lock(mutex_);
    if (error_) std::rethrow_exception(error_);
}

void StartupWork::run() {
    StartupProgress progress(*this);
    for (const auto& step : steps_) {
        if (cancelled_) return;
        {
            std::lock_guard lock(mutex_);
            status_.label = step.label;
            status_.done = status_.total = 0;
        }
        try {
            step.run(progress);
        } catch (...) {
            std::lock_guard lock(mutex_);
            error_ = std::current_exception();
            status_.failed = true;
            return;
        }
        // A cancelled step may have returned early: it is not finished.
        if (cancelled_) return;
        std::lock_guard lock(mutex_);
        ++status_.step;
    }
    std::lock_guard lock(mutex_);
    status_.ready = true;
}

} // namespace beachlife::runtime
