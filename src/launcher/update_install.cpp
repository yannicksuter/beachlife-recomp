#include "launcher/update_install.h"
#include "platform/http_client.h"
#include "platform/json.h"
#include "platform/path.h"
#include "platform/sha256.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace beachlife::launcher
{
namespace
{
namespace fs = std::filesystem;
using platform::utf8;
using platform::utf8_path;

// Limits against damaged or hostile archives (a package is ~10 MB unpacked).
constexpr std::uint64_t max_unpacked = 2ull << 30;
constexpr std::uint64_t max_entries = 20000;
constexpr const char* replacing_marker = "replacing";

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

// A package path as stored in an archive or manifest: relative, '/' separated,
// without "..", "." or empty parts, drive letters, backslashes or control characters.
fs::path safe_path(std::string_view name)
{
    if (name.empty() || name.size() > 1024 || name.front() == '/')
    {
        fail("Unsafe path in the package: " + std::string(name));
    }
    std::string_view rest = name;
    while (!rest.empty())
    {
        const auto slash = rest.find('/');
        const std::string_view part = rest.substr(0, slash);
        if (part.empty() || part == "." || part == ".." ||
            std::any_of(part.begin(),
                part.end(),
                [](char c)
                {
                    return static_cast<unsigned char>(c) < 0x20 ||
                           std::string_view("\\:*?\"<>|").find(c) != std::string_view::npos;
                }))
        {
            fail("Unsafe path in the package: " + std::string(name));
        }
        rest = slash == std::string_view::npos ? std::string_view() : rest.substr(slash + 1);
    }
    return utf8_path(name);
}

// A relative path as text with '/' separators.
std::string generic(const fs::path& path)
{
    std::string text = utf8(path);
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

bool present(const fs::path& path)
{
    std::error_code error;
    return fs::symlink_status(path, error).type() != fs::file_type::not_found && !error;
}

// Renames one file. Windows can rename (not replace) a running program or loaded
// library; a virus scanner may hold a fresh file for a moment.
void move_path(const fs::path& from, const fs::path& to)
{
#ifdef _WIN32
    for (int attempt = 0;; ++attempt)
    {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            return;
        }
        const DWORD error = GetLastError();
        if ((error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED ||
                error == ERROR_LOCK_VIOLATION) &&
            attempt < 30)
        {
            Sleep(100);
            continue;
        }
        throw std::system_error(
            static_cast<int>(error), std::system_category(), "Cannot move " + utf8(from));
    }
#else
    if (present(to))
    {
        fail("Cannot move " + utf8(from) + ": " + utf8(to) + " exists");
    }
    if (std::rename(from.c_str(), to.c_str()) != 0)
    {
        throw std::system_error(errno, std::generic_category(), "Cannot move " + utf8(from));
    }
#endif
}

// Writes one unpacked file, checking the limits as it grows.
class OutputFile
{
  public:
    OutputFile(const fs::path& path, std::uint64_t& total)
        : path_(path)
        , total_(total)
    {
        fs::create_directories(path.parent_path());
        if (present(path))
        {
            fail("The package contains " + utf8(path.filename()) + " twice");
        }
        stream_.open(path, std::ios::binary | std::ios::trunc);
        if (!stream_)
        {
            fail("Cannot create " + utf8(path));
        }
    }
    void write(const char* data, std::size_t size)
    {
        total_ += size;
        if (total_ > max_unpacked)
        {
            fail("The package unpacks to more than 2 GB");
        }
        if (!stream_.write(data, static_cast<std::streamsize>(size)))
        {
            fail("Cannot write " + utf8(path_));
        }
    }
    void close()
    {
        stream_.close();
        if (!stream_)
        {
            fail("Cannot write " + utf8(path_));
        }
    }

  private:
    fs::path path_;
    std::uint64_t& total_;
    std::ofstream stream_;
};

// Sequential reads of a gzip file (one or more members).
class GzipReader
{
  public:
    explicit GzipReader(const fs::path& file)
        : file_(file, std::ios::binary)
    {
        if (!file_)
        {
            fail("Cannot read " + utf8(file));
        }
        if (inflateInit2(&stream_, 16 + MAX_WBITS) != Z_OK)
        {
            fail("Cannot start decompression");
        }
    }
    ~GzipReader()
    {
        inflateEnd(&stream_);
    }
    GzipReader(const GzipReader&) = delete;
    GzipReader& operator=(const GzipReader&) = delete;

    // Fills `size` bytes; returns fewer only at the end of the data.
    std::size_t read(char* out, std::size_t size)
    {
        stream_.next_out = reinterpret_cast<Bytef*>(out);
        stream_.avail_out = static_cast<uInt>(size);
        while (stream_.avail_out && !finished_)
        {
            if (!stream_.avail_in)
            {
                file_.read(reinterpret_cast<char*>(input_.data()),
                    static_cast<std::streamsize>(input_.size()));
                stream_.next_in = input_.data();
                stream_.avail_in = static_cast<uInt>(file_.gcount());
                if (!stream_.avail_in)
                {
                    if (!member_started_)
                    {
                        finished_ = true; // a clean end after a member
                        break;
                    }
                    fail("The package is truncated");
                }
            }
            member_started_ = true;
            const int result = inflate(&stream_, Z_NO_FLUSH);
            if (result == Z_STREAM_END)
            {
                // Another gzip member may follow.
                member_started_ = false;
                if (inflateReset(&stream_) != Z_OK)
                {
                    fail("Cannot continue decompression");
                }
                continue;
            }
            if (result != Z_OK && result != Z_BUF_ERROR)
            {
                fail("The package is damaged (gzip)");
            }
        }
        return size - stream_.avail_out;
    }

    void read_exactly(char* out, std::size_t size)
    {
        if (read(out, size) != size)
        {
            fail("The package is truncated");
        }
    }

    void skip(std::uint64_t size)
    {
        char buffer[4096];
        while (size)
        {
            const std::size_t step =
                static_cast<std::size_t>(std::min<std::uint64_t>(size, sizeof buffer));
            read_exactly(buffer, step);
            size -= step;
        }
    }

  private:
    std::ifstream file_;
    z_stream stream_{};
    std::array<Bytef, 1 << 16> input_{};
    bool finished_ = false, member_started_ = false;
};

std::uint64_t tar_number(const char* field, std::size_t size)
{
    // Large values use base-256 with the high bit set.
    if (static_cast<unsigned char>(field[0]) & 0x80)
    {
        std::uint64_t value = static_cast<unsigned char>(field[0]) & 0x7F;
        for (std::size_t n = 1; n < size; ++n)
        {
            value = (value << 8) | static_cast<unsigned char>(field[n]);
        }
        return value;
    }
    std::uint64_t value = 0;
    std::size_t n = 0;
    while (n < size && field[n] == ' ')
    {
        ++n;
    }
    for (; n < size && field[n] >= '0' && field[n] <= '7'; ++n)
    {
        value = value * 8 + static_cast<std::uint64_t>(field[n] - '0');
    }
    return value;
}

std::string tar_text(const char* field, std::size_t size)
{
    return std::string(field, std::find(field, field + size, '\0'));
}

// tar.gz (ustar, pax or GNU long names), as Python's tarfile writes them.
void unpack_tar(
    const fs::path& archive, const fs::path& destination, const std::function<bool()>& cancelled)
{
    GzipReader input(archive);
    std::uint64_t total = 0, entries = 0;
    std::string long_name;
    char header[512];
    for (;;)
    {
        if (cancelled && cancelled())
        {
            fail("Cancelled");
        }
        if (input.read(header, sizeof header) != sizeof header)
        {
            fail("The package is truncated");
        }
        if (std::all_of(header,
                header + sizeof header,
                [](char c)
                {
                    return c == 0;
                }))
        {
            return; // the end
        }
        unsigned sum = 0;
        for (std::size_t n = 0; n < sizeof header; ++n)
        {
            sum += (n >= 148 && n < 156) ? unsigned(' ') : static_cast<unsigned char>(header[n]);
        }
        if (sum != tar_number(header + 148, 8))
        {
            fail("The package is damaged (tar)");
        }
        if (++entries > max_entries)
        {
            fail("The package has too many entries");
        }
        const std::uint64_t size = tar_number(header + 124, 12);
        const std::uint64_t padded = (size + 511) / 512 * 512;
        const char type = header[156];
        if (type == 'x' || type == 'L' || type == 'g')
        {
            // pax records ("<length> path=<name>\n") or a GNU long name.
            if (size > (1u << 20))
            {
                fail("The package is damaged (tar)");
            }
            std::string data(static_cast<std::size_t>(size), '\0');
            input.read_exactly(data.data(), data.size());
            input.skip(padded - size);
            if (type == 'L')
            {
                long_name = std::string(data.c_str());
            }
            else if (type == 'x')
            {
                std::size_t at = 0;
                while (at < data.size())
                {
                    const auto space = data.find(' ', at);
                    std::size_t length = 0;
                    if (space == std::string::npos)
                    {
                        break;
                    }
                    for (std::size_t n = at; n < space; ++n)
                    {
                        if (data[n] < '0' || data[n] > '9' || length > data.size())
                        {
                            fail("The package is damaged (pax)");
                        }
                        length = length * 10 + static_cast<std::size_t>(data[n] - '0');
                    }
                    if (length <= space - at + 1 || at + length > data.size())
                    {
                        fail("The package is damaged (pax)");
                    }
                    const std::string record = data.substr(space + 1, at + length - space - 2);
                    if (record.rfind("path=", 0) == 0)
                    {
                        long_name = record.substr(5);
                    }
                    at += length;
                }
            }
            continue;
        }
        std::string name = long_name;
        long_name.clear();
        if (name.empty())
        {
            name = tar_text(header, 100);
            const std::string prefix =
                std::string(header + 257, 5) == "ustar" ? tar_text(header + 345, 155) : "";
            if (!prefix.empty())
            {
                name = prefix + "/" + name;
            }
        }
        while (!name.empty() && name.back() == '/')
        {
            name.pop_back();
        }
        if (name.rfind("./", 0) == 0)
        {
            name.erase(0, 2);
        }
        if (type == '5')
        {
            if (!name.empty())
            {
                fs::create_directories(destination / safe_path(name));
            }
            input.skip(padded);
            continue;
        }
        if (type != '0' && type != '\0' && type != '7')
        {
            fail("The package contains a link or special file: " + name);
        }
        OutputFile file(destination / safe_path(name), total);
        char buffer[1 << 15];
        for (std::uint64_t left = size; left;)
        {
            const std::size_t step =
                static_cast<std::size_t>(std::min<std::uint64_t>(left, sizeof buffer));
            input.read_exactly(buffer, step);
            file.write(buffer, step);
            left -= step;
        }
        file.close();
        input.skip(padded - size);
    }
}

std::uint32_t read16(const unsigned char* data)
{
    return std::uint32_t(data[0]) | std::uint32_t(data[1]) << 8;
}
std::uint32_t read32(const unsigned char* data)
{
    return read16(data) | read16(data + 2) << 16;
}

// zip (stored or deflated entries, no zip64), as Python's zipfile writes them.
void unpack_zip(
    const fs::path& archive, const fs::path& destination, const std::function<bool()>& cancelled)
{
    std::ifstream file(archive, std::ios::binary);
    if (!file)
    {
        fail("Cannot read " + utf8(archive));
    }
    file.seekg(0, std::ios::end);
    const std::uint64_t length = static_cast<std::uint64_t>(file.tellg());
    // The end-of-central-directory record: 22 bytes plus up to 64 KiB of comment.
    const std::uint64_t tail = std::min<std::uint64_t>(length, 22 + 65535);
    std::vector<unsigned char> end(static_cast<std::size_t>(tail));
    file.seekg(static_cast<std::streamoff>(length - tail));
    if (!file.read(reinterpret_cast<char*>(end.data()), static_cast<std::streamsize>(tail)))
    {
        fail("The package is truncated");
    }
    std::optional<std::size_t> record;
    for (std::size_t at = tail >= 22 ? static_cast<std::size_t>(tail - 22) : 0; tail >= 22; --at)
    {
        if (read32(end.data() + at) == 0x06054b50)
        {
            record = at;
            break;
        }
        if (at == 0)
        {
            break;
        }
    }
    if (!record)
    {
        fail("The package is damaged (zip)");
    }
    const unsigned char* eocd = end.data() + *record;
    const std::uint32_t count = read16(eocd + 10), directory_size = read32(eocd + 12),
                        directory_at = read32(eocd + 16);
    if (count == 0xFFFF || directory_at == 0xFFFFFFFFu || read16(eocd + 4) || read16(eocd + 6))
    {
        fail("Unsupported zip package (zip64 or multiple parts)");
    }
    if (count > max_entries || std::uint64_t(directory_at) + directory_size > length)
    {
        fail("The package is damaged (zip)");
    }
    std::vector<unsigned char> directory(directory_size);
    file.seekg(directory_at);
    if (!file.read(reinterpret_cast<char*>(directory.data()),
            static_cast<std::streamsize>(directory.size())))
    {
        fail("The package is truncated");
    }
    std::uint64_t total = 0;
    std::size_t at = 0;
    for (std::uint32_t n = 0; n < count; ++n)
    {
        if (cancelled && cancelled())
        {
            fail("Cancelled");
        }
        if (at + 46 > directory.size() || read32(directory.data() + at) != 0x02014b50)
        {
            fail("The package is damaged (zip)");
        }
        const unsigned char* entry = directory.data() + at;
        const std::uint32_t flags = read16(entry + 8), method = read16(entry + 10),
                            crc = read32(entry + 16);
        const std::uint32_t packed = read32(entry + 20), size = read32(entry + 24);
        const std::uint32_t name_length = read16(entry + 28), extra = read16(entry + 30),
                            comment = read16(entry + 32);
        const std::uint32_t attributes = read32(entry + 38), local = read32(entry + 42);
        if (at + 46 + name_length > directory.size())
        {
            fail("The package is damaged (zip)");
        }
        std::string name(reinterpret_cast<const char*>(entry + 46), name_length);
        at += 46 + name_length + extra + comment;
        if (flags & 1)
        {
            fail("The package is encrypted");
        }
        // Unix modes in the upper half: links are refused.
        if (read16(entry + 4) >> 8 == 3 && ((attributes >> 16) & 0170000) == 0120000)
        {
            fail("The package contains a link: " + name);
        }
        if (!name.empty() && name.back() == '/')
        {
            name.pop_back();
            if (!name.empty())
            {
                fs::create_directories(destination / safe_path(name));
            }
            continue;
        }
        if (method != 0 && method != 8)
        {
            fail("Unsupported compression in the package: " + name);
        }
        unsigned char local_header[30];
        file.seekg(local);
        if (!file.read(reinterpret_cast<char*>(local_header), sizeof local_header) ||
            read32(local_header) != 0x04034b50)
        {
            fail("The package is damaged (zip)");
        }
        const std::uint64_t data_at =
            std::uint64_t(local) + 30 + read16(local_header + 26) + read16(local_header + 28);
        if (data_at + packed > length)
        {
            fail("The package is truncated");
        }
        file.seekg(static_cast<std::streamoff>(data_at));
        OutputFile output(destination / safe_path(name), total);
        uLong check = crc32(0, nullptr, 0);
        std::uint64_t written = 0;
        std::vector<char> in(1 << 16), out(1 << 16);
        z_stream inflater{};
        if (method == 8 && inflateInit2(&inflater, -MAX_WBITS) != Z_OK)
        {
            fail("Cannot start decompression");
        }
        struct End
        {
            z_stream& stream;
            bool active;
            ~End()
            {
                if (active)
                {
                    inflateEnd(&stream);
                }
            }
        } end_inflater{inflater, method == 8};
        std::uint64_t left = packed;
        bool done = method == 0 && packed == 0;
        while (!done)
        {
            const std::size_t step =
                static_cast<std::size_t>(std::min<std::uint64_t>(left, in.size()));
            if (step && !file.read(in.data(), static_cast<std::streamsize>(step)))
            {
                fail("The package is truncated");
            }
            left -= step;
            if (method == 0)
            {
                output.write(in.data(), step);
                check = crc32(
                    check, reinterpret_cast<const Bytef*>(in.data()), static_cast<uInt>(step));
                written += step;
                done = left == 0;
                continue;
            }
            inflater.next_in = reinterpret_cast<Bytef*>(in.data());
            inflater.avail_in = static_cast<uInt>(step);
            do
            {
                inflater.next_out = reinterpret_cast<Bytef*>(out.data());
                inflater.avail_out = static_cast<uInt>(out.size());
                const int result = inflate(&inflater, Z_NO_FLUSH);
                if (result != Z_OK && result != Z_STREAM_END && !(result == Z_BUF_ERROR && left))
                {
                    fail("The package is damaged (deflate): " + name);
                }
                const std::size_t produced = out.size() - inflater.avail_out;
                output.write(out.data(), produced);
                check = crc32(
                    check, reinterpret_cast<const Bytef*>(out.data()), static_cast<uInt>(produced));
                written += produced;
                if (result == Z_STREAM_END)
                {
                    done = true;
                }
            } while (!done && inflater.avail_out == 0);
            if (!done && (inflater.avail_in != 0 || left == 0))
            {
                fail("The package is damaged (deflate): " + name);
            }
        }
        output.close();
        if (written != size || static_cast<std::uint32_t>(check) != crc)
        {
            fail("The package is damaged (checksum): " + name);
        }
    }
}

// The folder holding SOURCE-MANIFEST.json: the destination (zip) or its only
// subfolder (tar.gz packages wrap their files in one).
fs::path package_root(const fs::path& destination)
{
    if (present(destination / package_manifest))
    {
        return destination;
    }
    std::optional<fs::path> only;
    for (const auto& entry : fs::directory_iterator(destination))
    {
        if (only || !entry.is_directory())
        {
            fail("The package has an unexpected layout");
        }
        only = entry.path();
    }
    if (!only || !present(*only / package_manifest))
    {
        fail("The package has no SOURCE-MANIFEST.json");
    }
    return *only;
}

void verify_package(const fs::path& root, const std::function<bool()>& cancelled)
{
    const auto manifest = read_package_manifest(root / package_manifest);
    std::set<std::string> listed;
    for (const auto& file : manifest.files)
    {
        if (cancelled && cancelled())
        {
            fail("Cancelled");
        }
        const fs::path path = root / safe_path(file.path);
        std::error_code error;
        if (!fs::is_regular_file(fs::symlink_status(path, error)) ||
            fs::file_size(path, error) != file.bytes)
        {
            fail("The package is incomplete: " + file.path);
        }
        if (platform::sha256_file(path) != file.sha256)
        {
            fail("The package is damaged: " + file.path);
        }
#ifndef _WIN32
        fs::permissions(path, static_cast<fs::perms>(file.mode & 0777), fs::perm_options::replace);
#endif
        listed.insert(file.path);
    }
    listed.insert(package_manifest);
    for (const auto& entry : fs::recursive_directory_iterator(root))
    {
        if (entry.is_directory())
        {
            continue;
        }
        const std::string relative = generic(fs::relative(entry.path(), root));
        if (!entry.is_regular_file() || !listed.count(relative))
        {
            fail("The package contains an unlisted file: " + relative);
        }
    }
}

// Moves the old files out and the new ones in. Steps that are already done (a
// replacement an interrupted launcher left half done) are skipped, so the same
// pass also completes such a replacement. `log` records the moves.
void apply_replacement(const fs::path& base,
    const fs::path& staged,
    const fs::path& backup,
    std::vector<std::pair<fs::path, fs::path>>* log,
    const std::function<void(const std::string&)>& before_move)
{
    const fs::path incoming_manifest = staged / package_manifest;
    if (!present(incoming_manifest))
    {
        return; // the manifest moves last: nothing left to do
    }
    const auto incoming = read_package_manifest(incoming_manifest);
    const fs::path installed_manifest =
        present(base / package_manifest) ? base / package_manifest : backup / package_manifest;
    const auto installed = read_package_manifest(installed_manifest);
    if (incoming.package() != installed.package())
    {
        fail("The download is the " + incoming.package() + " package; this folder holds " +
             installed.package());
    }
    std::set<std::string> paths, fresh;
    for (const auto& file : installed.files)
    {
        paths.insert(file.path);
    }
    for (const auto& file : incoming.files)
    {
        paths.insert(file.path);
        fresh.insert(file.path);
    }
    std::vector<std::string> order(paths.begin(), paths.end());
    order.push_back(package_manifest);
    fresh.insert(package_manifest);
    auto move = [&](const fs::path& from, const fs::path& to)
    {
        fs::create_directories(to.parent_path());
        move_path(from, to);
        if (log)
        {
            log->emplace_back(from, to);
        }
    };
    for (const auto& relative : order)
    {
        const fs::path path = safe_path(relative);
        const fs::path target = base / path, previous = backup / path, source = staged / path;
        const bool replace = fresh.count(relative) && present(source);
        if (!replace && fresh.count(relative))
        {
            continue; // already moved in
        }
        if (before_move)
        {
            before_move(relative);
        }
        if (present(target))
        {
            if (fs::is_directory(fs::symlink_status(target)))
            {
                fail("A folder is in the way of " + relative);
            }
            if (present(previous))
            {
                fail("Cannot keep the previous " + relative);
            }
            move(target, previous);
        }
        if (replace)
        {
            move(source, target);
        }
    }
}
} // namespace

PackageManifest read_package_manifest(const fs::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
    {
        fail("Cannot read " + utf8(file));
    }
    std::stringstream text;
    text << stream.rdbuf();
    const auto json = platform::parse_json(text.str());
    PackageManifest manifest;
    if (json["format"].integer().value_or(0) != 1)
    {
        fail("Unknown package manifest format in " + utf8(file));
    }
    const auto& build = json["build"];
    manifest.version = build["build_version"].text();
    manifest.platform = build["platform"].text();
    manifest.architecture = build["architecture"].text();
    const auto& files = json["files"];
    if (manifest.version.empty() || manifest.platform.empty() || manifest.architecture.empty() ||
        !files.is_object() || files.size() == 0)
    {
        fail("Incomplete package manifest in " + utf8(file));
    }
    for (std::size_t n = 0; n < files.size(); ++n)
    {
        const auto& entry = files.items()[n];
        PackageFile item;
        item.path = files.names()[n];
        safe_path(item.path);
        if (item.path == package_manifest || item.path.rfind(".update/", 0) == 0)
        {
            fail("Invalid file in the package manifest: " + item.path);
        }
        item.sha256 = entry["sha256"].text();
        const auto bytes = entry["bytes"].integer(), mode = entry["mode"].integer();
        if (item.sha256.size() != 64 || !bytes || *bytes < 0 ||
            (mode && (*mode < 0 || *mode > 07777)))
        {
            fail("Invalid entry in the package manifest: " + item.path);
        }
        item.bytes = static_cast<std::uint64_t>(*bytes);
        item.mode = mode ? static_cast<unsigned>(*mode) : 0644;
        manifest.files.push_back(std::move(item));
    }
    return manifest;
}

fs::path package_folder(const fs::path& program_folder)
{
    // macOS: beachlife.app/Contents/Resources (SDL's base path) or .../MacOS.
    fs::path folder = program_folder;
    for (int level = 0; level < 4 && !folder.empty(); ++level)
    {
        if (present(folder / package_manifest))
        {
            return folder;
        }
        if (folder.parent_path() == folder)
        {
            break;
        }
        folder = folder.parent_path();
    }
    return {};
}

fs::path unpack_package(
    const fs::path& archive, const fs::path& destination, const std::function<bool()>& cancelled)
{
    if (present(destination))
    {
        fail("The unpack folder exists already: " + utf8(destination));
    }
    fs::create_directories(destination);
    const std::string name = utf8(archive.filename());
    if (name.size() > 4 && name.substr(name.size() - 4) == ".zip")
    {
        unpack_zip(archive, destination, cancelled);
    }
    else if (name.size() > 7 && name.substr(name.size() - 7) == ".tar.gz")
    {
        unpack_tar(archive, destination, cancelled);
    }
    else
    {
        fail("Unknown package type: " + name);
    }
    const fs::path root = package_root(destination);
    verify_package(root, cancelled);
    return root;
}

void replace_package(const fs::path& base,
    const fs::path& staged,
    const fs::path& backup,
    const std::function<void(const std::string&)>& before_move)
{
    std::vector<std::pair<fs::path, fs::path>> moves;
    try
    {
        apply_replacement(base, staged, backup, &moves, before_move);
    }
    catch (...)
    {
        // Every move back, newest first; one that fails does not stop the rest.
        for (auto it = moves.rbegin(); it != moves.rend(); ++it)
        {
            try
            {
                move_path(it->second, it->first);
            }
            catch (...)
            {
            }
        }
        throw;
    }
}

fs::path update_folder(const fs::path& base)
{
    return base / ".update";
}

std::string install_blocker(const fs::path& base)
{
    if (base.empty() || !present(base / package_manifest))
    {
        return "This copy was not unpacked from a release package, so it cannot update itself.";
    }
    try
    {
        read_package_manifest(base / package_manifest);
    }
    catch (const std::exception& error)
    {
        return std::string("The package's file list cannot be read: ") + error.what() + ".";
    }
    std::error_code error;
    const fs::path folder = update_folder(base);
    fs::create_directories(folder, error);
    const fs::path probe = folder / "write-test";
    if (error || !std::ofstream(probe).good())
    {
        return "The launcher's folder is read-only: " + utf8(base) + ".";
    }
    fs::remove(probe, error);
    if (fs::is_empty(folder, error))
    {
        fs::remove(folder, error);
    }
    if (const auto reason = platform::http_unavailable(); !reason.empty())
    {
        return "Downloads are not available here: " + reason + ".";
    }
    return {};
}

void install_staged_package(const fs::path& base, const fs::path& staged, const fs::path& backup)
{
    const fs::path marker = update_folder(base) / replacing_marker;
    {
        std::ofstream stream(marker, std::ios::binary | std::ios::trunc);
        stream << generic(fs::relative(staged, update_folder(base))) << '\n'
               << generic(fs::relative(backup, update_folder(base))) << '\n';
        if (!stream.flush())
        {
            fail("Cannot write " + utf8(marker));
        }
    }
    std::error_code ignored;
    try
    {
        replace_package(base, staged, backup);
    }
    catch (...)
    {
        fs::remove(marker, ignored);
        throw;
    }
    fs::remove(marker, ignored);
}

std::string finish_updates(const fs::path& base)
{
    if (base.empty())
    {
        return {};
    }
    const fs::path folder = update_folder(base);
    std::error_code error;
    if (!fs::is_directory(folder, error))
    {
        return {};
    }
    // A replacement that stopped half way (the launcher was killed) is completed.
    const fs::path marker = folder / replacing_marker;
    if (present(marker))
    {
        std::string staged, backup;
        {
            // Closed before the marker is removed (Windows cannot delete open files).
            std::ifstream stream(marker);
            std::getline(stream, staged);
            std::getline(stream, backup);
        }
        try
        {
            apply_replacement(
                base, folder / safe_path(staged), folder / safe_path(backup), nullptr, {});
            fs::remove(marker, error);
        }
        catch (const std::exception&)
        {
            return {}; // kept for the next start; the folder stays as it is
        }
    }
    std::string replaced;
    for (const auto& entry : fs::directory_iterator(folder, error))
    {
        const std::string name = utf8(entry.path().filename());
        if (name.rfind("previous-", 0) == 0)
        {
            try
            {
                replaced = read_package_manifest(entry.path() / package_manifest).version;
            }
            catch (const std::exception&)
            {
            }
            fs::remove_all(entry.path(), error);
        }
        else if (name.rfind("staged-", 0) == 0 || name.rfind("download-", 0) == 0)
        {
            fs::remove_all(entry.path(), error);
        }
    }
    if (fs::is_empty(folder, error))
    {
        fs::remove(folder, error);
    }
    return replaced;
}

fs::path installed_launcher(const fs::path& base)
{
#ifdef _WIN32
    return base / "beachlife.exe";
#elif defined(__APPLE__)
    return base / "beachlife.app/Contents/MacOS/beachlife";
#else
    return base / "beachlife";
#endif
}

std::string restart_launcher(const fs::path& program, int argc, char** argv)
{
#ifdef _WIN32
    (void)argc;
    (void)argv;
    std::wstring command = GetCommandLineW();
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(program.c_str(),
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startup,
            &process))
    {
        return std::system_category().message(static_cast<int>(GetLastError()));
    }
    // Only the foreground process may hand the foreground on; without this the new
    // window opens behind other programs.
    AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return {};
#else
    std::vector<char*> arguments(argv, argv + argc);
    arguments.push_back(nullptr);
    execv(program.c_str(), arguments.data());
    return std::strerror(errno);
#endif
}

} // namespace beachlife::launcher
