#include "data/folder_source.h"
#include "platform/sha256.h"
#include "runtime/credits_data.h"
#include "runtime/retail_files.h"
#include "startup_screen_credits_data.h"
#include "startup_screen_manager_owner190_rows.h"
#include "startup_screen_text.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace beachlife::runtime
{
struct CreditsDataTestAccess
{
    static CreditsData read(const data::FileSource& source, std::string_view identity)
    {
        return CreditsData::read_verified(source, identity);
    }
};
} // namespace beachlife::runtime

namespace
{
namespace fs = std::filesystem;
namespace data = beachlife::data;
namespace runtime = beachlife::runtime;
std::size_t checks = 0;

void expect(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

template <class F> void rejects(F action)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    expect(rejected, "invalid credits input or binding was accepted");
}

void put16(std::vector<unsigned char>& image, std::size_t at, std::uint16_t value)
{
    image.at(at) = static_cast<unsigned char>(value);
    image.at(at + 1) = static_cast<unsigned char>(value >> 8);
}

void put32(std::vector<unsigned char>& image, std::size_t at, std::uint32_t value)
{
    for (unsigned byte = 0; byte < 4; ++byte)
    {
        image.at(at + byte) = static_cast<unsigned char>(value >> (byte * 8));
    }
}

void write(const fs::path& name, std::span<const unsigned char> bytes)
{
    std::ofstream stream(name, std::ios::binary | std::ios::trunc);
    stream.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    expect(static_cast<bool>(stream), "cannot write generated credits input");
}

std::string digest(std::span<const unsigned char> bytes)
{
    return beachlife::platform::sha256_hex(
        {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

class Generated
{
  public:
    explicit Generated(fs::path path)
        : root(std::move(path))
    {
        expect(fs::create_directory(root), "generated credits directory already exists");
    }
    ~Generated()
    {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    Generated(const Generated&) = delete;
    Generated& operator=(const Generated&) = delete;
    fs::path root;
};

std::vector<unsigned char> generated_image()
{
    // A synthetic PE data fixture: no retail image, instructions, rows or names.
    std::vector<unsigned char> bytes(runtime::CreditsData::supported_file_bytes, 0);
    put16(bytes, 0, 0x5a4d);
    put32(bytes, 0x3c, 0x80);
    put32(bytes, 0x80, 0x00004550);
    put16(bytes, 0x84, 0x014c);
    put16(bytes, 0x86, 1);
    put16(bytes, 0x94, 224);
    put16(bytes, 0x98, 0x010b);
    put32(bytes, 0x98 + 28, 0x00400000);
    put32(bytes, 0x98 + 56, 0x00200000);
    constexpr std::size_t section = 0x178;
    put32(bytes, section + 8, 0x2000);
    put32(bytes, section + 12, 0x180000);
    put32(bytes, section + 16, 0x2000);
    put32(bytes, section + 20, 0x400);
    put32(bytes, section + 36, 0xc0000040);
    constexpr std::size_t rows = 0x570;
    for (std::size_t i = 0; i < 205; ++i)
    {
        const auto at = rows + i * 20;
        put32(bytes, at, std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(i % 3) - 1));
        put32(bytes, at + 4, static_cast<std::uint32_t>(1000 + i));
        put32(bytes, at + 8, std::bit_cast<std::uint32_t>(-3 * static_cast<std::int32_t>(i)));
        bytes[at + 12] = i % 2 ? 0x80 : 0;
        bytes[at + 13] = 0xa5;
        bytes[at + 14] = static_cast<unsigned char>(i);
        bytes[at + 15] = 0x5a;
        put32(bytes, at + 16, i == 204 ? 1 : 0);
    }
    constexpr char format[] = "FIXTURE_LINE_%d";
    static_assert(sizeof(format) == runtime::CreditsData::format_bytes);
    std::copy(std::begin(format), std::end(format), bytes.begin() + rows + 4100);
    return bytes;
}

void run(const fs::path& root)
{
    Generated generated(root);
    const data::FolderSource missing(root, {});
    expect(!runtime::CreditsData::read_if_available(missing),
        "missing credits data rejected unrelated startup");
    rejects(RequireCreditsData);
    auto bytes = generated_image();
    write(root / "BeachLife.exe", bytes);
    const data::FolderSource executable(root, {{"BeachLife.exe", bytes.size()}});
    rejects(RequireCreditsData);
    rejects(
        [&]
        {
            runtime::CreditsData::read(executable);
        });
    expect(!runtime::CreditsData::read_if_available(executable),
        "an unsupported credits revision rejected unrelated startup");
    rejects(RequireCreditsData);
    const auto input = runtime::CreditsDataTestAccess::read(executable, digest(bytes));

    // Same decoder, with independently hashed malformed generated layouts.
    auto malformed = bytes;
    put32(malformed, 0x178 + 20, static_cast<std::uint32_t>(malformed.size() - 1));
    write(root / "BeachLife.exe", malformed);
    rejects(
        [&]
        {
            runtime::CreditsDataTestAccess::read(executable, digest(malformed));
        });
    malformed = bytes;
    malformed[0x570 + 4100 + 15] = 1;
    write(root / "BeachLife.exe", malformed);
    rejects(
        [&]
        {
            runtime::CreditsDataTestAccess::read(executable, digest(malformed));
        });
    write(root / "BeachLife.exe", bytes);

    const std::u16string_view text = u"\ufeff\"FIXTURE_LINE_1000\" \"first\"\n"
                                     u"\"FIXTURE_LINE_1102\" \"\u03a9\"\n"
                                     u"\"FIXTURE_LINE_1204\" \"last\"\n";
    std::vector<unsigned char> encoded;
    for (char16_t unit : text)
    {
        encoded.push_back(static_cast<unsigned char>(~static_cast<unsigned>(unit)));
        encoded.push_back(static_cast<unsigned char>(~(static_cast<unsigned>(unit) >> 8)));
    }
    write(root / "credits.ble", encoded);
    auto language_source = std::make_shared<data::FolderSource>(
        root, std::vector<data::SourceEntry>{{"credits.ble", encoded.size()}});
    expect(screen_text_lookup == nullptr, "language singleton was already live");
    {
        runtime::retail_files::Session files(language_source);
        ScreenTextLookup language("credits.uni");
        CreditsDataBinding binding(input);
        RequireCreditsData();
        expect(screen_manager_owner190_rows[204].word_10 == 1,
            "credits final-row marker was not retained");
        for (std::size_t i = 0; i < 205; ++i)
        {
            const auto& row = screen_manager_owner190_rows[i];
            expect(row.font_index == static_cast<int>(i % 3) - 1 &&
                       row.key_number == static_cast<int>(1000 + i) &&
                       row.word_08 == -3 * static_cast<int>(i) &&
                       row.flag_0c == (i % 2 ? 0x80 : 0) && row.unknown_0d[0] == 0xa5 &&
                       row.unknown_0d[1] == i && row.unknown_0d[2] == 0x5a &&
                       row.word_10 == (i == 204 ? 1u : 0u),
                "credits row order or scalar bits changed");
        }
        expect(std::strcmp(screen_manager_owner190_key_format, "FIXTURE_LINE_%d") == 0,
            "credits key format was not loaded");
        const std::array<unsigned, 3> selected{0, 102, 204};
        const std::array<std::u16string_view, 3> expected{u"first", u"\u03a9", u"last"};
        for (std::size_t i = 0; i < selected.size(); ++i)
        {
            char key[128];
            std::snprintf(key,
                sizeof(key),
                screen_manager_owner190_key_format,
                screen_manager_owner190_rows[selected[i]].key_number);
            const unsigned short* actual = language.LookupText(key);
            expect(actual != nullptr, "real language lookup missed a generated credits key");
            for (std::size_t unit = 0; unit <= expected[i].size(); ++unit)
            {
                expect(actual[unit] == (unit == expected[i].size() ? 0 : expected[i][unit]),
                    "real credits text changed its UTF16 units");
            }
        }
        auto* alias = &screen_manager_owner190_rows[101];
        alias->word_08 = 77;
        screen_manager_owner190_key_format[0] = 'Z';
        expect(screen_manager_owner190_rows[101].word_08 == 77 &&
                   screen_manager_owner190_key_format[0] == 'Z',
            "credits global aliases are not writable");
        rejects(
            [&]
            {
                CreditsDataBinding duplicate(input);
            });
        expect(alias->word_08 == 77 && screen_manager_owner190_key_format[0] == 'Z',
            "a rejected binding partially replaced live credits data");
        bool wrong_thread_rejected = false;
        std::thread other(
            [&]
            {
                try
                {
                    RequireCreditsData();
                }
                catch (const std::logic_error&)
                {
                    wrong_thread_rejected = true;
                }
            });
        other.join();
        expect(wrong_thread_rejected, "credits data accepted a foreign-thread consumer");
    }
    rejects(RequireCreditsData);
    expect(screen_text_lookup == nullptr, "real language owner did not unpublish itself");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
        {
            throw std::runtime_error("expected one new generated fixture directory");
        }
        run(argv[1]);
        std::cout << "groups=1 rows=205 language_owners=1 lookups=3 checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
