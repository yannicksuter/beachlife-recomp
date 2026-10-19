#include "startup_gui_parser.h"
#include "startup_screen_manager.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using Bytes = std::vector<unsigned char>;
constexpr std::size_t guard_size = 16;
constexpr std::array<unsigned int, 4> initial_offsets{0, 1, 7, 31};
constexpr std::array<int, 8> output_seeds{0,
    1,
    -1,
    std::numeric_limits<int>::max(),
    std::numeric_limits<int>::min(),
    0x13579bdf,
    -123456,
    42};
std::size_t sessions = 0;
std::size_t calls = 0;
std::size_t whitespace_calls = 0;
std::size_t integer_calls = 0;
std::size_t checks = 0;
std::size_t byte_checks = 0;

static_assert(std::numeric_limits<unsigned char>::digits == 8);
static_assert(std::numeric_limits<unsigned int>::digits == 32);
static_assert(std::numeric_limits<int>::digits == 31);
static_assert(std::is_standard_layout_v<GuiParserState>);
static_assert(std::is_trivially_copyable_v<GuiParserState>);
static_assert(std::is_trivially_copyable_v<ScreenManager>);

template <typename T> struct Guarded
{
    std::array<unsigned char, guard_size> before{};
    T value{};
    std::array<unsigned char, guard_size> after{};
};

enum class Operation
{
    skip,
    integer
};

struct Step
{
    Operation operation;
    bool consume;
    int value;
    unsigned int cursor;
};

struct Literal
{
    std::string_view input;
    unsigned int cursor;
};

struct Sequence
{
    std::string_view input;
    std::span<const Step> steps;
};

constexpr std::array<Literal, 12> literals{{
    // tab-control
    {{"\x09\x51", 2}, 1},
    // vertical-tab-stops
    {{"\x0b\x51", 2}, 0},
    // no-whitespace
    {{"\x51", 1}, 0},
    // four-whitespace
    {{"\x20\x0d\x0a\x09\x51", 5}, 4},
    // crlf
    {{"\x0d\x0a\x51", 3}, 2},
    // form-feed-stops
    {{"\x0c\x51", 2}, 0},
    // nul-stops
    {{"\x20\x00\x51", 3}, 1},
    // high-byte-stops
    {{"\x09\x20\x80\x51", 4}, 2},
    // comma-stops
    {{"\x2c\x51", 2}, 0},
    // close-stops
    {{"\x29\x51", 2}, 0},
    // minus-one-byte-stops
    {{"\x20\xff\x51", 3}, 1},
    // whitespace-before-vertical-tab
    {{"\x0a\x09\x0b\x51", 4}, 2},
}};

constexpr std::array<Step, 5> sequence_0{{
    {Operation::skip, false, 0, 2},
    {Operation::integer, true, 12, 7},
    {Operation::skip, false, 0, 7},
    {Operation::integer, true, 34, 11},
    {Operation::skip, false, 0, 11},
}};

constexpr std::array<Step, 7> sequence_1{{
    {Operation::skip, false, 0, 2},
    {Operation::integer, false, 0, 2},
    {Operation::skip, false, 0, 2},
    {Operation::integer, true, 0, 4},
    {Operation::skip, false, 0, 4},
    {Operation::integer, false, 7, 5},
    {Operation::integer, true, 0, 6},
}};

constexpr std::array<Step, 5> sequence_2{{
    {Operation::skip, false, 0, 1},
    {Operation::integer, true, -288, 6},
    {Operation::skip, false, 0, 6},
    {Operation::integer, true, 3, 8},
    {Operation::skip, false, 0, 8},
}};

constexpr std::array<Step, 3> sequence_3{{
    {Operation::skip, false, 0, 1},
    {Operation::integer, true, -37, 3},
    {Operation::skip, false, 0, 3},
}};

