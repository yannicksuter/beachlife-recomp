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
constexpr std::array<unsigned int, 2> output_offsets{0, 3};
constexpr std::array<unsigned char, 5> stops{')', '\r', ',', '(', '{'};
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
std::size_t text_calls = 0;
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
    text,
    skip,
    integer
};

struct Step
{
    Operation operation;
    bool consume;
    int value;
    unsigned int cursor;
    std::string_view written;
};

struct Literal
{
    std::string_view input;
    std::string_view written;
    unsigned int cursor;
};

struct Sequence
{
    std::string_view input;
    std::span<const Step> steps;
};

constexpr std::array<Literal, 46> literals{{
    // brace-control
    {{"\x20\x09\x4d\x65\x6e\x75\x7b\x73\x75\x66\x66\x69\x78\x29\x51", 15},
        {"\x4d\x65\x6e\x75\x00", 5},
        6},
    // left-parenthesis-is-unconsumed
    {{"\x4d\x65\x6e\x75\x28\x31\x30\x2c\x51", 9}, {"\x4d\x65\x6e\x75\x00", 5}, 4},
    // empty-close-consumed
    {{"\x29\x51", 2}, {"\x00", 1}, 1},
    // empty-comma-consumed
    {{"\x2c\x51", 2}, {"\x00", 1}, 1},
    // empty-left-brace
    {{"\x7b\x51\x29\x5a", 4}, {"\x00", 1}, 0},
    // empty-left-parenthesis
    {{"\x28\x51\x29\x5a", 4}, {"\x00", 1}, 0},
    // text-comma
    {{"\x41\x2c\x51", 3}, {"\x41\x00", 2}, 2},
    // text-close
    {{"\x41\x29\x51", 3}, {"\x41\x00", 2}, 2},
    // text-left-parenthesis
    {{"\x41\x28\x51", 3}, {"\x41\x00", 2}, 1},
    // text-left-brace
    {{"\x41\x7b\x51\x29\x5a", 5}, {"\x41\x00", 2}, 1},
    // cr-stop-without-delimiter
    {{"\x41\x0d\x51\x2c\x51", 5}, {"\x41\x00", 2}, 2},
    // interior-and-trailing-spaces-copy
    {{"\x41\x20\x42\x20\x2c\x51", 6}, {"\x41\x20\x42\x20\x00", 5}, 5},
    // interior-tab-copies
    {{"\x41\x09\x42\x2c\x51", 5}, {"\x41\x09\x42\x00", 4}, 4},
    // interior-lf-copies
    {{"\x41\x0a\x42\x2c\x51", 5}, {"\x41\x0a\x42\x00", 4}, 4},
    // embedded-nul-copies
    {{"\x41\x00\x42\x29\x51", 5}, {"\x41\x00\x42\x00", 4}, 4},
    // interior-cr-stops
    {{"\x41\x0d\x42\x2c\x51", 5}, {"\x41\x00", 2}, 2},
    // right-brace-copies
    {{"\x41\x7d\x42\x2c\x51", 5}, {"\x41\x7d\x42\x00", 4}, 4},
    // quote-does-not-protect-comma
    {{"\x22\x41\x2c\x42\x22\x2c\x51", 7}, {"\x22\x41\x00", 3}, 3},
    // single-quotes-copy
    {{"\x27\x41\x27\x2c\x51", 5}, {"\x27\x41\x27\x00", 4}, 4},
    // backslash-copies
    {{"\x41\x5c\x42\x2c\x51", 5}, {"\x41\x5c\x42\x00", 4}, 4},
    // leading-vt-is-text
    {{"\x0b\x41\x2c\x51", 4}, {"\x0b\x41\x00", 3}, 3},
    // leading-ff-is-text
    {{"\x0c\x41\x2c\x51", 4}, {"\x0c\x41\x00", 3}, 3},
    // high-bytes-copy
    {{"\x80\xff\x2c\x51", 4}, {"\x80\xff\x00", 3}, 3},
    // nul-is-text
    {{"\x00\x2c\x51", 3}, {"\x00\x00", 2}, 2},
    // four-leading-whitespace
    {{"\x20\x0d\x0a\x09\x41\x2c\x51", 7}, {"\x41\x00", 2}, 6},
    // no-skip-after-comma
    {{"\x41\x2c\x09\x20\x51", 5}, {"\x41\x00", 2}, 2},
    // no-skip-after-close
    {{"\x41\x29\x20\x0d\x51", 5}, {"\x41\x00", 2}, 2},
    // cr-skip-before-comma
    {{"\x41\x0d\x20\x0a\x09\x2c\x51", 7}, {"\x41\x00", 2}, 6},
    // cr-skip-before-close-keeps-following-space
    {{"\x41\x0d\x20\x0a\x09\x29\x20\x51", 8}, {"\x41\x00", 2}, 6},
    // cr-skip-stops-at-left-brace
    {{"\x41\x0d\x20\x0a\x09\x7b\x51\x29", 8}, {"\x41\x00", 2}, 5},
    // cr-skip-stops-at-left-parenthesis
    {{"\x41\x0d\x20\x0a\x09\x28\x51\x29", 8}, {"\x41\x00", 2}, 5},
    // cr-skip-stops-at-nul
    {{"\x41\x0d\x20\x0a\x09\x00\x2c\x51", 8}, {"\x41\x00", 2}, 5},
    // cr-skip-stops-at-high-byte
    {{"\x41\x0d\x20\x0a\x09\x80\x2c\x51", 8}, {"\x41\x00", 2}, 5},
    // cr-skip-does-not-resume-copy
    {{"\x41\x0d\x20\x0a\x09\x7d\x51\x2c", 8}, {"\x41\x00", 2}, 5},
    // only-one-comma-consumed
    {{"\x41\x2c\x2c\x51", 4}, {"\x41\x00", 2}, 2},
    // only-one-close-consumed
    {{"\x41\x29\x29\x51", 4}, {"\x41\x00", 2}, 2},
    // leading-space-tab-empty-comma
    {{"\x20\x09\x2c\x51", 4}, {"\x00", 1}, 3},
    // leading-space-tab-empty-close
    {{"\x20\x09\x29\x51", 4}, {"\x00", 1}, 3},
    // leading-space-tab-empty-open
    {{"\x20\x09\x28\x51", 4}, {"\x00", 1}, 2},
    // leading-space-tab-empty-brace
    {{"\x20\x09\x7b\x51\x29", 5}, {"\x00", 1}, 2},
    // leading-cr-is-whitespace
    {{"\x20\x09\x0d\x51\x2c\x51", 6}, {"\x51\x00", 2}, 5},
    // spaces-before-close-retained
    {{"\x41\x20\x20\x29\x51", 5}, {"\x41\x20\x20\x00", 4}, 4},
    // ff-byte-is-not-eof
    {{"\xff\x2c\x41", 3}, {"\xff\x00", 2}, 2},
    // multiple-nuls-copy
    {{"\x00\x41\x00\x2c\x51", 5}, {"\x00\x41\x00\x00", 4}, 4},
    // quoted-whitespace-and-right-brace-copy
    {{"\x22\x09\x0a\x20\x7d\x22\x2c\x51", 8}, {"\x22\x09\x0a\x20\x7d\x22\x00", 7}, 7},
    // cr-tab-space-before-comma
    {{"\x41\x0d\x20\x09\x2c\x51", 6}, {"\x41\x00", 2}, 5},
}};

