#include "startup_configuration_fixture.h"

#include "launcher/launcher.h"
#include "runtime/configured_runtime.h"
#include "runtime/retail_files.h"
#include "startup_config_owner.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <charconv>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace launcher = beachlife::launcher;
namespace runtime = beachlife::runtime;
namespace files = runtime::retail_files;
namespace
{
std::thread::id caller;
unsigned int factory_calls = 0;
std::weak_ptr<const beachlife::data::FileSource> mount;

void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

std::unique_ptr<runtime::StartupConfiguration> construct(
    std::shared_ptr<const beachlife::data::FileSource> source)
{
    require(
        std::this_thread::get_id() == caller, "Configuration factory ran off the caller thread.");
    require(
        ++factory_calls == 1 && !startup_config_owner, "Configuration owner was already active.");
    mount = source;
    auto owner = runtime::testing::StartupConfigurationFixture::create(std::move(source));
    require(startup_config_owner && startup_config_owner->head,
        "The actual constructor did not publish its nonempty owner.");
    require(std::string_view(startup_config_owner->head->name) == "bReady" &&
                startup_config_owner->head->flag,
        "The actual constructor did not read generated configuration.");
    return owner;
}

int write_configuration(const char* path)
{
    const auto bytes = runtime::testing::startup_configuration_document();
    std::ofstream output(launcher::path_from_utf8(path), std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    require(static_cast<bool>(output), "Cannot write generated configuration.");
    std::printf("GENERATED_CONFIGURATION bytes=%zu\n", bytes.size());
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string_view(argv[1]) == "--write-configuration")
        {
            return write_configuration(argv[2]);
        }
        launcher::Options launcher_options;
        launcher_options.argc = argc;
        launcher_options.argv = argv;
        runtime::Options options;
        bool play = false;
        for (int i = 1; i != argc; ++i)
        {
            const std::string_view argument(argv[i]);
            if (argument == "--play")
            {
                play = true;
                continue;
            }
            if (argument == "--smoke-test")
            {
                launcher_options.smoke = true;
                options.hidden = true;
                continue;
            }
            require(i + 1 != argc, "Incomplete runtime fixture argument.");
            const std::string_view value(argv[++i]);
            if (argument == "--config")
            {
                launcher_options.config = launcher::path_from_utf8(std::string(value));
            }
            else if (argument == "--cache-directory")
            {
                launcher_options.cache = launcher::path_from_utf8(std::string(value));
                options.cache = launcher_options.cache;
            }
            else if (argument == "--source")
            {
                launcher_options.source = value;
            }
            else if (argument == "--resources")
            {
                options.resources = launcher::path_from_utf8(std::string(value));
            }
            else if (argument == "--frames")
            {
                const auto result =
                    std::from_chars(value.data(), value.data() + value.size(), options.frames);
                require(result.ec == std::errc{} && result.ptr == value.data() + value.size() &&
                            options.frames > 0 && options.frames <= 1000,
                    "Runtime fixture frames must be in 1..1000.");
            }
            else
            {
                throw std::runtime_error("Unknown runtime fixture argument.");
            }
        }
        require(play && options.frames > 0 && !options.resources.empty(),
            "Runtime fixture requires --play, --frames and --resources.");
        caller = std::this_thread::get_id();
        require(
            !startup_config_owner && files::open("Data/Script/GameParameters.pis", "rb") == nullptr,
            "Unexpected initial game binding.");
        const auto outcome = launcher::play_settings(launcher_options);
        if (!outcome.play)
        {
            return outcome.exit_code;
        }
        options.settings = outcome.settings;
        options.config = outcome.config;
        const auto result = runtime::detail::run_with_configuration(options, construct);
        require(!startup_config_owner && mount.expired() &&
                    files::open("Data/Script/GameParameters.pis", "rb") == nullptr,
            "Configuration/source/session remained alive after runtime teardown.");
        if (result == 0)
        {
            require(factory_calls == 1,
                "Runtime completed without constructing the real configuration.");
            std::puts("RUNTIME_CONFIGURATION_SUCCESS caller_thread=1 calls=1 cleanup=1");
        }
        return result;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "RUNTIME_CONFIGURATION_FAILURE %s\n", error.what());
        return 2;
    }
}
