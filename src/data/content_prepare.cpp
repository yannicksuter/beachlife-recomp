#include "content_prepare.h"
#include "game_index.h"
#include "local_files.h"
#include "platform/path.h"
#include "platform/sha256.h"

extern "C" {
#include <md5/global.h>
#include <md5/md5.h>
}

#include <array>
#include <fstream>

namespace beachlife::data {
namespace {
void cancel(const PrepareOptions& options) {
    if (options.cancelled && options.cancelled()) throw PreparationCancelled();
}
void report(const PrepareOptions& options, const PrepareProgress& progress) {
    cancel(options);
    if (options.progress) options.progress(progress);
    cancel(options);
}

std::string fingerprint(const std::filesystem::path& image, const PrepareOptions& options,
                        const std::string& phase) {
    PrepareProgress progress{phase, {}, 0, std::filesystem::file_size(image), 0, 0};
    std::ifstream input(image, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open the source ISO for fingerprinting.");
    platform::Sha256 hash;
    std::array<char, 65536> buffer{};
    report(options, progress);
    while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
        const auto count = static_cast<std::uint64_t>(input.gcount());
        if (count > progress.total_bytes-progress.completed_bytes)
            throw std::runtime_error("The ISO changed during fingerprinting.");
        hash.update(buffer.data(), static_cast<std::size_t>(count));
        progress.completed_bytes += count;
        report(options, progress);
    }
    if (!input.eof() || progress.completed_bytes != progress.total_bytes)
        throw std::runtime_error("The ISO changed or could not be read completely.");
    return hash.finish_hex();
}

void write_text(StagedFile& file, std::string_view text) {
    file.write({reinterpret_cast<const unsigned char*>(text.data()), text.size()});
}

void claim_directory(const std::filesystem::path& root, const std::string& identity) {
    const auto marker = "beachlife-content\n"+identity+"\n";
    if (const auto size = content_file_size(root, ".owner")) {
        if (*size != marker.size()) throw std::runtime_error("Unrecognized content directory owner marker.");
        auto file = open_local_file(root, ".owner", *size);
        std::string text(marker.size(), '\0');
        if (file->read({reinterpret_cast<unsigned char*>(text.data()), text.size()}) != text.size() || text != marker)
            throw std::runtime_error("Unrecognized content directory owner marker.");
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(root))
            if (entry.path().filename() != ".lock" && entry.path().filename() != ".owner.part")
                throw std::runtime_error("Refusing to adopt a nonempty, unmanaged content directory.");
        StagedFile file(root, ".owner.part");
        write_text(file, marker);
        file.commit(root/".owner");
    }
}

// The original MD5 establishes that a surviving entry belongs to this source;
// the new manifest records SHA-256 for subsequent VFS integrity checks.
std::string reusable_file(const std::filesystem::path& root, const GameAsset& asset,
                          const std::string& expected_md5, const PrepareOptions& options,
                          PrepareProgress& progress) {
    const auto size = content_file_size(root, asset.path);
    if (!size || *size != asset.size) return {};
    auto file = open_local_file(root, asset.path, asset.size);
    MD5_CTX md5{}; MD5Init(&md5);
    platform::Sha256 sha;
    std::array<unsigned char, 65536> buffer{};
    while (const auto count = file->read(buffer)) {
        MD5Update(&md5, buffer.data(), static_cast<unsigned>(count));
        sha.update(buffer.data(), count);
        progress.completed_bytes += count;
        report(options, progress);
    }
    std::array<unsigned char, 16> digest{}; MD5Final(digest.data(), &md5);
    constexpr char hex[] = "0123456789abcdef";
    std::string text;
    for (const auto byte : digest) { text += hex[byte >> 4]; text += hex[byte & 15]; }
    return text == expected_md5 ? sha.finish_hex() : std::string{};
}
} // namespace