constexpr std::array<Step, 5> sequence_0{{
    {Operation::text, false, 0, 2, {"\x41\x00", 2}},
    {Operation::skip, false, 0, 4, {}},
    {Operation::integer, true, 12, 8, {}},
    {Operation::text, false, 0, 10, {"\x42\x00", 2}},
    {Operation::skip, false, 0, 10, {}},
}};

constexpr std::array<Step, 4> sequence_1{{
    {Operation::integer, true, 12, 4, {}},
    {Operation::text, false, 0, 9, {"\x4e\x61\x6d\x65\x00", 5}},
    {Operation::integer, true, 7, 11, {}},
    {Operation::skip, false, 0, 11, {}},
}};

constexpr std::array<Step, 3> sequence_2{{
    {Operation::text, false, 0, 4, {"\x41\x00\x42\x00", 4}},
    {Operation::skip, false, 0, 6, {}},
    {Operation::text, false, 0, 8, {"\x43\x00", 2}},
}};

constexpr std::array<Step, 3> sequence_3{{
    {Operation::text, false, 0, 5, {"\x4c\x6f\x6e\x67\x00", 5}},
    {Operation::text, false, 0, 6, {"\x00", 1}},
    {Operation::text, false, 0, 8, {"\x78\x00", 2}},
}};

constexpr std::array<Step, 2> sequence_4{{
    {Operation::text, false, 0, 4, {"\x41\x00", 2}},
    {Operation::text, false, 0, 6, {"\x42\x00", 2}},
}};

