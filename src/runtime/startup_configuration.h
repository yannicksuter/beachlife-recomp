#pragma once

#include "data/source.h"

#include <memory>

namespace beachlife::runtime
{
namespace testing
{
class StartupConfigurationFixture;
}

// Construct and destroy on the game thread, with no other live retail
// StartupConfig owner. The supported file identity is checked before the
// actual constructor publishes its singleton. Allocation failure during
// faithful construction terminates the process; recovery is not qualified.
// This uses the default host C++ allocator and a temporary process-wide
// new-handler policy; no other thread may replace that handler concurrently.
// Allocation failure on other threads also terminates during that interval.
class StartupConfiguration
{
  public:
    explicit StartupConfiguration(std::shared_ptr<const data::FileSource> source);
    ~StartupConfiguration();
    StartupConfiguration(const StartupConfiguration&) = delete;
    StartupConfiguration& operator=(const StartupConfiguration&) = delete;
    StartupConfiguration(StartupConfiguration&&) = delete;
    StartupConfiguration& operator=(StartupConfiguration&&) = delete;

  private:
    friend class testing::StartupConfigurationFixture;
    StartupConfiguration(
        std::shared_ptr<const data::FileSource> source, std::string_view expected_sha256);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace beachlife::runtime
