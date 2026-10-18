#include "startup_configuration_fixture.h"

#include "platform/sha256.h"
#include "runtime/config_snapshot.h"
#include "runtime/retail_files.h"
#include "startup_config_owner.h"
#include "startup_config_tokens.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace data = beachlife::data;
namespace runtime = beachlife::runtime;
namespace files = runtime::retail_files;

namespace allocation
{
struct Event
{
    std::size_t bytes;
    bool array;
    bool published;
};
thread_local bool enabled = false;
thread_local std::size_t count = 0;
thread_local std::size_t failure = 0;
thread_local bool invoke_handler = true;
struct Unrelated
{
};
thread_local bool unrelated = false;
thread_local std::array<Event, 512> events{};
void* allocate(std::size_t bytes, bool array)
{
    if (enabled)
    {
        if (count == events.size())
        {
            std::abort();
        }
        events[count++] = {bytes, array, startup_config_owner != nullptr};
        if (count == failure)
        {
            std::fprintf(stderr,
                "INJECTED allocation=%zu bytes=%zu array=%d published=%d\n",
                count,
                bytes,
                array,
                startup_config_owner != nullptr);
            if (unrelated)
            {
                throw Unrelated{};
            }
            if (invoke_handler)
            {
                if (const auto handler = std::get_new_handler())
                {
                    handler();
                }
            }
            throw std::bad_alloc();
        }
    }
    if (void* value = std::malloc(bytes ? bytes : 1))
    {
        return value;
    }
    throw std::bad_alloc();
}
} // namespace allocation

#ifndef STARTUP_CONFIGURATION_DEFAULT_ALLOCATOR
void* operator new(std::size_t bytes)
{
    return allocation::allocate(bytes, false);
}
void* operator new[](std::size_t bytes)
{
    return allocation::allocate(bytes, true);
}
void operator delete(void* value) noexcept
{
    std::free(value);
}
void operator delete[](void* value) noexcept
{
    std::free(value);
}
void operator delete(void* value, std::size_t) noexcept
{
    std::free(value);
}
void operator delete[](void* value, std::size_t) noexcept
{
    std::free(value);
}
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(bytes);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new[](bytes);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}
void operator delete(void* value, const std::nothrow_t&) noexcept
{
    ::operator delete(value);
}
void operator delete[](void* value, const std::nothrow_t&) noexcept
{
    ::operator delete[](value);
}
#endif

namespace
{
std::size_t checks = 0;
void previous_handler()
{
    throw std::bad_alloc();
}
class PreviousHandler
{
  public:
    PreviousHandler()
        : saved_(std::set_new_handler(previous_handler))
    {
    }
    ~PreviousHandler()
    {
        std::set_new_handler(saved_);
    }

