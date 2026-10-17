#include "build_version.h"

#include <cstdio>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::puts(beachlife::build::version);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--build-info") {
        std::puts(beachlife::build::json);
        return 0;
    }
    if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--help")) {
        std::puts("Beach Life recomp - native bootstrap\n"
                  "Usage: beachlife [--help | --version | --build-info]\n\n"
                  "The launcher and game are under development.\n"
                  "This build does not yet load or run the game.");
        return 0;
    }
    std::fputs("Unknown arguments. Run beachlife --help.\n", stderr);
    return 2;
}
