#include "build_version.h"
#include "data/game_path.h"
#include "data/iso9660.h"
#include "launcher/launcher.h"
#include "platform/game_log.h"
#include "runtime/runtime.h"
#if BEACHLIFE_BOOT_GAME
#include "runtime/boot_session.h"
#endif
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <ctime>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

int main(int argc, char** argv)
{
#if BEACHLIFE_BOOT_GAME
    // Play runs the decompiled game; missing code stops it at a trap.
    beachlife::runtime::set_game_entry(beachlife::runtime::run_boot_game);
#endif
    if (argc == 3 && std::string_view(argv[1]) == "--inspect-iso")
    {
        try
        {
            const auto index =
                beachlife::data::index_iso(beachlife::launcher::path_from_utf8(argv[2]));
            beachlife::data::write_iso_json(std::cout, index);
            return 0;
        }
        catch (const std::exception& error)
        {
            std::fprintf(stderr, "Beach Life: %s\n", error.what());
            return 1;
        }
    }
    if (argc == 2 && std::string_view(argv[1]) == "--version")
    {
        std::puts(beachlife::build::version);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--build-info")
    {
        std::puts(beachlife::build::json);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::puts("Beach Life - native launcher\n"
                  "Usage: beachlife [--launcher] [--source image.iso]\n"
                  "                 [--config file] [--page "
                  "play|files|gameplay|display|appearance|advanced|about]\n"
                  "                 [--cache-directory folder]\n"
                  "                 [--screenshot file.png] [--smoke-test]\n"
                  "       beachlife --play [--source image.iso] [--config file]\n"
                  "                 [--preview-screen Data/Screens/FrontScreen.tga]\n"
                  "                 [--frames count] [--screenshot file.png] [--smoke-test]\n"
                  "                 [--headless]\n"
                  "       beachlife --help | --version | --build-info\n\n"
                  "       beachlife --inspect-iso image.iso\n\n"
                  "Open without arguments to launch the graphical interface.\n"
                  "--play opens the game window and prepares files in its loading screen.\n"
                  "--preview-screen displays an original TGA image through the VFS.");
#if BEACHLIFE_BOOT_GAME
        std::puts("Play runs the decompiled game using your prepared ISO data.\n"
                  "Gameplay options are available under Advanced in the launcher.");
#else
        std::puts("This development launcher loads the supported game configuration.\n"
                  "Build the release preset to include the decompiled game.");
#endif
        return 0;
    }
    beachlife::launcher::Options options;
    options.argc = argc;
    options.argv = argv;
    bool play = false;
    std::string preview_screen;
    int frames = 0;
    bool headless = false;
    try
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg = argv[i];
            if (arg == "--launcher")
            {
                continue;
            }
            if (arg == "--smoke-test")
            {
                options.smoke = true;
                continue;
            }
            if (arg == "--headless")
            {
                headless = true;
                continue;
            }
            if (arg == "--play")
            {
                play = true;
                continue;
            }
            if (i + 1 == argc)
            {
                throw std::runtime_error("Unknown or incomplete argument.");
            }
            const std::string value = argv[++i];
            if (arg == "--config")
            {
                options.config = beachlife::launcher::path_from_utf8(value);
            }
            else if (arg == "--cache-directory")
            {
                options.cache = beachlife::launcher::path_from_utf8(value);
            }
            else if (arg == "--screenshot")
            {
                options.screenshot = beachlife::launcher::path_from_utf8(value);
            }
            else if (arg == "--update-preview")
            {
                options.update_preview = value;
            }
            else if (arg == "--preview-screen")
            {
                preview_screen = beachlife::data::normalize_game_path(value).display;
            }
            else if (arg == "--frames")
            {
                frames = std::stoi(value);
                if (frames <= 0)
                {
                    throw std::runtime_error("--frames needs a positive count.");
                }
            }
            else if (arg == "--source")
            {
                options.source = value;
            }
            else if (arg == "--page")
            {
                if (value == "play")
                {
                    options.page = beachlife::launcher::PagePlay;
                }
                else if (value == "files")
                {
                    options.page = beachlife::launcher::PageFiles;
                }
                else if (value == "gameplay")
                {
                    options.page = beachlife::launcher::PageGameplay;
                }
                else if (value == "display")
                {
                    options.page = beachlife::launcher::PageDisplay;
                }
                else if (value == "appearance")
                {
                    options.page = beachlife::launcher::PageAppearance;
                }
                else if (value == "advanced")
                {
                    options.page = beachlife::launcher::PageAdvanced;
                }
                else if (value == "about")
                {
                    options.page = beachlife::launcher::PageAbout;
                }
                else
                {
                    throw std::runtime_error("Unknown launcher page.");
                }
            }
            else
            {
                throw std::runtime_error("Unknown argument.");
            }
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s Run beachlife --help.\n", error.what());
        return 2;
    }
    if ((frames || !preview_screen.empty()) && !play)
    {
        std::fprintf(stderr, "--frames and --preview-screen need --play. Run beachlife --help.\n");
        return 2;
    }
    // The game window: straight from the command line, or each time Play is pressed,
    // returning to the launcher afterwards.
    beachlife::runtime::Options game;
    const char* base = SDL_GetBasePath();
    game.resources = beachlife::launcher::path_from_utf8(base ? base : "") / "assets" / "launcher";
    game.screenshot = play ? std::exchange(options.screenshot, {}) : std::filesystem::path{};
    game.frames = frames;
    game.hidden = options.smoke;
    game.headless = headless;
    game.cache = options.cache;
    for (;;)
    {
        // --play goes straight to the game window; its loading screen prepares the files.
        auto outcome =
            play ? beachlife::launcher::play_settings(options) : beachlife::launcher::run(options);
        if (!outcome.play)
        {
            return outcome.exit_code;
        }
        game.settings = outcome.settings;
        game.config = outcome.config;
        game.prepare_only = outcome.prepare_only;
        game.preview_screen = play ? preview_screen : outcome.preview_screen;
        // Real play sessions keep a log beside the game, to find out why one
        // stopped; checks do not, unless BEACHLIFE_GAME_LOG names a folder.
        std::optional<beachlife::platform::GameLog> log;
        const char* log_folder = SDL_getenv("BEACHLIFE_GAME_LOG");
        if ((log_folder && *log_folder) || (!frames && !headless && !options.smoke))
        {
            char started[32] = "";
            const std::time_t now = std::time(nullptr);
            std::strftime(started, sizeof started, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
            log.emplace(log_folder && *log_folder
                            ? beachlife::launcher::path_from_utf8(log_folder)
                            : beachlife::launcher::game_log_folder(game.config),
                std::string("Beach Life ") + beachlife::build::version + " on " +
                    SDL_GetPlatform() + ", game session started " + started);
        }
        const auto result = beachlife::runtime::run(game);
        if (play)
        {
            return result;
        }
        options.source.clear(); // the launcher shows the saved choice from now on
        options.page = outcome.prepare_only || !outcome.preview_screen.empty()
                           ? beachlife::launcher::PageFiles
                           : beachlife::launcher::PagePlay;
    }
}
