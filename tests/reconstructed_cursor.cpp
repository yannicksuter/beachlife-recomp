#include "runtime/retail_cursor.h"
#include "startup_input_state.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <string_view>
#include <thread>
#include <type_traits>

struct IDirectInputDeviceA;

namespace cursor_api = beachlife::runtime::retail_cursor;

namespace
{
std::size_t checks = 0;
std::size_t cursor_cases = 0;
std::size_t binding_cases = 0;
std::size_t method_calls = 0;
std::size_t wrapper_calls = 0;
std::size_t representation_checks = 0;
std::size_t load_events = 0;
std::size_t set_events = 0;
std::size_t failed_loads = 0;
std::size_t callback_cases = 0;
std::array<std::size_t, 3> keyboard_pointer_cases{};
std::array<std::array<std::size_t, 7>, 2> integer_cases{};
const char* phase = "cursor";
unsigned char handles[8]{};

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(
            stderr, "Cursor fixture failure: %s (%s case %zu).\n", message, phase, cursor_cases);
        std::exit(EXIT_FAILURE);
    }
}

struct GuardedInput
{
    unsigned char before[32];
    InputState input;
    unsigned char after[32];
};
using Representation = std::array<unsigned char, sizeof(GuardedInput)>;
static_assert(std::is_standard_layout_v<InputState>);
static_assert(std::is_trivially_copyable_v<GuardedInput>);
static_assert(std::is_same_v<decltype(InputState::resource_instance), void*>);
static_assert(std::is_same_v<decltype(InputState::cursor_identifier), int>);
static_assert(sizeof(int) == 4);
static_assert(offsetof(GuardedInput, input) == 32);
static_assert(offsetof(GuardedInput, after) == 32 + sizeof(InputState));

template <typename T>
constexpr bool has_keyboard_device = requires(T value) { value.keyboard_device; };

template <typename T> void native_values(Representation& bytes, unsigned int seed)
{
    if constexpr (has_keyboard_device<T>)
    {
        static_assert(std::is_same_v<decltype(T::keyboard_device), IDirectInputDeviceA*>);
        struct alignas(std::max_align_t) Identity
        {
            unsigned char value;
        };
        static Identity identities[2]{};
        const auto choice = seed % 3;
        using Pointer = decltype(T::keyboard_device);
        Pointer device = choice == 0
                             ? nullptr
                             : static_cast<Pointer>(static_cast<void*>(&identities[choice - 1]));
        // Identity pointers are only preserved as representations. No device
        // class is completed here and no call through these values is allowed.
        std::memcpy(bytes.data() + offsetof(GuardedInput, input) + offsetof(T, keyboard_device),
            &device,
            sizeof(device));
        ++keyboard_pointer_cases[choice];
    }
    constexpr std::array<int, 7> values{INT_MIN, -300, -1, 0, 1, 0x12345678, INT_MAX};
    if constexpr (requires { T::unknown_134; })
    {
        static_assert(std::is_same_v<decltype(T::unknown_134), int>);
        const auto choice = seed % values.size();
        const int first = values[choice];
        std::memcpy(bytes.data() + offsetof(GuardedInput, input) + offsetof(T, unknown_134),
            &first,
            sizeof(first));
        ++integer_cases[0][choice];
    }
    if constexpr (requires { T::unknown_138; })
    {
        if constexpr (std::is_same_v<decltype(T::unknown_138), int>)
        {
            const auto choice = (seed / values.size()) % values.size();
            const int second = values[choice];
            std::memcpy(bytes.data() + offsetof(GuardedInput, input) + offsetof(T, unknown_138),
                &second,
                sizeof(second));
            ++integer_cases[1][choice];
        }
    }
}

template <typename T> void report_native_values()
{
    std::printf(",\"keyboard_pointer\":%s,\"keyboard_pointer_offset\":",
        has_keyboard_device<T> ? "true" : "false");
    if constexpr (has_keyboard_device<T>)
    {
        std::printf("%zu", offsetof(T, keyboard_device));
    }
    else
    {
        std::printf("null");
    }
    std::printf(",\"keyboard_pointer_values\":[%zu,%zu,%zu],\"integer_values\":[",
        keyboard_pointer_cases[0],
        keyboard_pointer_cases[1],
        keyboard_pointer_cases[2]);
    for (std::size_t field = 0; field != integer_cases.size(); ++field)
    {
        std::printf(field == 0 ? "[" : ",[");
        for (std::size_t index = 0; index != integer_cases[field].size(); ++index)
        {
            std::printf(index == 0 ? "%zu" : ",%zu", integer_cases[field][index]);
        }
        std::printf("]");
    }
    std::printf("]");
}

