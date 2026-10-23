#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace beachlife::platform
{

// UTF-8 text for a path and back, the same on every system (std::filesystem's
// narrow strings use the Windows code page there).
inline std::string utf8(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

inline std::filesystem::path utf8_path(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

} // namespace beachlife::platform
