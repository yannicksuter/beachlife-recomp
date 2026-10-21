#include "startup_screen_manager.h"
#include "startup_screen_placement.h"

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

#include <unistd.h>

namespace
{
namespace fs = std::filesystem;
using Words = std::array<unsigned int, 12>;

static_assert(CHAR_BIT == 8 && sizeof(unsigned int) == 4 && sizeof(void*) == 8);
static_assert(std::endian::native == std::endian::little);
static_assert(std::is_aggregate_v<ScreenRequestGamePrefix>);
static_assert(std::is_standard_layout_v<ScreenRequestGamePrefix>);
static_assert(std::is_trivially_destructible_v<ScreenRequestGamePrefix>);
static_assert(offsetof(ScreenRequestGamePrefix, unknown_00) == 0);
static_assert(offsetof(ScreenRequestGamePrefix, placement) == 88);
static_assert(offsetof(ScreenRequestGamePrefix, unknown_58) == 96);
static_assert(offsetof(ScreenRequestGamePrefix, words_70) == 120);
static_assert(offsetof(ScreenRequestGamePrefix, scenario_a0) == 168);
static_assert(offsetof(ScreenRequestGamePrefix, byte_a4) == 172);
static_assert(sizeof(ScreenRequestGamePrefix) == 176 && alignof(ScreenRequestGamePrefix) == 8);
static_assert(std::is_same_v<decltype(&ScreenRequestGamePrefix::SetByteA4),
    void (ScreenRequestGamePrefix::*)(unsigned char)>);
static_assert(std::is_same_v<decltype(&ScreenRequestGamePrefix::LoadScenarioProgress),
    void (ScreenRequestGamePrefix::*)()>);

struct Failure
{
    const char* message;
};
const char* current_case = "setup";
unsigned long long checks = 0;
unsigned calls = 0;

void require(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw Failure{message};
    }
}

constexpr Words defaults{4, 4, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1};
// High-byte bit08 at index11 must not defeat the last true low-byte flag at7.
constexpr Words flagged{0x100, 8, 4, 0x800, 0, 0xffffff00u, 1, 8, 2, 0, 16, 0x800};
constexpr Words capped{0x100, 8, 4, 0x800, 0, 0xffffff00u, 1, 8, 2, 0, 16, 8};
constexpr Words unflagged{0x800, 4, 0x100, 0xffffff00u, 1, 2, 16, 0, 0x800, 0, 0, 0x800};

std::vector<unsigned char> encode(const Words& words)
{
    std::vector<unsigned char> bytes;
    bytes.reserve(48);
    for (unsigned int word : words)
    {
        for (unsigned shift = 0; shift != 32; shift += 8)
        {
            bytes.push_back(static_cast<unsigned char>(word >> shift));
        }
    }
    return bytes;
}

struct Files
{
    fs::path root;
    Files()
    {
        std::string pattern = (fs::temp_directory_path() / "beachlife-progress-XXXXXX").string();
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back('\0');
        char* created = ::mkdtemp(name.data());
        require(created != nullptr, "create isolated generated-file directory");
        root = created;
    }
    ~Files()
    {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    fs::path write(const char* name, const std::vector<unsigned char>& bytes) const
    {
        const fs::path path = root / name;
        const std::string spelling = path.string();
        FILE* file = std::fopen(spelling.c_str(), "wb");
        require(file != nullptr, "open generated input for writing");
        const std::size_t written =
            bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), file);
        const int closed = std::fclose(file);
        require(written == bytes.size() && closed == 0, "finish generated input");
        return path;
    }
};

struct PathScope
{
    const char* previous;
    explicit PathScope(const char* path)
        : previous(startup_game_state_path)
    {
        startup_game_state_path = path;
    }
    ~PathScope()
    {
        startup_game_state_path = previous;
    }
};

unsigned descriptors()
{
    // Linux-only fixture boundary. Each traversal owns the same one temporary
    // directory descriptor, closed before returning; no game CRT interception.
    unsigned count = 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator("/proc/self/fd"))
    {
        ++count;
    }
    return count;
}

