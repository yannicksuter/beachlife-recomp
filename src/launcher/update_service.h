#pragma once

#include "launcher/update_check.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace beachlife::platform
{
class HttpCancel;
}

namespace beachlife::launcher
{

// The launcher's update check and installation, on a background thread. The
// launcher polls snapshot() every frame. One task runs at a time: a new check
// stops a running check; nothing interrupts an installation's file moves.
class UpdateService
{
  public:
    enum class State
    {
        Idle,
        Checking,
        UpToDate,
        Available,
        CheckFailed,
        Downloading,
        Installing,
        Installed,
        InstallFailed
    };

    struct Status
    {
        State state = State::Idle;
        UpdateChannel channel = UpdateChannel::Release;
        std::string error; // CheckFailed, InstallFailed
        std::optional<PublishedBuild> offer;
        std::string install_blocker; // why Update can only open the release page
        std::uint64_t received = 0, total = 0;
        std::int64_t checked = 0; // when the last check succeeded, seconds since 1970
    };

    // `base`: the installed package's folder, empty for a local build.
    UpdateService(BuildIdentity build, std::filesystem::path base);
    ~UpdateService();
    UpdateService(const UpdateService&) = delete;
    UpdateService& operator=(const UpdateService&) = delete;

    void check(UpdateChannel channel);
    void install(); // the offer of the last check
    void cancel();  // a running check or download
    Status snapshot() const;
    const BuildIdentity& build() const
    {
        return build_;
    }
    // Screenshots and checks: a made-up state without network ("uptodate",
    // "checking", "failed", "available", "test", "back", "blocked",
    // "downloading", "installing", "installfailed"). False for others.
    bool preview(std::string_view state);

  private:
    void start(std::shared_ptr<platform::HttpCancel> cancel,
        const std::function<void(Status&)>& begin,
        std::function<void()> task);
    void set(const std::function<void(Status&)>& change);
    void run_check(UpdateChannel channel, std::shared_ptr<platform::HttpCancel> cancel);
    void run_install(PublishedBuild offer, std::shared_ptr<platform::HttpCancel> cancel);

    BuildIdentity build_;
    const std::filesystem::path base_;
    mutable std::mutex mutex_;
    Status status_;
    std::shared_ptr<platform::HttpCancel> cancel_;
    std::thread worker_;
};

} // namespace beachlife::launcher
