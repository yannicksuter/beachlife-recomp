#pragma once

#include <string>
#include <string_view>

namespace beachlife::data
{

struct GamePath
{
    std::string display;
    std::string key;
};

// Relative UTF-8 names with Windows separators and ASCII case-insensitive keys.
// Non-ASCII characters retain their spelling; parent traversal is rejected.
GamePath normalize_game_path(std::string_view path);

} // namespace beachlife::data