struct GuardedGame
{
    std::array<unsigned char, 16> before{};
    ScreenRequestGamePrefix value{};
    std::array<unsigned char, 16> after{};
    ScreenPlacementStore placement{};

    GuardedGame()
    {
        before.fill(0xc7);
        after.fill(0x7c);
        for (auto& byte : value.unknown_00)
        {
            byte = 0x36;
        }
        value.placement = &placement;
        for (auto& byte : value.unknown_58)
        {
            byte = 0x69;
        }
        for (auto& word : value.words_70)
        {
            word = 0xdeadbeefu;
        }
        value.scenario_a0 = 0x2468ace0u;
        value.byte_a4 = 0x5a;
    }
    void unchanged() const
    {
        for (std::size_t n = 0; n != before.size(); ++n)
        {
            require(before[n] == 0xc7 && after[n] == 0x7c, "game backing guard changed");
        }
        for (auto byte : value.unknown_00)
        {
            require(byte == 0x36, "game prefix opaque byte changed");
        }
        for (auto byte : value.unknown_58)
        {
            require(byte == 0x69, "game tail opaque byte changed");
        }
        require(value.placement == &placement, "unrelated placement pointer changed");
        for (auto byte : placement.unknown_00)
        {
            require(byte == 0, "borrowed placement prefix changed");
        }
        require(placement.saved.head == nullptr, "borrowed placement head changed");
    }
};
static_assert(offsetof(GuardedGame, value) == 16);
static_assert(offsetof(GuardedGame, after) == 16 + sizeof(ScreenRequestGamePrefix));

void run(const char* name,
    const fs::path& path,
    unsigned char mode,
    const Words& expected,
    unsigned int index)
{
    current_case = name;
    GuardedGame game;
    const char* old_path = startup_game_state_path;
    const std::string spelling = path.string();
    const unsigned before = descriptors();
    {
        PathScope selected(spelling.c_str());
        game.value.SetByteA4(mode);
        ++calls;
        require(startup_game_state_path == spelling.c_str(), "loader changed path pointer");
        require(game.value.byte_a4 == mode, "exact mode byte differs");
        for (std::size_t n = 0; n != expected.size(); ++n)
        {
            require(game.value.words_70[n] == expected[n], "literal progress word differs");
        }
        require(game.value.scenario_a0 == index, "literal scenario index differs");
        game.unchanged();
    }
    require(startup_game_state_path == old_path, "path owner was not restored");
    require(descriptors() == before, "loader retained an opened FILE descriptor");
}
} // namespace

int main()
{
    try
    {
        require(std::strcmp(startup_game_state_path, "sp.bin") == 0,
            "real path-global initialization differs");
        const char* original_path = startup_game_state_path;
        {
            Files files;
            const auto exact = files.write("exact.bin", encode(flagged));
            const auto last = files.write("last.bin", encode(capped));
            const auto none = files.write("none.bin", encode(unflagged));
            const auto empty = files.write("empty.bin", {});
            const auto partial = files.write("partial.bin", std::vector<unsigned char>(47, 0xff));
            const auto missing = files.root / "absent.bin";
            require(!fs::exists(missing), "missing input unexpectedly exists");

            run("full-record", exact, 0, flagged, 8);
            run("last-index-cap", last, 0, capped, 11);
            run("no-low-byte-flags", none, 0, unflagged, 0);
            run("missing", missing, 0, defaults, 0);
            run("empty", empty, 0, defaults, 0);
            run("partial-item", partial, 0, defaults, 0);
            run("mode-one", exact, 1, flagged, 0);
            run("mode-high-bit", exact, 0x80, flagged, 0);
            run("mode-all-bits", exact, 0xff, flagged, 0);
        }
        require(startup_game_state_path == original_path, "global owner escaped the fixture");
        require(calls == 9, "focused call count differs");
        std::printf("{\"groups\":3,\"mode_calls\":%u,\"progress_calls\":%u,"
                    "\"checks\":%llu,\"game_bytes\":%zu}\n",
            calls,
            calls,
            checks,
            sizeof(ScreenRequestGamePrefix));
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(
            stderr, "game-mode-progress failure case=%s: %s\n", current_case, failure.message);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "game-mode-progress host failure: %s\n", failure.what());
        return 2;
    }
}
