#include "startup_gui_parser.h"
#include "startup_screen_manager.h"

#include <algorithm>
#include <array>
#include <charconv>
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

struct Step
{
    bool consume;
    int value;
    unsigned int cursor;
};

struct Literal
{
    std::string_view input;
    int value;
    unsigned int cursor_false;
    unsigned int cursor_true;
};

struct Sequence
{
    std::string_view input;
    std::span<const Step> steps;
};

constexpr std::array<Literal, 50> literals{{
    // control-witness
    {{"\x31\x32\x29\x51", 4}, 12, 2, 3},
    // empty-comma
    {{"\x2c\x51", 2}, 0, 0, 1},
    // empty-close
    {{"\x29\x51", 2}, 0, 0, 1},
    // zero
    {{"\x30\x2c\x51", 3}, 0, 1, 2},
    // ordinary-close
    {{"\x31\x37\x29\x51", 4}, 17, 2, 3},
    // leading-four-whitespace
    {{"\x20\x0d\x0a\x09\x2c\x51", 6}, 0, 4, 5},
    // space-break-then-all-whitespace
    {{"\x34\x32\x20\x09\x0d\x0a\x2c\x20\x09\x51", 10}, 42, 6, 9},
    // space-break-nondelimiter
    {{"\x34\x32\x20\x0d\x0a\x78\x51", 7}, 42, 5, 5},
    // one-comma-only
    {{"\x31\x37\x2c\x2c\x20\x51", 6}, 17, 2, 3},
    // one-close-only
    {{"\x31\x37\x29\x20\x09\x29\x51", 7}, 17, 2, 5},
    // vertical-tab-after-break
    {{"\x31\x37\x20\x0b\x2c\x51", 6}, 17, 3, 3},
    // vertical-tab-is-arithmetic
    {{"\x0b\x29\x51", 3}, -37, 1, 2},
    // form-feed-is-arithmetic
    {{"\x0c\x29\x51", 3}, -36, 1, 2},
    // interior-tab
    {{"\x31\x09\x32\x29\x51", 5}, -288, 3, 4},
    // interior-cr
    {{"\x31\x0d\x32\x29\x51", 5}, -248, 3, 4},
    // interior-lf
    {{"\x31\x0a\x32\x29\x51", 5}, -278, 3, 4},
    // nul-is-arithmetic
    {{"\x00\x29\x51", 3}, -48, 1, 2},
    // interior-nul-space-break
    {{"\x31\x00\x20\x32\x51", 5}, -38, 3, 3},
    // uppercase-is-arithmetic
    {{"\x41\x2c\x51", 3}, 17, 1, 2},
    // lowercase-is-arithmetic
    {{"\x61\x29\x51", 3}, 49, 1, 2},
    // colon-is-arithmetic
    {{"\x3a\x29\x51", 3}, 10, 1, 2},
    // slash-is-arithmetic
    {{"\x2f\x29\x51", 3}, -1, 1, 2},
    // minus-is-not-sign
    {{"\x2d\x31\x32\x29\x51", 5}, -288, 3, 4},
    // plus-is-not-sign
    {{"\x2b\x31\x32\x29\x51", 5}, -488, 3, 4},
    // period-is-not-decimal
    {{"\x31\x32\x2e\x35\x29\x51", 6}, 1185, 4, 5},
    // hex-prefix-is-not-special
    {{"\x30\x78\x31\x30\x29\x51", 6}, 7210, 4, 5},
    // lowest-signed-byte
    {{"\x80\x29\x51", 3}, -176, 1, 2},
    // minus-one-byte
    {{"\xff\x29\x51", 3}, -49, 1, 2},
    // highest-signed-byte
    {{"\x7f\x29\x51", 3}, 79, 1, 2},
    // signed-byte-followed-by-digit
    {{"\x80\x31\x2c\x51", 4}, -1759, 2, 3},
    // minus-one-byte-followed-by-zero
    {{"\xff\x30\x2c\x51", 4}, -490, 2, 3},
    // two-negative-bytes
    {{"\x81\x82\x29\x51", 4}, -1924, 2, 3},
    // mixed-sign-bytes
    {{"\x7f\xff\x29\x51", 4}, 741, 2, 3},
    // leading-zeroes
    {{"\x30\x30\x30\x30\x30\x31\x37\x2c\x51", 9}, 17, 7, 8},
    // large-safe-decimal
    {{"\x39\x39\x39\x39\x39\x39\x39\x39\x29\x51", 10}, 99999999, 8, 9},
    // post-delimiter-nul-is-next-byte
    {{"\x31\x32\x2c\x00", 4}, 12, 2, 3},
    // post-delimiter-all-whitespace
    {{"\x31\x32\x2c\x20\x0d\x0a\x09\x51", 8}, 12, 2, 7},
    // whitespace-on-all-sides
    {{"\x20\x09\x31\x32\x20\x0a\x29\x20\x0d\x51", 10}, 12, 6, 9},
    // open-paren-after-break
    {{"\x31\x32\x20\x28\x51", 5}, 12, 3, 3},
    // high-byte-after-break
    {{"\x31\x32\x20\x80\x51", 5}, 12, 3, 3},
    // negative-intermediate
    {{"\x30\x2f\x39\x29\x51", 5}, -1, 3, 4},
    // minus-in-token
    {{"\x30\x31\x2d\x32\x29\x51", 6}, 72, 4, 5},
    // leading-tab-before-empty-comma
    {{"\x20\x09\x2c\x51", 4}, 0, 2, 3},
    // leading-newlines-empty-close
    {{"\x0d\x0a\x29\x20\x51", 5}, 0, 2, 4},
    // comma-does-not-consume-close
    {{"\x31\x32\x2c\x29\x09\x51", 6}, 12, 2, 3},
    // close-does-not-consume-comma
    {{"\x31\x32\x29\x0d\x0a\x2c\x51", 7}, 12, 2, 5},
    // nul-after-space-is-unconsumed
    {{"\x31\x32\x20\x00\x2c\x51", 6}, 12, 3, 3},
    // empty-close-followed-by-tab-comma
    {{"\x20\x29\x09\x2c\x51", 5}, 0, 1, 3},
    // next-number-is-unconsumed
    {{"\x30\x30\x20\x33\x34\x29\x51", 7}, 0, 3, 3},
    // leading-whitespace-zeroes
    {{"\x20\x09\x30\x30\x30\x30\x30\x30\x29\x51", 10}, 0, 8, 9},
}};

