// The launcher's update check and in-place installation without network: JSON,
// SHA-256, GitHub's release answers, the choice of an update, and unpacking and
// installing packages tests/launcher_update_fixture.py writes like tools/package_build.py.
#include "launcher/release_notes.h"
#include "launcher/update_check.h"
#include "launcher/update_install.h"
#include "platform/json.h"
#include "platform/sha256.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace beachlife;
using namespace beachlife::launcher;
namespace fs = std::filesystem;

namespace
{
int failures = 0;

void check(bool ok, const std::string& what)
{
    if (ok)
    {
        return;
    }
    ++failures;
    std::cerr << "FAILED: " << what << '\n';
}

bool throws(const std::function<void()>& action)
{
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        return true;
    }
    return false;
}

std::string read(const fs::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    std::stringstream text;
    text << stream.rdbuf();
    return text.str();
}

// Every file below `root` with its contents, by relative path, without .update.
std::map<std::string, std::string> tree(const fs::path& root)
{
    std::map<std::string, std::string> files;
    if (!fs::exists(root))
    {
        return files;
    }
    for (const auto& entry : fs::recursive_directory_iterator(root))
    {
        const std::string name = fs::relative(entry.path(), root).generic_string();
        if (entry.is_regular_file() && name.rfind(".update/", 0) != 0)
        {
            files[name] = read(entry.path());
        }
    }
    return files;
}

void test_json()
{
    const auto value = platform::parse_json(
        R"({"name": "Beach \"Life\"", "list": [1, -2, 3.5e2, true, null], "nested": {"a": {}},
            "text": "caf\u00e9 \ud83d\udc4d \/", "big": 38790034498})");
    check(value["name"].text() == "Beach \"Life\"", "JSON string escapes");
    check(value["list"].size() == 5 && value["list"].items()[1].integer() == -2,
        "JSON array and integers");
    check(!value["list"].items()[2].integer() && value["list"].items()[2].text() == "3.5e2",
        "JSON number text");
    check(
        value["list"].items()[3].boolean() && value["list"].items()[4].is_null(), "JSON literals");
    check(value["nested"]["a"].is_object() && value["missing"]["deeper"].is_null(),
        "JSON missing members");
    check(value["text"].text() == "caf\xC3\xA9 \xF0\x9F\x91\x8D /",
        "JSON unicode escapes and surrogate pairs");
    check(value["big"].integer() == 38790034498LL, "JSON 64-bit integers");
    for (const char* bad :
        {"", "{", "[1,]", "{\"a\" 1}", "01", "\"\x01\"", "[1] x", "{\"a\":tru}", "\"\\x\""})
    {
        check(throws(
                  [&]
                  {
                      platform::parse_json(bad);
                  }),
            std::string("JSON rejects ") + bad);
    }
    check(throws(
              []
              {
                  platform::parse_json(std::string(100, '[') + std::string(100, ']'));
              }),
        "JSON depth limit");
}

