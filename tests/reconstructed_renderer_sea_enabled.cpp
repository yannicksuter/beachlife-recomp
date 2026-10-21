#include "renderer_queue_test_state.h"
#include "startup_renderer_sea_state.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <type_traits>

namespace
{
using State = RendererSeaStatePrefix;
constexpr std::size_t state_bytes =
    offsetof(State, sea_enabled_10081) + sizeof(State::sea_enabled_10081);
constexpr std::size_t sea_offset = offsetof(State, sea_enabled_10081);
constexpr std::size_t payload_bytes = 23;

static_assert(CHAR_BIT == 8 && sizeof(void*) == 8 && sizeof(unsigned int) == 4);
static_assert(std::is_standard_layout_v<State> && !std::is_polymorphic_v<State>);
static_assert(offsetof(State, unknown_04) == sizeof(void*));
static_assert(
    offsetof(State, word_1007c) == offsetof(State, unknown_04) + sizeof(State::unknown_04));
static_assert(offsetof(State, byte_10080) == offsetof(State, word_1007c) + sizeof(unsigned int));
static_assert(sea_offset == offsetof(State, byte_10080) + sizeof(unsigned char));
static_assert(state_bytes == 0x10086 && sizeof(State) == 0x10088);
static_assert(std::is_aggregate_v<ScreenRequestRenderer>);
static_assert(!std::is_polymorphic_v<ScreenRequestRenderer>);
static_assert(std::is_same_v<decltype(&ScreenRequestRenderer::SetSeaEnabled),
    void (ScreenRequestRenderer::*)(unsigned char)>);

template <class T> struct Guarded
{
    std::array<unsigned char, 32> before{};
    T value{};
    std::array<unsigned char, 32> after{};
};

struct Failure
{
    const char* reason;
};

std::size_t checks = 0;
std::size_t calls = 0;

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        throw Failure{reason};
    }
}

template <class T> void initialize_guards(Guarded<T>& owner)
{
    owner.before.fill(0xa5);
    owner.after.fill(0x5a);
}

template <class T> void check_guards(const Guarded<T>& owner)
{
    require(std::all_of(owner.before.begin(),
                owner.before.end(),
                [](unsigned char value)
                {
                    return value == 0xa5;
                }) &&
                std::all_of(owner.after.begin(),
                    owner.after.end(),
                    [](unsigned char value)
                    {
                        return value == 0x5a;
                    }),
        "outside guard changed");
}

void initialize_state(State& state, unsigned char pattern, unsigned int word)
{
    require(state.owned_data_00 == nullptr && state.word_1007c == 0xffffffffu &&
                state.byte_10080 == 0 && state.sea_enabled_10081 == 1,
        "qualified child construction differs");
    // The genuine constructor has already run. Initialize actual opaque bytes
    // and known scalars, not a whole live object or its ABI padding.
    std::fill(std::begin(state.unknown_04), std::end(state.unknown_04), pattern);
    state.word_1007c = word;
    state.byte_10080 = static_cast<unsigned char>(pattern ^ 0xff);
}

struct ScalarDelete
{
    void operator()(void* pointer) const noexcept
    {
        ::operator delete(pointer);
    }
};

// Both genuine child destructors and the host deque/storage destructors complete
// before process leak checking. No retail renderer constructor is supplied.
[[gnu::noinline]] void exercise()
{
    Guarded<State> first;
    Guarded<State> second;
    Guarded<ScreenRequestRenderer> renderer;
    initialize_guards(first);
    initialize_guards(second);
    initialize_guards(renderer);
    initialize_state(first.value, 0x35, 0x12345678u);
    initialize_state(second.value, 0x72, 0x87654321u);

    std::unique_ptr<void, ScalarDelete> payload(::operator new(payload_bytes));
    std::memset(payload.get(), 0xc7, payload_bytes);
    first.value.owned_data_00 = payload.release(); // Actual child owns this block.

    // This is a typed renderer read model with a real native deque. It does not
    // construct or replace the game's full renderer/platform owner.
    renderer_queue_test::initialize(renderer.value, 0x29);
    ScreenRequestRenderer expected_renderer = renderer.value;
    const auto offset = [&](const void* member)
    {
        return reinterpret_cast<std::uintptr_t>(member) -
               reinterpret_cast<std::uintptr_t>(&renderer.value);
    };
    require(offset(&renderer.value.buffer_130) == 376 &&
                offset(&renderer.value.sea_state_134) == 384 && sizeof(renderer.value) == 392,
        "native renderer tail differs");

    std::array<unsigned char, state_bytes> expected_first;
    std::array<unsigned char, state_bytes> expected_second;
    std::memcpy(expected_first.data(), &first.value, state_bytes);
    std::memcpy(expected_second.data(), &second.value, state_bytes);
    std::array<unsigned char, payload_bytes> expected_payload;
    expected_payload.fill(0xc7);

    constexpr std::array<unsigned char, 3> values{0, 0x80, 0xff};
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        const bool use_second = index == 1;
        State* selected = use_second ? &second.value : &first.value;
        renderer.value.sea_state_134 = selected;
        expected_renderer.sea_state_134 = selected;
        renderer.value.SetSeaEnabled(values[index]);
        ++calls;
        require(selected->sea_enabled_10081 == values[index], "parent forwarded byte differs");
        (use_second ? expected_second : expected_first)[sea_offset] = values[index];
        require(std::memcmp(&first.value, expected_first.data(), state_bytes) == 0 &&
                    std::memcmp(&second.value, expected_second.data(), state_bytes) == 0,
            "child member representation changed");
        require(std::memcmp(first.value.owned_data_00, expected_payload.data(), payload_bytes) == 0,
            "owned payload changed");
        require(renderer_queue_test::equal(renderer.value, expected_renderer),
            "renderer members or queue changed");
        check_guards(first);
        check_guards(second);
        check_guards(renderer);
    }
    // The borrowed renderer references are retired while the children live.
    renderer.value.sea_state_134 = nullptr;
    expected_renderer.sea_state_134 = nullptr;
}
} // namespace

int main()
{
    try
    {
        exercise();
        require(calls == 3, "parent call count differs");
        std::printf("{\"groups\":1,\"parent_calls\":%zu,\"child_lifetimes\":2,"
                    "\"owned_blocks_transferred\":1,\"checks\":%zu}\n",
            calls,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "renderer sea-state failure: %s\n", failure.reason);
        return 2;
    }
    catch (const std::exception& exception)
    {
        std::fprintf(stderr, "renderer sea-state setup exception: %s\n", exception.what());
        return 3;
    }
}