constexpr std::array<Step, 5> sequence_4{{
    {Operation::skip, false, 0, 1},
    {Operation::integer, true, -48, 3},
    {Operation::skip, false, 0, 3},
    {Operation::integer, true, 0, 4},
    {Operation::skip, false, 0, 4},
}};

constexpr std::array<Step, 5> sequence_5{{
    {Operation::skip, false, 0, 1},
    {Operation::integer, false, -176, 2},
    {Operation::skip, false, 0, 2},
    {Operation::integer, true, 0, 3},
    {Operation::skip, false, 0, 3},
}};

constexpr std::array<Sequence, 6> sequences{{
    // ordinary-two-values
    {{"\x09\x20\x31\x32\x2c\x0d\x0a\x33\x34\x29\x20\x51", 12}, sequence_0},
    // empty-unconsumed-delimiter
    {{"\x0d\x0a\x2c\x09\x37\x29\x51", 7}, sequence_1},
    // interior-tab-is-numeric
    {{"\x09\x31\x09\x32\x2c\x09\x33\x29\x51", 9}, sequence_2},
    // vertical-tab-is-numeric
    {{"\x20\x0b\x29\x51", 4}, sequence_3},
    // nul-is-numeric
    {{"\x09\x00\x29\x2c\x51", 5}, sequence_4},
    // high-byte-is-numeric
    {{"\x0a\x80\x29\x51", 4}, sequence_5},
}};

[[noreturn]] void fail(const char* message)
{
    std::cerr << "case " << calls << ": " << message << '\n';
    std::exit(2);
}

void require(bool value, const char* message)
{
    if (!value)
    {
        fail(message);
    }
}

void expect(bool value, const char* message)
{
    ++checks;
    require(value, message);
}

unsigned int word(std::size_t value)
{
    require(value <= std::numeric_limits<unsigned int>::max(), "fixture cursor exceeds word range");
    return static_cast<unsigned int>(value);
}

void fill(std::span<unsigned char> bytes, std::size_t seed)
{
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        bytes[i] = static_cast<unsigned char>((seed * 29 + i * 17 + 31) & 0xff);
    }
}

template <typename T> void guard(Guarded<T>& object, std::size_t seed)
{
    fill(object.before, seed);
    fill(object.after, seed + 37);
}

template <typename T> auto snapshot(const T& object)
{
    std::array<unsigned char, sizeof(T)> result{};
    std::memcpy(result.data(), std::addressof(object), result.size());
    return result;
}

template <std::size_t N, typename T>
void expected_write(
    std::array<unsigned char, N>& bytes, const void* object, const T& field, const T& value)
{
    const auto start = reinterpret_cast<std::uintptr_t>(object);
    const auto location = reinterpret_cast<std::uintptr_t>(std::addressof(field));
    require(location >= start, "fixture field precedes its guarded object");
    const auto difference = location - start;
    require(
        difference <= N && sizeof(T) <= N - difference, "fixture field exceeds its guarded object");
    std::memcpy(bytes.data() + difference, std::addressof(value), sizeof(T));
}

void expect_bytes(const void* actual, std::span<const unsigned char> expected, const char* message)
{
    const auto* bytes = static_cast<const unsigned char*>(actual);
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        ++byte_checks;
        require(bytes[i] == expected[i], message);
    }
    ++checks;
}

Bytes bytes(std::string_view input)
{
    return {input.begin(), input.end()};
}

void append(Bytes& target, std::span<const unsigned char> part)
{
    target.insert(target.end(), part.begin(), part.end());
}

void initialize_manager(Guarded<ScreenManager>& object, std::size_t seed)
{
    // These are live borrowed observed prefixes. No original manager constructor,
    // destructor or method is supplied or invoked by the reader fixture.
    guard(object, seed);
    fill(object.value.unknown_04, seed + 1);
    fill(object.value.unknown_90, seed + 2);
    fill(object.value.unknown_c0, seed + 3);
    object.value.renderer = nullptr;
    object.value.registry = nullptr;
    object.value.active.head = nullptr;
    object.value.mode = -123;
    object.value.previous_mode = 456;
#if defined(BEACHLIFE_DECOMP_SCREEN_ACTIVATION)
    object.value.auxiliary_130 = nullptr;
    object.value.item_134 = nullptr;
    object.value.value_138 = 0.0f;
#endif
}

