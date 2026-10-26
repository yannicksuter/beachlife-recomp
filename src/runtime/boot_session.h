#pragma once

#include "runtime/game_host.h"

namespace beachlife::runtime
{
// Binds the prepared game data for the decompiled game, then runs its entry.
int run_boot_game(GameHost& host);
} // namespace beachlife::runtime