void initialize(GuardedInput& value, unsigned int seed, void* instance, int identifier)
{
    Representation bytes;
    for (std::size_t offset = 0; offset != bytes.size(); ++offset)
    {
        bytes[offset] = static_cast<unsigned char>(seed + offset * 73u);
    }
    // Construct valid pointer and signed-integer representations before the
    // pattern enters the object. Other observed fields are unsigned bytes/words.
    std::memcpy(
        bytes.data() + offsetof(GuardedInput, input) + offsetof(InputState, resource_instance),
        &instance,
        sizeof(instance));
    std::memcpy(
        bytes.data() + offsetof(GuardedInput, input) + offsetof(InputState, cursor_identifier),
        &identifier,
        sizeof(identifier));
    native_values<InputState>(bytes, seed);
    std::memcpy(&value, bytes.data(), bytes.size());
}

void same_representation(
    const GuardedInput& actual, const GuardedInput& expected, const char* message)
{
    ++representation_checks;
    require(std::memcmp(&actual, &expected, sizeof(actual)) == 0, message);
}

struct Binding
{
    int identifier;
    std::uint16_t resource;
};
// Independent identifier/resource specification, not generated from the source.
constexpr std::array<Binding, 7> bindings{
    {{1, 106}, {2, 101}, {3, 102}, {4, 103}, {5, 107}, {6, 108}, {7, 111}}};

std::uint16_t resource_for(int identifier)
{
    for (const auto& binding : bindings)
    {
        if (binding.identifier == identifier)
        {
            return binding.resource;
        }
    }
    return 0;
}

enum class Kind
{
    load,
    set
};

struct Event
{
    Kind kind;
    void* instance = nullptr;
    std::uint16_t resource = 0;
    std::uint32_t type = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::uint32_t flags = 0;
    void* cursor = nullptr;
    int observed_identifier = 0;
    void* observed_instance = nullptr;
    bool operator==(const Event&) const = default;
};

constexpr int load_identifier = INT_MIN + 123;
constexpr int set_identifier = INT_MAX - 123;

struct ScriptedProvider final : cursor_api::Provider
{
    InputState* input = nullptr;
    void* load_result = nullptr;
    void* set_result = nullptr;
    unsigned int mutation = 0;
    std::array<Event, 8> events{};
    std::size_t count = 0;

    void record(Event event) noexcept
    {
        require(count != events.size(), "Provider event capacity exceeded.");
        if (input)
        {
            event.observed_identifier = input->cursor_identifier;
            event.observed_instance = input->resource_instance;
        }
        events[count++] = event;
    }

    void* load_image(void* instance,
        std::uint16_t resource,
        std::uint32_t type,
        std::int32_t width,
        std::int32_t height,
        std::uint32_t flags) noexcept override
    {
        ++load_events;
        record({Kind::load, instance, resource, type, width, height, flags});
        if (input && (mutation & 1u))
        {
            input->cursor_identifier = load_identifier;
            input->resource_instance = &handles[7];
        }
        return load_result;
    }

    void* set_cursor(void* cursor) noexcept override
    {
        ++set_events;
        Event event{Kind::set};
        event.cursor = cursor;
        record(event);
        if (input && (mutation & 2u))
        {
            input->cursor_identifier = set_identifier;
            input->states[13] = 0x96;
            input->unknown_120 = 0x84560123u;
        }
        return set_result;
    }
};

