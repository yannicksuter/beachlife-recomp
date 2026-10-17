#include "data/folder_source.h"
#include "platform/path.h"

#include <array>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace beachlife;
void expect(bool value) { if (!value) throw std::runtime_error("Folder VFS check failed."); }
template<class F> void rejects(F fn) {
    bool failed = false;
    try { fn(); } catch (const std::exception&) { failed = true; }
    expect(failed);
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto root = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(root / "Data");
    const auto name = platform::utf8_path("Data/Gr\xc3\xbc" "sse.txt");
    { std::ofstream out(root/name, std::ios::binary); out << "hello"; }
    { std::ofstream out(root/"private.txt"); out << "unlisted"; }
    std::unique_ptr<data::SourceFile> surviving;
    {
        const data::FolderSource source(root, {{platform::utf8(name), 5}});
        surviving = source.open(".\\DATA\\Gr\xc3\xbc" "SSE.TXT");
        auto other = source.open(platform::utf8(name));
        std::array<unsigned char, 8> bytes{};
        expect(surviving->size() == 5 && surviving->read(std::span(bytes).first(2)) == 2);
        expect(bytes[0] == 'h' && bytes[1] == 'e' && surviving->tell() == 2 && other->tell() == 0);
        surviving->seek(4); expect(surviving->read(bytes) == 1 && bytes[0] == 'o');
        expect(surviving->read(bytes) == 0);
        rejects([&] { surviving->seek(6); });
        rejects([&] { surviving->seek(std::numeric_limits<std::uint64_t>::max()); });
        expect(surviving->tell() == 5);
        surviving->seek(0); expect(surviving->read({}) == 0);
        for (const auto* path : {"../private.txt", "/private.txt", "private.txt", "Data", "Data/missing"})
            rejects([&] { source.open(path); });
    }
    std::array<unsigned char, 8> bytes{};
    expect(surviving->read(bytes) == 5 && std::string(bytes.begin(), bytes.begin()+5) == "hello");
    rejects([&] { data::FolderSource source(root, {{"Data/a",1},{"data/A",1}}); });
    rejects([&] { data::FolderSource source(root, {{"Data",1},{"Data/a",1}}); });
    rejects([&] { data::FolderSource source(root, {{"../outside",1}}); });
    rejects([&] { data::FolderSource source(root, {{"Data\\a",1}}); });
    const data::FolderSource wrong(root, {{platform::utf8(name),6}});
    rejects([&] { wrong.open(platform::utf8(name)); });
    const data::FolderSource directory(root, {{"Data",0}});
    rejects([&] { directory.open("Data"); });
    const data::FolderSource source(root, {{platform::utf8(name),5},{"Link/file",1},{"LinkFile",1}});
    auto truncated = source.open(platform::utf8(name));
    std::filesystem::resize_file(root/name, 1);
    rejects([&] { truncated->read(bytes); });
    rejects([&] { source.open(platform::utf8(name)); });
    std::error_code error;
    { std::ofstream out(root/"Data/file"); out << "x"; }
    std::filesystem::create_directory_symlink(root/"Data", root/"Link", error);
    if (!error) rejects([&] { source.open("Link/file"); });
    error.clear(); std::filesystem::create_symlink(root/name, root/"LinkFile", error);
    if (!error) rejects([&] { source.open("LinkFile"); });
    std::cout << "Prepared-folder reads, bounds, aliases, lifetime, truncation and link rejection passed.\n";
}
