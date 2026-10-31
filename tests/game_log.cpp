#include "platform/game_log.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>

namespace
{
namespace fs = std::filesystem;
unsigned checks = 0;

void require(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

std::string read(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

[[noreturn]] void fail()
{
    throw std::runtime_error("the disk is on fire");
}

void error_ends_session(const fs::path& folder)
{
    {
        beachlife::platform::GameLog log(folder, "header");
        try
        {
            fail();
        }
        catch (const std::exception& error)
        {
            beachlife::platform::log_fatal_error(error.what());
        }
    }
    const std::string text = read(folder / "game.log");
    require(text.find("Beach Life stopped: the disk is on fire") != std::string::npos,
        "The error's message is missing from the log");
    require(text.find("Call stack where it was thrown") != std::string::npos,
        "The throw site's call stack was not kept");
    require(beachlife::platform::unreported_crash(folder) == folder / "game.log",
        "A session ended by an error was not reported");
    require(beachlife::platform::unreported_crash(folder).empty(),
        "A reported session was reported again");
}

void ordinary_session(const fs::path& folder)
{
    {
        beachlife::platform::GameLog log(folder, "header");
        try
        {
            fail(); // caught by the game itself, not an error that ends the session
        }
        catch (const std::exception&)
        {
        }
    }
    require(beachlife::platform::unreported_crash(folder).empty(),
        "A session that ended in order was reported");
}
} // namespace

int main()
{
    const fs::path folder = fs::temp_directory_path() /
                            ("beachlife-game-log-" + std::to_string(std::random_device{}()));
    int status = 0;
    try
    {
        error_ends_session(folder);
        ordinary_session(folder);
        std::printf("{\"checks\":%u}\n", checks);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        status = 1;
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    return status;
}
