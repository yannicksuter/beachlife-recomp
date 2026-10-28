#include "launcher/update_service.h"
#include "launcher/update_install.h"
#include "platform/http_client.h"
#include "platform/json.h"
#include "platform/path.h"
#include "platform/sha256.h"

#include <ctime>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace beachlife::launcher
{
namespace
{
namespace fs = std::filesystem;

platform::HttpRequest github_request(const std::string& url,
    const BuildIdentity& build,
    const std::shared_ptr<platform::HttpCancel>& cancel)
{
    platform::HttpRequest request;
    request.url = url;
    request.user_agent = "BeachLife/" + build.version;
    request.headers = {"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28"};
    request.cancel = cancel;
    return request;
}

std::string github_failure(long status)
{
    if (status == 403 || status == 429)
    {
        return "GitHub is limiting requests from this network; try again later.";
    }
    if (status == 404)
    {
        return "The project's releases were not found on GitHub.";
    }
    return "GitHub answered with error " + std::to_string(status) + ".";
}

std::string stamp()
{
    static unsigned counter = 0;
    return std::to_string(std::time(nullptr)) + "-" + std::to_string(++counter);
}

// Removes `folder` when it holds no files (only empty folders).
void remove_if_empty(const fs::path& folder)
{
    std::error_code error;
    for (const auto& entry : fs::recursive_directory_iterator(folder, error))
    {
        if (!entry.is_directory())
        {
            return;
        }
    }
    fs::remove_all(folder, error);
}
} // namespace

UpdateService::UpdateService(BuildIdentity build, fs::path base)
    : build_(std::move(build))
    , base_(std::move(base))
{
    // The installed package names its platform (the Steam Deck build of Linux, say).
    try
    {
        if (!base_.empty())
        {
            build_.package = installed_package(base_);
        }
    }
    catch (const std::exception&)
    {
    }
}

UpdateService::~UpdateService()
{
    cancel();
    if (worker_.joinable())
    {
        worker_.join();
    }
}

void UpdateService::set(const std::function<void(Status&)>& change)
{
    const std::lock_guard lock(mutex_);
    change(status_);
}

UpdateService::Status UpdateService::snapshot() const
{
    const std::lock_guard lock(mutex_);
    return status_;
}

void UpdateService::cancel()
{
    const std::lock_guard lock(mutex_);
    if (cancel_)
    {
        cancel_->cancel();
    }
}

void UpdateService::start(std::shared_ptr<platform::HttpCancel> cancel,
    const std::function<void(Status&)>& begin,
    std::function<void()> task)
{
    std::shared_ptr<platform::HttpCancel> previous;
    {
        const std::lock_guard lock(mutex_);
        previous = std::exchange(cancel_, std::move(cancel));
    }
    if (previous)
    {
        previous->cancel();
    }
    if (worker_.joinable())
    {
        worker_.join();
    }
    set(begin); // shows at once, before the next snapshot()
    worker_ = std::thread(std::move(task));
}

void UpdateService::check(UpdateChannel channel)
{
    {
        const std::lock_guard lock(mutex_);
        // An installation is never interrupted.
        if (status_.state == State::Downloading || status_.state == State::Installing ||
            status_.state == State::Installed)
        {
            return;
        }
    }
    auto cancel = std::make_shared<platform::HttpCancel>();
    start(
        cancel,
        [channel](Status& status)
        {
            status.state = State::Checking;
            status.channel = channel;
            status.error.clear();
            status.offer.reset();
            status.pull.reset();
            status.install_blocker.clear();
        },
        [this, channel, cancel]
        {
            run_check(channel, cancel);
        });
}

void UpdateService::install()
{
    std::optional<PublishedBuild> offer;
    {
        const std::lock_guard lock(mutex_);
        if ((status_.state != State::Available && status_.state != State::InstallFailed) ||
            !status_.offer || !status_.install_blocker.empty())
        {
            return;
        }
        offer = status_.offer;
    }
    auto cancel = std::make_shared<platform::HttpCancel>();
    start(
        cancel,
        [](Status& status)
        {
            status.state = State::Downloading;
            status.error.clear();
            status.received = status.total = 0;
        },
        [this, offer = *offer, cancel]
        {
            run_install(offer, cancel);
        });
}

void UpdateService::run_check(UpdateChannel channel, std::shared_ptr<platform::HttpCancel> cancel)
{
    try
    {
        const auto answer =
            platform::http_get(github_request(releases_api_url(build_.repository), build_, cancel));
        if (answer.status != 200)
        {
            throw std::runtime_error(github_failure(answer.status));
        }
        const auto published = parse_releases(platform::parse_json(answer.body));
        const PublishedBuild* found = find_update(build_, channel, published);
        if (!found)
        {
            set(
                [](Status& status)
                {
                    status.state = State::UpToDate;
                    status.checked = std::time(nullptr);
                });
            return;
        }
        PublishedBuild offer = *found;
        // A test build's notes are its pull request's description.
        std::optional<PullRequestInfo> pull;
        if (offer.test)
        {
            try
            {
                const auto reply = platform::http_get(github_request(
                    pull_request_api_url(build_.repository, offer.pull_request), build_, cancel));
                if (reply.status == 200)
                {
                    pull = parse_pull_request(platform::parse_json(reply.body));
                }
            }
            catch (const platform::HttpError& error)
            {
                if (error.cancelled())
                {
                    throw;
                }
            }
            catch (const std::exception&)
            {
            }
        }
        const std::string blocker = install_blocker(base_);
        set(
            [&](Status& status)
            {
                status.state = State::Available;
                status.offer = std::move(offer);
                status.pull = std::move(pull);
                status.install_blocker = blocker;
                status.checked = std::time(nullptr);
            });
    }
    catch (const platform::HttpError& error)
    {
        if (error.cancelled())
        {
            set(
                [](Status& status)
                {
                    status.state = State::Idle;
                });
        }
        else
        {
            set(
                [&](Status& status)
                {
                    status.state = State::CheckFailed;
                    status.error = error.what();
                });
        }
    }
    catch (const std::exception& error)
    {
        set(
            [&](Status& status)
            {
                status.state = State::CheckFailed;
                status.error = error.what();
            });
    }
}

void UpdateService::run_install(PublishedBuild offer, std::shared_ptr<platform::HttpCancel> cancel)
{
    const fs::path folder = update_folder(base_);
    const std::string id = stamp();
    const fs::path download = folder / ("download-" + id), staged = folder / ("staged-" + id);
    const fs::path backup = folder / ("previous-" + id);
    auto cleanup = [&]
    {
        std::error_code error;
        fs::remove_all(download, error);
        fs::remove_all(staged, error);
    };
    try
    {
        const PackageAsset* asset = offer.package_for(build_.package);
        if (!asset)
        {
            throw std::runtime_error("This release has no package for " + build_.package + ".");
        }
        std::string expected = expected_sha256(*asset, {});
        if (expected.empty())
        {
            if (const PackageAsset* sums = offer.checksums())
            {
                const auto reply = platform::http_get(github_request(sums->url, build_, cancel));
                if (reply.status == 200)
                {
                    expected = expected_sha256(*asset, reply.body);
                }
            }
        }
        if (expected.empty())
        {
            throw std::runtime_error("The release has no checksum for " + asset->name + ".");
        }

        fs::create_directories(download);
        const fs::path archive = download / platform::utf8_path(asset->name);
        auto request = github_request(asset->url, build_, cancel);
        request.headers = {"Accept: application/octet-stream"};
        request.stall_timeout = 60;
        const long status = platform::http_download(request,
            archive,
            [&](std::uint64_t received, std::uint64_t total)
            {
                set(
                    [&](Status& state)
                    {
                        state.received = received;
                        state.total = total ? total : asset->size;
                    });
            });
        if (status != 200)
        {
            throw std::runtime_error(
                "The download failed with error " + std::to_string(status) + ".");
        }
        if (asset->size && fs::file_size(archive) != asset->size)
        {
            throw std::runtime_error("The download is incomplete.");
        }
        if (platform::sha256_file(archive) != expected)
        {
            throw std::runtime_error("The download is damaged (its checksum does not match).");
        }

        set(
            [](Status& state)
            {
                state.state = State::Installing;
            });
        const fs::path root = unpack_package(archive,
            staged,
            [&]
            {
                return cancel->cancelled();
            });
        const auto incoming = read_package_manifest(root / package_manifest);
        const PortVersion version = parse_version(incoming.version);
        if (compare_versions(version, offer.version) != 0 || version.commit != offer.version.commit)
        {
            throw std::runtime_error(
                "The package holds version " + incoming.version + ", not the one offered.");
        }
        if (cancel->cancelled())
        {
            throw platform::HttpError("Cancelled", true);
        }
        install_staged_package(base_, root, backup);
        cleanup();
        set(
            [](Status& state)
            {
                state.state = State::Installed;
            });
    }
    catch (const std::exception& error)
    {
        cleanup();
        remove_if_empty(backup);
        const auto* http = dynamic_cast<const platform::HttpError*>(&error);
        const bool cancelled = cancel->cancelled() || (http && http->cancelled());
        set(
            [&](Status& state)
            {
                state.state = cancelled ? State::Available : State::InstallFailed;
                state.error = cancelled ? std::string() : std::string(error.what());
            });
    }
}

bool UpdateService::preview(std::string_view state)
{
    if (build_.package.empty())
    {
        build_.package = "linux-x86_64";
    }
    const PortVersion self = parse_version(build_.version);
    auto made_up = [&](bool test, int pull_request, PortVersion version)
    {
        PublishedBuild offer;
        offer.test = test;
        offer.pull_request = pull_request;
        offer.version = std::move(version);
        offer.tag = test ? "test-pr" + std::to_string(pull_request) + "-" + offer.version.commit
                         : "v" + version_number(offer.version);
        offer.page = test ? pull_request_page_url(build_.repository, pull_request)
                          : releases_page_url(build_.repository) + "/tag/" + offer.tag;
        offer.published = std::time(nullptr) - 3 * 3600;
        PackageAsset asset;
        asset.name = "beachlife-" + version_number(offer.version) + "+g" + offer.version.commit +
                     "-" + build_.package + ".tar.gz";
        asset.url = offer.page;
        asset.size = 9713229;
        offer.assets.push_back(std::move(asset));
        return offer;
    };
    PortVersion next = self.valid() ? self : parse_version("0.0.1");
    ++next.patch;
    next.commit = "4f2a9c81d03e";
    const std::string release_notes =
        "A made-up release that shows how release notes look in the launcher.\n\n"
        "## Launcher\n- **Updates** ask before they install and restart the launcher; settings "
        "stay.\n- The `Release / Test` switch chooses between releases and test builds.\n\n"
        "## Game\n- More of the reconstructed front end runs natively.";
    const std::string pull_notes =
        "Promotes `main` to `stable`.\n\n## To try\n- Switch to **Test** in the footer and "
        "install this build.\n- Switch back to **Release** to return to the official release.";
    Status status;
    status.checked = std::time(nullptr) - 120;
    if (state == "uptodate")
    {
        status.state = State::UpToDate;
    }
    else if (state == "checking")
    {
        status.state = State::Checking;
    }
    else if (state == "failed")
    {
        status.state = State::CheckFailed;
        status.error = "Could not resolve host: api.github.com";
    }
    else if (state == "available" || state == "blocked" || state == "downloading" ||
             state == "installing" || state == "installfailed")
    {
        status.offer = made_up(false, 0, next);
        status.offer->title = "Beach Life " + version_number(next);
        status.offer->notes = release_notes;
        status.state = state == "downloading"     ? State::Downloading
                       : state == "installing"    ? State::Installing
                       : state == "installfailed" ? State::InstallFailed
                                                  : State::Available;
        if (state == "blocked")
        {
            status.install_blocker =
                "This copy was not unpacked from a release package, so it cannot update itself.";
        }
        if (state == "installfailed")
        {
            status.error = "The download is damaged (its checksum does not match).";
        }
        status.received = 4598323;
        status.total = 9713229;
    }
    else if (state == "test")
    {
        PortVersion version = self.valid() ? self : parse_version("0.0.1");
        version.commit = "b81e07c4a2d9";
        status.channel = UpdateChannel::Test;
        status.offer = made_up(true, 3, version);
        status.offer->title = "Beach Life test build: PR #3";
        PullRequestInfo pull;
        pull.number = 3;
        pull.title = "Promote main to stable";
        pull.body = pull_notes;
        pull.page = status.offer->page;
        status.pull = std::move(pull);
        status.state = State::Available;
    }
    else if (state == "back")
    {
        build_.origin = BuildOrigin::Test;
        build_.pull_request = 3;
        PortVersion release = self.valid() ? self : parse_version("0.0.1");
        release.commit = "6144eb682311";
        status.offer = made_up(false, 0, release);
        status.offer->title = "Beach Life " + version_number(release);
        status.offer->notes = release_notes;
        status.state = State::Available;
    }
    else
    {
        return false;
    }
    const std::lock_guard lock(mutex_);
    status_ = std::move(status);
    return true;
}

} // namespace beachlife::launcher