void test_sha256()
{
    check(platform::sha256_hex("") ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "SHA-256 of nothing");
    check(platform::sha256_hex("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 of abc");
    check(platform::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        "SHA-256 over two blocks");
    platform::Sha256 hash;
    const std::string chunk(1000, 'a');
    for (int n = 0; n < 1000; ++n)
    {
        hash.update(chunk.data(), chunk.size());
    }
    check(hash.finish_hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
        "SHA-256 of a million a");
}

void test_versions()
{
    const auto version = parse_version("0.0.2+ge8cc547c0177");
    check(version.valid() && version.patch == 2 && version.commit == "e8cc547c0177",
        "Version with commit");
    check(parse_version("v1.2.3").minor == 2 &&
              parse_version("0.0.1+g6144eb682311.dirty").commit == "6144eb682311",
        "Tag and dirty versions");
    for (const char* bad : {"", "1", "1.0", "1.0.0.0", "1.x.0", "1.0.0+main", "1.0.0+g", "-1.0.0"})
    {
        check(!parse_version(bad).valid(), std::string("Version rejects ") + bad);
    }
    check(compare_versions(parse_version("0.0.10"), parse_version("0.0.9")) > 0, "0.0.10 > 0.0.9");
    check(compare_versions(parse_version("0.1.0"), parse_version("0.0.99")) > 0, "0.1.0 > 0.0.99");
    check(compare_versions(parse_version("0.0.1+gaaaa"), parse_version("0.0.1+gbbbb")) == 0,
        "Commits do not order");
    check(version_number(version) == "0.0.2", "Version number text");
    check(parse_utc_time("2026-10-15T17:22:00Z") == 1792084920 &&
              parse_utc_time("1970-01-01T00:00:00Z") == 0 &&
              parse_utc_time("2026-10-15 17:22:00") == 0,
        "UTC times");
    check(format_utc_time(1792084920) == "2026-10-15 17:22 UTC" &&
              format_utc_time(951782400) == "2000-02-29 00:00 UTC",
        "UTC time text");
}

std::string release(const std::string& tag,
    bool prerelease,
    const std::string& published,
    const std::string& version,
    bool draft = false,
    bool digest = true)
{
    std::string assets;
    for (const char* package :
        {"linux-x86_64", "steamdeck-x86_64", "macos-arm64", "windows-x86_64"})
    {
        const std::string extension =
            std::string(package).rfind("windows", 0) == 0 ? ".zip" : ".tar.gz";
        const std::string name = "beachlife-" + version + "-" + package + extension;
        assets += std::string(assets.empty() ? "" : ",") + R"({"name":")" + name +
                  R"(","browser_download_url":"https://github.com/o/r/releases/download/)" + tag +
                  "/" + name + R"(","size":1234,"digest":)" +
                  (digest ? "\"sha256:" + std::string(64, 'a') + "\"" : "null") + "}";
    }
    assets +=
        R"(,{"name":"SHA256SUMS","browser_download_url":"https://github.com/o/r/releases/download/)" +
        tag + R"(/SHA256SUMS","size":10,"digest":null})";
    return R"({"tag_name":")" + tag + R"(","name":"Title )" + tag + R"(","draft":)" +
           (draft ? "true" : "false") + R"(,"prerelease":)" + (prerelease ? "true" : "false") +
           R"(,"html_url":"https://github.com/o/r/releases/tag/)" + tag + R"(","published_at":")" +
           published + R"(","body":"Notes","assets":[)" + assets + "]}";
}

BuildIdentity build(BuildOrigin origin,
    const std::string& version,
    std::int64_t time,
    int pull_request = 0,
    const std::string& package = "linux-x86_64")
{
    BuildIdentity identity;
    identity.origin = origin;
    identity.version = version;
    identity.commit_time = time;
    identity.pull_request = pull_request;
    identity.repository = "o/r";
    identity.package = package;
    return identity;
}