void run_case(int requested,
    int previous,
    int ignored,
    unsigned int instance_choice,
    unsigned int load_choice,
    unsigned int set_choice,
    unsigned int mutation)
{
    GuardedInput actual;
    void* instance = instance_choice == 0 ? nullptr : &handles[instance_choice];
    initialize(actual, static_cast<unsigned int>(cursor_cases * 19u + 11u), instance, previous);
    GuardedInput before;
    std::memcpy(&before, &actual, sizeof(before));
    ScriptedProvider provider;
    provider.input = &actual.input;
    provider.load_result = load_choice == 0 ? nullptr : &handles[load_choice + 2];
    provider.set_result = set_choice == 0 ? nullptr : &handles[set_choice + 4];
    provider.mutation = mutation;
    cursor_api::Session session(provider);

    // This wrapper passes the already cached identifier. Its unchanged second
    // argument does not force a reload, even for an invalid cached identifier.
    actual.input.ApplyCurrentCursor();
    ++method_calls;
    ++wrapper_calls;
    require(provider.count == 0, "Current-cursor wrapper emitted an API event.");
    same_representation(actual, before, "Current-cursor wrapper changed the representation.");

    const auto resource = resource_for(requested);
    const bool load = requested != previous && resource != 0;
    const bool set = load && provider.load_result != nullptr;
    std::array<Event, 2> expected_events{};
    std::size_t expected_count = 0;
    GuardedInput expected;
    std::memcpy(&expected, &before, sizeof(expected));
    if (load)
    {
        expected_events[expected_count++] = {
            Kind::load, instance, resource, 2, 32, 32, 0x8000, nullptr, previous, instance};
        if (mutation & 1u)
        {
            expected.input.cursor_identifier = load_identifier;
            expected.input.resource_instance = &handles[7];
        }
    }
    if (set)
    {
        Event event{Kind::set};
        event.cursor = provider.load_result;
        event.observed_identifier = expected.input.cursor_identifier;
        event.observed_instance = expected.input.resource_instance;
        expected_events[expected_count++] = event;
        if (mutation & 2u)
        {
            expected.input.cursor_identifier = set_identifier;
            expected.input.states[13] = 0x96;
            expected.input.unknown_120 = 0x84560123u;
        }
    }
    if (requested != previous)
    {
        expected.input.cursor_identifier = requested;
    }

    actual.input.SetCursorIdentifier(requested, ignored);
    ++method_calls;
    require(provider.count == expected_count, "Setter API event count differs from the oracle.");
    for (std::size_t index = 0; index != expected_count; ++index)
    {
        require(provider.events[index] == expected_events[index],
            "Setter API arguments, order or callback observation differs from the oracle.");
    }
    same_representation(actual, expected, "Setter final representation differs from the oracle.");
    failed_loads += load && !set;
    callback_cases += (load && (mutation & 1u)) || (set && (mutation & 2u));

    provider.count = 0;
    actual.input.ApplyCurrentCursor();
    ++method_calls;
    ++wrapper_calls;
    require(provider.count == 0, "Current-cursor wrapper reloaded a remembered request.");
    same_representation(actual, expected, "Current-cursor wrapper changed a remembered request.");
    const int other_ignored = ignored == INT_MAX ? INT_MIN : ignored + 1;
    actual.input.SetCursorIdentifier(requested, other_ignored);
    ++method_calls;
    require(
        provider.count == 0, "Repeated request emitted an event for a changed ignored argument.");
    same_representation(actual, expected, "Repeated request changed the representation.");
    ++cursor_cases;
}

void cursor_contract()
{
    constexpr std::array<int, 4> ignored{INT_MIN, -1, 0, INT_MAX};
    for (const auto& binding : bindings)
    {
        for (const int previous : {0, binding.identifier})
        {
            for (unsigned int instance = 0; instance != 3; ++instance)
            {
                for (unsigned int load = 0; load != 3; ++load)
                {
                    for (unsigned int set = 0; set != 3; ++set)
                    {
                        for (unsigned int mutation = 0; mutation != 4; ++mutation)
                        {
                            for (const int unused : ignored)
                            {
                                run_case(binding.identifier,
                                    previous,
                                    unused,
                                    instance,
                                    load,
                                    set,
                                    mutation);
                            }
                        }
                    }
                }
            }
        }
    }
    constexpr std::array<int, 9> invalid{INT_MIN, -32769, -1, 0, 8, 255, 256, 65535, INT_MAX};
    constexpr std::array<int, 7> ignored_boundaries{
        INT_MIN, INT_MIN + 1, -1, 0, 1, INT_MAX - 1, INT_MAX};
    for (const int requested : invalid)
    {
        for (const int previous : {requested, requested == INT_MAX ? 1 : requested + 1})
        {
            for (unsigned int instance = 0; instance != 3; ++instance)
            {
                for (const int unused : ignored_boundaries)
                {
                    run_case(requested, previous, unused, instance, 2, 2, 3);
                }
            }
        }
    }
}

