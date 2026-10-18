#pragma once

#include "runtime/runtime.h"
#include "runtime/startup_configuration.h"

#include <memory>

namespace beachlife::runtime::detail
{

// Internal entry used by the generated runtime fixture. The application's
// public run() always supplies the fixed retail configuration constructor.
using ConfigurationFactory = std::unique_ptr<StartupConfiguration> (*)(
    std::shared_ptr<const data::FileSource>);

int run_with_configuration(const Options& options, ConfigurationFactory configuration);

} // namespace beachlife::runtime::detail
