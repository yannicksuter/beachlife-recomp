#include "data/game_index.h"
#include "data/wise.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace beachlife::data;
void expect(bool value)
{
    if (!value)
    {
        throw std::runtime_error("Game mapping check failed.");
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

// A generated Wise installer: a minimal PE image, then raw-deflate streams each
// followed by the CRC-32 of its inflated bytes. No retail installer bytes.
class MemoryFile final : public SourceFile
{
  public:
    explicit MemoryFile(std::vector<unsigned char> bytes)
        : bytes_(std::move(bytes))
    {
    }
    std::uint64_t size() const override
    {
        return bytes_.size();
    }
    std::uint64_t tell() const override
    {
        return position_;
    }
    void seek(std::uint64_t position) override
    {
        position_ = std::min<std::uint64_t>(position, bytes_.size());
    }
    std::size_t read(std::span<unsigned char> destination) override
    {
        const auto count = static_cast<std::size_t>(
            std::min<std::uint64_t>(destination.size(), bytes_.size() - position_));
        std::memcpy(destination.data(), bytes_.data() + position_, count);
        position_ += count;
        return count;
    }

  private:
    std::vector<unsigned char> bytes_;
    std::uint64_t position_ = 0;
};

void put32(std::vector<unsigned char>& bytes, std::size_t at, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        bytes.at(at + i) = static_cast<unsigned char>(value >> (8 * i));
    }
}

std::uint32_t crc_of(const std::string& text)
{
    return static_cast<std::uint32_t>(
        crc32(0, reinterpret_cast<const Bytef*>(text.data()), static_cast<uInt>(text.size())));
}

// Appends one stream; returns its offset.
std::size_t append_stream(std::vector<unsigned char>& image, const std::string& text)
{
    z_stream z{};
    expect(deflateInit2(&z, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    std::vector<unsigned char> out(text.size() + 128);
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(text.data()));
    z.avail_in = static_cast<uInt>(text.size());
    z.next_out = out.data();
    z.avail_out = static_cast<uInt>(out.size());
    expect(deflate(&z, Z_FINISH) == Z_STREAM_END);
    out.resize(z.total_out);
    deflateEnd(&z);
    const auto offset = image.size();
    image.insert(image.end(), out.begin(), out.end());
    image.resize(image.size() + 4);
    put32(image, image.size() - 4, crc_of(text));
    return offset;
}

std::string record(
    std::uint32_t start, std::uint32_t end, const std::string& data, const std::string& path)
{
    // Opcode and flags, then start, end, date/time, size, 20 zero bytes, CRC.
    std::vector<unsigned char> bytes(48, 0);
    put32(bytes, 8, start);
    put32(bytes, 12, end);
    put32(bytes, 20, static_cast<std::uint32_t>(data.size()));
    put32(bytes, 44, crc_of(data));
    return std::string(bytes.begin() + 4, bytes.end()) + "%MAINDIR%\\" + path +
           std::string(1, '\0');
}

void check_wise()
{
    std::vector<unsigned char> image(0x200, 0);
    image[0] = 'M';
    image[1] = 'Z';
    put32(image, 0x3c, 0x40);
    put32(image, 0x40, 0x00004550);
    image[0x46] = 1;                // one section, no optional header
    put32(image, 0x58 + 16, 0x100); // raw size
    put32(image, 0x58 + 20, 0x100); // raw pointer: the overlay starts at 0x200
    image.insert(image.end(), {'W', 'i', 's', 'e', 0, 0, 0});
    const std::string exe = "generated executable bytes", text = "generated text data";
    // Data-area offsets: the base is the dummy stream; files follow it.
    const std::string dummy = "generated installer component";
    std::vector<unsigned char> probe;
    const auto dummy_bytes = append_stream(probe, dummy), exe_at = probe.size();
    append_stream(probe, exe);
    const auto text_at = probe.size();
    append_stream(probe, text);
    const auto end = probe.size();
    const std::string script =
        "header" + record(exe_at - dummy_bytes, text_at, exe, "BeachLife.exe") +
        record(text_at, end, text, "Data\\text_uk.ble") + record(1, 2, "x", "INSTALL.LOG\\");
    append_stream(image, script);
    const auto base = image.size();
    append_stream(image, dummy);
    append_stream(image, exe);
    append_stream(image, text);
    MemoryFile file(image);
    const auto installer = index_wise_file(file, "Setup.exe");
    expect(installer && installer->installer == "Setup.exe" && installer->files.size() == 2);
    expect(installer->files[0].path == "BeachLife.exe" &&
           installer->files[0].offset == base + exe_at && installer->files[0].size == exe.size() &&
           installer->files[0].crc == crc_of(exe));
    expect(installer->files[1].path == "Data\\text_uk.ble" &&
           installer->files[1].offset == base + text_at);
    const auto mapped = map_wise_game_files(*installer);
    expect(mapped.profile == "uk-wise-v1" && mapped.language == "en" && mapped.files.size() == 2 &&
           mapped.files[1].path == "Data/text_uk.ble" && mapped.files[1].wise &&
           mapped.files[1].wise_offset == base + text_at &&
           mapped.files[0].source_path == "Setup.exe");
    expect(
        supported_game_profile("uk-wise-v1", "en") && !supported_game_profile("uk-wise-v1", "pl"));
    rejects(
        [&]
        {
            map_wise_game_files(*installer, "pl");
        });
    // A damaged stream CRC: the data area is never found.
    auto damaged = image;
    damaged[base + exe_at - 1] ^= 1;
    MemoryFile damaged_file(damaged);
    expect(!index_wise_file(damaged_file, "Setup.exe"));
    MemoryFile not_pe(std::vector<unsigned char>(4096, 0x5a));
    expect(!index_wise_file(not_pe, "Setup.exe"));
}

int main()
{
    CabinetIndex cab;
    cab.files = {{"Setup.exe", "", "cabinet", 20},
        {"BeachLife.ini", "", "cabinet", 4},
        {"screen.tga", "Data\\Screens", "cabinet", 8},
        {"link.url", "Web", "cabinet", 1},
        {"", "", "placeholder", 0},
        {"loose.pis", "Data\\Script", "external", 3},
        {"BeachLife.exe", "", "external", 4}};
    cab.groups = {
        {"<Support>Files", 0, 0}, {"Ess_English", 1, 1}, {"UK Comp", 2, 4}, {"UK Uncomp", 5, 6}};
    IsoIndex iso;
    IsoEntry loose;
    loose.path = "DATA/Script/Loose.pis";
    loose.size = 3;
    iso.entries.push_back(loose);
    IsoEntry executable;
    executable.path = "BeachLife.exe";
    executable.size = 4;
    iso.entries.push_back(executable);
    const auto mapped = map_game_files(cab, iso);
    expect(mapped.files.size() == 4 && mapped.total_bytes == 19 && mapped.language == "en");
    expect(mapped.files[0].path == "BeachLife.exe" && mapped.files[0].external);
    expect(mapped.files[1].path == "BeachLife.ini");
    expect(mapped.files[3].external && mapped.files[3].source_path == loose.path);
    rejects(
        [&]
        {
            map_game_files(cab, IsoIndex{});
        });
    auto missing_executable = iso;
    missing_executable.entries.pop_back();
    rejects(
        [&]
        {
            map_game_files(cab, missing_executable);
        });
    auto duplicate_executable = iso;
    executable.path = "BEACHLIFE.EXE";
    duplicate_executable.entries.push_back(executable);
    rejects(
        [&]
        {
            map_game_files(cab, duplicate_executable);
        });
    auto compressed_executable = cab;
    compressed_executable.files[6].kind = "cabinet";
    expect(!map_game_files(compressed_executable, missing_executable).files[0].external);
    auto missing_descriptor = cab;
    missing_descriptor.groups[3].last_file = 5;
    rejects(
        [&]
        {
            map_game_files(missing_descriptor, iso);
        });
    auto bad = cab;
    bad.files[5].size = 4;
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    bad = cab;
    bad.files[2].directory = "../Data";
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    bad = cab;
    bad.files[2].name = "../screen.tga";
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    bad = cab;
    bad.groups[1].name = "Ess_German";
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    bad = cab;
    bad.groups.push_back(cab.groups[2]);
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    bad = cab;
    bad.files[3] = bad.files[2];
    bad.files[3].name = "SCREEN.TGA";
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    bad = cab;
    bad.files[3] = {"Screens", "Data", "cabinet", 8};
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });
    bad = cab;
    bad.files[2].name = "\xe9.tga";
    rejects(
        [&]
        {
            map_game_files(bad, iso);
        });

    // European disc: per-language INI groups, the game's Language order and a
    // copy-protected loose executable that differs from its descriptor.
    CabinetIndex euro;
    euro.files = {{"BeachLife.ini", "", "cabinet", 4},
        {"BeachLife.ini", "", "cabinet", 5},
        {"text_polish.ble", "Data", "cabinet", 8},
        {"L\xe9"
         "eme.htm",
            "",
            "cabinet",
            2},
        {"BeachLife.exe", "", "external", 4}};
    euro.groups = {
        {"Ess_Polish", 0, 0}, {"Ess_German", 1, 1}, {"EURO Comp", 2, 3}, {"EURO Uncomp", 4, 4}};
    auto protected_iso = iso;
    protected_iso.entries.back().size = 9;
    const auto german = map_game_files(euro, protected_iso);
    expect(german.profile == "euro-v1" && german.language == "de" &&
           german.languages == std::vector<std::string>{"de", "pl"});
    expect(german.files[0].path == "BeachLife.exe" && german.files[0].loose &&
           german.files[0].size == 9 && german.files[1].size == 5 && german.files.size() == 3);
    const auto polish = map_game_files(euro, protected_iso, "pl");
    expect(polish.language == "pl" && polish.files[1].size == 4 && polish.total_bytes == 21);
    expect(supported_game_profile("euro-v1", "pl") && !supported_game_profile("euro-v1", "en") &&
           !supported_game_profile("uk-english-v1", "pl") && language_name("pl") == "Polish");
    rejects(
        [&]
        {
            map_game_files(euro, protected_iso, "en");
        });
    rejects(
        [&]
        {
            map_game_files(cab, iso, "pl");
        });
    // US disc: SpringBreak.exe is read as BeachLife.exe and the names the UK
    // code opens alias the "_us" files, which keep their own names too.
    CabinetIndex us;
    us.files = {{"BeachLife.ini", "", "cabinet", 4},
        {"text_us.ble", "Data", "cabinet", 6},
        {"MainMenu_us.ble", "Data\\Script\\GUI", "cabinet", 1},
        {"Intro_us.ble", "Data\\Script\\GUI", "cabinet", 1},
        {"gamewon_us.ble", "Data\\Script\\GUI", "cabinet", 1},
        {"loading_us_1.tga", "Data\\Screens", "cabinet", 2},
        {"loading_us_2.tga", "Data\\Screens", "cabinet", 2},
        {"FrontScreen_us.tga", "Data\\Screens", "cabinet", 3},
        {"SpringBreak.exe", "", "external", 4}};
    us.groups = {{"Ess_English", 0, 0}, {"US Comp", 1, 7}, {"US Uncomp", 8, 8}};
    IsoIndex us_iso;
    IsoEntry spring_break;
    spring_break.path = "SpringBreak.exe";
    spring_break.size = 7;
    us_iso.entries.push_back(spring_break);
    const auto american = map_game_files(us, us_iso);
    std::set<std::string> us_paths;
    for (const auto& file : american.files)
    {
        us_paths.insert(file.path);
    }
    expect(american.profile == "us-english-v1" && american.language == "en" &&
           american.files.size() == 16 && us_paths.contains("BeachLife.exe") &&
           !us_paths.contains("SpringBreak.exe") && us_paths.contains("Data/text_uk.ble") &&
           us_paths.contains("Data/text_us.ble") &&
           us_paths.contains("Data/Screens/loading_2.tga") &&
           us_paths.contains("Data/Script/GUI/MainMenu.ble"));
    expect(supported_game_profile("us-english-v1", "en") &&
           !supported_game_profile("us-english-v1", "pl"));
    us.files[6].name = "missing.tga";
    rejects(
        [&]
        {
            map_game_files(us, us_iso);
        });
    check_wise();
    std::cout << "Game path mapping, support exclusion, loose files and ambiguity checks passed.\n";
}
