#include "launcher/update_check.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>

namespace beachlife::launcher {
namespace {
constexpr std::string_view platforms[] = {"windows", "linux", "steamdeck", "macos"};
constexpr std::string_view package_prefix = "beachlife-";

bool parse_number(std::string_view text, int& value) {
    if (text.empty() || text.size() > 9) return false;
    const auto result = std::from_chars(text.data(), text.data()+text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data()+text.size();
}

bool is_hex(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

// "test-pr3-e8cc547c0177": the pull request and the commit.
bool parse_test_tag(std::string_view tag, int& pull_request, std::string& commit) {
    if (tag.substr(0, 7) != "test-pr") return false;
    tag.remove_prefix(7);
    const auto dash = tag.find('-');
    if (dash == std::string_view::npos || !parse_number(tag.substr(0, dash), pull_request) || pull_request <= 0) return false;
    commit = std::string(tag.substr(dash+1));
    return is_hex(commit);
}

// beachlife-<version>-<platform>-<arch>.zip/.tar.gz: the version text.
bool package_version(std::string_view name, std::string& version) {
    if (name.substr(0, package_prefix.size()) != package_prefix) return false;
    name.remove_prefix(package_prefix.size());
    if (name.size() > 4 && name.substr(name.size()-4) == ".zip") name.remove_suffix(4);
    else if (name.size() > 7 && name.substr(name.size()-7) == ".tar.gz") name.remove_suffix(7);
    else return false;
    for (const auto platform : platforms) {
        const std::string marker = "-" + std::string(platform) + "-";
        const auto at = name.rfind(marker);
        if (at != std::string_view::npos && at > 0 && at+marker.size() < name.size()) {
            version = std::string(name.substr(0, at));
            return true;
        }
    }
    return false;
}

bool newer(const PublishedBuild& a, const PublishedBuild& b) {
    const int order = compare_versions(a.version, b.version);
    return order > 0 || (order == 0 && a.published > b.published);
}

std::int64_t days_from_civil(int year, int month, int day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year-399)/400;
    const int yoe = year-era*400;
    const int doy = (153*(month+(month > 2 ? -3 : 9))+2)/5+day-1;
    const int doe = yoe*365+yoe/4-yoe/100+doy;
    return std::int64_t(era)*146097+doe-719468;
}
} // namespace

PortVersion parse_version(std::string_view text) {
    PortVersion version;
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V')) text.remove_prefix(1);
    std::string_view build;
    if (const auto plus = text.find('+'); plus != std::string_view::npos) {
        build = text.substr(plus+1);
        text = text.substr(0, plus);
    }
    int parts[3] = {};
    for (int n = 0; n < 3; ++n) {
        const auto dot = text.find('.');
        if ((n < 2) == (dot == std::string_view::npos)) return {};
        if (!parse_number(text.substr(0, dot), parts[n]) || parts[n] < 0) return {};
        text = n < 2 ? text.substr(dot+1) : std::string_view();
    }
    if (!build.empty()) {
        if (build.size() > 6 && build.substr(build.size()-6) == ".dirty") build.remove_suffix(6);
        if (build.size() < 2 || build[0] != 'g' || !is_hex(build.substr(1))) return {};
        version.commit = std::string(build.substr(1));
    }
    version.major = parts[0];
    version.minor = parts[1];
    version.patch = parts[2];
    return version;
}

int compare_versions(const PortVersion& a, const PortVersion& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

std::string version_number(const PortVersion& version) {
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
}

BuildIdentity parse_build_identity(std::string_view json) {
    const auto info = platform::parse_json(json);
    BuildIdentity build;
    build.version = info["build_version"].text();
    const std::string& channel = info["channel"].text();
    build.origin = channel == "release" ? BuildOrigin::Release : channel == "test" ? BuildOrigin::Test : BuildOrigin::Dev;
    build.commit = info["commit"].text();
    build.pull_request = static_cast<int>(info["pull_request"].integer().value_or(0));
    build.run = info["run_id"].integer().value_or(0);
    build.commit_time = info["commit_time"].integer().value_or(0);
    if (!info["platform"].text().empty() && !info["architecture"].text().empty())
        build.package = info["platform"].text() + "-" + info["architecture"].text();
    return build;
}

std::string describe_origin(const BuildIdentity& build) {
    switch (build.origin) {
    case BuildOrigin::Release: return "Official release";
    case BuildOrigin::Test: return "Test build of pull request #" + std::to_string(build.pull_request);
    case BuildOrigin::Dev: break;
    }
    return build.run ? "Development build (CI run " + std::to_string(build.run) + ")" : "Local build";
}

std::string describe_build(const BuildIdentity& build) {
    std::string text = describe_origin(build);
    if (!build.commit.empty()) text += ", commit " + build.commit.substr(0, 12);
    if (build.commit_time) text += " from " + format_utc_time(build.commit_time);
    return text;
}

const PackageAsset* PublishedBuild::package_for(const std::string& package) const {
    if (package.empty()) return nullptr;
    for (const auto& asset : assets) {
        std::string text;
        if (!package_version(asset.name, text)) continue;
        const std::string stem = std::string(package_prefix) + text + "-" + package;
        if (asset.name == stem + ".zip" || asset.name == stem + ".tar.gz") return &asset;
    }
    return nullptr;
}

const PackageAsset* PublishedBuild::checksums() const {
    for (const auto& asset : assets)
        if (asset.name == "SHA256SUMS") return &asset;
    return nullptr;
}

std::vector<PublishedBuild> parse_releases(const platform::JsonValue& releases) {
    std::vector<PublishedBuild> builds;
    for (const auto& item : releases.items()) {
        if (!item.is_object() || item["draft"].boolean()) continue;
        PublishedBuild build;
        build.tag = item["tag_name"].text();
        std::string tag_commit;
        build.test = parse_test_tag(build.tag, build.pull_request, tag_commit);
        // Test builds are prereleases; other prereleases are not offered.
        if (item["prerelease"].boolean() != build.test) continue;
        build.title = item["name"].text();
        build.page = item["html_url"].text();
        build.published = parse_utc_time(item["published_at"].text());
        for (const auto& entry : item["assets"].items()) {
            PackageAsset asset;
            asset.name = entry["name"].text();
            asset.url = entry["browser_download_url"].text();
            asset.digest = entry["digest"].text();
            asset.size = static_cast<std::uint64_t>(std::max<std::int64_t>(entry["size"].integer().value_or(0), 0));
            if (asset.name.empty() || asset.url.rfind("https://", 0) != 0) continue;
            std::string version;
            if (!build.version.valid() && package_version(asset.name, version)) build.version = parse_version(version);
            build.assets.push_back(std::move(asset));
        }
        if (!build.version.valid()) continue;
        // A test build's packages carry the commit its tag names.
        if (build.test && build.version.commit.empty()) build.version.commit = tag_commit;
        builds.push_back(std::move(build));
    }
    return builds;
}

const PublishedBuild* find_update(const BuildIdentity& build, UpdateChannel channel,
                                  const std::vector<PublishedBuild>& published) {
    const PortVersion self = parse_version(build.version);
    if (!self.valid()) return nullptr;
    // A test build follows its own pull request while that has test builds.
    bool own_pull_request = false;
    if (channel == UpdateChannel::Test && build.origin == BuildOrigin::Test)
        for (const auto& candidate : published)
            own_pull_request = own_pull_request ||
                (candidate.test && candidate.pull_request == build.pull_request && candidate.package_for(build.package));
    const PublishedBuild* best = nullptr;
    for (const auto& candidate : published) {
        if (!candidate.package_for(build.package)) continue;
        if (candidate.test && (channel == UpdateChannel::Release ||
                               (own_pull_request && candidate.pull_request != build.pull_request)))
            continue;
        if (!best || newer(candidate, *best)) best = &candidate;
    }
    if (!best) return nullptr;
    // This very build (the same commit).
    if (!best->version.commit.empty() && !self.commit.empty() &&
        (best->version.commit.rfind(self.commit, 0) == 0 || self.commit.rfind(best->version.commit, 0) == 0))
        return nullptr;
    // The way back from a test build to the release, whatever its version.
    if (channel == UpdateChannel::Release && build.origin == BuildOrigin::Test) return best;
    return is_newer(build, *best) ? best : nullptr;
}

bool is_newer(const BuildIdentity& build, const PublishedBuild& offer) {
    const PortVersion self = parse_version(build.version);
    if (!self.valid()) return false;
    const int order = compare_versions(offer.version, self);
    if (order != 0) return order > 0;
    if (build.origin == BuildOrigin::Dev) return false;
    return build.commit_time && offer.published > build.commit_time;
}

OfferKind classify_offer(const BuildIdentity& build, const PublishedBuild& offer) {
    if (offer.test)
        return build.origin == BuildOrigin::Test && offer.pull_request == build.pull_request ? OfferKind::Update
                                                                                             : OfferKind::TestBuild;
    return is_newer(build, offer) ? OfferKind::Update : OfferKind::BackToRelease;
}

std::string describe_replacement(std::string_view replaced, const BuildIdentity& build) {
    const PortVersion before = parse_version(replaced), now = parse_version(build.version);
    if (!before.valid() || !now.valid()) return {};
    // Equal numbers (test builds of one version, or its release) differ in their commits.
    const bool same_number = compare_versions(before, now) == 0;
    auto label = [same_number](const PortVersion& version) {
        std::string text = version_number(version);
        if (same_number && !version.commit.empty()) text += " (" + version.commit.substr(0, 8) + ")";
        return text;
    };
    std::string target = label(now);
    if (build.origin == BuildOrigin::Release) target = "release " + target;
    else if (build.origin == BuildOrigin::Test) target = "test build " + target;
    return std::string(compare_versions(now, before) < 0 ? "Switched" : "Updated") + " from " + label(before) + " to " +
           target + ".";
}

std::string expected_sha256(const PackageAsset& asset, std::string_view checksums) {
    auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    if (asset.digest.rfind("sha256:", 0) == 0 && asset.digest.size() == 7+64 && is_hex(asset.digest.substr(7)))
        return lower(asset.digest.substr(7));
    // sha256sum lines: "<hex>  <name>" (or " *<name>" in binary mode).
    while (!checksums.empty()) {
        const auto end = checksums.find('\n');
        std::string_view line = checksums.substr(0, end);
        checksums = end == std::string_view::npos ? std::string_view() : checksums.substr(end+1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() < 66 || !is_hex(line.substr(0, 64)) || line[64] != ' ') continue;
        std::string_view name = line.substr(65);
        if (!name.empty() && (name[0] == ' ' || name[0] == '*')) name.remove_prefix(1);
        if (name == asset.name) return lower(std::string(line.substr(0, 64)));
    }
    return {};
}

std::int64_t parse_utc_time(std::string_view text) {
    // YYYY-MM-DDTHH:MM:SSZ, GitHub's format.
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':' ||
        text[19] != 'Z' || !parse_number(text.substr(0, 4), year) || !parse_number(text.substr(5, 2), month) ||
        !parse_number(text.substr(8, 2), day) || !parse_number(text.substr(11, 2), hour) ||
        !parse_number(text.substr(14, 2), minute) || !parse_number(text.substr(17, 2), second) || month < 1 ||
        month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60)
        return 0;
    return days_from_civil(year, month, day)*86400+hour*3600+minute*60+second;
}

std::string format_utc_time(std::int64_t seconds) {
    std::int64_t days = seconds/86400, rest = seconds%86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    // The civil date from days since 1970-01-01 (H. Hinnant's algorithm).
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days-146096)/146097;
    const std::int64_t doe = days-era*146097;
    const std::int64_t yoe = (doe-doe/1460+doe/36524-doe/146096)/365;
    const std::int64_t doy = doe-(365*yoe+yoe/4-yoe/100);
    const std::int64_t mp = (5*doy+2)/153;
    const int day = static_cast<int>(doy-(153*mp+2)/5+1);
    const int month = static_cast<int>(mp < 10 ? mp+3 : mp-9);
    const long long year = static_cast<long long>(yoe+era*400+(month <= 2));
    char text[48];
    std::snprintf(text, sizeof text, "%04lld-%02d-%02d %02d:%02d UTC", year, month, day, static_cast<int>(rest/3600),
                  static_cast<int>(rest%3600/60));
    return text;
}

std::string releases_api_url(const std::string& repository) {
    return "https://api.github.com/repos/" + repository + "/releases?per_page=30";
}

std::string releases_page_url(const std::string& repository) { return "https://github.com/" + repository + "/releases"; }

std::string pull_request_page_url(const std::string& repository, int number) {
    return "https://github.com/" + repository + "/pull/" + std::to_string(number);
}

} // namespace beachlife::launcher
