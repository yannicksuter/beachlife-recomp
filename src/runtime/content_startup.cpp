#include "runtime/content_startup.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace beachlife::runtime {

StartupStep read_game_files(std::shared_ptr<const data::FileSource> content) {
    if (!content) throw std::runtime_error("Game data is not mounted.");
    return {"Loading game files", [content = std::move(content)](StartupProgress& progress) {
        if (progress.cancelled()) return;
        auto ini = content->open("BeachLife.ini");
        const auto size = ini->size();
        if (size > 1024*1024) throw std::runtime_error("BeachLife.ini exceeds the startup read limit.");
        progress.total(size);
        std::array<unsigned char, 4096> buffer{};
        std::uint64_t read = 0;
        while (read < size) {
            if (progress.cancelled()) return;
            const auto remaining = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), size-read));
            const auto count = ini->read(std::span(buffer).first(remaining));
            if (progress.cancelled()) return;
            if (!count || count > remaining) throw std::runtime_error("Cannot read BeachLife.ini from prepared game data.");
            read += count;
            progress.advance(count);
        }
        if (progress.cancelled()) return;
        std::printf("Game data: BeachLife.ini %llu bytes read through VFS.\n", static_cast<unsigned long long>(read));
    }};
}

} // namespace beachlife::runtime
