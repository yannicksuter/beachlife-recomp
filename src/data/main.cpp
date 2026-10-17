#include "source.h"
#include "cabinet.h"
#include "game_index.h"
#include "game_path.h"
#include "content_prepare.h"
#include "platform/path.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace {
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }

std::string json_string(std::string_view value) {
    std::string text = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : value) {
        if (ch == '\\' || ch == '"') { text += '\\'; text += static_cast<char>(ch); }
        else if (ch < 32) { text += "\\u00"; text += hex[ch >> 4]; text += hex[ch & 15]; }
        else text += static_cast<char>(ch);
    }
    return text+'"';
}

std::uint64_t number(std::string_view text) {
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data()+text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data()+text.size())
        throw std::invalid_argument("Offset and count must be unsigned decimal integers.");
    return value;
}

int run(const std::vector<std::string>& args) {
    try {
        if (args.size() == 2 && args[1] == "--help") {
            std::cout << "beachlife-data list image.iso\n"
                         "beachlife-data cabinets image.iso\n"
                         "beachlife-data game-files image.iso\n"
                         "beachlife-data game-cat image.iso logical/path\n"
                         "beachlife-data cat image.iso logical/path\n"
                         "beachlife-data read image.iso logical/path offset count\n\n"
                         "beachlife-data prepare image.iso [cache-directory]\n"
                         "beachlife-data vfs-list prepared-directory\n"
                         "beachlife-data vfs-cat prepared-directory logical/path\n"
                         "beachlife-data vfs-read prepared-directory logical/path offset count\n"
                         "beachlife-data verify prepared-directory\n\n"
                         "List/prepare commands return JSON. Cat/read write raw bytes to stdout.\n"
                         "Preparation progress goes to stderr; Ctrl+C cancels and permits resuming.\n";
            return 0;
        }
        if (args.size() < 3) throw std::invalid_argument("Use --help for data commands.");
        const auto& name = args[2];
        const std::filesystem::path image(std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size()));
        if ((args.size() == 3 || args.size() == 4) && args[1] == "prepare") {
            std::signal(SIGINT, interrupt);
            std::signal(SIGTERM, interrupt);
            auto previous = std::chrono::steady_clock::time_point{};
            std::string phase;
            beachlife::data::PrepareOptions options;
            options.cancelled = [] { return interrupted != 0; };
            options.progress = [&](const auto& progress) {
                const auto now = std::chrono::steady_clock::now();
                if (phase != progress.phase || now-previous >= std::chrono::milliseconds(250)) {
                    std::cerr << progress.phase << ": " << progress.completed_bytes << '/'
                              << progress.total_bytes << " bytes (" << progress.completed_files << '/'
                              << progress.total_files << ") " << progress.path << '\n';
                    phase = progress.phase; previous = now;
                }
            };
            const auto result = beachlife::data::prepare_content(image,
                args.size() == 4 ? beachlife::platform::utf8_path(args[3]) : beachlife::data::default_content_directory(), options);
            std::cout << "{\"root\":" << json_string(beachlife::platform::utf8(result.root))
                      << ",\"identity\":" << json_string(result.manifest.identity())
                      << ",\"files\":" << result.manifest.files.size()
                      << ",\"bytes\":" << result.manifest.total_bytes
                      << ",\"reused\":" << result.files_reused << ",\"written\":" << result.files_written << "}\n";
            return std::cout ? 0 : 1;
        }
        if (args.size() == 3 && (args[1] == "vfs-list" || args[1] == "verify")) {
            const beachlife::data::PreparedSource source(image);
            if (args[1] == "verify") {
                source.verify();
                std::cout << "Verified " << source.manifest().files.size() << " prepared game files.\n";
            } else std::cout << beachlife::data::content_manifest_json(source.manifest());
            return std::cout ? 0 : 1;
        }
        if (args.size() == 3 && args[1] == "list") {
            beachlife::data::write_iso_json(std::cout, beachlife::data::index_iso(image));
            return 0;
        }
        if (args.size() == 3 && args[1] == "cabinets") {
            const beachlife::data::IsoSource source(image);
            beachlife::data::write_cabinet_json(std::cout, beachlife::data::index_cabinet(source));
            std::cout.flush();
            if (!std::cout) throw std::runtime_error("Cannot write output.");
            return 0;
        }
        if (args.size() == 3 && args[1] == "game-files") {
            const beachlife::data::IsoSource source(image);
            beachlife::data::write_game_index_json(std::cout,
                beachlife::data::map_game_files(beachlife::data::index_cabinet(source), source.index()));
            return std::cout ? 0 : 1;
        }
        if (args.size() == 4 && args[1] == "game-cat") {
            const beachlife::data::IsoSource source(image);
            beachlife::data::CabinetReader cabinet(source);
            const auto index = beachlife::data::map_game_files(cabinet.index(), source.index());
            const auto key = beachlife::data::normalize_game_path(args[3]).key;
            const auto found = std::find_if(index.files.begin(), index.files.end(), [&](const auto& file) {
                return beachlife::data::normalize_game_path(file.path).key == key;
            });
            if (found == index.files.end()) throw std::runtime_error("The game asset is not present in the source.");
#ifdef _WIN32
            if (_setmode(_fileno(stdout), _O_BINARY) == -1) throw std::runtime_error("Cannot set binary output.");
#endif
            cabinet.extract(found->cabinet_index, [&](std::span<const unsigned char> bytes) {
                std::cout.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                if (!std::cout) throw std::runtime_error("Cannot write output.");
            });
            std::cout.flush();
            return std::cout ? 0 : 1;
        }
        if (!((args.size() == 4 && (args[1] == "cat" || args[1] == "vfs-cat")) ||
              (args.size() == 6 && (args[1] == "read" || args[1] == "vfs-read"))))
            throw std::invalid_argument("Use --help for data commands.");
        std::unique_ptr<beachlife::data::FileSource> source;
        if (args[1].starts_with("vfs-")) source = std::make_unique<beachlife::data::PreparedSource>(image);
        else source = std::make_unique<beachlife::data::IsoSource>(image);
        auto file = source->open(args[3]);
        auto count = file->size();
        if (args[1] == "read" || args[1] == "vfs-read") {
            file->seek(number(args[4]));
            count = number(args[5]);
            if (count > file->size()-file->tell()) throw std::out_of_range("Read is outside the file.");
        }
#ifdef _WIN32
        if (_setmode(_fileno(stdout), _O_BINARY) == -1) throw std::runtime_error("Cannot set binary output.");
#endif
        std::array<unsigned char, 65536> buffer{};
        while (count) {
            const auto amount = static_cast<std::size_t>(std::min<std::uint64_t>(count, buffer.size()));
            const auto received = file->read(std::span(buffer).first(amount));
            if (!received) throw std::runtime_error("Unexpected end of the file.");
            std::cout.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(received));
            if (!std::cout) throw std::runtime_error("Cannot write output.");
            count -= received;
        }
        std::cout.flush();
        if (!std::cout) throw std::runtime_error("Cannot write output.");
        return 0;
    } catch (const beachlife::data::PreparationCancelled& error) {
        std::cerr << "Beach Life data: " << error.what() << '\n';
        return 130;
    } catch (const std::exception& error) {
        std::cerr << "Beach Life data: " << error.what() << '\n';
        return 1;
    }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (!count) return 1;
        std::string text(static_cast<std::size_t>(count), '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, text.data(), count, nullptr, nullptr)) return 1;
        text.pop_back(); args.push_back(std::move(text));
    }
    return run(args);
}
#else
int main(int argc, char** argv) { return run({argv, argv+argc}); }
#endif
