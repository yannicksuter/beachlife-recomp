#pragma once

#include <filesystem>
struct SDL_Window;

namespace beachlife::platform
{
// Package artwork, independent of the selected ISO or the prepared game data.
void set_app_icon(SDL_Window* window, const std::filesystem::path& image);
} // namespace beachlife::platform