void test_releases()
{
    const std::string list =
        "[" +
        release("test-pr3-cccccccccccc", true, "2026-10-16T10:00:00Z", "0.0.2+gcccccccccccc") +
        "," +
        release("test-pr5-dddddddddddd", true, "2026-10-16T12:00:00Z", "0.0.3+gdddddddddddd") +
        "," + release("v0.0.2", false, "2026-10-15T18:00:00Z", "0.0.2+gbbbbbbbbbbbb", true) + "," +
        release("v0.0.1", false, "2026-10-15T17:22:00Z", "0.0.1+gaaaaaaaaaaaa") + "," +
        release("nightly", true, "2026-10-17T00:00:00Z", "0.0.4+geeeeeeeeeeee") + "," +
        release("v0.0.0", false, "2026-10-13T20:00:38Z", "0.0.0+g999999999999", false, false) + "]";
    const auto builds = parse_releases(platform::parse_json(list));
    check(builds.size() == 4, "Drafts and other prereleases are left out");
    const PublishedBuild* pr3 = nullptr;
    const PublishedBuild* first = nullptr;
    for (const auto& item : builds)
    {
        if (item.tag == "test-pr3-cccccccccccc")
        {
            pr3 = &item;
        }
        if (item.tag == "v0.0.1")
        {
            first = &item;
        }
    }
    check(pr3 && pr3->test && pr3->pull_request == 3 && pr3->version.commit == "cccccccccccc",
        "Test build tag");
    check(first && !first->test && first->version.patch == 1 && first->published == 1792084920,
        "Release fields");
    check(first && first->package_for("steamdeck-x86_64") &&
              first->package_for("steamdeck-x86_64")->name ==
                  "beachlife-0.0.1+gaaaaaaaaaaaa-steamdeck-x86_64.tar.gz" &&
              first->package_for("windows-x86_64")->name.find(".zip") != std::string::npos &&
              !first->package_for("macos-x86_64") && first->checksums(),
        "Package per platform");

    // Release builds: newer versions only; the very same build is current.
    check(!find_update(build(BuildOrigin::Release, "0.0.1+gaaaaaaaaaaaa", 1792084000),
              UpdateChannel::Release,
              builds),
        "A release is up to date with itself");
    auto update = find_update(build(BuildOrigin::Release, "0.0.0+g999999999999", 1791000000),
        UpdateChannel::Release,
        builds);
    check(update && update->tag == "v0.0.1", "A release finds the newest release");
    update = find_update(build(BuildOrigin::Release, "0.0.1+gaaaaaaaaaaaa", 1792084000),
        UpdateChannel::Test,
        builds);
    check(update && update->tag == "test-pr5-dddddddddddd",
        "The test channel offers the newest test build");
    check(!find_update(
              build(BuildOrigin::Release, "0.0.0+g999999999999", 1791000000, 0, "macos-x86_64"),
              UpdateChannel::Release,
              builds),
        "No package for this platform, no update");

    // A test build follows its pull request while that has test builds.
    update = find_update(build(BuildOrigin::Test, "0.0.2+gffffffffffff", 1792150000, 3),
        UpdateChannel::Test,
        builds);
    check(!update, "A test build made after its pull request's newest build is current");
    update = find_update(build(BuildOrigin::Test, "0.0.2+gffffffffffff", 1792000000, 3),
        UpdateChannel::Test,
        builds);
    check(update && update->tag == "test-pr3-cccccccccccc",
        "A test build follows its own pull request");
    check(!find_update(build(BuildOrigin::Test, "0.0.2+gcccccccccccc", 1792140000, 3),
              UpdateChannel::Test,
              builds),
        "The newest build of the pull request is current");
    update = find_update(build(BuildOrigin::Test, "0.0.2+gffffffffffff", 1792140000, 2),
        UpdateChannel::Test,
        builds);
    check(update && update->tag == "test-pr5-dddddddddddd",
        "A closed pull request's build takes the newest test build");

    const BuildIdentity test3 = build(BuildOrigin::Test, "0.0.2+gffffffffffff", 1792000000, 3);
    const BuildIdentity release1 = build(BuildOrigin::Release, "0.0.1+gaaaaaaaaaaaa", 1792084000);
    update = find_update(test3, UpdateChannel::Test, builds);
    check(update && classify_offer(test3, *update) == OfferKind::Update,
        "A newer build of the own pull request updates");
    update = find_update(release1, UpdateChannel::Test, builds);
    check(update && classify_offer(release1, *update) == OfferKind::TestBuild,
        "A release switches to a test build");

    // Back from a test build: the newest release, also an older one.
    update = find_update(test3, UpdateChannel::Release, builds);
    check(update && update->tag == "v0.0.1" &&
              classify_offer(test3, *update) == OfferKind::BackToRelease &&
              !is_newer(test3, *update),
        "On releases a test build is offered the way back to the newest release");
    check(!find_update(build(BuildOrigin::Dev, "0.0.2+gffffffffffff", 1792140000),
              UpdateChannel::Release,
              builds),
        "Local builds are not offered older releases");
    check(find_update(
              build(BuildOrigin::Dev, "0.0.0+gffffffffffff", 0), UpdateChannel::Release, builds),
        "Local builds are offered higher versions");
    check(!find_update(
              build(BuildOrigin::Release, "not a version", 0), UpdateChannel::Release, builds),
        "An unknown own version offers nothing");

    // Merged and released: the release of the same version is the update.
    const auto released = parse_releases(platform::parse_json(
        "[" + release("v0.0.2", false, "2026-10-16T18:00:00Z", "0.0.2+gbbbbbbbbbbbb") + "]"));
    const BuildIdentity tested = build(BuildOrigin::Test, "0.0.2+gcccccccccccc", 1792140000, 3);
    update = find_update(tested, UpdateChannel::Test, released);
    check(update && update->tag == "v0.0.2" && classify_offer(tested, *update) == OfferKind::Update,
        "A test build moves on to the later release of its version");

    PackageAsset asset;
    asset.name = "beachlife-0.0.1+gaaaaaaaaaaaa-linux-x86_64.tar.gz";
    asset.digest = "sha256:" + std::string(64, 'A');
    check(expected_sha256(asset, {}) == std::string(64, 'a'), "GitHub's digest");
    asset.digest.clear();
    const std::string sums = std::string(64, 'b') +
                             "  beachlife-0.0.1+gaaaaaaaaaaaa-linux-x86_64.tar.gz\n" +
                             std::string(64, 'c') + " *other.zip\r\n";
    check(expected_sha256(asset, sums) == std::string(64, 'b'), "SHA256SUMS line");
    asset.name = "other.zip";
    check(expected_sha256(asset, sums) == std::string(64, 'c'), "SHA256SUMS binary-mode line");
    asset.name = "missing.zip";
    check(expected_sha256(asset, sums).empty(), "No checksum");

    check(describe_replacement(
              "0.0.1+gaaaaaaaaaaaa", build(BuildOrigin::Release, "0.0.2+gbbbbbbbbbbbb", 0)) ==
              "Updated from 0.0.1 to release 0.0.2.",
        "Update notice");
    check(describe_replacement(
              "0.0.2+gcccccccccccc", build(BuildOrigin::Test, "0.0.2+gdddddddddddd", 0, 3)) ==
              "Updated from 0.0.2 (cccccccc) to test build 0.0.2 (dddddddd).",
        "Same-version notice names the commits");
    check(describe_replacement(
              "0.0.3+gcccccccccccc", build(BuildOrigin::Release, "0.0.2+gbbbbbbbbbbbb", 0)) ==
              "Switched from 0.0.3 to release 0.0.2.",
        "Back-to-release notice");

    const auto identity =
        parse_build_identity(R"({"build_version":"0.0.1+ge8cc547c0177","channel":"test",
        "commit":"e8cc547c017785dd940fe3564b1c98e5e67f0851","commit_time":1792263357,"platform":"steamdeck",
        "architecture":"x86_64","pull_request":3,"run_id":39048027795})");
    check(identity.origin == BuildOrigin::Test && identity.pull_request == 3 &&
              identity.run == 39048027795 && identity.package == "steamdeck-x86_64" &&
              identity.repository == project_repository,
        "Build identity from the embedded build info");
    check(describe_build(identity) ==
              "Test build of pull request #3, commit e8cc547c0177 from 2026-10-17 18:55 UTC",
        "Build description");
}

