#include "data/content_prepare.h"
#include "launcher/content_service.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace
{
namespace data = beachlife::data;
namespace launcher = beachlife::launcher;
namespace fs = std::filesystem;
using State = launcher::ContentService::State;
unsigned int checks = 0;

void expect(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

template <class F> void rejects(F action, const char* message)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    expect(rejected, message);
}

void read_executable(data::SourceFile& file)
{
    constexpr std::array<unsigned char, 4> expected{'t', 'e', 's', 't'};
    std::array<unsigned char, 4> actual{};
    expect(file.size() == expected.size(), "Cached executable size differs.");
    expect(file.read(actual) == actual.size() && actual == expected,
        "Cached executable bytes differ.");
    unsigned char extra = 0;
    expect(file.read(std::span(&extra, 1)) == 0, "Cached executable has unexpected trailing data.");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        return 2;
    }
    try
    {
        const auto image = fs::absolute(argv[1]).lexically_normal();
        const fs::path cache(argv[2]);
        const std::string identity(argv[3]);
        const auto root = cache / identity;
        const auto offline = image.parent_path() / "disconnected.iso";
        launcher::Settings settings;
        settings.source = launcher::path_utf8(image);
        settings.content = {identity,
            settings.source,
            fs::file_size(image),
            static_cast<std::int64_t>(fs::last_write_time(image).time_since_epoch().count())};

        // The Python fixture supplies the former five-file format1 cache for
        // this exact ISO, including its real marker and existing file hashes.
        std::map<fs::path, fs::file_time_type> retained;
        for (const auto& entry : fs::recursive_directory_iterator(root / "files"))
        {
            if (entry.is_regular_file())
            {
                retained.emplace(entry.path(), entry.last_write_time());
            }
        }
        expect(retained.size() == 5, "Legacy fixture does not contain the five original assets.");
        const auto check_retained = [&]
        {
            for (const auto& [path, time] : retained)
            {
                expect(fs::last_write_time(path) == time, "Repair rewrote an unchanged asset.");
            }
        };
        auto old = launcher::open_content(settings, cache, false);
        expect(old.status.state == State::NeedsPreparation && !old.source &&
                   old.status.error.find("requires BeachLife.exe") != std::string::npos,
            "Old cache was advertised as complete before executable preparation.");
        rejects(
            [&]
            {
                data::PreparedSource source(root);
            },
            "Old incomplete cache mounted.");

        fs::rename(image, offline);
        old = launcher::open_content(settings, cache, false);
        expect(old.status.state == State::Failed && !old.source &&
                   old.status.error.find("requires BeachLife.exe") != std::string::npos,
            "Offline old cache did not report the required repair.");
        check_retained();
        fs::rename(offline, image);

        std::set<std::string> extracted;
        data::PrepareOptions options;
        options.progress = [&](const data::PrepareProgress& progress)
        {
            if (progress.phase == "extracting" && !progress.path.empty())
            {
                extracted.insert(progress.path);
            }
        };
        auto repaired = launcher::open_content(settings, cache, true, options);
        expect(repaired.status.state == State::Ready && repaired.source &&
                   repaired.status.binding.identity == identity && repaired.status.files == 6,
            "Repair did not retain the cache identity and publish the complete set.");
        expect(extracted == std::set<std::string>{"BeachLife.exe"},
            "Repair extracted something other than the missing executable.");
        check_retained();
        settings.content = repaired.status.binding;
        repaired.source->verify();
        {
            auto file = repaired.source->open("BEACHLIFE.EXE");
            read_executable(*file);
        }
        rejects(
            [&]
            {
                repaired.source->open("Setup.exe");
            },
            "An unrelated executable was cached.");
        repaired.source.reset();

        // Same-size damage uses the unchanged descriptor checksum and staging
        // route. Mount still checks size; verify and the credits loader hash
        // are the corresponding content checks, not execute/load operations.
        {
            std::ofstream damaged(root / "files/BeachLife.exe", std::ios::binary | std::ios::trunc);
            damaged << "fail";
            expect(static_cast<bool>(damaged), "Could not create generated checksum damage.");
        }
        rejects(
            [&]
            {
                data::PreparedSource(root).verify();
            },
            "Same-size executable damage passed verify.");
        extracted.clear();
        repaired = launcher::open_content(settings, cache, true, options);
        expect(extracted == std::set<std::string>{"BeachLife.exe"} && repaired.source,
            "Checksum repair did not replace only the damaged executable.");
        check_retained();
        repaired.source->verify();
        repaired.source.reset();

        // A fresh mount and stream have no live ISO dependency. The stream also
        // retains the actual prepared-source lock after its mount is released.
        fs::rename(image, offline);
        expect(!fs::exists(image), "Generated ISO was not disconnected.");
        auto reopened = launcher::open_content(settings, cache, false);
        expect(reopened.status.state == State::Ready && reopened.source,
            "Repaired cache did not reopen offline.");
        auto stream = reopened.source->open("beachlife.exe");
        reopened.source.reset();
        read_executable(*stream);
        stream.reset();
        {
            data::PreparedSource independent(root);
            auto file = independent.open("BeachLife.exe");
            read_executable(*file);
            auto ini = independent.open("BeachLife.ini");
            expect(ini->size() == 27, "INI contents were not retained.");
            independent.verify();
        }
        check_retained();
        expect(fs::is_empty(root / ".stage"), "Staging data survived successful publication.");
        std::cout << "Retail constants cache: one group, two repairs, offline reopen; " << checks
                  << " checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Retail constants cache check failed: " << error.what() << '\n';
        return 2;
    }
}
