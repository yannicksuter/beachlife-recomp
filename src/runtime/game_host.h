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
// Host services borrowed by the compat layer while retail WinMain runs.
struct GameHost
{
    platform::Video& video;
    // Inject automation and schedule captures before the retail message poll.
    void (*prepare_frame)(void* context);
    void* context;
    // The prepared game data, mounted for the whole session.
    std::shared_ptr<const data::FileSource> source;
    bool hidden;
};

using GameEntry = int (*)(GameHost& host);

// Play runs this entry after loading instead of its placeholder frame. Set
// before runtime::run; the boot build's bootstrap sets it.
void set_game_entry(GameEntry entry);
} // namespace beachlife::runtime
