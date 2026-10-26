#include "platform/compat/compat_files.h"
#include "compat_environment.h"
#include "data/folder_source.h"
#include "io.h"
#include "msvcrt.h"
#include "platform/compat/kernel_files.h"
#include "platform/path.h"
#include "runtime/retail_files.h"
#include "windows.h"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
namespace fs = std::filesystem;
int failures = 0;
void check(bool condition, const char* detail)
{
    if (!condition)
    {
        std::fprintf(stderr, "%s\n", detail);
        ++failures;
    }
}

struct Fixture
{
    CompatProfileEnvironment environment;
    fs::path root;
    Fixture()
    {
        root = fs::temp_directory_path() /
               ("beachlife-compat-files-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!fs::create_directory(root))
        {
            throw std::runtime_error("Cannot create fixture");
        }
        root = fs::canonical(root);
        SDL_setenv_unsafe("BEACHLIFE_BOOT_USER_DIR",
            beachlife::platform::utf8(root / "user-\xc3\xa9").c_str(),
            1);
        fs::create_directories(root / "retail/Data");
        fs::create_directories(root / "retail/Data/Scenario/Test");
        std::ofstream(root / "retail/Data/Asset.bin", std::ios::binary).put('!');
        std::ofstream(root / "retail/Data/Scenario/Test/level.dat", std::ios::binary).put('?');
    }
    ~Fixture()
    {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};
} // namespace

int main()
{
    Fixture fixture;
    const std::string input = ";comment\r\n [Video] \r\n Width = 800 \r\nTitle=\"Beach Life\"\r\n"
                              "Empty=\r\n[Audio]\r\nVolume=90\r\n";
    {
        std::ofstream file(fixture.root / "retail/Data/Settings.ini", std::ios::binary);
        file << input;
        if (!file)
        {
            return 2;
        }
    }
    auto source = std::make_shared<beachlife::data::FolderSource>(fixture.root / "retail",
        std::vector<beachlife::data::SourceEntry>{{"Data/Settings.ini", input.size()},
            {"Data/Asset.bin", 1},
            {"Data/Scenario/Test/level.dat", 1}});
    beachlife::runtime::retail_files::Session session(source);
    beachlife::compat::DirectorySession directories(source->entries());
    std::array<char, 256> buffer{};
    const char* name = "data\\settings.INI";
    check(
        GetPrivateProfileStringA("video", "WIDTH", "0", buffer.data(), buffer.size(), name) == 3 &&
            std::strcmp(buffer.data(), "800") == 0,
        "INI lookup uses VFS and ignores section/key case and spaces");
    check(
        GetPrivateProfileStringA("Video", "Title", "", buffer.data(), buffer.size(), name) == 10 &&
            std::strcmp(buffer.data(), "Beach Life") == 0,
        "INI quoted values lose only their surrounding quotes");
    check(GetPrivateProfileStringA(
              "Video", "Empty", "fallback", buffer.data(), buffer.size(), name) == 0 &&
              buffer[0] == 0,
        "present empty INI value suppresses the fallback");
    check(GetPrivateProfileStringA(
              "Missing", "Key", " fallback  ", buffer.data(), buffer.size(), name) == 9 &&
              std::strcmp(buffer.data(), " fallback") == 0,
        "INI fallback trims trailing spaces and preserves leading spaces");
    check(
        GetPrivateProfileStringA(nullptr, nullptr, "", buffer.data(), buffer.size(), name) == 12 &&
            std::memcmp(buffer.data(), "Video\0Audio\0\0", 13) == 0,
        "INI section enumeration has two final nulls");
    buffer.fill('!');
    check(GetPrivateProfileStringA("Video", nullptr, "", buffer.data(), 7, name) == 5 &&
              std::memcmp(buffer.data(), "Width\0\0", 7) == 0 && buffer[7] == '!',
        "INI truncated key enumeration reports size minus two without overrunning");
    check(WritePrivateProfileStringA("VIDEO", "Width", "1024", name),
        "INI write creates an override");
    check(GetPrivateProfileStringA("Video", "Width", "", buffer.data(), buffer.size(), name) == 4 &&
              std::strcmp(buffer.data(), "1024") == 0,
        "INI override is read before retail content");
    check(
        GetPrivateProfileStringA("Audio", "Volume", "", buffer.data(), buffer.size(), name) == 2 &&
            std::strcmp(buffer.data(), "90") == 0,
        "INI write retains other sections from the VFS original");
    check(WritePrivateProfileStringA("Video", "Title", nullptr, name) &&
              WritePrivateProfileStringA("Audio", nullptr, nullptr, name),
        "INI deletion writes the override");
    check(GetPrivateProfileStringA("Video", "Title", "gone", buffer.data(), buffer.size(), name) ==
                  4 &&
              std::strcmp(buffer.data(), "gone") == 0,
        "deleted key no longer shadows its fallback");
    check(
        WritePrivateProfileStringA("New", "Key", "'spaced value'", name) &&
            GetPrivateProfileStringA("New", "Key", "", buffer.data(), buffer.size(), name) == 12 &&
            std::strcmp(buffer.data(), "spaced value") == 0,
        "INI append and single-quote handling");
    std::ifstream original(fixture.root / "retail/Data/Settings.ini", std::ios::binary);
    const std::string unchanged((std::istreambuf_iterator<char>(original)), {});
    check(unchanged == input, "all INI writes leave prepared game data unchanged");
    check(!CreateDirectoryA("../escape", nullptr) && GetLastError() == ERROR_INVALID_NAME,
        "user paths reject parent traversal");
    check(CreateDirectoryA("Saves", nullptr) && !CreateDirectoryA("sAVES", nullptr) &&
              GetLastError() == ERROR_ALREADY_EXISTS,
        "directory creation uses case-insensitive user paths");
    _finddata_t found;
    static_assert(sizeof(found) == 280 && offsetof(_finddata_t, name) == 20);
    check(_findfirst("photos", &found) == -1 && CreateDirectoryA("photos", nullptr),
        "constructor's missing photo folder search allows real user-folder creation");
    long search = _findfirst("PHOTOS", &found);
    check(search != -1 && (found.attrib & _A_SUBDIR) && std::strcmp(found.name, "photos") == 0 &&
              _findclose(search) == 0 && _findclose(-1) == -1,
        "plain-name folder lookup returns a closeable long token and rejects invalid tokens");
    search = _findfirst("DATA/*.b?n", &found);
    check(search != -1 && std::strcmp(found.name, "Asset.bin") == 0 && (found.attrib & _A_RDONLY) &&
              _findnext(search, &found) == -1 && _findclose(search) == 0,
        "VFS wildcard enumeration uses the manifest and case-insensitive directory matching");
    search = _findfirst("Data/Scenario/*.*", &found);
    check(search != -1 && std::strcmp(found.name, "Test") == 0 && (found.attrib & _A_SUBDIR) &&
              _findclose(search) == 0,
        "scenario searches infer immediate VFS folders, including names without an extension");
    const int descriptor = _open("saves\\TEST.sav", _O_CREAT | _O_TRUNC | _O_RDWR | _O_BINARY);
    check(descriptor >= 0 && _write(descriptor, "save", 4) == 4 && _lseek(descriptor, 0, 0) == 0 &&
              _read(descriptor, buffer.data(), 4) == 4 &&
              std::memcmp(buffer.data(), "save", 4) == 0 && _close(descriptor) == 0,
        "CRT descriptors write and read per-user files");
    const auto root_size = GetCurrentDirectoryA(0, nullptr);
    check(
        root_size > 1 && GetCurrentDirectoryA(buffer.size(), buffer.data()) == root_size - 1 &&
            beachlife::platform::utf8_path(buffer.data()) == beachlife::compat::kernel::user_root(),
        "current directory identifies the virtual user working folder");
    check(GetFileAttributesA("SAVES/test.SAV") == FILE_ATTRIBUTE_ARCHIVE &&
              GetFileAttributesA("Saves") == FILE_ATTRIBUTE_DIRECTORY,
        "user attributes distinguish files and directories");
    check(GetFileAttributesA("data/scenario/TEST") ==
                  (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY) &&
              GetFileAttributesA("data/missing") == INVALID_FILE_ATTRIBUTES &&
              GetLastError() == ERROR_FILE_NOT_FOUND,
        "VFS attributes use the manifest to distinguish directories from missing names");
    FILE* stream = beachlife_crt_fopen("DATA/asset.BIN", "rb");
    check(stream && std::fgetc(stream) == '!' && std::fgetc(stream) == EOF && std::feof(stream) &&
              beachlife_crt_fseek(stream, 0, SEEK_SET) == 0 && !std::feof(stream) &&
              std::fgetc(stream) == '!' && std::fclose(stream) == 0,
        "stdio reads through the VFS with normal buffering, seek and EOF behavior");
    stream = beachlife_crt_fopen("DATA/asset.BIN", "rb");
    check(stream && std::fwrite("?", 1, 1, stream) == 0 && std::ferror(stream),
        "read-only VFS stdio rejects writes");
    if (stream)
    {
        // UCRT's fclose reports a stream's existing error flag too.
        std::clearerr(stream);
        check(std::fclose(stream) == 0, "close read-only VFS stdio after clearing its error");
    }
    stream = beachlife_crt_fopen("sAVes/stdio.dat", "w+b");
    check(stream && std::fwrite("header\n\0tail", 1, 12, stream) == 12 &&
              std::fflush(stream) == 0 && beachlife_crt_ftell(stream) == 12,
        "stdio writes binary saves beneath the per-user folder");
    struct Position
    {
        int64_t value;
        uint64_t guard;
    } position{0, 0x123456789abcdef0ULL};
    check(stream && beachlife_crt_fgetpos(stream, &position.value) == 0 && position.value == 12 &&
              position.guard == 0x123456789abcdef0ULL &&
              beachlife_crt_fseek(stream, 0, SEEK_SET) == 0 &&
              std::fgets(buffer.data(), buffer.size(), stream) &&
              std::strcmp(buffer.data(), "header\n") == 0 && std::fgetc(stream) == 0 &&
              std::fread(buffer.data(), 3, 2, stream) == 1 && std::feof(stream) &&
              beachlife_crt_ftell(stream) == 12 && std::fclose(stream) == 0,
        "save header parsing keeps embedded nulls, partial element counts and 64-bit fpos_t");
    stream = beachlife_crt_fopen("saves/STDIO.dat", "wt");
    check(stream && std::fclose(stream) == 0 &&
              fs::file_size(beachlife::compat::kernel::user_path("saves/stdio.dat")) == 0 &&
              !fs::exists(fixture.root / "retail/saves"),
        "the preferences text-mode truncate stays inside the user folder");
    stream = beachlife_crt_fopen("Saves/append.dat", "wb");
    check(stream && std::fwrite("first", 1, 5, stream) == 5 && std::fclose(stream) == 0,
        "create a buffered binary append fixture");
    stream = beachlife_crt_fopen("saves/APPEND.dat", "ab+");
    check(stream && beachlife_crt_fseek(stream, 0, SEEK_SET) == 0 &&
              std::fwrite("last", 1, 4, stream) == 4 && beachlife_crt_ftell(stream) == 9 &&
              std::fflush(stream) == 0 && beachlife_crt_fseek(stream, 0, SEEK_SET) == 0 &&
              std::fread(buffer.data(), 1, 10, stream) == 9 && std::feof(stream) &&
              std::memcmp(buffer.data(), "firstlast", 9) == 0 && std::fclose(stream) == 0,
        "append writes stay at EOF after seek, and buffered update reads preserve position");
    stream = beachlife_crt_fopen("saves/append.dat", "wb");
    check(stream && std::fwrite("!", 1, 1, stream) == 1 &&
              beachlife_crt_fseek(stream, 0, SEEK_SET) == 0 && std::fgetc(stream) == EOF,
        "write-only stdio rejects reads");
#ifndef _WIN32
    // Wine's UCRT returns EOF for this operation without setting a stream error.
    check(stream && std::ferror(stream), "POSIX write-only streams report a read error");
#endif
    if (stream)
    {
        std::clearerr(stream);
        check(std::fclose(stream) == 0, "close a stream after clearing its deliberate read error");
    }
    // Exercise the separate native mapping backend while keeping retail handles
    // opaque. Closing the mapping must not invalidate a surviving mapped view.
    OFSTRUCT info{};
    const HFILE mapped_file = OpenFile("saves/mapped.bin", &info, OF_CREATE | OF_READWRITE);
    HANDLE mapping =
        CreateFileMappingA(reinterpret_cast<HANDLE>(static_cast<intptr_t>(mapped_file)),
            nullptr,
            PAGE_READWRITE,
            0,
            65536,
            nullptr);
    auto* view = static_cast<char*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 65536));
    check(mapped_file != HFILE_ERROR && mapping && view, "create a writable user-file mapping");
    if (view)
    {
        std::memcpy(view, "mapped", 6);
        check(CloseHandle(mapping), "close mapping while the view stays alive");
        check(std::memcmp(view, "mapped", 6) == 0 && UnmapViewOfFile(view),
            "mapped view survives its mapping handle");
    }
    check(CloseHandle(reinterpret_cast<HANDLE>(static_cast<intptr_t>(mapped_file))),
        "close mapped file");
    stream = beachlife_crt_fopen("saves/mapped.bin", "rb");
    check(stream && std::fread(buffer.data(), 1, 6, stream) == 6 &&
              std::memcmp(buffer.data(), "mapped", 6) == 0 && std::fclose(stream) == 0,
        "shared mapping writes persist in the user file");
    mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 65536, nullptr);
    view = static_cast<char*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0));
    check(mapping && view && view[0] == 0 && view[65535] == 0, "anonymous mapping is zero filled");
    if (view)
    {
        check(UnmapViewOfFile(view), "unmap anonymous memory");
    }
    check(CloseHandle(mapping), "close anonymous mapping");
    const HFILE copy_file = OpenFile("DATA/asset.BIN", &info, OF_READ);
    mapping = CreateFileMappingA(reinterpret_cast<HANDLE>(static_cast<intptr_t>(copy_file)),
        nullptr,
        PAGE_READONLY,
        0,
        0,
        nullptr);
    view = static_cast<char*>(MapViewOfFile(mapping, FILE_MAP_COPY, 0, 0, 0));
    check(mapping && view && view[0] == '!', "map VFS bytes with private copy access");
    if (view)
    {
        view[0] = '?';
        check(UnmapViewOfFile(view), "unmap a modified private copy");
    }
    view = static_cast<char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    check(view && view[0] == '!', "copy-on-write leaves the backing file unchanged");
    if (view)
    {
        check(UnmapViewOfFile(view), "unmap the original view");
    }
    check(CloseHandle(mapping) &&
              CloseHandle(reinterpret_cast<HANDLE>(static_cast<intptr_t>(copy_file))),
        "close VFS mapping handles");
    const auto mutex_name = "beachlife-compat-test-" + fixture.root.filename().string();
    HANDLE mutex = CreateMutexA(nullptr, TRUE, mutex_name.c_str());
    check(mutex && GetLastError() == ERROR_SUCCESS, "create a named owned mutex");
    HANDLE second = CreateMutexA(nullptr, FALSE, mutex_name.c_str());
    check(
        second && GetLastError() == ERROR_ALREADY_EXISTS, "named mutex detects an existing owner");
    check(ReleaseMutex(mutex) && CloseHandle(second) && CloseHandle(mutex),
        "release and close mutex handles");
    mutex = CreateMutexA(nullptr, FALSE, mutex_name.c_str());
    check(mutex && GetLastError() == ERROR_SUCCESS && CloseHandle(mutex),
        "last close releases the mutex name");
    check(DeleteFileA("sAVes/TEST.sav") && !DeleteFileA("saves/test.sav") &&
              GetLastError() == ERROR_FILE_NOT_FOUND,
        "deletion reports a genuine missing user file");
    return failures ? 1 : 0;
}