void binding_contract()
{
    phase = "binding";
    ScriptedProvider outer;
    outer.load_result = &handles[1];
    outer.set_result = &handles[2];
    cursor_api::Session outer_session(outer);
    require(
        cursor_api::load_image(
            &handles[0], UINT16_MAX, UINT32_MAX, INT32_MIN, INT32_MAX, 0x80000001u) == &handles[1],
        "Bridge changed a load return.");
    const Event boundary{
        Kind::load, &handles[0], UINT16_MAX, UINT32_MAX, INT32_MIN, INT32_MAX, 0x80000001u};
    require(outer.count == 1 && outer.events[0] == boundary, "Bridge narrowed numeric arguments.");
    require(cursor_api::set_cursor(nullptr) == &handles[2], "Bridge changed a set return.");
    require(
        outer.count == 2 && outer.events[1] == Event{Kind::set}, "Bridge changed a null cursor.");
    ++binding_cases;
    {
        ScriptedProvider inner;
        cursor_api::Session inner_session(inner);
        require(cursor_api::load_image(nullptr, 0, 0, 0, -1, 0) == nullptr,
            "Nested load return changed.");
        require(cursor_api::set_cursor(&handles[6]) == nullptr, "Nested set return changed.");
        Event expected_set{Kind::set};
        expected_set.cursor = &handles[6];
        require(inner.count == 2 && inner.events[0] == Event{Kind::load, nullptr, 0, 0, 0, -1, 0} &&
                    inner.events[1] == expected_set,
            "Nested binding forwarded different events.");
        require(outer.count == 2, "Nested binding reached the outer provider.");
        ++binding_cases;
    }
    require(cursor_api::set_cursor(&handles[4]) == &handles[2], "Outer binding was not restored.");
    require(outer.count == 3 && outer.events[2].cursor == &handles[4],
        "Restored binding lost its argument.");
    ++binding_cases;
    bool thread_result = false;
    std::thread worker(
        [&]
        {
            ScriptedProvider local;
            local.load_result = &handles[3];
            local.set_result = &handles[5];
            cursor_api::Session session(local);
            thread_result =
                cursor_api::load_image(nullptr, 106, 2, 32, 32, 0x8000) == &handles[3] &&
                cursor_api::set_cursor(&handles[7]) == &handles[5] && local.count == 2;
        });
    worker.join();
    require(thread_result, "Worker binding did not use its own provider.");
    require(outer.count == 3, "Worker binding reached the creating thread's provider.");
    require(cursor_api::set_cursor(&handles[0]) == &handles[2],
        "Worker binding changed the outer session.");
    ++binding_cases;
}

int precondition_probe(std::string_view probe)
{
    // A deterministic host-only termination code avoids platform abort dialogs.
    // These probes do not execute either reconstructed method.
    std::set_terminate(
        []
        {
            std::_Exit(73);
        });
    if (probe == "--unbound-load")
    {
        cursor_api::load_image(nullptr, 106, 2, 32, 32, 0x8000);
    }
    else if (probe == "--unbound-set")
    {
        cursor_api::set_cursor(nullptr);
    }
    else if (probe == "--wrong-thread")
    {
        ScriptedProvider provider;
        auto* session = new cursor_api::Session(provider);
        std::thread worker(
            [&]
            {
                delete session;
            });
        worker.join();
    }
    else if (probe == "--wrong-order")
    {
        ScriptedProvider provider;
        auto* outer = new cursor_api::Session(provider);
        auto* inner = new cursor_api::Session(provider);
        delete outer;
        delete inner;
    }
    else
    {
        return 64;
    }
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2)
    {
        return precondition_probe(argv[1]);
    }
    if (argc != 1)
    {
        return 64;
    }
    cursor_contract();
    binding_contract();
    std::printf(
        "{\"cases\":%zu,\"checks\":%zu,\"cursor_cases\":%zu,\"binding_cases\":%zu,"
        "\"method_calls\":%zu,\"wrapper_calls\":%zu,\"representation_checks\":%zu,"
        "\"load_events\":%zu,\"set_events\":%zu,\"failed_loads\":%zu,\"callback_cases\":%zu",
        cursor_cases + binding_cases,
        checks,
        cursor_cases,
        binding_cases,
        method_calls,
        wrapper_calls,
        representation_checks,
        load_events,
        set_events,
        failed_loads,
        callback_cases);
    report_native_values<InputState>();
    std::printf("}\n");
    return 0;
}
