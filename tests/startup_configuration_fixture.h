#pragma once

#include "platform/sha256.h"
#include "runtime/startup_configuration.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace beachlife::runtime::testing
{

// Complete generated records, with padding inside the loader's actual extent.
// This is synthetic input for the private fixture factory, never a production
// identity override or an original configuration sample.
inline std::string startup_configuration_document()
{
    std::string value = "'bReady' \"true\"\r\n'iCount' -123\r\n'fScale' 2.5\r\n"
                        "'sName' \"owned\"\r\n'bReady' \"True\"\r\n";
    value.resize(1242, '\r');
    return value;
}

class StartupConfigurationFixture
{
  public:
    static std::unique_ptr<StartupConfiguration> create(
        std::shared_ptr<const data::FileSource> source, std::string_view digest)
    {
        return std::unique_ptr<StartupConfiguration>(
            new StartupConfiguration(std::move(source), digest));
    }

    static std::unique_ptr<StartupConfiguration> create(
        std::shared_ptr<const data::FileSource> source)
    {
        return create(std::move(source), platform::sha256_hex(startup_configuration_document()));
    }
};

} // namespace beachlife::runtime::testing