constexpr std::array<Step, 3> sequence_5{{
    {Operation::text, false, 0, 0, {"\x00", 1}},
    {Operation::text, false, 0, 0, {"\x00", 1}},
    {Operation::text, false, 0, 0, {"\x00", 1}},
}};

constexpr std::array<Step, 2> sequence_6{{
    {Operation::text, false, 0, 0, {"\x00", 1}},
    {Operation::text, false, 0, 0, {"\x00", 1}},
}};

constexpr std::array<Step, 2> sequence_7{{
    {Operation::text, false, 0, 1, {"\x00", 1}},
    {Operation::text, false, 0, 2, {"\x00", 1}},
}};

constexpr std::array<Step, 3> sequence_8{{
    {Operation::integer, false, 1, 1, {}},
    {Operation::text, false, 0, 2, {"\x00", 1}},
    {Operation::text, false, 0, 5, {"\x32\x33\x00", 3}},
}};

constexpr std::array<Step, 3> sequence_9{{
    {Operation::text, false, 0, 3, {"\x41\x20\x00", 3}},
    {Operation::integer, true, 0, 6, {}},
    {Operation::skip, false, 0, 6, {}},
}};

constexpr std::array<Step, 3> sequence_10{{
    {Operation::text, false, 0, 2, {"\x41\x00", 2}},
    {Operation::skip, false, 0, 3, {}},
    {Operation::text, false, 0, 5, {"\x80\x00", 2}},
}};