PrepareResult prepare_content(const std::filesystem::path& image, const std::filesystem::path& cache,
                              const PrepareOptions& options) {
    PrepareResult result;
    result.manifest.source_sha256 = fingerprint(image, options, "fingerprint");
    const IsoSource source(image);
    CabinetReader cabinet(source);
    const auto game = map_game_files(cabinet.index(), source.index());
    result.manifest.profile = game.profile;
    result.manifest.language = game.language;
    result.manifest.total_bytes = game.total_bytes;
    if (game.total_bytes > max_content_bytes) throw std::runtime_error("The game content exceeds the supported size limit.");
    cancel(options);

    std::filesystem::create_directories(cache);
    const auto base = canonical_content_root(cache);
    const auto identity = result.manifest.identity();
    ensure_content_directory(base, identity);
    result.root = base/identity;
    ContentLock lock(result.root);
    claim_directory(result.root, identity);
    ensure_content_directory(result.root, ".stage");
    ensure_content_directory(result.root, "files");
    // Reject an unsafe readiness marker even when all data is reusable.
    content_file_size(result.root, "manifest.json");

    PrepareProgress progress{"checking", {}, 0, game.total_bytes, 0, game.files.size()};
    std::uint64_t needed = 0;
    std::vector<std::size_t> pending;
    report(options, progress);
    for (std::size_t i = 0; i < game.files.size(); ++i) {
        const auto& asset = game.files[i];
        progress.path = asset.path;
        const auto before = progress.completed_bytes;
        auto sha = reusable_file(result.root/"files", asset, cabinet.index().files[asset.cabinet_index].md5,
                                 options, progress);
        progress.completed_bytes = before+asset.size;
        ++progress.completed_files;
        if (sha.empty()) {
            // Remove readiness as soon as a damaged/missing asset is known.
            // A failed repair leaves verified entries available for resumption.
            remove_content_file(result.root/"manifest.json");
            pending.push_back(i);
            needed += asset.size;
        } else ++result.files_reused;
        result.manifest.files.push_back({{asset.path, asset.size}, std::move(sha)});
        report(options, progress);
    }
    constexpr std::uint64_t headroom = 32*1024*1024;
    if (std::filesystem::space(result.root).available < needed+headroom)
        throw std::runtime_error("Not enough free space to prepare game content.");

    progress = {"extracting", {}, 0, needed, 0, pending.size()};
    report(options, progress);
    for (const auto i : pending) {
        const auto& asset = game.files[i];
        progress.path = asset.path;
        const auto destination = result.root/"files"/platform::utf8_path(asset.path);
        const auto parent = platform::utf8_path(asset.path).parent_path();
        if (!parent.empty()) ensure_content_directory(result.root/"files", platform::utf8(parent));
        StagedFile staged(result.root/".stage", std::to_string(i)+".part");
        platform::Sha256 hash;
        cabinet.extract(asset.cabinet_index, [&](std::span<const unsigned char> bytes) {
            cancel(options);
            staged.write(bytes);
            hash.update(bytes.data(), bytes.size());
            progress.completed_bytes += bytes.size();
            report(options, progress);
        });
        cancel(options);
        staged.commit(destination);
        result.manifest.files[i].sha256 = hash.finish_hex();
        ++result.files_written;
        ++progress.completed_files;
        report(options, progress);
    }
    if (fingerprint(image, options, "confirming") != result.manifest.source_sha256)
        throw std::runtime_error("The ISO changed during preparation; select it again.");
    const auto manifest = content_manifest_json(result.manifest);
    cancel(options);
    StagedFile staged(result.root/".stage", "manifest.part");
    write_text(staged, manifest);
    staged.commit(result.root/"manifest.json");
    // Publication is the commit point: cancellation after it is a ready result.
    if (options.progress) options.progress({"ready", {}, game.total_bytes, game.total_bytes,
                                           game.files.size(), game.files.size()});
    return result;
}
} // namespace beachlife::data