constexpr std::array<Step, 2> sequence_0{{
    {true, 12, 5},
    {true, 34, 9},
}};

constexpr std::array<Step, 4> sequence_1{{
    {false, 17, 2},
    {false, 0, 2},
    {true, 0, 3},
    {true, 0, 5},
}};

constexpr std::array<Step, 4> sequence_2{{
    {false, 0, 0},
    {true, 0, 3},
    {false, 7, 4},
    {true, 0, 6},
}};

constexpr std::array<Step, 2> sequence_3{{
    {true, 1, 4},
    {true, 2, 7},
}};

constexpr std::array<Step, 2> sequence_4{{
    {true, 12, 3},
    {true, -48, 6},
}};

constexpr std::array<Step, 3> sequence_5{{
    {true, -288, 5},
    {false, 3, 6},
    {true, 0, 7},
}};

constexpr std::array<Sequence, 6> sequences{{
    // two-ordinary-numbers
    {{"\x31\x32\x2c\x20\x09\x33\x34\x29\x20\x51", 10}, sequence_0},
    // unconsumed-and-adjacent-commas
    {{"\x31\x37\x2c\x2c\x20\x51", 6}, sequence_1},
    // empty-then-number-then-empty-close
    {{"\x2c\x20\x09\x37\x29\x20\x51", 7}, sequence_2},
    // space-break-next-number
    {{"\x31\x20\x0d\x0a\x32\x2c\x20\x51", 8}, sequence_3},
    // nul-next-number
    {{"\x31\x32\x2c\x00\x29\x20\x51", 7}, sequence_4},
    // malformed-then-digit
    {{"\x31\x09\x32\x2c\x20\x33\x29\x51", 8}, sequence_5},
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
}

void run_session(
    std::span<const unsigned char> input, unsigned int initial_cursor, std::span<const Step> steps)
{
    require(!input.empty() && !steps.empty(), "fixture requires readable input and a call");
    require(initial_cursor <= 31 && input.size() <= 64, "fixture exceeded its bounded extent");
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

    for (const auto& step : steps)
    {
        require(step.cursor < input.size(), "fixture expected cursor is not readable");
        const unsigned int expected_cursor = initial_cursor + step.cursor;
        auto expected_reader = snapshot(reader);
        expected_write(expected_reader, &reader, reader.value.cursor, expected_cursor);

        Guarded<int> output{};
        guard(output, calls + 41);
        output.value = output_seeds[calls % output_seeds.size()];
        auto expected_output = snapshot(output);
        expected_write(expected_output, &output, output.value, step.value);

        reader.value.ReadInteger(output.value, step.consume);

        // The multiplier control must fail this first assertion for case zero.
        expect(output.value == step.value, "integer value differs");
        expect(reader.value.cursor == expected_cursor, "integer cursor differs");
        expect(reader.value.buffer == buffer, "buffer identity changed");
        expect(reader.value.manager == manager, "manager identity changed");
        expect_bytes(&reader, expected_reader, "reader representation or guards changed");
        expect_bytes(&output, expected_output, "output representation or guards changed");
        expect_bytes(storage.get(), original, "input buffer or guards changed");
        expect_bytes(&managers, expected_managers, "borrowed manager prefixes or guards changed");
        ++calls;
    }
    ++sessions;
}

void run_one(std::span<const unsigned char> input,
    unsigned int offset,
    bool consume,
    int value,
    unsigned int cursor)
{
    const std::array<Step, 1> steps{{{consume, value, cursor}}};
    run_session(input, offset, steps);
}

unsigned int varied_offset()
{
    return initial_offsets[sessions % initial_offsets.size()];
}

void literal_cases()
{
    for (const auto& test : literals)
    {
        const auto input = bytes(test.input);
        for (const auto offset : initial_offsets)
        {
            for (const bool consume : {false, true})
            {
                run_one(input,
                    offset,
                    consume,
                    test.value,
                    consume ? test.cursor_true : test.cursor_false);
            }
        }
    }
    require(sessions == 400 && calls == 400, "literal case count differs");
}

void decimal_cases()
{
    const std::array<std::string_view, 8> whitespace{
        "", " ", "\r", "\n", "\t", " \r\n\t", "\t\n\r ", "  "};
    for (int value = 0; value != 10000; ++value)
    {
        std::array<char, 16> encoded{};
        const auto converted =
            std::to_chars(encoded.data(), encoded.data() + encoded.size(), value);
        require(converted.ec == std::errc{}, "fixture decimal encoding failed");
        const auto digits =
            bytes({encoded.data(), static_cast<std::size_t>(converted.ptr - encoded.data())});
        const auto leading = bytes(whitespace[static_cast<std::size_t>(value) % whitespace.size()]);
        const auto following =
            bytes(whitespace[(static_cast<std::size_t>(value) / 8) % whitespace.size()]);
        for (const unsigned char delimiter :
            {static_cast<unsigned char>(','), static_cast<unsigned char>(')')})
        {
            Bytes input = leading;
            append(input, digits);
            input.push_back(delimiter);
            append(input, following);
            input.push_back('Q');
            const auto stop = word(leading.size() + digits.size());
            for (const bool consume : {false, true})
            {
                // The value predates decimal encoding. The cursor comes from
                // constructed lengths; this oracle does not parse the bytes.
                run_one(input,
                    varied_offset(),
                    consume,
                    value,
                    consume ? stop + 1 + word(following.size()) : stop);
            }
        }
    }
    require(sessions == 40400 && calls == 40400, "decimal case count differs");
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

void whitespace_cases()
{
    for (const auto& space : whitespace_strings())
    {
        for (unsigned int role = 0; role != 3; ++role)
        {
            const Bytes leading = role == 0 ? space : Bytes{};
            Bytes gap;
            if (role == 1)
            {
                gap.push_back(' ');
                append(gap, space);
            }
            const Bytes following = role == 2 ? space : Bytes{};
            for (const unsigned char delimiter :
                {static_cast<unsigned char>(','), static_cast<unsigned char>(')')})
            {
                Bytes input = leading;
                input.push_back('4');
                input.push_back('2');
                append(input, gap);
                input.push_back(delimiter);
                append(input, following);
                input.push_back('Q');
                const auto stop = word(leading.size() + 2 + gap.size());
                for (const bool consume : {false, true})
                {
                    run_one(input,
                        varied_offset(),
                        consume,
                        42,
                        consume ? stop + 1 + word(following.size()) : stop);
                }
            }
        }
    }
    require(sessions == 41420 && calls == 41420, "whitespace case count differs");
}

int signed_byte(unsigned int byte)
{
    return byte < 128 ? static_cast<int>(byte) : static_cast<int>(byte) - 256;
}

bool numeric_terminator(unsigned int byte)
{
    return byte == 0x20 || byte == 0x2c || byte == 0x29;
}

void single_byte_cases()
{
    for (unsigned int byte = 0; byte != 256; ++byte)
    {
        const std::array<unsigned char, 4> input{'1', static_cast<unsigned char>(byte), ')', 'Q'};
        const bool early_delimiter = byte == 0x2c || byte == 0x29;
        const int value = numeric_terminator(byte) ? 1 : signed_byte(byte) - 38;
        const unsigned int stop = early_delimiter ? 1U : 2U;
        for (const bool consume : {false, true})
        {
            run_one(input, varied_offset(), consume, value, stop + (consume ? 1U : 0U));
        }
    }
    require(sessions == 41932 && calls == 41932, "single-byte case count differs");
}

void two_byte_cases()
{
    for (unsigned int first = 0; first != 256; ++first)
    {
        if (numeric_terminator(first))
        {
            continue;
        }
        for (unsigned int second = 0; second != 256; ++second)
        {
            if (numeric_terminator(second))
            {
                continue;
            }
            const std::array<unsigned char, 5> input{'1',
                static_cast<unsigned char>(first),
                static_cast<unsigned char>(second),
                ')',
                'Q'};
            // Closed polynomial for the known three-byte numeric segment.
            // The reconstructed scanning/accumulation loop is not duplicated.
            const int value = 10 * signed_byte(first) + signed_byte(second) - 428;
            for (const bool consume : {false, true})
            {
                run_one(input, varied_offset(), consume, value, consume ? 4U : 3U);
            }
        }
    }
    require(sessions == 169950 && calls == 169950, "two-byte case count differs");
}

void repeated_reader_cases()
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
    decimal_cases();
    whitespace_cases();
    single_byte_cases();
    two_byte_cases();
    repeated_reader_cases();
    require(sessions == 169974 && calls == 170018, "final session/call count differs");
    require(checks == 8 * calls, "state assertion count differs");
    std::cout << "sessions=" << sessions << " calls=" << calls << " checks=" << checks
              << " byte_checks=" << byte_checks << '\n';
}
