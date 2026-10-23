#pragma once

#include <memory>

// The boundary between Play and the boot build's decompiled game entry.
namespace beachlife::platform
{
class Video;
}

namespace beachlife::data
{
class FileSource;
}

namespace beachlife::runtime
{
// What the decompiled entry receives in place of the retail window class,
// window and message loop. Play owns all three.
struct GameHost
{
    platform::Video& video;
    // Handles pending window events; false once the player has quit.
    bool (*pump_events)(void* context);
    void* context;
    // The prepared game data, mounted for the whole session.
    std::shared_ptr<const data::FileSource> source;
};

using GameEntry = int (*)(GameHost& host);

// Play runs this entry after loading instead of its placeholder frame. Set
// before runtime::run; the boot build's bootstrap sets it.
void set_game_entry(GameEntry entry);
} // namespace beachlife::runtime
