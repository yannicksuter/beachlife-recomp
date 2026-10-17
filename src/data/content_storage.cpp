#include "content_storage.h"
#include "game_path.h"
#include "local_files.h"
#include "platform/path.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace beachlife::data {
namespace {
void require_regular_if_present(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status))) return;
    if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
        throw std::runtime_error("Content destination is not a regular unlinked file.");
    if (std::filesystem::hard_link_count(path) != 1)
        throw std::runtime_error("Content destination is a hard link.");
#ifdef _WIN32
    const auto flags = GetFileAttributesW(path.c_str());
    if (flags == INVALID_FILE_ATTRIBUTES || (flags & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Content destination is a reparse point.");
#endif
}
} // namespace

struct ContentLock::Impl {
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    ~Impl() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
#else
    int file = -1;
    ~Impl() { if (file >= 0) ::close(file); }
#endif
};

ContentLock::ContentLock(const std::filesystem::path& root, bool shared) : impl_(std::make_unique<Impl>()) {
    require_real_directory(root);
    const auto path = root/".lock";
    require_regular_if_present(path);
#ifdef _WIN32
    impl_->file = CreateFileW(path.c_str(), shared ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE,
        shared ? FILE_SHARE_READ : 0, nullptr, shared ? OPEN_EXISTING : OPEN_ALWAYS,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (impl_->file == INVALID_HANDLE_VALUE) throw std::runtime_error("Content is already in use or its lock is inaccessible.");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(impl_->file, &info) ||
        info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT) || info.nNumberOfLinks != 1)
#else
    impl_->file = ::open(path.c_str(), (shared ? O_RDONLY : O_RDWR | O_CREAT) | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    if (impl_->file < 0) throw std::runtime_error("Cannot open content lock.");
    struct stat info{};
    if (fstat(impl_->file, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1)
#endif
        throw std::runtime_error("Invalid content lock file.");
#ifndef _WIN32
    if (flock(impl_->file, (shared ? LOCK_SH : LOCK_EX) | LOCK_NB))
        throw std::runtime_error("Content is already in use or its lock is inaccessible.");
#endif
}
ContentLock::~ContentLock() = default;

void ensure_content_directory(const std::filesystem::path& root, std::string_view relative) {
    require_real_directory(root);
    auto current = root;
    for (const auto& part : platform::utf8_path(normalize_game_path(relative).display)) {
        current /= part;
        // create_directory returns false for an existing directory. Verify it
        // afterwards to reject directory links instead of following them.
        std::filesystem::create_directory(current);
        require_real_directory(current);
    }
}

void remove_content_file(const std::filesystem::path& path) {
    require_real_directory(path.parent_path());
    require_regular_if_present(path);
    std::filesystem::remove(path);
}

std::optional<std::uint64_t> content_file_size(const std::filesystem::path& root, std::string_view relative) {
    const auto logical = normalize_game_path(relative);
    if (logical.display != relative) throw std::runtime_error("Invalid content path.");
    require_real_directory(root);
    auto current = root;
    const auto path = platform::utf8_path(relative);
    for (const auto& part : path) {
        current /= part;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status))) return {};
        if (error) throw std::runtime_error("Cannot inspect prepared content.");
        if (current == root/path) require_regular_if_present(current);
        else require_real_directory(current);
    }
    return std::filesystem::file_size(current);
}

struct StagedFile::Impl {
    std::filesystem::path path;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    void close() { if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); file = INVALID_HANDLE_VALUE; } }
#else
    int file = -1;
    void close() { if (file >= 0) { ::close(file); file = -1; } }
#endif
    ~Impl() {
        close();
        if (!path.empty()) {
            try { remove_content_file(path); } catch (...) { /* incomplete files are never ready */ }
        }
    }
};

StagedFile::StagedFile(const std::filesystem::path& staging, std::string_view name) : impl_(std::make_unique<Impl>()) {
    const auto logical = normalize_game_path(name);
    if (logical.display != name || name.find('/') != std::string_view::npos)
        throw std::runtime_error("Invalid staging filename.");
    require_real_directory(staging);
    const auto path = staging/platform::utf8_path(name);
    remove_content_file(path); // a previous interrupted attempt, while holding the content lock
#ifdef _WIN32
    impl_->file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (impl_->file == INVALID_HANDLE_VALUE)
#else
    impl_->file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (impl_->file < 0)
#endif
        throw std::runtime_error("Cannot create a staged content file.");
    impl_->path = path;
}
StagedFile::~StagedFile() = default;

void StagedFile::write(std::span<const unsigned char> bytes) {
    std::size_t done = 0;
    while (done < bytes.size()) {
        const auto count = std::min<std::size_t>(bytes.size()-done, 1024*1024);
#ifdef _WIN32
        DWORD written = 0;
        if (!WriteFile(impl_->file, bytes.data()+done, static_cast<DWORD>(count), &written, nullptr) || !written)
            throw std::runtime_error("Cannot write game data; check free space and permissions.");
#else
        const auto written = ::write(impl_->file, bytes.data()+done, count);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) throw std::runtime_error("Cannot write game data; check free space and permissions.");
#endif
        done += static_cast<std::size_t>(written);
    }
}

void StagedFile::commit(const std::filesystem::path& destination) {
    require_real_directory(destination.parent_path());
    require_regular_if_present(destination);
#ifdef _WIN32
    if (!FlushFileBuffers(impl_->file)) throw std::runtime_error("Cannot flush prepared game data.");
    impl_->close();
    if (!MoveFileExW(impl_->path.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish prepared game data.");
#else
    if (fsync(impl_->file)) throw std::runtime_error("Cannot flush prepared game data.");
    impl_->close();
    std::filesystem::rename(impl_->path, destination);
    const int directory = ::open(destination.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory < 0) throw std::runtime_error("Cannot open the content directory for sync.");
    const int result = fsync(directory);
    ::close(directory);
    if (result) throw std::runtime_error("Cannot sync the content directory.");
#endif
    impl_->path.clear();
}

std::filesystem::path default_content_directory() {
#ifdef _WIN32
    if (const auto* local = _wgetenv(L"LOCALAPPDATA"); local && *local)
        return std::filesystem::path(local)/L"BeachLife"/L"content";
    throw std::runtime_error("LOCALAPPDATA is unavailable; specify a content directory.");
#else
#ifndef __APPLE__
    if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data && std::filesystem::path(data).is_absolute())
        return platform::utf8_path(data)/"beachlife"/"content";
#endif
    const char* home = std::getenv("HOME");
    if (!home || !*home || !std::filesystem::path(home).is_absolute())
        throw std::runtime_error("Home directory is unavailable; specify a content directory.");
#ifdef __APPLE__
    return platform::utf8_path(home)/"Library"/"Application Support"/"BeachLife"/"content";
#else
    return platform::utf8_path(home)/".local"/"share"/"beachlife"/"content";
#endif
#endif
}
} // namespace beachlife::data
