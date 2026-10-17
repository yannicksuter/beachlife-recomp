#include "build_version.h"
#include "launcher/launcher.h"
#include "data/iso9660.h"
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--inspect-iso") {
        try {
            const auto index = beachlife::data::index_iso(beachlife::launcher::path_from_utf8(argv[2]));
            beachlife::data::write_iso_json(std::cout, index);
            return 0;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "Beach Life: %s\n", error.what());
            return 1;
        }
    }
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::puts(beachlife::build::version);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--build-info") {
        std::puts(beachlife::build::json);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::puts("Beach Life - native launcher\n"
                  "Usage: beachlife [--launcher] [--source image.iso]\n"
                  "                 [--config file] [--page play|files|display|appearance|advanced|about]\n"
                  "                 [--screenshot file.png] [--smoke-test]\n"
                  "       beachlife --help | --version | --build-info\n\n"
                  "       beachlife --inspect-iso image.iso\n\n"
                  "Open without arguments to launch the graphical interface.\n"
                  "This preview selects and remembers your game source.\n"
                  "It does not yet load or run the game.");
        return 0;
    }
    beachlife::launcher::Options options;
    options.argc = argc;
    options.argv = argv;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--launcher") continue;
            if (arg == "--smoke-test") { options.smoke = true; continue; }
            if (i+1 == argc) throw std::runtime_error("Unknown or incomplete argument.");
            const std::string value = argv[++i];
            if (arg == "--config") options.config = beachlife::launcher::path_from_utf8(value);
            else if (arg == "--screenshot") options.screenshot = beachlife::launcher::path_from_utf8(value);
            else if (arg == "--update-preview") options.update_preview = value;
            else if (arg == "--source") options.source = value;
            else if (arg == "--page") {
                if (value == "play") options.page = beachlife::launcher::PagePlay;
                else if (value == "files") options.page = beachlife::launcher::PageFiles;
                else if (value == "display") options.page = beachlife::launcher::PageDisplay;
                else if (value == "appearance") options.page = beachlife::launcher::PageAppearance;
                else if (value == "advanced") options.page = beachlife::launcher::PageAdvanced;
                else if (value == "about") options.page = beachlife::launcher::PageAbout;
                else throw std::runtime_error("Unknown launcher page.");
            } else throw std::runtime_error("Unknown argument.");
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s Run beachlife --help.\n", error.what());
        return 2;
    }
    return beachlife::launcher::run(options);
}
