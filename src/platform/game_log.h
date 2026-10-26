#pragma once

#include <filesystem>
#include <string>

// A log of each game session, to find out why one ended: the game's own messages
// (traps, missing host services, its message boxes) and, when the process crashes,
// what stopped it with the call stack. It lives in a logs folder beside the launcher
// settings and keeps the previous session's log as game-previous.log. Nothing is
// sent anywhere.
namespace beachlife::platform
{

class GameLog
{
  public:
    // Starts <folder>/game.log with `header`. While the session runs, the game's
    // messages on stderr and stdout go to it (a terminal the game was started from
    // keeps them), and a crash appends its cause and call stack before the
    // system's own crash handling.
    GameLog(const std::filesystem::path& folder, const std::string& header);
    // Marks the session as ended and restores the previous output.
    ~GameLog();
    GameLog(const GameLog&) = delete;
    GameLog& operator=(const GameLog&) = delete;
};

// The last session's log when that session crashed or was killed and no launcher
// has reported it yet, else empty. Marks it reported.
std::filesystem::path unreported_crash(const std::filesystem::path& folder);

} // namespace beachlife::platform
