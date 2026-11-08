#include "content_prepare.h"
#include "clickteam_patch.h"
#include "game_disc.h"
#include "game_index.h"
#include "game_path.h"
#include "local_files.h"
#include "platform/path.h"
#include "platform/sha256.h"

extern "C"
{
#include <md5/global.h>
#include <md5/md5.h>
}

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <optional>

namespace beachlife::data
{
namespace
{
void cancel(const PrepareOptions& options)
{
    if (options.cancelled && options.cancelled())
    {
        throw PreparationCancelled();
    }
}
void report(const PrepareOptions& options, const PrepareProgress& progress)
{
    cancel(options);
    if (options.progress)
    {
        options.progress(progress);
    }
    cancel(options);
}

std::string fingerprint(
    const std::filesystem::path& image, const PrepareOptions& options, const std::string& phase)
{
    PrepareProgress progress{phase, {}, 0, std::filesystem::file_size(image), 0, 0};
    std::ifstream input(image, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Cannot open the source ISO for fingerprinting.");
    }
    platform::Sha256 hash;
    std::array<char, 65536> buffer{};
    report(options, progress);
    while (input.read(buffer.data(), buffer.size()) || input.gcount())
    {
        const auto count = static_cast<std::uint64_t>(input.gcount());
        if (count > progress.total_bytes - progress.completed_bytes)
        {
            throw std::runtime_error("The ISO changed during fingerprinting.");
        }
        hash.update(buffer.data(), static_cast<std::size_t>(count));
        progress.completed_bytes += count;
        report(options, progress);
    }
    if (!input.eof() || progress.completed_bytes != progress.total_bytes)
    {
        throw std::runtime_error("The ISO changed or could not be read completely.");
    }
    return hash.finish_hex();
}

void write_text(StagedFile& file, std::string_view text)
{
    file.write({reinterpret_cast<const unsigned char*>(text.data()), text.size()});
}

void claim_directory(const std::filesystem::path& root, const std::string& identity)
{
    const auto marker = "beachlife-content\n" + identity + "\n";
    if (const auto size = content_file_size(root, ".owner"))
    {
        if (*size != marker.size())
        {
            throw std::runtime_error("Unrecognized content directory owner marker.");
        }
        auto file = open_local_file(root, ".owner", *size);
        std::string text(marker.size(), '\0');
        if (file->read({reinterpret_cast<unsigned char*>(text.data()), text.size()}) !=
                text.size() ||
            text != marker)
        {
            throw std::runtime_error("Unrecognized content directory owner marker.");
        }
    }
    else
    {
        for (const auto& entry : std::filesystem::directory_iterator(root))
        {
            if (entry.path().filename() != ".lock" && entry.path().filename() != ".owner.part")
            {
                throw std::runtime_error(
                    "Refusing to adopt a nonempty, unmanaged content directory.");
            }
        }
        StagedFile file(root, ".owner.part");
        write_text(file, marker);
        file.commit(root / ".owner");
    }
}

struct Planned
{
    const GameAsset* asset = nullptr; // null for a file the patch adds
    const ClickteamPatch::Entry* patch = nullptr;
    std::string path;
    std::uint64_t size = 0;
};

// The 1.1 patch replaces data files and adds a few. The disc's executable is
// kept: the runtime reads the disc's own module image, and the patch would
// turn it into the copy-protected retail 1.1 build. Entries for files outside
// Data (readmes) are ignored.
std::vector<Planned> plan_files(const GameIndex& game, const ClickteamPatch* patch)
{
    // Each official patch updates the text of every language it serves (the UK
    // patch text_uk, the European and Polish ones the five European texts), so
    // a patch without the selected language's text belongs to another edition.
    constexpr std::array<std::pair<std::string_view, std::string_view>, 6> texts{{
        {"en", "data/text_uk.ble"},
        {"fr", "data/text_french.ble"},
        {"it", "data/text_italian.ble"},
        {"de", "data/text_german.ble"},
        {"es", "data/text_spanish.ble"},
        {"pl", "data/text_polish.ble"},
    }};
    std::map<std::string, const ClickteamPatch::Entry*> entries;
    static const std::vector<ClickteamPatch::Entry> none;
    for (const auto& entry : patch ? patch->entries() : none)
    {
        const bool ascii = std::all_of(entry.path.begin(),
            entry.path.end(),
            [](unsigned char c)
            {
                return c < 128;
            });
        const auto key = ascii ? normalize_game_path(entry.path).key : std::string{};
        if (!key.starts_with("data/"))
        {
            continue;
        }
        if (!entry.supported)
        {
            throw std::runtime_error("The 1.1 patch uses an unsupported operation.");
        }
        if (!entries.emplace(key, &entry).second)
        {
            throw std::runtime_error("The 1.1 patch names one file twice.");
        }
    }
    std::vector<Planned> plan;
    for (const auto& asset : game.files)
    {
        const auto found = entries.find(normalize_game_path(asset.path).key);
        const ClickteamPatch::Entry* entry = nullptr;
        if (found != entries.end())
        {
            entry = found->second;
            entries.erase(found);
        }
        plan.push_back({&asset, entry, asset.path, entry ? entry->size : asset.size});
    }
    if (patch)
    {
        const auto text = std::find_if(texts.begin(),
            texts.end(),
            [&](const auto& item)
            {
                return item.first == game.language;
            });
        if (text == texts.end() ||
            std::none_of(plan.begin(),
                plan.end(),
                [&](const Planned& file)
                {
                    return file.patch && normalize_game_path(file.path).key == text->second;
                }))
        {
            throw std::runtime_error(
                "This 1.1 patch is for another edition: it does not update this language.");
        }
    }
    // Patches of files this disc does not install are skipped.
    for (const auto& [key, entry] : entries)
    {
        if (entry->added)
        {
            plan.push_back({nullptr, entry, normalize_game_path(entry->path).display, entry->size});
        }
    }
    return plan;
}

// The original MD5 establishes that a surviving entry belongs to this source;
// the new manifest records SHA-256 for subsequent VFS integrity checks.
std::string reusable_file(const std::filesystem::path& root,
    const GameAsset& asset,
    const std::string& expected_md5,
    const PrepareOptions& options,
    PrepareProgress& progress)
{
    const auto size = content_file_size(root, asset.path);
    if (!size || *size != asset.size)
    {
        return {};
    }
    auto file = open_local_file(root, asset.path, asset.size);
    MD5_CTX md5{};
    MD5Init(&md5);
    platform::Sha256 sha;
    std::array<unsigned char, 65536> buffer{};
    while (const auto count = file->read(buffer))
    {
        MD5Update(&md5, buffer.data(), static_cast<unsigned>(count));
        sha.update(buffer.data(), count);
        progress.completed_bytes += count;
        report(options, progress);
    }
    std::array<unsigned char, 16> digest{};
    MD5Final(digest.data(), &md5);
    constexpr char hex[] = "0123456789abcdef";
    std::string text;
    for (const auto byte : digest)
    {
        text += hex[byte >> 4];
        text += hex[byte & 15];
    }
    return text == expected_md5 ? sha.finish_hex() : std::string{};
}
} // namespace

void copy_loose_file(const IsoSource& source,
    const GameAsset& asset,
    const std::function<void(std::span<const unsigned char>)>& sink)
{
    auto file = source.open(asset.source_path);
    if (file->size() != asset.size)
    {
        throw std::runtime_error("A loose game file changed size.");
    }
    std::array<unsigned char, 65536> buffer{};
    std::uint64_t done = 0;
    while (const auto received = file->read(buffer))
    {
        done += received;
        sink(std::span(buffer).first(received));
    }
    if (done != asset.size)
    {
        throw std::runtime_error("A loose game file is truncated.");
    }
}

PrepareResult prepare_content(const std::filesystem::path& image,
    const std::filesystem::path& cache,
    const PrepareOptions& options)
{
    PrepareResult result;
    result.manifest.source_sha256 = fingerprint(image, options, "fingerprint");
    const IsoSource source(image);
    GameDisc disc(source);
    const auto game = disc.map(options.language);
    std::optional<ClickteamPatch> patch;
    if (!options.patch.empty())
    {
        patch.emplace(options.patch);
        result.manifest.patch_sha256 = patch->sha256();
    }
    const auto plan = plan_files(game, patch ? &*patch : nullptr);
    result.manifest.profile = game.profile;
    result.manifest.language = game.language;
    for (const auto& file : plan)
    {
        result.manifest.total_bytes += file.size;
    }
    if (result.manifest.total_bytes > max_content_bytes)
    {
        throw std::runtime_error("The game content exceeds the supported size limit.");
    }
    cancel(options);

    std::filesystem::create_directories(cache);
    const auto base = canonical_content_root(cache);
    const auto identity = result.manifest.identity();
    ensure_content_directory(base, identity);
    result.root = base / identity;
    ContentLock lock(result.root);
    claim_directory(result.root, identity);
    ensure_content_directory(result.root, ".stage");
    ensure_content_directory(result.root, "files");
    // Reject an unsafe readiness marker even when all data is reusable.
    content_file_size(result.root, "manifest.json");

    const auto total = result.manifest.total_bytes;
    PrepareProgress progress{"checking", {}, 0, total, 0, plan.size()};
    std::uint64_t needed = 0;
    std::vector<std::size_t> pending;
    report(options, progress);
    for (std::size_t i = 0; i < plan.size(); ++i)
    {
        const auto& file = plan[i];
        progress.path = file.path;
        const auto before = progress.completed_bytes;
        // Loose, Wise and patched files have no installer MD5; they are made again.
        const auto md5 = file.patch ? std::string{} : disc.expected_md5(*file.asset);
        auto sha = md5.empty()
                       ? std::string{}
                       : reusable_file(result.root / "files", *file.asset, md5, options, progress);
        progress.completed_bytes = before + file.size;
        ++progress.completed_files;
        if (sha.empty())
        {
            // Remove readiness as soon as a damaged/missing asset is known.
            // A failed repair leaves verified entries available for resumption.
            remove_content_file(result.root / "manifest.json");
            pending.push_back(i);
            needed += file.size;
        }
        else
        {
            ++result.files_reused;
        }
        result.manifest.files.push_back({{file.path, file.size}, std::move(sha)});
        report(options, progress);
    }
    constexpr std::uint64_t headroom = 32 * 1024 * 1024;
    if (std::filesystem::space(result.root).available < needed + headroom)
    {
        throw std::runtime_error("Not enough free space to prepare game content.");
    }

    progress = {"extracting", {}, 0, needed, 0, pending.size()};
    report(options, progress);
    for (const auto i : pending)
    {
        const auto& file = plan[i];
        progress.path = file.path;
        const auto destination = result.root / "files" / platform::utf8_path(file.path);
        const auto parent = platform::utf8_path(file.path).parent_path();
        if (!parent.empty())
        {
            ensure_content_directory(result.root / "files", platform::utf8(parent));
        }
        StagedFile staged(result.root / ".stage", std::to_string(i) + ".part");
        platform::Sha256 hash;
        const auto sink = [&](std::span<const unsigned char> bytes)
        {
            cancel(options);
            staged.write(bytes);
            hash.update(bytes.data(), bytes.size());
            progress.completed_bytes += bytes.size();
            report(options, progress);
        };
        if (!file.patch)
        {
            disc.extract(*file.asset, sink);
        }
        else if (file.patch->added)
        {
            sink(patch->added(*file.patch));
        }
        else
        {
            constexpr std::uint64_t max_patched_bytes = 256 * 1024 * 1024;
            if (file.asset->size > max_patched_bytes)
            {
                throw std::runtime_error("A file the 1.1 patch changes is too large.");
            }
            std::vector<unsigned char> original;
            original.reserve(static_cast<std::size_t>(file.asset->size));
            disc.extract(*file.asset,
                [&](std::span<const unsigned char> bytes)
                {
                    cancel(options);
                    original.insert(original.end(), bytes.begin(), bytes.end());
                });
            const auto patched = patch->apply(*file.patch, original);
            if (!patched)
            {
                throw std::runtime_error(
                    "This 1.1 patch does not fit " + file.path + " on this disc.");
            }
            sink(*patched);
        }
        cancel(options);
        staged.commit(destination);
        result.manifest.files[i].sha256 = hash.finish_hex();
        ++result.files_written;
        ++progress.completed_files;
        report(options, progress);
    }
    if (fingerprint(image, options, "confirming") != result.manifest.source_sha256)
    {
        throw std::runtime_error("The ISO changed during preparation; select it again.");
    }
    const auto manifest = content_manifest_json(result.manifest);
    cancel(options);
    StagedFile staged(result.root / ".stage", "manifest.part");
    write_text(staged, manifest);
    staged.commit(result.root / "manifest.json");
    // Publication is the commit point: cancellation after it is a ready result.
    if (options.progress)
    {
        options.progress({"ready", {}, total, total, plan.size(), plan.size()});
    }
    return result;
}
} // namespace beachlife::data
