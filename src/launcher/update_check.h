#pragma once

#include "platform/json.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// What the launcher's update check decides, without network or UI: the running
// build's origin, GitHub's releases and test builds, and which of them is an
// update. Test builds are prereleases tagged test-pr<N>-<commit> (the build
// workflow); packages are named beachlife-<version>-<platform>-<arch>.zip/.tar.gz
// (tools/package_build.py).
namespace beachlife::launcher {

inline constexpr const char* project_repository = "yannicksuter/beachlife-recomp";

// A port version, x.y.z, and the commit the build adds as "+g<commit>".
struct PortVersion {
    int major = -1, minor = 0, patch = 0;
    std::string commit;
    bool valid() const { return major >= 0; }
};
// "0.0.1", "v0.0.1" or "0.0.1+ge8cc547c0177" (a trailing ".dirty" is ignored).
PortVersion parse_version(std::string_view text);
// Orders x.y.z; the commit does not count.
int compare_versions(const PortVersion& a, const PortVersion& b);
std::string version_number(const PortVersion& version); // "0.0.1"

enum class BuildOrigin { Release, Test, Dev };

// The running build: where it comes from and which packages fit it.
struct BuildIdentity {
    std::string version; // e.g. 0.0.1+ge8cc547c0177
    BuildOrigin origin = BuildOrigin::Dev;
    std::string repository = project_repository; // owner/name
    std::string commit;
    int pull_request = 0;        // test builds
    std::int64_t run = 0;        // CI run, 0 for none
    std::int64_t commit_time = 0; // seconds since 1970
    std::string package;         // linux-x86_64, steamdeck-x86_64, macos-arm64, macos-x86_64, windows-x86_64
};

// The identity in the build's embedded build-info JSON (tools/build_version.py).
BuildIdentity parse_build_identity(std::string_view json);
// "Official release", "Test build of pull request #3", "Local build", ...
std::string describe_origin(const BuildIdentity& build);
// One line with origin, commit and time, for the Advanced page.
std::string describe_build(const BuildIdentity& build);

enum class UpdateChannel { Release, Test };

struct PackageAsset {
    std::string name, url;
    std::string digest; // "sha256:<hex>" when GitHub reports it
    std::uint64_t size = 0;
};

// An official release, or the test build of a pull request.
struct PublishedBuild {
    std::string tag, title, page;
    bool test = false;
    int pull_request = 0; // test builds
    PortVersion version;
    std::int64_t published = 0;
    std::vector<PackageAsset> assets;
    // The package for a package id (e.g. windows-x86_64), or null.
    const PackageAsset* package_for(const std::string& package) const;
    const PackageAsset* checksums() const; // SHA256SUMS, or null
};

// GitHub's release list (GET /repos/{owner}/{name}/releases): official releases
// and test builds; drafts, other prereleases and entries without a recognizable
// version are left out.
std::vector<PublishedBuild> parse_releases(const platform::JsonValue& releases);

// The newest build the channel offers that has a package for this build and is
// newer than it, or null. A test build follows its pull request's test builds
// while there are any; a build is newer with a higher version or, for release
// and test builds, a later build of the same version (such as the release of a
// tested version). Local builds are only offered higher versions. On the release
// channel a test build is offered the newest release even when that is older:
// the way back from test builds.
const PublishedBuild* find_update(const BuildIdentity& build, UpdateChannel channel,
                                  const std::vector<PublishedBuild>& published);
// Whether `offer` is newer than the running build, by find_update's rule.
bool is_newer(const BuildIdentity& build, const PublishedBuild& offer);

// What an offer means for the running build.
enum class OfferKind {
    Update,        // a newer release, or a newer test build of the same pull request
    TestBuild,     // a test build for another kind of build, or of another pull request
    BackToRelease, // a test build's way back to the newest release, which is not newer
};
OfferKind classify_offer(const BuildIdentity& build, const PublishedBuild& offer);

// The notice after an update replaced the build of version `replaced`, e.g.
// "Updated from 0.0.1 to release 0.0.2.".
std::string describe_replacement(std::string_view replaced, const BuildIdentity& build);

// The expected SHA-256 (lowercase hex) of an asset: GitHub's digest, else its line
// in the release's SHA256SUMS; empty when neither knows it.
std::string expected_sha256(const PackageAsset& asset, std::string_view checksums);

// Seconds since 1970 for an ISO 8601 UTC time ("2026-10-17T17:58:32Z"); 0 if malformed.
std::int64_t parse_utc_time(std::string_view text);
std::string format_utc_time(std::int64_t seconds); // "2026-10-17 17:58 UTC"

std::string releases_api_url(const std::string& repository);
std::string releases_page_url(const std::string& repository);
std::string pull_request_page_url(const std::string& repository, int number);

} // namespace beachlife::launcher
