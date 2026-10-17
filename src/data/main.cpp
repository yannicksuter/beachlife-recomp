#include "source.h"

#include <algorithm>
#include <array>
#include <charconv>
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
                         "beachlife-data cat image.iso logical/path\n"
                         "beachlife-data read image.iso logical/path offset count\n\n"
                         "List returns JSON. Cat/read write raw bytes to stdout.\n";
            return 0;
        }
        if (args.size() < 3) throw std::invalid_argument("Use --help for data commands.");
        const auto& name = args[2];
        const std::filesystem::path image(std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size()));
        if (args.size() == 3 && args[1] == "list") {
            beachlife::data::write_iso_json(std::cout, beachlife::data::index_iso(image));
            return 0;
        }
        if (!((args.size() == 4 && args[1] == "cat") || (args.size() == 6 && args[1] == "read")))
            throw std::invalid_argument("Use --help for data commands.");
        const beachlife::data::IsoSource source(image);
        auto file = source.open(args[3]);
        auto count = file->size();
        if (args[1] == "read") {
            file->seek(number(args[4]));
            count = number(args[5]);
            if (count > file->size()-file->tell()) throw std::out_of_range("Read is outside the ISO file.");
        }
#ifdef _WIN32
        if (_setmode(_fileno(stdout), _O_BINARY) == -1) throw std::runtime_error("Cannot set binary output.");
#endif
        std::array<unsigned char, 65536> buffer{};
        while (count) {
            const auto amount = static_cast<std::size_t>(std::min<std::uint64_t>(count, buffer.size()));
            const auto received = file->read(std::span(buffer).first(amount));
            if (!received) throw std::runtime_error("Unexpected end of the ISO file.");
            std::cout.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(received));
            if (!std::cout) throw std::runtime_error("Cannot write output.");
            count -= received;
        }
        std::cout.flush();
        if (!std::cout) throw std::runtime_error("Cannot write output.");
        return 0;
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