fs::path fresh(const fs::path& root, const std::string& name)
{
    const fs::path path = root / name;
    fs::remove_all(path);
    return path;
}

void test_unpack(const fs::path& fixtures, const fs::path& work)
{
    const std::string linux_name = "beachlife-0.0.2+gbbbbbbbbbbbb-linux-x86_64";
    const fs::path linux_root =
        unpack_package(fixtures / (linux_name + ".tar.gz"), fresh(work, "unpack-linux"));
    check(linux_root.filename() == linux_name, "tar.gz packages unpack into their folder");
    const auto manifest = read_package_manifest(linux_root / package_manifest);
    check(manifest.package() == "linux-x86_64" && manifest.version == "0.0.2+gbbbbbbbbbbbb" &&
              manifest.files.size() == 5,
        "Unpacked manifest");
    check(read(linux_root / "docs/NEW.md") == "only in the new package\n", "Unpacked file");
    check(read(linux_root / ("LICENSES/dependencies/" + std::string(35, 'a') + "/" +
                                std::string(35, 'b') + "/LICENSE")) == "license text\n",
        "Long pax path");
#ifndef _WIN32
    check((fs::status(linux_root / "beachlife").permissions() & fs::perms::owner_exec) !=
                  fs::perms::none &&
              (fs::status(linux_root / "docs/NEW.md").permissions() & fs::perms::owner_exec) ==
                  fs::perms::none,
        "Manifest modes");
#endif
    const fs::path windows_root =
        unpack_package(fixtures / "beachlife-0.0.2+gbbbbbbbbbbbb-windows-x86_64.zip",
            fresh(work, "unpack-windows"));
    check(windows_root == work / "unpack-windows" &&
              read(windows_root / "assets/launcher/logo.png").size() == 9000,
        "zip packages unpack at their root");

    for (const char* bad : {"bad-traversal.tar.gz",
             "bad-symlink.tar.gz",
             "bad-hash.tar.gz",
             "bad-absolute.tar.gz",
             "bad-unlisted.zip",
             "bad-truncated.tar.gz"})
    {
        check(throws(
                  [&]
                  {
                      unpack_package(fixtures / bad, fresh(work, std::string("unpack-") + bad));
                  }),
            std::string("Refuses ") + bad);
    }
    check(!fs::exists(work / "evil") && !fs::exists(work.parent_path() / "evil") &&
              !fs::exists("/tmp/beachlife-absolute"),
        "Nothing escapes the unpack folder");
    check(throws(
              [&]
              {
                  unpack_package(fixtures / (linux_name + ".tar.gz"), work / "unpack-linux");
              }),
        "The unpack folder must be new");
    check(throws(
              [&]
              {
                  unpack_package(fixtures / (linux_name + ".tar.gz"),
                      fresh(work, "unpack-cancelled"),
                      []
                      {
                          return true;
                      });
              }),
        "Unpacking can be cancelled");
}

