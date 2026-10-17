#include "data/content_prepare.h"

#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <source_location>
#include <thread>

using namespace beachlife::data;
void expect(bool value, const std::source_location& location = std::source_location::current())
{
    if (!value)
    {
        throw std::runtime_error(
            "Preparation API check failed at line " + std::to_string(location.line()));
    }
}
template <class F> void rejects(F fn)
{
    bool failed = false;
    try
    {
        fn();
    }
    catch (const std::exception&)
    {
        failed = true;
    }
    expect(failed);
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--platform")
        {
#ifdef _WIN32
            std::cout << "windows\n";
#else
            std::cout << "posix\n";
#endif
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "hold")
        {
            PreparedSource source(argv[2]);
            std::cout << "locked" << std::endl;
            std::this_thread::sleep_for(std::chrono::minutes(2));
            return 0;
        }
        if (argc != 3)
        {
            return 2;
        }
        const std::filesystem::path image(argv[1]), cache(argv[2]);
        bool cancelled = false;
        PrepareOptions options;
        options.cancelled = [&]
        {
            return cancelled;
        };
        options.progress = [&](const auto& progress)
        {
            if (progress.phase == "extracting" && progress.completed_files == 1)
            {
                cancelled = true;
            }
        };
        try
        {
            prepare_content(image, cache, options);
            expect(false);
        }
        catch (const PreparationCancelled&)
        {
        }
        const auto root = std::filesystem::directory_iterator(cache)->path();
        expect(!std::filesystem::exists(root / "manifest.json"));
        rejects(
            [&]
            {
                PreparedSource source(root);
            });
        const auto ini = root / "files/BeachLife.ini";
        const auto modified = std::filesystem::last_write_time(ini);

        // An empty asset commits, then interrupt within the next nonempty asset.
        cancelled = false;
        options.progress = [&](const auto& progress)
        {
            if (progress.phase == "extracting" && progress.completed_bytes)
            {
                cancelled = true;
            }
        };
        try
        {
            prepare_content(image, cache, options);
            expect(false);
        }
        catch (const PreparationCancelled&)
        {
        }
        expect(std::filesystem::exists(root / "files/Data/empty.bin"));
        expect(!std::filesystem::exists(root / "files/Data/loose.txt"));
        expect(std::filesystem::is_empty(root / ".stage"));
        const auto result = prepare_content(image, cache);
        expect(result.files_reused == 2 && result.files_written == 3);
        expect(std::filesystem::last_write_time(ini) == modified);

        std::unique_ptr<SourceFile> surviving;
        {
            PreparedSource source(root), second(root);
            source.verify();
            second.verify();
            surviving = source.open("beachlife.INI");
            rejects(
                [&]
                {
                    prepare_content(image, cache);
                });
        }
        rejects(
            [&]
            {
                prepare_content(image, cache);
            }); // open streams retain the lock
        std::array<unsigned char, 8> bytes{};
        expect(surviving->read(bytes) == bytes.size());
        surviving.reset();
        expect(prepare_content(image, cache).files_reused == 5);

        // A source changed between extraction and final publication stays incomplete.
        const auto changing_cache = cache.parent_path() / "changing-source";
        char last = 0;
        {
            std::ifstream in(image, std::ios::binary);
            in.seekg(-1, std::ios::end);
            in.get(last);
        }
        bool changed = false;
        options = {};
        options.progress = [&](const auto& progress)
        {
            if (progress.phase == "confirming" && !changed)
            {
                std::fstream out(image, std::ios::binary | std::ios::in | std::ios::out);
                out.seekp(-1, std::ios::end);
                out.put(static_cast<char>(last ^ 1));
                changed = true;
            }
        };
        rejects(
            [&]
            {
                prepare_content(image, changing_cache, options);
            });
        expect(changed);
        const auto changing_root = std::filesystem::directory_iterator(changing_cache)->path();
        expect(!std::filesystem::exists(changing_root / "manifest.json"));
        {
            std::fstream out(image, std::ios::binary | std::ios::in | std::ios::out);
            out.seekp(-1, std::ios::end);
            out.put(last);
        }
        expect(prepare_content(image, changing_cache).files_reused == 5);

        // Write/publication failure leaves the original destination untouched.
        const auto scratch = cache.parent_path() / "staging";
        std::filesystem::create_directory(scratch);
        std::filesystem::create_directory(scratch / "directory");
        {
            StagedFile staged(scratch, "output.part");
            staged.write(bytes);
            rejects(
                [&]
                {
                    staged.commit(scratch / "directory");
                });
            expect(std::filesystem::is_directory(scratch / "directory"));
        }
        expect(!std::filesystem::exists(scratch / "output.part"));
        std::cout << "Cancellation, resumption, lifetime locks, source changes and failed "
                     "publication passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
