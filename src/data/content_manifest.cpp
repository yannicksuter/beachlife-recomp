#include "content_manifest.h"
#include "game_path.h"
#include "local_files.h"
#include "platform/json.h"
#include "platform/sha256.h"

#include <algorithm>
#include <array>
#include <set>
#include <sstream>
#include <stdexcept>

namespace beachlife::data
{
namespace
{
class PreparedFile final : public SourceFile
{
  public:
    PreparedFile(std::shared_ptr<ContentLock> lock, std::unique_ptr<SourceFile> file)
        : lock_(std::move(lock))
        , file_(std::move(file))
    {
    }
    std::uint64_t size() const override
    {
        return file_->size();
    }
    std::uint64_t tell() const override
    {
        return file_->tell();
    }
    void seek(std::uint64_t position) override
    {
        file_->seek(position);
    }
    std::size_t read(std::span<unsigned char> bytes) override
    {
        return file_->read(bytes);
    }

  private:
    // Open streams keep the shared lock even after their mount is destroyed.
    std::shared_ptr<ContentLock> lock_;
    std::unique_ptr<SourceFile> file_;
};

bool digest(std::string_view text)
{
    return text.size() == 64 &&
           text.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

void fields(const platform::JsonValue& object, std::initializer_list<const char*> expected)
{
    std::set<std::string> wanted(expected.begin(), expected.end());
    if (!object.is_object() || object.size() != wanted.size())
    {
        throw std::runtime_error("Invalid content manifest object.");
    }
    for (const auto& name : object.names())
    {
        if (!wanted.erase(name))
        {
            throw std::runtime_error("Duplicate or unexpected content manifest field.");
        }
    }
}

std::uint64_t number(const platform::JsonValue& value)
{
    const auto integer = value.integer();
    if (!integer || *integer < 0)
    {
        throw std::runtime_error("Invalid content manifest integer.");
    }
    return static_cast<std::uint64_t>(*integer);
}

std::string string(const platform::JsonValue& value)
{
    if (!value.is_string())
    {
        throw std::runtime_error("Invalid content manifest string.");
    }
    return value.text();
}

void validate(const ContentManifest& manifest)
{
    if (!digest(manifest.source_sha256) || manifest.profile != "uk-english-v1" ||
        manifest.language != "en")
    {
        throw std::runtime_error("Unsupported content manifest identity/profile.");
    }
    if (manifest.files.empty() || manifest.files.size() > 100000 ||
        manifest.total_bytes > max_content_bytes)
    {
        throw std::runtime_error("Unsupported content manifest size.");
    }
    std::uint64_t total = 0;
    std::size_t encoded_size = 512; // identity fields plus conservative per-entry JSON overhead
    std::set<std::string> paths;
    bool ini = false, data = false, executable = false;
    for (const auto& file : manifest.files)
    {
        if (file.path.size() + 192 > max_content_manifest_bytes - encoded_size)
        {
            throw std::runtime_error("Content manifest exceeds the supported size limit.");
        }
        encoded_size += file.path.size() + 192;
        const auto path = normalize_game_path(file.path);
        if (path.display != file.path || !paths.insert(path.key).second ||
            std::any_of(file.path.begin(),
                file.path.end(),
                [](unsigned char c)
                {
                    return c >= 128;
                }) ||
            !digest(file.sha256) || file.size > 0x7fffffff || file.size > max_content_bytes - total)
        {
            throw std::runtime_error("Invalid or ambiguous prepared file record.");
        }
        if (path.key == "beachlife.ini")
        {
            ini = true;
        }
        else if (path.key == "beachlife.exe")
        {
            executable = true;
        }
        else if (path.key.starts_with("data/"))
        {
            data = true;
        }
        else
        {
            throw std::runtime_error("Manifest contains a non-game file.");
        }
        total += file.size;
    }
    for (const auto& path : paths)
    {
        for (auto slash = path.find('/'); slash != std::string::npos;
            slash = path.find('/', slash + 1))
        {
            if (paths.contains(path.substr(0, slash)))
            {
                throw std::runtime_error("Manifest file/directory conflict.");
            }
        }
    }
    // Keep the identity/schema stable: repair can reuse the old verified assets
    // and add this one file before atomically replacing the old manifest.
    if (!executable)
    {
        throw std::runtime_error("Prepared game data requires BeachLife.exe; prepare or repair it "
                                 "from the original ISO.");
    }
    if (!ini || !data || total != manifest.total_bytes)
    {
        throw std::runtime_error("Incomplete content manifest or incorrect total size.");
    }
}
} // namespace

std::string ContentManifest::identity() const
{
    return platform::sha256_hex("beachlife-content-" + std::to_string(content_format) + "\n" +
                                source_sha256 + "\n" + profile + "\n" + language + "\n");
}

ContentManifest parse_content_manifest(std::string_view text)
{
    const auto root = platform::parse_json(text);
    fields(root, {"format", "source_sha256", "profile", "language", "total_bytes", "files"});
    if (number(root["format"]) != content_format ||
        root["files"].type() != platform::JsonValue::Type::Array)
    {
        throw std::runtime_error("Unsupported content manifest format.");
    }
    ContentManifest result;
    result.source_sha256 = string(root["source_sha256"]);
    result.profile = string(root["profile"]);
    result.language = string(root["language"]);
    result.total_bytes = number(root["total_bytes"]);
    for (const auto& value : root["files"].items())
    {
        fields(value, {"path", "size", "sha256"});
        result.files.push_back(
            {{string(value["path"]), number(value["size"])}, string(value["sha256"])});
    }
    validate(result);
    return result;
}

std::string content_manifest_json(const ContentManifest& manifest)
{
    validate(manifest);
    std::ostringstream out;
    out << "{\"format\":" << content_format << ",\"source_sha256\":\"" << manifest.source_sha256
        << "\",\"profile\":\"" << manifest.profile << "\",\"language\":\"" << manifest.language
        << "\",\"total_bytes\":" << manifest.total_bytes << ",\"files\":[";
    for (std::size_t i = 0; i < manifest.files.size(); ++i)
    {
        if (i)
        {
            out << ',';
        }
        const auto& file = manifest.files[i];
        out << "{\"path\":\"" << file.path << "\",\"size\":" << file.size << ",\"sha256\":\""
            << file.sha256 << "\"}";
    }
    out << "]}\n";
    return out.str();
}

std::string sha256_source_file(SourceFile& file)
{
    file.seek(0);
    platform::Sha256 hash;
    std::array<unsigned char, 65536> buffer{};
    while (const auto received = file.read(buffer))
    {
        hash.update(buffer.data(), received);
    }
    return hash.finish_hex();
}

PreparedSource::PreparedSource(const std::filesystem::path& path)
{
    const auto root = canonical_content_root(path);
    lock_ = std::make_shared<ContentLock>(root, true);
    const auto size = content_file_size(root, "manifest.json").value_or(0);
    if (size > max_content_manifest_bytes)
    {
        throw std::runtime_error("Content manifest exceeds 32 MiB.");
    }
    auto input = open_local_file(root, "manifest.json", size);
    std::string text(static_cast<std::size_t>(size), '\0');
    std::size_t done = 0;
    while (done < text.size())
    {
        const auto read =
            input->read({reinterpret_cast<unsigned char*>(text.data() + done), text.size() - done});
        if (!read)
        {
            throw std::runtime_error("Truncated content manifest.");
        }
        done += read;
    }
    manifest_ = parse_content_manifest(text);
    if (root.filename().string() != manifest_.identity())
    {
        throw std::runtime_error("Content directory identity mismatch.");
    }
    std::vector<SourceEntry> files;
    for (const auto& entry : manifest_.files)
    {
        files.push_back({entry.path, entry.size});
    }
    files_ = std::make_unique<FolderSource>(root / "files", std::move(files));
    for (const auto& entry : manifest_.files)
    {
        files_->open(entry.path);
    }
}

std::unique_ptr<SourceFile> PreparedSource::open(std::string_view path) const
{
    return std::make_unique<PreparedFile>(lock_, files_->open(path));
}
void PreparedSource::verify() const
{
    for (const auto& entry : manifest_.files)
    {
        auto file = open(entry.path);
        if (sha256_source_file(*file) != entry.sha256)
        {
            throw std::runtime_error("Prepared content checksum failed: " + entry.path);
        }
    }
}
} // namespace beachlife::data