constexpr std::array<Sequence, 11> sequences{{
    // text-skip-integer-text
    {{"\x41\x2c\x20\x09\x31\x32\x2c\x20\x42\x29\x51", 11}, sequence_0},
    // integer-text-integer
    {{"\x31\x32\x2c\x20\x4e\x61\x6d\x65\x2c\x37\x29\x51", 12}, sequence_1},
    // nul-text-then-crlf
    {{"\x41\x00\x42\x2c\x0d\x0a\x43\x29\x51", 9}, sequence_2},
    // shorter-output-preserves-old-tail
    {{"\x4c\x6f\x6e\x67\x2c\x2c\x78\x29\x51", 9}, sequence_3},
    // text-cr-text-no-delimiter
    {{"\x41\x0d\x20\x09\x42\x29\x51", 7}, sequence_4},
    // repeated-left-brace-does-not-advance
    {{"\x7b\x73\x75\x66\x66\x69\x78\x29\x51", 9}, sequence_5},
    // repeated-left-parenthesis-does-not-advance
    {{"\x28\x73\x75\x66\x66\x69\x78\x29\x51", 9}, sequence_6},
    // consecutive-empty-delimiters
    {{"\x29\x2c\x51", 3}, sequence_7},
    // integer-leaves-comma-for-text
    {{"\x31\x2c\x32\x33\x29\x51", 6}, sequence_8},
    // text-space-preserved-before-integer
    {{"\x41\x20\x2c\x09\x30\x29\x51", 7}, sequence_9},
    // text-skip-high-byte-text
    {{"\x41\x2c\x09\x80\x29\x51", 6}, sequence_10},
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
#if defined(BEACHLIFE_DECOMP_FONT_PUBLICATION)
    for (auto& data : object.value.fonts_04)
    {
        data = nullptr;
    }
#else
    fill(object.value.unknown_04, seed + 1);
#endif
#if defined(BEACHLIFE_DECOMP_AUXILIARY_TEXT)
    for (auto& font : object.value.fonts_2c)
    {
        font = nullptr;
    }
    fill(object.value.unknown_54, seed + 1);
#endif
#if defined(BEACHLIFE_DECOMP_MAIN_ITEM_PUBLICATION)
    object.value.item_90 = nullptr;
    fill(object.value.unknown_94, seed + 2);
#else
    fill(object.value.unknown_90, seed + 2);
#endif
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

void run_session(std::span<const unsigned char> input,
    unsigned int initial_cursor,
    unsigned int output_offset,
    std::span<const Step> steps)
{
    require(!input.empty() && !steps.empty(), "fixture requires readable input and a call");
    require(initial_cursor <= 31 && input.size() <= 1026 && output_offset <= 3,
        "fixture exceeded its bounded extent");
    const std::size_t input_size = guard_size + initial_cursor + input.size() + guard_size;
    Bytes original(input_size);
    fill(original, sessions);
    std::copy(input.begin(), input.end(), original.begin() + guard_size + initial_cursor);
    std::unique_ptr<char[]> input_storage(new char[input_size]);
    std::memcpy(input_storage.get(), original.data(), input_size);
    const char* const buffer = input_storage.get() + guard_size;

    const std::size_t capacity = input.size() + 1;
    const std::size_t text_size = guard_size + output_offset + capacity + guard_size;
    Bytes expected_text(text_size);
    fill(expected_text, sessions + 59);
    std::unique_ptr<char[]> text_storage(new char[text_size]);
    std::memcpy(text_storage.get(), expected_text.data(), text_size);
    char* const text_output = text_storage.get() + guard_size + output_offset;

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
        if (step.operation == Operation::text)
        {
            require(!step.written.empty() && step.written.back() == '\0',
                "fixture text expectation requires its explicit appended NUL");
            require(step.written.size() <= capacity, "fixture text expectation exceeds output");
            // A shorter result overwrites only its bytes and appended NUL;
            // the previous result's remaining tail stays in this snapshot.
            std::copy(step.written.begin(),
                step.written.end(),
                expected_text.begin() + guard_size + output_offset);
        }

        if (step.operation == Operation::text)
        {
            reader.value.ReadText(text_output);
        }
        else if (step.operation == Operation::skip)
        {
            reader.value.SkipWhitespace();
        }
        else
        {
            reader.value.ReadInteger(output.value, step.consume);
        }

        // The brace control safely reaches the first literal's later ')',
        // and must fail this fixed cursor expectation before any other case.
        const char* cursor_message = step.operation == Operation::text ? "text cursor differs"
                                     : step.operation == Operation::skip
                                         ? "whitespace cursor differs"
                                         : "integer cursor differs";
        expect(reader.value.cursor == expected_cursor, cursor_message);
        expect(output.value == expected_value, "integer output or untouched word differs");
        expect(reader.value.buffer == buffer, "buffer identity changed");
        expect(reader.value.manager == manager, "manager identity changed");
        expect_bytes(&reader, expected_reader, "reader representation or guards changed");
        expect_bytes(&output, expected_output, "integer representation or guards changed");
        expect_bytes(
            text_storage.get(), expected_text, "text output, untouched tail or guards changed");
        expect_bytes(input_storage.get(), original, "input buffer or guards changed");
        expect_bytes(&managers, expected_managers, "borrowed manager prefixes or guards changed");
        if (step.operation == Operation::text)
        {
            ++text_calls;
        }
        else if (step.operation == Operation::skip)
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

void run_written_case(
    std::span<const unsigned char> input, std::string_view written, unsigned int cursor)
{
    const std::array<Step, 1> steps{{{Operation::text, false, 0, cursor, written}}};
    for (const auto initial : initial_offsets)
    {
        for (const auto output : output_offsets)
        {
            run_session(input, initial, output, steps);
        }
    }
}

void run_text_case(std::span<const unsigned char> input,
    std::span<const unsigned char> copied,
    unsigned int cursor)
{
    Bytes written(copied.begin(), copied.end());
    written.push_back(0);
    run_written_case(
        input, {reinterpret_cast<const char*>(written.data()), written.size()}, cursor);
}

void literal_cases()
{
    for (const auto& row : literals)
    {
        const auto input = bytes(row.input);
        run_written_case(input, row.written, row.cursor);
    }
    require(sessions == 368 && calls == 368, "literal case count differs");
}

void all_leading_bytes()
{
    for (unsigned int byte = 0; byte != 256; ++byte)
    {
        const std::array<unsigned char, 4> input{static_cast<unsigned char>(byte), 'A', ',', 'Q'};
        Bytes copied;
        unsigned int cursor = 3;
        if (byte == 32 || byte == 13 || byte == 10 || byte == 9)
        {
            copied = {'A'};
        }
        else if (byte == 41 || byte == 44)
        {
            cursor = 1;
        }
        else if (byte == 40 || byte == 123)
        {
            cursor = 0;
        }
        else
        {
            copied = {static_cast<unsigned char>(byte), 'A'};
        }
        run_text_case(input, copied, cursor);
    }
    require(sessions == 2416 && calls == 2416, "leading byte count differs");
}

void all_interior_bytes()
{
    for (unsigned int byte = 0; byte != 256; ++byte)
    {
        for (const auto stop : stops)
        {
            const std::array<unsigned char, 5> input{
                'A', static_cast<unsigned char>(byte), 'Z', stop, 'Q'};
            Bytes copied{'A'};
            unsigned int cursor;
            if (byte == 41 || byte == 13 || byte == 44)
            {
                cursor = 2;
            }
            else if (byte == 40 || byte == 123)
            {
                cursor = 1;
            }
            else
            {
                copied.push_back(static_cast<unsigned char>(byte));
                copied.push_back('Z');
                cursor = stop == ')' || stop == '\r' || stop == ',' ? 4U : 3U;
            }
            run_text_case(input, copied, cursor);
        }
    }
    require(sessions == 12656 && calls == 12656, "interior byte count differs");
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

void leading_whitespace_words()
{
    constexpr std::array<unsigned char, 9> first_bytes{')', '\r', ',', '(', '{', 0, 128, 255, 'A'};
    for (const auto& prefix : whitespace_strings())
    {
        for (const auto first : first_bytes)
        {
            Bytes input = prefix;
            append(input, std::array<unsigned char, 4>{first, 'Z', ',', 'Q'});
            Bytes copied;
            unsigned int cursor;
            if (first == '\r')
            {
                copied = {'Z'};
                cursor = word(prefix.size()) + 3;
            }
            else if (first == ')' || first == ',')
            {
                cursor = word(prefix.size()) + 1;
            }
            else if (first == '(' || first == '{')
            {
                cursor = word(prefix.size());
            }
            else
            {
                copied = {first, 'Z'};
                cursor = word(prefix.size()) + 3;
            }
            run_text_case(input, copied, cursor);
        }
    }
    require(sessions == 18776 && calls == 18776, "leading whitespace count differs");
}

void cr_whitespace_tails()
{
    constexpr std::array<unsigned char, 8> next_bytes{'Q', ',', ')', '(', '{', 0, 128, '}'};
    for (const auto& suffix : whitespace_strings())
    {
        for (const auto next : next_bytes)
        {
            Bytes input{'A', '\r'};
            append(input, suffix);
            append(input, std::array<unsigned char, 4>{next, ' ', '\t', 'Q'});
            const unsigned int cursor =
                2 + word(suffix.size()) + (next == ',' || next == ')' ? 1U : 0U);
            run_text_case(input, std::array<unsigned char, 1>{'A'}, cursor);
        }
    }
    require(sessions == 24216 && calls == 24216, "CR whitespace count differs");
}

void constructed_copy_lengths()
{
    constexpr std::array<unsigned int, 14> lengths{
        0, 1, 4, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256, 1023};
    constexpr std::array<unsigned char, 4> uniform{'A', 0, 128, 255};
    constexpr std::array<unsigned char, 8> cycle{'A', ' ', '\t', '\n', 0, 128, '}', '"'};
    for (const auto length : lengths)
    {
        for (unsigned int pattern = 0; pattern != 5; ++pattern)
        {
            Bytes copied(length);
            for (unsigned int i = 0; i != length; ++i)
            {
                copied[i] = pattern == 4 ? cycle[i % cycle.size()] : uniform[pattern];
            }
            for (const auto stop : stops)
            {
                Bytes input = copied;
                // For an empty payload CR is leading whitespace, so this
                // explicit comma also bounds that case's empty copy.
                append(input, std::array<unsigned char, 3>{stop, ',', 'Q'});
                const unsigned int cursor =
                    length + (stop == '\r' ? 2U : (stop == ')' || stop == ',' ? 1U : 0U));
                run_text_case(input, copied, cursor);
            }
        }
    }
    require(sessions == 27016 && calls == 27016, "constructed length count differs");
}

void sequential_consumers()
{
    for (const auto& sequence : sequences)
    {
        const auto input = bytes(sequence.input);
        for (const auto initial : initial_offsets)
        {
            for (const auto output : output_offsets)
            {
                run_session(input, initial, output, sequence.steps);
            }
        }
    }
}
} // namespace

int main()
{
    literal_cases();
    all_leading_bytes();
    all_interior_bytes();
    leading_whitespace_words();
    cr_whitespace_tails();
    constructed_copy_lengths();
    sequential_consumers();
    require(sessions == 27104 && calls == 27280, "final session/call count differs");
    require(text_calls == 27192 && whitespace_calls == 48 && integer_calls == 40,
        "reader call count differs");
    require(text_calls + whitespace_calls + integer_calls == calls && checks == 9 * calls,
        "state assertion count differs");
    std::cout << "sessions=" << sessions << " calls=" << calls << " text_calls=" << text_calls
              << " whitespace_calls=" << whitespace_calls << " integer_calls=" << integer_calls
              << " checks=" << checks << " byte_checks=" << byte_checks << '\n';
}
