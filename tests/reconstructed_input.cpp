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

static_assert(std::is_standard_layout_v<InputState>);
static_assert(std::is_trivially_copyable_v<GuardedInput>);
static_assert(sizeof(InputState) == 0x614); // Current native prefix, including its tail padding.
static_assert(sizeof(unsigned int) == 4);
static_assert(offsetof(InputState, states) == 0x1d);
static_assert(std::is_same_v<decltype(InputState::unknown_00), unsigned char[0x1d]>);
static_assert(sizeof(InputState::states) == 256);
static_assert(offsetof(InputState, unknown_120) == 0x120);
static_assert(offsetof(InputState, unknown_124) == 0x124);
static_assert(offsetof(InputState, unknown_610) == 0x610);
static_assert(offsetof(InputState, unknown_611) == 0x611);
static_assert(offsetof(GuardedInput, input) == 32);
static_assert(offsetof(GuardedInput, after) == 32 + sizeof(InputState));

// These offsets describe the observed fields, not the complete retail object.
// Native tail padding and every other declared byte remain part of the guard.
constexpr std::array<std::size_t, 10> cleared_bytes{
    0x120, 0x121, 0x122, 0x123, 0x124, 0x125, 0x126, 0x127, 0x610, 0x611};
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

void patterned(GuardedInput& value, unsigned int seed)
{
    Representation bytes;
    for (std::size_t offset = 0; offset != bytes.size(); ++offset)
    {
        bytes[offset] = static_cast<unsigned char>(offset * 73u + seed);
    }
    // All declared fields have unsigned integer/byte representations. Populate
    // the trivial object and its padding without introducing invalid values.
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
    const auto state = before[offsetof(GuardedInput, input) + 0x1d + index];
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
            << sizeof(InputState) << "}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Input fixture failure: " << error.what() << " (" << phase << " case "
                  << current_case << ").\n";
        return 1;
    }
}
