#include "platform/compat/compat_files.h"
#include "platform/compat/shell_link.h"
#include "runtime/retail_files.h"

#include <algorithm>
#include <array>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace
{
using Bytes = std::vector<unsigned char>;
using beachlife::compat::shell::InvalidLink;
using beachlife::compat::shell::parse;
using beachlife::compat::shell::UnsupportedLink;
void require(bool value, const char* detail)
{
    if (!value)
    {
        std::fprintf(stderr, "ShellLink: %s\n", detail);
        std::abort();
    }
}
void u16(Bytes& bytes, std::size_t offset, unsigned value)
{
    bytes.at(offset) = value;
    bytes.at(offset + 1) = value >> 8;
}
void u32(Bytes& bytes, std::size_t offset, std::uint32_t value)
{
    u16(bytes, offset, value);
    u16(bytes, offset + 2, value >> 16);
}
void word(Bytes& bytes, unsigned value)
{
    const auto offset = bytes.size();
    bytes.resize(offset + 2);
    u16(bytes, offset, value);
}
std::size_t ansi_z(Bytes& bytes, std::string_view text)
{
    const auto offset = bytes.size();
    bytes.insert(bytes.end(), text.begin(), text.end());
    bytes.push_back(0);
    return offset;
}
std::size_t wide_z(Bytes& bytes, std::u16string_view text)
{
    const auto offset = bytes.size();
    for (const auto code : text)
    {
        word(bytes, code);
    }
    word(bytes, 0);
    return offset;
}
void string_data(Bytes& bytes, std::u16string_view text, bool wide)
{
    word(bytes, text.size());
    for (const auto code : text)
    {
        if (wide)
        {
            word(bytes, code);
        }
        else
        {
            bytes.push_back(code);
        }
    }
}
Bytes header(DWORD flags)
{
    Bytes bytes(76);
    u32(bytes, 0, 76);
    const std::array<unsigned char, 16> clsid{
        1, 0x14, 2, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
    std::copy(clsid.begin(), clsid.end(), bytes.begin() + 4);
    u32(bytes, 20, flags);
    u32(bytes, 24, FILE_ATTRIBUTE_DIRECTORY);
    u32(bytes, 28, 0x12345678);
    u32(bytes, 32, 0x10203040);
    u32(bytes, 52, 55);
    u32(bytes, 60, 1);
    return bytes;
}
Bytes relative_link(std::u16string_view target, bool wide = true)
{
    auto bytes = header(8 | (wide ? 128 : 0));
    string_data(bytes, target, wide);
    bytes.resize(bytes.size() + 4); // TerminalBlock
    return bytes;
}
Bytes local_link(bool unicode, bool all_fields = false)
{
    auto bytes = header(all_fields ? 0xff : 2);
    if (all_fields)
    {
        word(bytes, 6); // one bounded ItemID plus TerminalID
        word(bytes, 4);
        word(bytes, 0x1234);
        word(bytes, 0);
    }
    Bytes info(unicode ? 36 : 28);
    u32(info, 4, info.size());
    u32(info, 8, 1);
    u32(info, 12, info.size());
    const auto volume = info.size();
    info.resize(volume + 17);
    u32(info, volume, 17);
    u32(info, volume + 4, 3); // fixed drive
    u32(info, volume + 8, 0x11223344);
    u32(info, volume + 12, 16); // empty ANSI volume label
    const auto base = ansi_z(info, "C:\\Music\\");
    const auto suffix = ansi_z(info, "Album");
    u32(info, 16, base);
    u32(info, 24, suffix);
    if (unicode)
    {
        const auto wide_base = wide_z(info, u"C:\\M\u00fasica\\");
        const auto wide_suffix = wide_z(info, u"\u00c1lbum");
        u32(info, 28, wide_base);
        u32(info, 32, wide_suffix);
    }
    u32(info, 0, info.size());
    bytes.insert(bytes.end(), info.begin(), info.end());
    if (all_fields)
    {
        for (const auto text :
            {u"description", u"..\\Tracks", u"C:\\Music", u"arguments", u"icon.ico"})
        {
            string_data(bytes, text, true);
        }
        const auto tracker = bytes.size();
        bytes.resize(tracker + 96);
        u32(bytes, tracker, 96);
        u32(bytes, tracker + 4, 0xa0000003);
        u32(bytes, tracker + 8, 88);
    }
    bytes.resize(bytes.size() + 4);
    return bytes;
}
template <class Error> void rejects(const Bytes& bytes, const char* detail)
{
    try
    {
        (void)parse(bytes);
    }
    catch (const Error&)
    {
        return;
    }
    require(false, detail);
}
void parser_checks()
{
    require(
        parse(local_link(false)).local_path == "C:\\Music\\Album", "parse ANSI LinkInfo offsets");
    const auto complete = local_link(true, true);
    auto target = parse(complete);
    require(target.local_path == "C:\\M\xc3\xbasica\\\xc3\x81lbum" &&
                target.relative_path == "..\\Tracks",
        "prefer Unicode LinkInfo and walk all StringData fields after IDList");
    require(target.metadata.ftCreationTime.dwLowDateTime == 0x12345678 &&
                target.metadata.ftCreationTime.dwHighDateTime == 0x10203040 &&
                target.metadata.nFileSizeLow == 55,
        "decode little-endian cached target metadata");
    require(parse(relative_link(u"..\\Caf\u00e9", false)).relative_path == "..\\Caf\xc3\xa9",
        "decode ANSI StringData as Windows-1252");
    require(parse(relative_link(u"..\\\U0001f334")).relative_path == "..\\\xf0\x9f\x8c\xb4",
        "decode UTF-16 surrogate pairs independently of host wchar_t");
    for (std::size_t n = 0; n < complete.size(); ++n)
    {
        rejects<InvalidLink>(
            Bytes(complete.begin(), complete.begin() + n), "reject every truncated structure");
    }
    auto invalid = local_link(false);
    u32(invalid, 76 + 16, 0xffffffff);
    rejects<InvalidLink>(invalid, "bound LinkInfo string offsets");
    invalid = complete;
    u16(invalid, 78, 1);
    rejects<InvalidLink>(invalid, "reject undersized IDList entries");
    invalid = relative_link(std::u16string(1, char16_t(0xd800)));
    rejects<InvalidLink>(invalid, "reject an unpaired surrogate");
    invalid = relative_link(u"target");
    u16(invalid, 76, 0xffff);
    rejects<InvalidLink>(invalid, "bound StringData character counts");
    invalid = complete;
    invalid[4] ^= 1;
    rejects<InvalidLink>(invalid, "verify the ShellLink class identifier");
    auto unsupported = relative_link(u"target");
    u32(unsupported, 20, 0x208);
    rejects<UnsupportedLink>(unsupported, "do not ignore environment targets");
    unsupported = local_link(false);
    u32(unsupported, 84, 2);
    rejects<UnsupportedLink>(unsupported, "do not pretend network locations are local");
    unsupported = header(1);
    word(unsupported, 2);
    word(unsupported, 0);
    unsupported.resize(unsupported.size() + 4);
    rejects<UnsupportedLink>(unsupported, "reject shell-namespace-only targets");
    unsupported = relative_link(u"target");
    unsupported.resize(unsupported.size() - 4);
    const auto block = unsupported.size();
    unsupported.resize(block + 12);
    u32(unsupported, block, 8);
    u32(unsupported, block + 4, 0xa0000001);
    rejects<UnsupportedLink>(unsupported, "reject target-changing ExtraData");
}

struct Fixture
{
    std::filesystem::path root;
    std::vector<beachlife::data::SourceEntry> entries;
    Fixture()
    {
        char pattern[] = "/tmp/beachlife-shell-link-XXXXXX";
        const auto* directory = mkdtemp(pattern);
        require(directory, "create isolated generated fixture");
        root = directory;
        require(!setenv("XDG_DATA_HOME", root.c_str(), 1), "isolate per-user data lookup");
        add("Music/Links/Folder.lnk", relative_link(u"..\\Tracks"));
        add("Music/Links/Portable.lnk", local_link(true, true));
        add("Music/Links/Missing.lnk", relative_link(u"..\\Missing"));
        add("Music/Links/Broken.lnk", Bytes(10));
        add("Music/Links/Drive.lnk", local_link(false));
        auto unsupported = relative_link(u"target");
        u32(unsupported, 20, 0x208);
        add("Music/Links/Unsupported.lnk", unsupported);
        add("Music/Tracks/Tone.wav", Bytes{1, 2, 3});
    }
    void add(const std::string& name, const Bytes& bytes)
    {
        const auto path = root / "retail" / name;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        require(bool(file), "write generated link");
        entries.push_back({name, bytes.size()});
    }
    ~Fixture()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};

void stops_loudly(const std::function<void()>& action, std::string_view api)
{
    int pipes[2];
    require(!pipe(pipes), "create diagnostic pipe");
    const auto pid = fork();
    require(pid >= 0, "fork unsupported-variant check");
    if (!pid)
    {
        const rlimit no_core{0, 0};
        setrlimit(RLIMIT_CORE, &no_core);
        close(pipes[0]);
        dup2(pipes[1], STDERR_FILENO);
        close(pipes[1]);
        action();
        _exit(0);
    }
    close(pipes[1]);
    std::string message;
    std::array<char, 512> buffer;
    ssize_t count;
    while ((count = read(pipes[0], buffer.data(), buffer.size())) > 0)
    {
        message.append(buffer.data(), count);
    }
    close(pipes[0]);
    int status = 0;
    require(waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT &&
                message.find(api) != std::string::npos,
        "unsupported variants abort with the API named");
}

void com_checks()
{
    Fixture fixture;
    auto source =
        std::make_shared<beachlife::data::FolderSource>(fixture.root / "retail", fixture.entries);
    beachlife::runtime::retail_files::Session files(source);
    beachlife::compat::DirectorySession directories(source->entries());
    constexpr IID unknown_iid = {0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
    IShellLinkA* link = nullptr;
    require(
        CoCreateInstance(
            CLSID_ShellLink, nullptr, 1, IID_IShellLinkA, reinterpret_cast<void**>(&link)) == S_OK,
        "create ShellLink through the shared COM dispatcher");
    IPersistFile* file = nullptr;
    require(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)) == S_OK,
        "query the persist interface");
    IUnknown* identity = nullptr;
    require(file->QueryInterface(unknown_iid, reinterpret_cast<void**>(&identity)) == S_OK &&
                identity == static_cast<IUnknown*>(link) && identity->Release() == 2,
        "preserve COM identity across interfaces");
    CLSID clsid{};
    require(file->GetClassID(&clsid) == S_OK &&
                !std::memcmp(&clsid, &CLSID_ShellLink, sizeof(clsid)) && file->IsDirty() == 1,
        "report the class and unmodified loaded state");
    // Independently call the vtable slots the retail ABI uses: Load=5,
    // GetPath=3, Resolve=19, irrespective of our declarations' method order.
    const auto load = reinterpret_cast<HRESULT (*)(IPersistFile*, const unsigned short*, DWORD)>(
        (*reinterpret_cast<void***>(file))[5]);
    const auto resolve = reinterpret_cast<HRESULT (*)(IShellLinkA*, HWND, DWORD)>(
        (*reinterpret_cast<void***>(link))[19]);
    const auto get_path =
        reinterpret_cast<HRESULT (*)(IShellLinkA*, char*, int, WIN32_FIND_DATAA*, DWORD)>(
            (*reinterpret_cast<void***>(link))[3]);
    const std::array<unsigned short, 23> path{'M',
        'u',
        's',
        'i',
        'c',
        '\\',
        'L',
        'i',
        'n',
        'k',
        's',
        '\\',
        'F',
        'o',
        'l',
        'd',
        'e',
        'r',
        '.',
        'l',
        'n',
        'k',
        0};
    require(load(file, path.data(), 0) == S_OK && resolve(link, nullptr, 1) == S_OK,
        "load UTF-16 through the VFS and resolve against the shortcut directory");
    struct Output
    {
        std::array<char, 260> path;
        WIN32_FIND_DATAA data;
        DWORD guard;
    } output{};
    output.guard = 0xfeedface;
    require(get_path(link, output.path.data(), output.path.size(), &output.data, 1) == S_OK &&
                std::strcmp(output.path.data(), "music\\Tracks") == 0 &&
                (output.data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                std::strcmp(output.data.cFileName, "Tracks") == 0 &&
                !output.data.cAlternateFileName[0] && output.guard == 0xfeedface,
        "return the actual folder and a bounded 320-byte find-data record");
    std::array<char, 4> small{'!', '!', '!', '!'};
    require(FAILED(link->GetPath(small.data(), 3, nullptr, 0)) && !small[0] && small[3] == '!',
        "report insufficient path storage without overrunning it");
    const auto load_name = [&](const char* name)
    {
        std::array<WCHAR, 260> wide{};
        require(MultiByteToWideChar(0, 0, name, -1, wide.data(), wide.size()) > 0,
            "convert a generated filename");
        return file->Load(wide.data(), 0);
    };
    require(load_name("Music\\Links\\Portable.lnk") == S_OK && link->Resolve(nullptr, 1) == S_OK,
        "resolve relocated Windows links using their relative path in the VFS");
    require(load_name("Music\\Links\\Missing.lnk") == S_OK && FAILED(link->Resolve(nullptr, 1)),
        "do not report missing targets as resolved");
    require(FAILED(load_name("Music\\Links\\Broken.lnk")) &&
                FAILED(link->GetPath(output.path.data(), 260, nullptr, 0)) && output.path[0] == 0,
        "a malformed reload clears the previous target");
    require(
        FAILED(load_name("Music\\Links\\Absent.lnk")), "return a failure for an absent shortcut");
    stops_loudly(
        [&]
        {
            load_name("Music\\Links\\Unsupported.lnk");
        },
        "IPersistFile::Load");
    require(load_name("Music\\Links\\Drive.lnk") == S_OK, "load an explicit Windows drive target");
    stops_loudly(
        [&]
        {
            link->Resolve(nullptr, 1);
        },
        "IShellLinkA::Resolve");
    void* unknown = link;
    const IID other{0x12345678, 0, 0, {}};
    require(FAILED(link->QueryInterface(other, &unknown)) && !unknown,
        "reject unsupported interface queries");
    require(file->Release() == 1 && link->Release() == 0, "release both COM views exactly once");
}
} // namespace

int main()
{
    parser_checks();
    com_checks();
    std::puts("Generated ShellLink parser, COM ABI, VFS resolution and failure checks passed");
}
