#include "startup_input_state.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace
{
struct GuardedInput
{
    unsigned char before[32];
    InputState input;
    unsigned char after[32];
};
using Representation = std::array<unsigned char, sizeof(GuardedInput)>;

template <typename T>
constexpr bool has_resource_instance = requires(T value) { value.resource_instance; };

template <typename T> constexpr bool qualified_prefix()
{
    static_assert(std::is_same_v<decltype(T::unknown_120), unsigned int>);
    static_assert(std::is_same_v<decltype(T::unknown_124), unsigned int>);
    static_assert(std::is_same_v<decltype(T::unknown_610), unsigned char>);
    static_assert(std::is_same_v<decltype(T::unknown_611), unsigned char>);
    static_assert(std::is_same_v<decltype(T::states), unsigned char[256]>);
    static_assert(sizeof(T::unknown_11d) == 3);
    if constexpr (has_resource_instance<T>)
    {
        static_assert(std::is_same_v<decltype(T::resource_instance), void*>);
        static_assert(std::is_same_v<decltype(T::cursor_identifier), int>);
        static_assert(std::is_same_v<decltype(T::unknown_00), unsigned char[0x10]>);
        static_assert(sizeof(T::unknown_14) == 9);
        static_assert(sizeof(T::unknown_128) == 0x1c);
        static_assert(sizeof(T::unknown_148) == 0x4c8);
        static_assert(offsetof(T, resource_instance) == 0x10);
        static_assert(offsetof(T, unknown_14) == offsetof(T, resource_instance) + sizeof(void*));
        static_assert(offsetof(T, states) == offsetof(T, unknown_14) + sizeof(T::unknown_14));
        static_assert(offsetof(T, cursor_identifier) == offsetof(T, unknown_128) + 0x1c);
        static_assert(offsetof(T, unknown_148) == offsetof(T, cursor_identifier) + sizeof(int));
        static_assert(offsetof(T, unknown_610) == offsetof(T, unknown_148) + 0x4c8);
    }
    else
    {
        // The earlier all-unsigned prefix has no pointer-width-dependent fields.
        static_assert(std::is_same_v<decltype(T::unknown_00), unsigned char[0x1d]>);
        static_assert(sizeof(T::unknown_128) == 0x4e8);
        static_assert(offsetof(T, states) == 0x1d);
        static_assert(offsetof(T, unknown_120) == 0x120);
        static_assert(offsetof(T, unknown_610) == 0x610);
        static_assert(sizeof(T) == 0x614);
    }
    static_assert(offsetof(T, unknown_120) == offsetof(T, states) + 256 + 3);
    static_assert(offsetof(T, unknown_124) == offsetof(T, unknown_120) + 4);
    static_assert(offsetof(T, unknown_128) == offsetof(T, unknown_124) + 4);
    static_assert(offsetof(T, unknown_611) == offsetof(T, unknown_610) + 1);
    static_assert(
        sizeof(T) == (offsetof(T, unknown_611) + 1 + alignof(T) - 1) / alignof(T) * alignof(T));
    return true;
}

static_assert(std::is_standard_layout_v<InputState>);
static_assert(std::is_trivially_copyable_v<GuardedInput>);
static_assert(sizeof(unsigned int) == 4);
static_assert(sizeof(int) == 4);
static_assert(qualified_prefix<InputState>());
static_assert(offsetof(GuardedInput, input) == 32);
static_assert(offsetof(GuardedInput, after) == 32 + sizeof(InputState));

// The audited contract clears two four-byte words and two single bytes. Locate
// those fields in the actual native layout; all other bytes remain guarded.
constexpr std::array<std::size_t, 10> cleared_bytes{offsetof(InputState, unknown_120),
    offsetof(InputState, unknown_120) + 1,
    offsetof(InputState, unknown_120) + 2,
    offsetof(InputState, unknown_120) + 3,
    offsetof(InputState, unknown_124),
    offsetof(InputState, unknown_124) + 1,
    offsetof(InputState, unknown_124) + 2,
    offsetof(InputState, unknown_124) + 3,
    offsetof(InputState, unknown_610),
    offsetof(InputState, unknown_611)};
constexpr auto accepted = []
{
    std::array<std::array<unsigned char, 256>, 2> table{};
    table[0][1] = 1;
    table[1][2] = 1;
    return table;
}();

std::size_t checks = 0;
std::size_t predicate_cases = 0;
std::size_t reset_cases = 0;
std::size_t representation_checks = 0;
std::array<std::size_t, 3> resource_cases{};
const char* phase = "predicate";
std::size_t current_case = 0;

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

Representation snapshot(const GuardedInput& value)
{
    Representation bytes;
    std::memcpy(bytes.data(), &value, bytes.size());
    return bytes;
}

template <typename T> void native_values(Representation& bytes, unsigned int seed)
{
    if constexpr (has_resource_instance<T>)
    {
        static unsigned char resources[2]{};
        const auto choice = seed % 3;
        void* instance = choice == 0 ? nullptr : &resources[choice - 1];
        constexpr std::array<int, 7> cursors{-2147483647 - 1, -1, 0, 1, 7, 0x12345678, 2147483647};
        const int cursor = cursors[seed % cursors.size()];
        // Copy representations of real typed values into the pattern before it
        // enters the object. No arbitrary bit pattern is used as a pointer.
        std::memcpy(bytes.data() + offsetof(GuardedInput, input) + offsetof(T, resource_instance),
            &instance,
            sizeof(instance));
        std::memcpy(bytes.data() + offsetof(GuardedInput, input) + offsetof(T, cursor_identifier),
            &cursor,
            sizeof(cursor));
        ++resource_cases[choice];
    }
}

void patterned(GuardedInput& value, unsigned int seed)
{
    Representation bytes;
    for (std::size_t offset = 0; offset != bytes.size(); ++offset)
    {
        bytes[offset] = static_cast<unsigned char>(offset * 73u + seed);
    }
    native_values<InputState>(bytes, seed);
    // The remaining declared fields are unsigned words/bytes. The snapshot
    // comparison includes typed values, opaque fields, padding and outer guards.
    std::memcpy(&value, bytes.data(), bytes.size());
}

void require_representation(
    const GuardedInput& value, const Representation& expected, const char* message)
{
    ++representation_checks;
    require(std::memcmp(&value, expected.data(), expected.size()) == 0, message);
}

void predicates()
{
    phase = "predicate";
    for (unsigned int index = 0; index != 256; ++index)
    {
        for (unsigned int state = 0; state != 256; ++state)
        {
            current_case = index * 256 + state;
            GuardedInput value;
            patterned(value, index * 17u + 11u);
            value.input.states[index] = static_cast<unsigned char>(state);
            const auto before = snapshot(value);
            // The independent accepted-state sets also require the exact
            // returned byte, rather than converting any nonzero result to true.
            require(value.input.IsStateOne(static_cast<int>(index)) == accepted[0][state],
                "IsStateOne returned a byte outside its accepted-state oracle.");
            require_representation(value, before, "IsStateOne changed the object or its guards.");
            require(value.input.IsStateTwo(static_cast<int>(index)) == accepted[1][state],
                "IsStateTwo returned a byte outside its accepted-state oracle.");
            require_representation(value, before, "IsStateTwo changed the object or its guards.");
            ++predicate_cases;
        }
    }
}

void reset(GuardedInput& value, unsigned int index)
{
    current_case = reset_cases;
    const auto before = snapshot(value);
    auto expected = before;
    for (const auto offset : cleared_bytes)
    {
        expected[offsetof(GuardedInput, input) + offset] = 0;
    }
    const auto state = before[offsetof(GuardedInput, input) + offsetof(InputState, states) + index];
    value.input.ResetTransition();
    require_representation(
        value, expected, "ResetTransition changed bytes outside its exact clear contract.");
    require(value.input.IsStateOne(static_cast<int>(index)) == accepted[0][state],
        "ResetTransition changed state-one query behavior.");
    require(value.input.IsStateTwo(static_cast<int>(index)) == accepted[1][state],
        "ResetTransition changed state-two query behavior.");
    value.input.ResetTransition();
    require_representation(value, expected, "ResetTransition was not idempotent.");
    ++reset_cases;
}

void transitions()
{
    phase = "reset";
    constexpr std::array<unsigned int, 9> words{0u,
        1u,
        0x7fffffffu,
        0x80000000u,
        0xffffffffu,
        0x01234567u,
        0x89abcdefu,
        0xaaaaaaaau,
        0x55555555u};
    for (unsigned int byte = 0; byte != 256; ++byte)
    {
        GuardedInput value;
        patterned(value, byte * 19u + 11u);
        value.input.states[byte] = static_cast<unsigned char>(byte);
        value.input.unknown_120 = words[byte % words.size()];
        value.input.unknown_124 = words[(byte / words.size()) % words.size()];
        value.input.unknown_610 = static_cast<unsigned char>(byte);
        value.input.unknown_611 = static_cast<unsigned char>(byte ^ 0x5au);
        reset(value, byte);
    }
    // One active bit in each of the ten cleared bytes isolates the observed
    // store widths. The surrounding opaque fields and tail remain nonzero.
    for (const auto changed : cleared_bytes)
    {
        GuardedInput value;
        patterned(value, 0x37u);
        auto bytes = snapshot(value);
        for (const auto offset : cleared_bytes)
        {
            bytes[offsetof(GuardedInput, input) + offset] = 0;
        }
        bytes[offsetof(GuardedInput, input) + changed] = 0x80;
        std::memcpy(&value, bytes.data(), bytes.size());
        reset(value, static_cast<unsigned int>(changed % 256));
    }
}
} // namespace

int main()
{
    try
    {
        predicates();
        transitions();
        std::cout
            << "{\"cases\":" << predicate_cases + reset_cases << ",\"checks\":" << checks
            << ",\"predicate_cases\":" << predicate_cases << ",\"reset_cases\":" << reset_cases
            << ",\"representation_checks\":" << representation_checks
            << ",\"indices\":256,\"state_values\":256,\"cleared_bytes\":10,\"native_prefix_bytes\":"
            << sizeof(InputState)
            << ",\"pointer_prefix\":" << (has_resource_instance<InputState> ? "true" : "false")
            << ",\"state_offset\":" << offsetof(InputState, states)
            << ",\"reset_word_offset\":" << offsetof(InputState, unknown_120)
            << ",\"reset_byte_offset\":" << offsetof(InputState, unknown_610)
            << ",\"resource_values\":[" << resource_cases[0] << ',' << resource_cases[1] << ','
            << resource_cases[2] << "]}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Input fixture failure: " << error.what() << " (" << phase << " case "
                  << current_case << ").\n";
        return 1;
    }
}