fs::path installed_copy(const fs::path& fixtures, const fs::path& work, const std::string& name)
{
    const fs::path base = fresh(work, name);
    fs::copy(fixtures / "installed", base, fs::copy_options::recursive);
    return base;
}

void test_install(const fs::path& fixtures, const fs::path& work)
{
    const std::string archive = "beachlife-0.0.2+gbbbbbbbbbbbb-linux-x86_64.tar.gz";
    const auto installed = tree(fixtures / "installed");

    // A complete update: package files replaced, other files untouched.
    {
        const fs::path base = installed_copy(fixtures, work, "install");
        check(install_blocker(base).empty() ||
                  install_blocker(base).rfind("Downloads are not available", 0) == 0,
            "A package folder can be updated");
        check(package_folder(base / "assets/launcher") == base && package_folder(base) == base &&
                  package_folder(base / "beachlife.app/Contents/Resources/") == base &&
                  package_folder(base / "beachlife.app/Contents/MacOS") == base,
            "The package folder is found, also from inside the macOS app");
        const fs::path root = unpack_package(fixtures / archive, update_folder(base) / "staged-1");
        install_staged_package(base, root, update_folder(base) / "previous-1");
        check(read(base / "beachlife") == "new launcher\n" &&
                  read(base / "docs/NEW.md") == "only in the new package\n" &&
                  !fs::exists(base / "docs/OLD.md"),
            "New files in place, removed files gone");
        check(read_package_manifest(base / package_manifest).version == "0.0.2+gbbbbbbbbbbbb",
            "New manifest in place");
        check(read(base / "my-notes.txt") == "kept by the player\n" &&
                  read(base / "screenshots/beach.png") == "screenshot",
            "Other files stay");
#ifndef _WIN32
        check((fs::status(base / "beachlife").permissions() & fs::perms::owner_exec) !=
                  fs::perms::none,
            "The launcher stays executable");
#endif
        check(
            !fs::exists(update_folder(base) / "replacing"), "No replacement marker after success");
        check(finish_updates(base) == "0.0.1+gaaaaaaaaaaaa",
            "The next start reports the replaced version");
        check(!fs::exists(update_folder(base)), "The next start removes the update folder");
        check(finish_updates(base).empty(), "Nothing to report afterwards");
    }

    // A failure half way moves everything back.
    {
        const fs::path base = installed_copy(fixtures, work, "rollback");
        const fs::path root = unpack_package(fixtures / archive, update_folder(base) / "staged-1");
        const auto staged = tree(root);
        int moves = 0;
        check(throws(
                  [&]
                  {
                      replace_package(base,
                          root,
                          update_folder(base) / "previous-1",
                          [&](const std::string&)
                          {
                              if (++moves == 4)
                              {
                                  throw std::runtime_error("disk full");
                              }
                          });
                  }),
            "A failing move fails the replacement");
        check(tree(base) == installed, "The installed files are back unchanged");
        check(tree(root) == staged, "The unpacked package is back in its folder");
        check(
            tree(update_folder(base) / "previous-1").empty(), "No previous files are left behind");
    }

    // An interrupted replacement (the launcher was killed) is completed at the next start.
    {
        const fs::path base = installed_copy(fixtures, work, "interrupted");
        const fs::path root = unpack_package(fixtures / archive, update_folder(base) / "staged-1");
        const fs::path previous = update_folder(base) / "previous-1";
        fs::create_directories(previous / "docs");
        fs::rename(base / "docs/OLD.md", previous / "docs/OLD.md");
        fs::rename(
            base / "beachlife", previous / "beachlife"); // moved aside, the new one not yet in
        std::ofstream(update_folder(base) / "replacing")
            << "staged-1/" << root.filename().generic_string() << "\nprevious-1\n";
        check(finish_updates(base) == "0.0.1+gaaaaaaaaaaaa", "The interrupted update is completed");
        check(read(base / "beachlife") == "new launcher\n" &&
                  read(base / "docs/NEW.md") == "only in the new package\n" &&
                  !fs::exists(base / "docs/OLD.md") &&
                  read(base / "my-notes.txt") == "kept by the player\n" &&
                  read_package_manifest(base / package_manifest).version == "0.0.2+gbbbbbbbbbbbb",
            "Completed files");
        check(!fs::exists(update_folder(base)), "Completed update cleaned up");
    }

    // The package must be for this folder's platform.
    {
        const fs::path base = installed_copy(fixtures, work, "mismatch");
        const fs::path root =
            unpack_package(fixtures / "beachlife-0.0.2+gbbbbbbbbbbbb-steamdeck-x86_64.tar.gz",
                update_folder(base) / "staged-1");
        check(throws(
                  [&]
                  {
                      replace_package(base, root, update_folder(base) / "previous-1");
                  }),
            "A package for another platform is refused");
        check(tree(base) == installed, "Nothing changed");
    }

    // Folders that were not installed from a package are not updated in place.
    const fs::path plain = fresh(work, "plain");
    fs::create_directories(plain);
    check(!install_blocker(plain).empty() && !install_blocker({}).empty(),
        "No manifest, no in-place update");
    check(finish_updates(plain).empty() && finish_updates({}).empty(), "Nothing to finish");
}
} // namespace

