#include "startup_configuration.h"

#include "config_snapshot.h"
#include "runtime/retail_files.h"
#include "startup_config_owner.h"

#include <cstdlib>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>

namespace beachlife::runtime
{
namespace
{
[[noreturn]] void allocation_failure()
{
    std::abort();
}

// The default C++ allocator consults this handler before returning a failed
// nothrow allocation. The policy is process-wide only during the synchronous
// faithful constructor; preflight remains outside this interval.
class ConstructionAllocationPolicy
{
  public:
    ConstructionAllocationPolicy()
        : previous_(std::set_new_handler(allocation_failure))
    {
    }
    ~ConstructionAllocationPolicy()
    {
        std::set_new_handler(previous_);
    }
    ConstructionAllocationPolicy(const ConstructionAllocationPolicy&) = delete;
    ConstructionAllocationPolicy& operator=(const ConstructionAllocationPolicy&) = delete;

  private:
    std::new_handler previous_;
};
} // namespace

struct StartupConfiguration::Impl
{
    Impl(std::shared_ptr<const data::FileSource> mount, std::string_view expected_sha256)
        : source(ConfigSnapshot::read(std::move(mount), expected_sha256))
        , files(source, retail_files::AllocationFailure::terminate_process)
        , thread(std::this_thread::get_id())
    {
        ConstructionAllocationPolicy allocation_policy;
        try
        {
            config.reset(new StartupConfig(ConfigSnapshot::filename.data()));
        }
        catch (const std::bad_alloc&)
        {
            // Retail construction has no qualified rollback for partial lists.
            std::abort();
        }
        if (!config->head)
        {
            // The retained destructor requires a nonempty list. In particular,
            // the native nothrow token allocation can leave an empty owner.
            std::abort();
        }
    }

    ~Impl()
    {
        if (thread != std::this_thread::get_id())
        {
            std::abort();
        }
    }

    std::shared_ptr<const ConfigSnapshot> source;
    retail_files::Session files;
    std::thread::id thread;
    std::unique_ptr<StartupConfig> config;
};

StartupConfiguration::StartupConfiguration(std::shared_ptr<const data::FileSource> source)
    : StartupConfiguration(std::move(source), ConfigSnapshot::supported_sha256)
{
}

StartupConfiguration::StartupConfiguration(
    std::shared_ptr<const data::FileSource> source, std::string_view expected_sha256)
{
    if (startup_config_owner)
    {
        throw std::logic_error("The game configuration is already active.");
    }
    impl_ = std::make_unique<Impl>(std::move(source), expected_sha256);
}

StartupConfiguration::~StartupConfiguration() = default;

} // namespace beachlife::runtime