void run_session(
    std::span<const unsigned char> input, unsigned int initial_cursor, std::span<const Step> steps)
{
    require(!input.empty() && !steps.empty(), "fixture requires readable input and a call");
    require(initial_cursor <= 31 && input.size() <= 1024, "fixture exceeded its bounded extent");
    const std::size_t size = guard_size + initial_cursor + input.size() + guard_size;
    Bytes original(size);
    fill(original, sessions);
    std::copy(input.begin(), input.end(), original.begin() + guard_size + initial_cursor);
    std::unique_ptr<char[]> storage(new char[size]);
    std::memcpy(storage.get(), original.data(), size);
    const char* const buffer = storage.get() + guard_size;

    std::array<Guarded<ScreenManager>, 2> managers{};
    initialize_manager(managers[0], sessions + 11);
    initialize_manager(managers[1], sessions + 19);
    const auto expected_managers = snapshot(managers);
    const std::array<ScreenManager*, 3> manager_choices{
        nullptr, &managers[0].value, &managers[1].value};
    ScreenManager* const manager = manager_choices[sessions % manager_choices.size()];

    Guarded<GuiParserState> reader{};
    guard(reader, sessions + 23);
    reader.value.manager = manager;
    reader.value.cursor = initial_cursor;
    reader.value.buffer = buffer;
    // Only untouched prefix representation is exercised here; context lifetimes
    // and registration remain separate from these reader cases.
    reader.value.current_context = nullptr;
    reader.value.alternate_context_10 = nullptr;
    reader.value.alternate_context_14 = nullptr;
    reader.value.context_head = nullptr;
    reader.value.previous_context = nullptr;
    reader.value.last_registered_item = nullptr;
    std::memset(reader.value.unknown_24, 0x5a, sizeof(reader.value.unknown_24));
    reader.value.registry_head = nullptr;
    reader.value.items_head = nullptr;
    reader.value.items_tail = nullptr;
    reader.value.registry_tail = nullptr;
    reader.value.word_38 = 0xa5a50000U ^ static_cast<unsigned int>(sessions);

    Guarded<int> output{};
    guard(output, sessions + 41);
    int expected_value = output_seeds[sessions % output_seeds.size()];
    output.value = expected_value;

    for (const auto& step : steps)
    {
        require(step.cursor < input.size(), "fixture expected cursor is not readable");
        const unsigned int expected_cursor = initial_cursor + step.cursor;
        auto expected_reader = snapshot(reader);
        expected_write(expected_reader, &reader, reader.value.cursor, expected_cursor);
        if (step.operation == Operation::integer)
        {
            expected_value = step.value;
        }
        auto expected_output = snapshot(output);
        expected_write(expected_output, &output, output.value, expected_value);

        if (step.operation == Operation::skip)
        {
            reader.value.SkipWhitespace();
        }
        else
        {
            reader.value.ReadInteger(output.value, step.consume);
        }

        // The TAB-to-VT control must fail here on the first literal call.
        expect(reader.value.cursor == expected_cursor,
            step.operation == Operation::skip ? "whitespace cursor differs"
                                              : "integer cursor differs");
        expect(output.value == expected_value, "integer output or untouched word differs");
        expect(reader.value.buffer == buffer, "buffer identity changed");
        expect(reader.value.manager == manager, "manager identity changed");
        expect_bytes(&reader, expected_reader, "reader representation or guards changed");
        expect_bytes(&output, expected_output, "output representation or guards changed");
        expect_bytes(storage.get(), original, "input buffer or guards changed");
        expect_bytes(&managers, expected_managers, "borrowed manager prefixes or guards changed");
        if (step.operation == Operation::skip)
        {
            ++whitespace_calls;
        }
        else
        {
            ++integer_calls;
        }
        ++calls;
    }
    ++sessions;
}