void test_notes()
{
    const auto blocks = parse_release_notes(
        "Intro **bold** and `code`,\r\nwrapped [link](https://x/y) ![img](a.png)<br>\n\n"
        "<!-- hidden -->## Changes ##\n- one\n  - nested\n3) three\n\n---\n```\nraw *text*\n```\n");
    check(blocks.size() == 7, "Notes blocks");
    if (blocks.size() == 7)
    {
        const auto& intro = blocks[0].spans;
        check(blocks[0].kind == NoteBlock::Kind::Paragraph && intro.size() == 7 &&
                  intro[1].style == NoteSpan::Style::Strong && intro[1].text == "bold" &&
                  intro[3].style == NoteSpan::Style::Code && intro[4].text == ", wrapped " &&
                  intro[5].style == NoteSpan::Style::Link && intro[5].text == "link" &&
                  intro[6].text == " ",
            "Notes paragraph spans, lines joined, images and tags dropped");
        check(blocks[1].kind == NoteBlock::Kind::Heading && blocks[1].level == 2 &&
                  blocks[1].spans[0].text == "Changes",
            "Notes heading after a comment");
        check(blocks[2].kind == NoteBlock::Kind::Item && blocks[2].level == 0 &&
                  blocks[3].level == 1 && blocks[4].marker == "3.",
            "Notes list items");
        check(blocks[5].kind == NoteBlock::Kind::Rule && blocks[6].kind == NoteBlock::Kind::Code &&
                  blocks[6].spans[0].text == "raw *text*",
            "Notes rule and code");
    }
    const auto pull = parse_pull_request(platform::parse_json(
        R"({"number":3,"title":"Promote","body":null,"html_url":"https://github.com/o/r/pull/3"})"));
    check(pull.number == 3 && pull.title == "Promote" && pull.body.empty() &&
              pull_request_api_url("o/r", 3) == "https://api.github.com/repos/o/r/pulls/3",
        "Pull request description");
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr
            << "Usage: launcher_update_tests FIXTURE_DIR (see tests/launcher_update_fixture.py)\n";
        return 2;
    }
    const fs::path fixtures = fs::absolute(argv[1]);
    const fs::path work = fixtures / "work";
    fs::create_directories(work);
    try
    {
        test_json();
        test_sha256();
        test_versions();
        test_releases();
        test_notes();
        test_unpack(fixtures, work);
        test_install(fixtures, work);
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED with an exception: " << error.what() << '\n';
        return 1;
    }
    if (failures)
    {
        std::cerr << failures << " launcher update check(s) failed\n";
        return 1;
    }
    std::cout << "Launcher update checks passed.\n";
    return 0;
}