  private:
    std::new_handler saved_;
};
void require(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

enum class Error
{
    none,
    missing,
    open,
    size,
    seek,
    read,
    zero,
    oversized,
    grow
};
struct State
{
    std::string bytes;
    std::string marker = "inner";
    std::size_t chunk = std::numeric_limits<std::size_t>::max();
    std::size_t opens = 0, reads = 0, closes = 0;
    Error error = Error::none;
    bool unbound_preflight = true;
    bool reject_after_first = false;
    bool destroyed_with_owner = false;
    std::size_t size_calls = 0;
};
class MemoryFile final : public data::SourceFile
{
  public:
    MemoryFile(std::shared_ptr<State> state, bool marker)
        : state_(std::move(state))
        , marker_(marker)
    {
    }
    ~MemoryFile() override
    {
        ++state_->closes;
    }
    std::uint64_t size() const override
    {
        if (!marker_ && state_->error == Error::size)
        {
            throw std::runtime_error("size");
        }
        ++state_->size_calls;
        return bytes().size() + (!marker_ && state_->error == Error::grow && state_->reads != 0);
    }
    std::uint64_t tell() const override
    {
        return position_;
    }
    void seek(std::uint64_t position) override
    {
        if (!marker_ && state_->error == Error::seek)
        {
            throw std::runtime_error("seek");
        }
        if (position > bytes().size())
        {
            throw std::out_of_range("seek");
        }
        position_ = static_cast<std::size_t>(position);
    }
    std::size_t read(std::span<unsigned char> output) override
    {
        ++state_->reads;
        if (!marker_ && state_->error == Error::read)
        {
            throw std::runtime_error("read");
        }
        if (!marker_ && state_->error == Error::zero)
        {
            return 0;
        }
        if (!marker_ && state_->error == Error::oversized)
        {
            return output.size() + 1;
        }
        auto count = std::min({output.size(), bytes().size() - position_, state_->chunk});
        if (count)
        {
            std::memcpy(output.data(), bytes().data() + position_, count);
        }
        position_ += count;
        return count;
    }

  private:
    const std::string& bytes() const
    {
        return marker_ ? state_->marker : state_->bytes;
    }
    std::shared_ptr<State> state_;
    bool marker_;
    std::size_t position_ = 0;
};
class MemorySource final : public data::FileSource
{
  public:
    explicit MemorySource(std::shared_ptr<State> state)
        : state_(std::move(state))
    {
    }
    ~MemorySource() override
    {
        state_->destroyed_with_owner = startup_config_owner != nullptr;
    }
    std::unique_ptr<data::SourceFile> open(std::string_view path) const override
    {
        if (path == "marker")
        {
            return std::make_unique<MemoryFile>(state_, true);
        }
        if (path != runtime::ConfigSnapshot::filename)
        {
            return nullptr;
        }
        ++state_->opens;
        state_->unbound_preflight &= startup_config_owner == nullptr;
        if (state_->error == Error::open || (state_->reject_after_first && state_->opens > 1))
        {
            throw std::runtime_error("open");
        }
        if (state_->error == Error::missing)
        {
            return nullptr;
        }
        return std::make_unique<MemoryFile>(state_, false);
    }

  private:
    std::shared_ptr<State> state_;
};

std::shared_ptr<State> state()
{
    auto result = std::make_shared<State>();
    result->bytes = runtime::testing::startup_configuration_document();
    return result;
}
std::string digest(const State& value)
{
    return beachlife::platform::sha256_hex(value.bytes);
}
using Handle = std::unique_ptr<files::File, decltype(&files::close)>;
std::string read_bridge(const char* path)
{
    Handle file(files::open(path, "rb"), files::close);
    require(file != nullptr, "Expected a bound file.");
    require(files::seek(file.get(), 0, files::SeekOrigin::end) == 0, "Size seek failed.");
    files::Position length = 0;
    require(files::position(file.get(), &length) == 0 && length <= 4096, "Position failed.");
    require(files::seek(file.get(), 0, files::SeekOrigin::begin) == 0, "Rewind failed.");
    std::string result(static_cast<std::size_t>(length), '\0');
    require(
        files::read(result.data(), 1, result.size(), file.get()) == result.size(), "Read failed.");
    return result;
}
struct Expected
{
    std::string name;
    bool flag;
    std::int32_t integer;
    std::uint32_t number;
    bool has_text;
    std::string text;
};
void verify(const std::vector<Expected>& expected)
{
    require(startup_config_owner != nullptr, "Owner was not published.");
    const auto* node = startup_config_owner->head;
    for (const auto& item : expected)
    {
        require(node != nullptr, "Missing ordered node.");
        require(node->name == item.name, "Name differs from independent record.");
        require(node->flag == item.flag && node->integer == item.integer &&
                    std::bit_cast<std::uint32_t>(node->number) == item.number,
            "Typed value differs.");
        require((node->text != nullptr) == item.has_text, "Text presence differs.");
        if (item.has_text)
        {
            require(node->text == item.text, "Owned text differs.");
        }
        node = node->next;
    }
    require(!node, "Unexpected trailing node or cycle.");
}
std::vector<Expected> expected()
{
    return {{"bReady", true, 0, 0, false, {}},
        {"iCount", false, -123, 0, false, {}},
        {"fScale", false, 0, 0x40200000, false, {}},
        {"sName", false, 0, 0, true, "owned"},
        {"bReady", false, 0, 0, false, {}}};
}

void positive()
{
    PreviousHandler previous_handler_scope;
    require(files::open("marker", "rb") == nullptr, "Unexpected initial binding.");
    auto outer_state = state();
    outer_state->marker = "outer";
    auto outer = std::make_shared<MemorySource>(outer_state);
    {
        files::Session previous(outer);
        for (const std::size_t chunk : {std::size_t(1242), std::size_t(7), std::size_t(1)})
        {
            auto input = state();
            input->chunk = chunk;
            input->reject_after_first = true;
            auto mount = std::make_shared<MemorySource>(input);
            std::weak_ptr<const data::FileSource> weak = mount;
            auto owner =
                runtime::testing::StartupConfigurationFixture::create(mount, digest(*input));
            require(std::get_new_handler() == previous_handler,
                "Prior allocation handler was not restored.");
            mount.reset();
            require(!weak.expired() && input->opens == 1 && input->closes == 1 &&
                        input->unbound_preflight,
                "Snapshot preflight/mount retention differs.");
            verify(expected());
            const auto stable = input->bytes;
            input->bytes.assign(1242, 'X');
            input->error = Error::read;
            require(read_bridge("data\\SCRIPT\\gameparameters.PIS") == stable,
                "Live configuration read escaped immutable snapshot.");
            require(read_bridge("marker") == "inner", "Other paths did not delegate.");
            bool worker_unbound = false;
            std::thread worker(
                [&]
                {
                    worker_unbound = files::open("marker", "rb") == nullptr;
                });
            worker.join();
            require(worker_unbound, "Caller binding leaked to another thread.");
            auto second = state();
            bool rejected = false;
            try
            {
                auto duplicate = runtime::testing::StartupConfigurationFixture::create(
                    std::make_shared<MemorySource>(second), digest(*second));
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
            require(rejected && second->opens == 0, "Nested retail owner was not rejected early.");
            verify(expected());
            Handle retained(
                files::open(runtime::ConfigSnapshot::filename.data(), "rb"), files::close);
            require(retained != nullptr, "Retained stream missing.");
            owner.reset();
            require(
                std::get_new_handler() == previous_handler, "Teardown changed allocation handler.");
            require(!startup_config_owner && !weak.expired(),
                "Owner reset/source handle lifetime differs.");
            require(read_bridge("marker") == "outer", "Previous binding was not restored.");
            std::array<char, 1242> after{};
            require(files::read(after.data(), 1, after.size(), retained.get()) == after.size() &&
                        std::string_view(after.data(), after.size()) == stable,
                "Retained stream changed after teardown.");
            retained.reset();
            require(weak.expired() && !input->destroyed_with_owner,
                "Mount outlived handle or owner teardown.");
        }
    }
    require(!startup_config_owner && files::open("marker", "rb") == nullptr,
        "Session teardown failed.");
}

void rejection()
{
    for (const Error error : {Error::none,
             Error::missing,
             Error::open,
             Error::size,
             Error::seek,
             Error::read,
             Error::zero,
             Error::oversized,
             Error::grow})
    {
        auto input = state();
        input->error = error;
        bool caught = false;
        try
        {
            runtime::StartupConfiguration owner(std::make_shared<MemorySource>(input));
        }
        catch (const std::runtime_error&)
        {
            caught = true;
        }
        require(caught && !startup_config_owner && input->unbound_preflight,
            "Recoverable preflight error published an owner.");
    }
    for (const std::size_t size : {std::size_t(0), std::size_t(1241), std::size_t(1243)})
    {
        auto input = state();
        input->bytes.resize(size);
        bool caught = false;
        try
        {
            runtime::StartupConfiguration owner(std::make_shared<MemorySource>(input));
        }
        catch (const std::runtime_error&)
        {
            caught = true;
        }
        require(caught && input->reads == 0 && !startup_config_owner,
            "Size rejection read/published data.");
    }
    bool caught = false;
    try
    {
        runtime::StartupConfiguration owner(nullptr);
    }
    catch (const std::invalid_argument&)
    {
        caught = true;
    }
    require(caught && !startup_config_owner && files::open("marker", "rb") == nullptr,
        "Null mount rejection changed binding.");
}

#ifndef STARTUP_CONFIGURATION_DEFAULT_ALLOCATOR
void allocation_free_snapshot()
{
    auto input = state();
    auto snapshot =
        runtime::ConfigSnapshot::read(std::make_shared<MemorySource>(input), digest(*input));
    files::Session session(snapshot, files::AllocationFailure::terminate_process);
    Handle file(files::open(runtime::ConfigSnapshot::filename.data(), "rb"), files::close);
    require(file != nullptr, "Snapshot bridge open failed.");
    std::array<unsigned char, 1243> bytes{};
    files::Position at = 0;
    allocation::count = 0;
    allocation::enabled = true;
    const int seek = files::seek(file.get(), 0, files::SeekOrigin::end);
    const int position = files::position(file.get(), &at);
    const int rewind = files::seek(file.get(), 0, files::SeekOrigin::begin);
    const auto read = files::read(bytes.data(), 1, bytes.size(), file.get());
    const auto eof = files::read(bytes.data(), 1, 1, file.get());
    allocation::enabled = false;
    require(seek == 0 && position == 0 && at == 1242 && rewind == 0 && read == 1242 && eof == 0 &&
                allocation::count == 0,
        "Snapshot read/seek/tell allocated or changed contract.");
}

#endif

int fault(std::size_t failure)
{
    PreviousHandler previous_handler_scope;
    auto input = state();
    auto mount = std::make_shared<MemorySource>(input);
    const auto hash = digest(*input);
    allocation::count = 0;
    allocation::failure = failure;
    allocation::enabled = true;
    try
    {
        auto owner = runtime::testing::StartupConfigurationFixture::create(mount, hash);
        allocation::enabled = false;
        require(
            std::get_new_handler() == previous_handler, "Success did not restore prior handler.");
        verify(expected());
        if (failure)
        {
            std::fprintf(stderr, "UNEXPECTED SURVIVAL\n");
            return 7;
        }
    }
    catch (const std::bad_alloc&)
    {
        allocation::enabled = false;
        require(!startup_config_owner && files::open("marker", "rb") == nullptr,
            "Recoverable allocation failure changed state.");
        require(std::get_new_handler() == previous_handler, "Preflight changed the prior handler.");
        std::printf("RECOVERED %zu\n", failure);
        return 0;
    }
    catch (const allocation::Unrelated&)
    {
        allocation::enabled = false;
        require(!startup_config_owner && std::get_new_handler() == previous_handler &&
                    files::open("marker", "rb") == nullptr,
            "Unrelated construction exception did not restore the boundary.");
        std::printf("UNRELATED_PROPAGATED %zu\n", failure);
        return 0;
    }
    for (std::size_t i = 0; i != allocation::count; ++i)
    {
        const auto& e = allocation::events[i];
        std::printf("ALLOCATION %zu %zu %d %d\n", i + 1, e.bytes, e.array, e.published);
    }
    std::printf("ALLOCATION_BASELINE count=%zu owner_bytes=%zu token_bytes=%zu\n",
        allocation::count,
        sizeof(StartupConfig),
        5 * sizeof(ConfigToken));
    return 0;
}

int bridge_fault(bool terminate, unsigned int stage, std::size_t failure)
{
    auto input = state();
    auto snapshot =
        runtime::ConfigSnapshot::read(std::make_shared<MemorySource>(input), digest(*input));
    files::Session session(snapshot,
        terminate ? files::AllocationFailure::terminate_process
                  : files::AllocationFailure::return_error);
    for (unsigned int i = 1; i <= 3; ++i)
    {
        if (i == stage)
        {
            allocation::count = 0;
            allocation::failure = failure;
            allocation::enabled = true;
        }
        Handle file(files::open(runtime::ConfigSnapshot::filename.data(), "rb"), files::close);
        allocation::enabled = false;
        if (i == stage && failure)
        {
            require(!terminate && !file, "Default allocation policy did not return null.");
            std::printf("BRIDGE_RECOVERED %u %zu\n", stage, failure);
            return 0;
        }
        require(file != nullptr, "Bridge baseline open failed.");
    }
    std::printf("BRIDGE_BASELINE %u %zu\n", stage, allocation::count);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    try
    {
        require(argc >= 2, "Expected fixture mode.");
        if (std::strcmp(argv[1], "fault") == 0 || std::strcmp(argv[1], "fault-direct") == 0 ||
            std::strcmp(argv[1], "fault-exception") == 0)
        {
            require(argc == 3, "Fault argument.");
            allocation::invoke_handler = std::strcmp(argv[1], "fault-direct") != 0;
            allocation::unrelated = std::strcmp(argv[1], "fault-exception") == 0;
            return fault(std::strtoull(argv[2], nullptr, 10));
        }
        if (std::strcmp(argv[1], "bridge") == 0)
        {
            require(argc == 5, "Bridge arguments.");
            return bridge_fault(std::strcmp(argv[2], "strict") == 0,
                static_cast<unsigned int>(std::strtoul(argv[3], nullptr, 10)),
                std::strtoull(argv[4], nullptr, 10));
        }
        require(std::strcmp(argv[1], "generated") == 0, "Unknown fixture mode.");
        positive();
        rejection();
#ifndef STARTUP_CONFIGURATION_DEFAULT_ALLOCATOR
        allocation_free_snapshot();
#endif
        std::printf("STARTUP_CONFIGURATION_SUCCESS checks=%zu\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        allocation::enabled = false;
        std::fprintf(stderr, "STARTUP_CONFIGURATION_FAILURE %s\n", error.what());
        return 2;
    }
}
