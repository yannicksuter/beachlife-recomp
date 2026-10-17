#include "game_path.h"

#include <cstdint>
#include <stdexcept>

namespace beachlife::data {
namespace {
void require(bool valid) {
    if (!valid) throw std::invalid_argument("Invalid relative game path.");
}

void validate_utf8(std::string_view text) {
    for (std::size_t pos = 0; pos < text.size();) {
        const auto lead = static_cast<unsigned char>(text[pos++]);
        if (lead < 0x80) { require(lead >= 32 && lead != 127); continue; }
        const unsigned count = lead >= 0xc2 && lead <= 0xdf ? 1 :
                               lead >= 0xe0 && lead <= 0xef ? 2 :
                               lead >= 0xf0 && lead <= 0xf4 ? 3 : 0;
        require(count != 0 && count <= text.size()-pos);
        std::uint32_t code = lead & (0x7f >> count);
        for (unsigned i = 0; i < count; ++i) {
            const auto next = static_cast<unsigned char>(text[pos++]);
            require((next & 0xc0) == 0x80);
            code = (code << 6) | (next & 0x3f);
        }
        require(code >= (count == 1 ? 0x80u : count == 2 ? 0x800u : 0x10000u) &&
                code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff));
    }
}

bool device_name(std::string_view key) {
    const auto stem = key.substr(0, key.find('.'));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
        stem == "conin$" || stem == "conout$") return true;
    return stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
           stem[3] >= '1' && stem[3] <= '9';
}
} // namespace

GamePath normalize_game_path(std::string_view path) {
    require(!path.empty() && path.size() <= 4096 && path[0] != '/' && path[0] != '\\');
    validate_utf8(path);
    GamePath result;
    for (std::size_t pos = 0; pos < path.size();) {
        const auto next = path.find_first_of("/\\", pos);
        const auto part = path.substr(pos, next == std::string_view::npos ? next : next-pos);
        pos = next == std::string_view::npos ? path.size() : next+1;
        if (part.empty() || part == ".") continue;
        require(part != ".." && part.back() != '.' && part.back() != ' ' &&
                part.find_first_of("<>:\"|?*") == std::string_view::npos);
        std::string key(part);
        for (auto& ch : key) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch-'A'+'a');
        require(!device_name(key));
        if (!result.display.empty()) { result.display += '/'; result.key += '/'; }
        result.display += part;
        result.key += key;
    }
    require(!result.display.empty());
    return result;
}
} // namespace beachlife::data