void run_two_skips(std::span<const unsigned char> input, unsigned int offset, unsigned int cursor)
{
    const std::array<Step, 2> steps{
        {{Operation::skip, false, 0, cursor}, {Operation::skip, false, 0, cursor}}};
    run_session(input, offset, steps);
}

void literal_cases()
{
    for (const auto& test : literals)
    {
        const auto input = bytes(test.input);
        for (const auto offset : initial_offsets)
        {
            run_two_skips(input, offset, test.cursor);
        }
    }
    require(sessions == 48 && calls == 96, "literal case count differs");
}

std::vector<Bytes> whitespace_strings()
{
    constexpr std::array<unsigned char, 4> alphabet{' ', '\r', '\n', '\t'};
    std::vector<Bytes> result{{}};
    for (unsigned int length = 1; length <= 3; ++length)
    {
        for (unsigned int choice = 0; choice != (1U << (2 * length)); ++choice)
        {
            Bytes text(length);
            for (unsigned int i = 0; i != length; ++i)
            {
                text[i] = alphabet[(choice >> (2 * i)) & 3];
            }
            result.push_back(std::move(text));
        }
    }
    require(result.size() == 85, "whitespace set differs");
    return result;
}

void boundary_byte_cases()
{
    for (const auto& prefix : whitespace_strings())
    {
        for (unsigned int boundary = 0; boundary != 256; ++boundary)
        {
            Bytes input;
            append(input, prefix);
            input.push_back(static_cast<unsigned char>(boundary));
            input.push_back('Q');
            // The known prefix length and one boundary-byte classification are
            // independent of the reconstructed scanning loop.
            const bool continues =
                boundary == 9 || boundary == 10 || boundary == 13 || boundary == 32;
            const unsigned int cursor = word(prefix.size()) + (continues ? 1U : 0U);
            for (const auto offset : initial_offsets)
            {
                run_two_skips(input, offset, cursor);
            }
        }
    }
    require(sessions == 87088 && calls == 174176, "boundary case count differs");
}

void long_run_cases()
{
    constexpr std::array<unsigned int, 12> lengths{
        4, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256, 1023};
    constexpr std::array<unsigned char, 4> alphabet{' ', '\r', '\n', '\t'};
    for (const auto length : lengths)
    {
        for (unsigned int pattern = 0; pattern != 5; ++pattern)
        {
            Bytes input(length);
            for (unsigned int i = 0; i != length; ++i)
            {
                input[i] = alphabet[pattern == 4 ? i % 4 : pattern];
            }
            input.push_back('Q');
            for (const auto offset : initial_offsets)
            {
                run_two_skips(input, offset, length);
            }
        }
    }
    require(sessions == 87328 && calls == 174656, "long case count differs");
}

void sequential_reader_cases()
{
    for (const auto& sequence : sequences)
    {
        const auto input = bytes(sequence.input);
        for (const auto offset : initial_offsets)
        {
            run_session(input, offset, sequence.steps);
        }
    }
}
} // namespace

int main()
{
    literal_cases();
    boundary_byte_cases();
    long_run_cases();
    sequential_reader_cases();
    require(sessions == 87352 && calls == 174776, "final session/call count differs");
    require(whitespace_calls == 174724 && integer_calls == 52, "reader call count differs");
    require(whitespace_calls + integer_calls == calls && checks == 8 * calls,
        "state assertion count differs");
    std::cout << "sessions=" << sessions << " calls=" << calls
              << " whitespace_calls=" << whitespace_calls << " integer_calls=" << integer_calls
              << " checks=" << checks << " byte_checks=" << byte_checks << '\n';
}
