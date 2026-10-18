#include "startup_config_tokens.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Bytes = std::vector<unsigned char>;
std::size_t cases = 0;
std::size_t checks = 0;
std::size_t records = 0;
std::size_t unsafe_inputs = 0;
std::size_t fractional_inputs = 0;
std::size_t zero_count_cases = 0;
std::size_t empty_cases = 0;
std::size_t name_cases = 0;
std::size_t value_cases = 0;
std::size_t numeric_cases = 0;
std::array<std::size_t, 3> kinds{};

void expect(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

Bytes bytes(std::string_view value)
{
    return Bytes(value.begin(), value.end());
}

constexpr auto accepted = []
{
    std::array<bool, 256> value{};
    for (unsigned int byte = 33; byte != 122; ++byte)
    {
        value[byte] = true;
    }
    return value;
}();

struct Record
{
    Bytes name;
    unsigned int kind = 0;
    Bytes text;
    std::uint32_t number = 0;

    bool operator==(const Record&) const = default;
};

struct UnsafeInput
{
};

struct UnqualifiedNumber
{
};

int quote_position(std::span<const unsigned char> input)
{
    bool comment = false;
    for (std::size_t i = 0; i != input.size(); ++i)
    {
        if (comment)
        {
            if (input[i] == '\r')
            {
                comment = false;
            }
        }
        else if (i != 0 && input[i] == '/')
        {
            comment = true;
        }
        else if (input[i] == '\'')
        {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(input.size());
}

std::uint32_t number_bits(std::span<const unsigned char> storage, std::size_t start)
{
    // The converter has no size argument. Model its readable prefix using
    // integer arithmetic; restrict to exact finite integers and zero fractions.
    std::size_t i = start;
    const auto at = [&](std::size_t position)
    {
        if (position >= storage.size())
        {
            throw UnsafeInput{};
        }
        return storage[position];
    };
    const bool negative = at(i) == '-';
    i += negative ? 1 : 0;
    unsigned int digits = 0;
    unsigned int integer = 0;
    while (at(i) >= '0' && at(i) <= '9')
    {
        if (++digits > 7)
        {
            throw UnqualifiedNumber{};
        }
        integer = integer * 10 + static_cast<unsigned int>(at(i++) - '0');
        if (integer > 1000000)
        {
            throw UnqualifiedNumber{};
        }
    }
    if (at(i) == '.')
    {
        ++i;
        unsigned int fraction_digits = 0;
        while (at(i) >= '0' && at(i) <= '9')
        {
            if (at(i++) != '0' || ++fraction_digits > 1)
            {
                throw UnqualifiedNumber{};
            }
        }
    }
    const float magnitude = static_cast<float>(integer);
    return std::bit_cast<std::uint32_t>(negative ? -magnitude : magnitude);
}

std::vector<Record> model(std::span<const unsigned char> storage, int size)
{
    // Absolute positions and bounded span searches are independent of calls to
    // the reconstructed leaves. One extra byte is required by the quote seeker.
    if (storage.empty() || size >= static_cast<int>(storage.size()))
    {
        throw std::logic_error("The parser oracle needs one readable lookahead byte.");
    }
    const auto at = [&](int position)
    {
        if (position < 0 || position >= static_cast<int>(storage.size()))
        {
            throw UnsafeInput{};
        }
        return storage[static_cast<std::size_t>(position)];
    };
    const auto remaining = [&](int position)
    {
        if (position < 0 || position > static_cast<int>(storage.size()))
        {
            throw UnsafeInput{};
        }
        return storage.subspan(static_cast<std::size_t>(position),
            static_cast<std::size_t>(std::max(size - position, 0)));
    };
    const auto skip = [&](int position)
    {
        const auto data = remaining(position);
        const auto next = std::find_if(data.begin(),
            data.end(),
            [](unsigned char byte)
            {
                return accepted[byte];
            });
        return position + static_cast<int>(next - data.begin());
    };
    const auto quoted_end = [&](int position, unsigned char delimiter)
    {
        if (size - position <= 1)
        {
            return position + 1;
        }
        const auto data = remaining(position + 1);
        const auto closing = std::find(data.begin(), data.end(), delimiter);
        return position + 1 + static_cast<int>(closing - data.begin()) +
               (closing != data.end() ? 1 : 0);
    };
    const auto slice = [&](int begin, int end)
    {
        if (begin < 0 || end < begin || end > static_cast<int>(storage.size()))
        {
            throw UnsafeInput{};
        }
        return Bytes(storage.begin() + begin, storage.begin() + end);
    };
    int cursor = skip(0);
    std::vector<Record> result;
    for (;;)
    {
        const auto tail = remaining(cursor);
        if (std::count(tail.begin(), tail.end(), '\'') < 2)
        {
            return result;
        }
        cursor += quote_position(tail);
        const int name_end = quoted_end(cursor, '\'');
        if (name_end - cursor < 2)
        {
            throw UnsafeInput{};
        }
        Record record;
        record.name = slice(cursor + 1, name_end - 1);
        cursor = skip(name_end);
        const unsigned char first = at(cursor);
        int value_end = cursor;
        if (first == '"')
        {
            value_end = quoted_end(cursor, '"');
        }
        else
        {
            const auto value = remaining(cursor);
            const auto end = std::find_if_not(value.begin(),
                value.end(),
                [](unsigned char byte)
                {
                    return accepted[byte];
                });
            value_end += static_cast<int>(end - value.begin());
        }
        const auto value = slice(cursor, value_end);
        if ((first >= '0' && first <= '9') || first == '.' || first == '-')
        {
            record.kind = 2;
            record.number = number_bits(storage, static_cast<std::size_t>(cursor));
        }
        else if (std::count(value.begin(), value.end(), '"') >= 2)
        {
            record.kind = 1;
            const auto first_quote = std::find(value.begin(), value.end(), '"');
            const auto second_quote = std::find(first_quote + 1, value.end(), '"');
            // The retail setter drops byte zero by position even if its first
            // quote comes later; preserve that behavior instead of parsing syntax.
            record.text = Bytes(value.begin() + 1, second_quote);
        }
        result.push_back(record);
        cursor = skip(value_end);
        if (result.size() > storage.size())
        {
            throw std::logic_error("The bounded parser model failed to advance.");
        }
    }
}

struct Snapshot
{
    char* name;
    char* text;
    float* number;
    std::array<char, 4> name_bytes;
    std::array<char, 4> text_bytes;
    std::uint32_t number_bits;
    unsigned int kind;
};

struct Fixture
{
    std::uint64_t before = 0x2974a5ed632089bfULL;
    // Generated caller-owned storage only. This does not model the separate
    // retail file-loading constructor or imply that a default one was recovered.
    ConfigTokenBuffer owner{};
    std::uint64_t after = 0x6f3982b47a1c50deULL;
    std::vector<Snapshot> old;

    explicit Fixture(std::size_t capacity)
    {
        owner.tokens = new ConfigToken[capacity];
        owner.count = static_cast<unsigned int>(capacity);
        old.reserve(capacity);
        for (std::size_t i = 0; i != capacity; ++i)
        {
            auto& token = owner.tokens[i];
            token.Reset();
            expect(!token.name && !token.text && !token.number && token.kind == 0,
                "Actual token Reset did not initialize the fixture.");
            token.name = new char[4]{'N', static_cast<char>(i), 'n', 0};
            token.text = new char[4]{'T', static_cast<char>(i), 't', 0};
            token.number = new float(static_cast<float>(1024 + i));
            token.kind = 0xa5000000u | static_cast<unsigned int>(i);
            old.push_back({token.name,
                token.text,
                token.number,
                {'N', static_cast<char>(i), 'n', 0},
                {'T', static_cast<char>(i), 't', 0},
                std::bit_cast<std::uint32_t>(*token.number),
                token.kind});
        }
    }

    ~Fixture()
    {
        // The parser overwrites pointers without releasing old payloads. The
        // fixture owns those displaced allocations independently and frees them;
        // the actual retail destructor then releases every still-active payload.
        const auto active = [&](const void* pointer)
        {
            for (std::size_t i = 0; i != old.size(); ++i)
            {
                const auto& token = owner.tokens[i];
                if (token.name == pointer || token.text == pointer || token.number == pointer)
                {
                    return true;
                }
            }
            return false;
        };
        for (const auto& item : old)
        {
            if (!active(item.name))
            {
                delete[] item.name;
            }
            if (!active(item.text))
            {
                delete[] item.text;
            }
            if (!active(item.number))
            {
                delete item.number;
            }
        }
        // This is harness ownership, after checking the method left count alone.
        owner.count = static_cast<unsigned int>(old.size());
    }
};

void payload(const char* actual, const Bytes& expected, const char* message)
{
    expect(actual != nullptr, "Expected allocated payload is null.");
    expect(expected.empty() || std::memcmp(actual, expected.data(), expected.size()) == 0, message);
    expect(actual[expected.size()] == 0, "Allocated payload has no terminator at its extent.");
}

void unchanged_prior_payloads(const Fixture& fixture)
{
    for (const auto& previous : fixture.old)
    {
        expect(std::memcmp(previous.name, previous.name_bytes.data(), 4) == 0,
            "Assignment released or modified a prior name allocation.");
        expect(std::memcmp(previous.text, previous.text_bytes.data(), 4) == 0,
            "Assignment released or modified a prior text allocation.");
        expect(std::bit_cast<std::uint32_t>(*previous.number) == previous.number_bits,
            "Assignment released or modified a prior numeric allocation.");
    }
}

Bytes name_model(std::span<const unsigned char> input)
{
    if (input.size() < 2)
    {
        throw std::logic_error("Name assignment needs a safe copied extent.");
    }
    const auto close = std::find(input.begin() + 1, input.end(), '\'');
    // A missing closing apostrophe drops the final byte too. The first byte is
    // never validated as an opening delimiter; preserve that positional rule.
    const auto end = close == input.end() ? input.end() - 1 : close;
    return Bytes(input.begin() + 1, end);
}

Record value_model(std::span<const unsigned char> storage, int size)
{
    if (storage.empty() || size >= static_cast<int>(storage.size()))
    {
        throw std::logic_error("Value assignment needs readable byte zero and a terminator.");
    }
    Record result;
    const unsigned char first = storage[0];
    if ((first >= '0' && first <= '9') || first == '.' || first == '-')
    {
        result.kind = 2;
        result.number = number_bits(storage, 0);
        return result;
    }
    const auto input = storage.first(static_cast<std::size_t>(std::max(size, 0)));
    const auto first_quote = std::find(input.begin(), input.end(), '"');
    if (first_quote != input.end())
    {
        const auto close = std::find(first_quote + 1, input.end(), '"');
        if (close != input.end())
        {
            result.kind = 1;
            result.text = Bytes(input.begin() + 1, close);
        }
    }
    return result;
}

void direct_assignment(Bytes input, int size, bool name)
{
    if (size > static_cast<int>(input.size()) || (name && size < 2))
    {
        throw std::logic_error("Direct assignment fixture is outside its bounded domain.");
    }
    input.push_back(0);
    Record expected;
    if (name)
    {
        expected.name = name_model(std::span(input).first(static_cast<std::size_t>(size)));
    }
    else
    {
        expected = value_model(input, size);
    }
    std::unique_ptr<char[]> storage(new char[input.size()]);
    std::memcpy(storage.get(), input.data(), input.size());
    Fixture fixture(1);
    ConfigToken* const table = fixture.owner.tokens;
    const Snapshot& previous = fixture.old[0];
    ConfigToken& actual = *table;
    if (name)
    {
        actual.AssignName(storage.get(), size);
        expect(actual.name != previous.name, "Name assignment retained its prior pointer.");
        payload(actual.name, expected.name, "Name assignment differs from the positional slice.");
        expect(actual.kind == previous.kind && actual.text == previous.text &&
                   actual.number == previous.number,
            "Name assignment changed an unrelated token field.");
        ++name_cases;
    }
    else
    {
        actual.AssignValue(storage.get(), size);
        expect(actual.name == previous.name, "Value assignment changed the token name.");
        expect(actual.kind == expected.kind, "Value assignment selected an unexpected kind.");
        if (expected.kind == 1)
        {
            expect(actual.text != previous.text, "Text assignment retained its prior pointer.");
            payload(
                actual.text, expected.text, "Text assignment differs from the positional slice.");
            expect(actual.number == previous.number, "Text assignment changed the number pointer.");
        }
        else if (expected.kind == 2)
        {
            expect(actual.number != nullptr && actual.number != previous.number,
                "Numeric assignment did not publish a new allocation.");
            expect(std::bit_cast<std::uint32_t>(*actual.number) == expected.number,
                "Numeric assignment differs from the exact bounded integer oracle.");
            expect(actual.text == previous.text, "Numeric assignment changed the text pointer.");
            ++numeric_cases;
        }
        else
        {
            expect(actual.text == previous.text && actual.number == previous.number,
                "Unclassified assignment changed a payload pointer.");
        }
        ++value_cases;
    }
    unchanged_prior_payloads(fixture);
    expect(fixture.owner.tokens == table && fixture.owner.count == 1,
        "Assignment changed the caller-owned table or count.");
    expect(fixture.before == 0x2974a5ed632089bfULL && fixture.after == 0x6f3982b47a1c50deULL,
        "Assignment changed bytes adjacent to its owner.");
    expect(std::memcmp(storage.get(), input.data(), input.size()) == 0,
        "Assignment changed its source bytes or terminator.");
}

void direct_setters()
{
    expect(name_model(bytes("'name'")) == bytes("name") &&
               name_model(bytes("xname!")) == bytes("name") && name_model(bytes("''")).empty(),
        "The independent name oracle disagrees with literal positional slices.");
    const Bytes odd_text{'x', '"', 'b', 'c', '"', 0};
    const Record odd = value_model(odd_text, 5);
    expect(odd.kind == 1 && odd.text == bytes("\"bc"),
        "The independent value oracle disagrees with the literal offset quote case.");
    expect(value_model(Bytes{'-', '0', 0}, -1).number == 0x80000000u,
        "The independent numeric oracle lost signed zero or used the declared extent.");

    for (unsigned int first = 0; first != 256; ++first)
    {
        for (unsigned int second = 0; second != 256; ++second)
        {
            direct_assignment(
                {'\'', static_cast<unsigned char>(first), static_cast<unsigned char>(second), '\''},
                4,
                true);
            direct_assignment(
                {'"', static_cast<unsigned char>(first), static_cast<unsigned char>(second), '"'},
                4,
                false);
        }
        const Bytes input{static_cast<unsigned char>(first), 'x', '"', '"'};
        for (const int size : {-2, -1, 0, 1, 2, 3, 4})
        {
            direct_assignment(input, size, false);
        }
    }
    for (const std::size_t size : {2u, 3u, 4u, 7u, 16u, 31u, 64u, 255u, 1024u})
    {
        for (std::size_t close = 1; close <= size; ++close)
        {
            Bytes input(size, 0xff);
            input[0] = 'x';
            if (close < size)
            {
                input[close] = '\'';
            }
            direct_assignment(input, static_cast<int>(size), true);
            input[0] = '"';
            if (close < size)
            {
                input[close] = '"';
            }
            direct_assignment(input, static_cast<int>(size), false);
        }
    }
    for (const auto input : {"",
             "\"\"",
             "\"",
             "x\"bc\"tail",
             "abc",
             "+3",
             " 3",
             "-0",
             ".",
             "-.",
             "--1",
             "1e2",
             "0x10",
             "1000000",
             "-1000000.0"})
    {
        const Bytes data = bytes(input);
        for (int size = -2; size <= static_cast<int>(data.size()); ++size)
        {
            direct_assignment(data, size, false);
        }
    }
    for (int value = -1000000; value <= 1000000; value += 997)
    {
        const auto input = bytes(std::to_string(value) + ".0tail");
        direct_assignment(input, static_cast<int>(input.size()), false);
        direct_assignment(input, 0, false);
    }
}

void check(Bytes input, int size, const std::vector<Record>* literal = nullptr)
{
    if (size > static_cast<int>(input.size()))
    {
        throw std::logic_error("The declared fixture extent exceeds its payload.");
    }
    input.resize(static_cast<std::size_t>(std::max(size, 0)) + 1, 0);
    input.back() = 0;
    std::vector<Record> expected;
    try
    {
        expected = model(input, size);
    }
    catch (const UnsafeInput&)
    {
        expect(literal == nullptr, "A literal parser fixture has an unsafe domain.");
        ++unsafe_inputs;
        return;
    }
    catch (const UnqualifiedNumber&)
    {
        expect(literal == nullptr, "A literal fixture exceeds the bounded numeric proof.");
        ++fractional_inputs;
        return;
    }
    if (literal)
    {
        expect(expected == *literal, "The independent parser oracle disagrees with a literal.");
    }
    std::unique_ptr<char[]> storage(new char[input.size()]);
    std::memcpy(storage.get(), input.data(), input.size());
    Fixture fixture(expected.size() + 2);
    ConfigToken* const table = fixture.owner.tokens;
    // Prove that parser iteration is not bounded by, and does not update, count.
    const unsigned int count = cases % 2 ? fixture.owner.count : 0;
    fixture.owner.count = count;
    fixture.owner.ParseTokens(storage.get(), size);
    expect(fixture.owner.tokens == table, "Parser replaced the caller-owned token table.");
    expect(fixture.owner.count == count, "Parser changed the caller-owned token count.");
    expect(fixture.before == 0x2974a5ed632089bfULL && fixture.after == 0x6f3982b47a1c50deULL,
        "Parser changed adjacent nonowned bytes.");
    expect(std::memcmp(storage.get(), input.data(), input.size()) == 0,
        "Parser or setter changed source bytes or the readable guard.");
    for (std::size_t i = 0; i != fixture.old.size(); ++i)
    {
        const auto& previous = fixture.old[i];
        const auto& actual = table[i];
        expect(std::memcmp(previous.name, previous.name_bytes.data(), 4) == 0,
            "Parser released or modified a prior name allocation.");
        expect(std::memcmp(previous.text, previous.text_bytes.data(), 4) == 0,
            "Parser released or modified a prior text allocation.");
        expect(std::bit_cast<std::uint32_t>(*previous.number) == previous.number_bits,
            "Parser released or modified a prior numeric allocation.");
        if (i >= expected.size())
        {
            expect(actual.name == previous.name && actual.kind == previous.kind &&
                       actual.text == previous.text && actual.number == previous.number,
                "Parser changed a trailing initialized guard token.");
            continue;
        }
        const auto& wanted = expected[i];
        expect(actual.name != previous.name, "Parser did not assign the expected token slot.");
        payload(actual.name, wanted.name, "Parser name bytes differ from the input record.");
        expect(actual.kind == wanted.kind, "Parser value kind differs from the input record.");
        if (wanted.kind == 1)
        {
            expect(actual.text != previous.text, "Text assignment retained the old text pointer.");
            payload(actual.text, wanted.text, "Parser text bytes differ from the input record.");
            expect(actual.number == previous.number,
                "Text assignment changed the old number pointer.");
        }
        else if (wanted.kind == 2)
        {
            expect(
                actual.number != previous.number, "Numeric assignment retained the old pointer.");
            expect(std::bit_cast<std::uint32_t>(*actual.number) == wanted.number,
                "Parser numeric bits differ from the exact bounded integer oracle.");
            expect(
                actual.text == previous.text, "Numeric assignment changed the old text pointer.");
        }
        else
        {
            expect(actual.text == previous.text && actual.number == previous.number,
                "Unclassified assignment changed payload pointers.");
        }
        ++kinds[wanted.kind];
    }
    records += expected.size();
    empty_cases += expected.empty() ? 1 : 0;
    zero_count_cases += count == 0 ? 1 : 0;
    ++cases;
}

void check(const Bytes& input)
{
    check(input, static_cast<int>(input.size()));
}

void known(std::string_view input, std::vector<Record> expected)
{
    check(bytes(input), static_cast<int>(input.size()), &expected);
}

Record number(std::string_view name, float value)
{
    return {bytes(name), 2, {}, std::bit_cast<std::uint32_t>(value)};
}

void literal_cases()
{
    known("'name' \"text\"", {{bytes("name"), 1, bytes("text"), 0}});
    known(
        "'a' 12\r'b' -7.0\r'c' abc\r", {number("a", 12), number("b", -7), {bytes("c"), 0, {}, 0}});
    known("'' \"\"", {{{}, 1, {}, 0}});
    known("'a'", {{bytes("a"), 0, {}, 0}});
    known("'a' \"", {{bytes("a"), 0, {}, 0}});
    known("'a' x\"bc\"tail\r", {{bytes("a"), 1, bytes("\"bc"), 0}});
    known("'a' .\r'b' -.\r'c' --1\r'd' 1e2\r'e' 0x10",
        {number("a", 0.0f),
            number("b", -0.0f),
            number("c", -0.0f),
            number("d", 1.0f),
            number("e", 0.0f)});
    known("x/'ignored'\nrest\r'kept' 9", {number("kept", 9)});
    known("/'first' 3", {number("first", 3)});
    known("no records here", {});
    known("", {});
    for (const int size : {0, -1, -2, -31, std::numeric_limits<int>::min()})
    {
        const std::vector<Record> expected;
        check({'x'}, size, &expected);
    }
    // These composed domains are deliberately never passed to unchecked source.
    check(bytes("x/'hidden'"));
    check(bytes("x/'hidden'\n"));
    check(bytes("'n' 0.5"));
    check(bytes("'n' 1000001"));
}

void byte_payloads()
{
    for (unsigned int byte = 0; byte != 256; ++byte)
    {
        if (byte != '\'')
        {
            const Bytes input{'\'', static_cast<unsigned char>(byte), '\'', ' ', '1'};
            const std::vector<Record> expected{
                {{static_cast<unsigned char>(byte)}, 2, {}, std::bit_cast<std::uint32_t>(1.0f)}};
            check(input, static_cast<int>(input.size()), &expected);
        }
        if (byte != '"')
        {
            const Bytes input{'\'', 'n', '\'', ' ', '"', static_cast<unsigned char>(byte), '"'};
            const std::vector<Record> expected{
                {bytes("n"), 1, {static_cast<unsigned char>(byte)}, 0}};
            check(input, static_cast<int>(input.size()), &expected);
        }
    }
}

void composed_records()
{
    const std::array<Bytes, 6> separators{{{' '}, {'\r', '\n'}, {0}, {0x80}, {'z'}, {0xff, '\t'}}};
    const std::array<std::string_view, 10> numeric{
        {"0", "-0", "1", "-7", "999999", "1000000", "42.0", "-.0", "1e2", "0x10"}};
    const std::array<float, 10> numeric_values{
        {0.0f, -0.0f, 1.0f, -7.0f, 999999.0f, 1000000.0f, 42.0f, -0.0f, 1.0f, 0.0f}};
    for (std::size_t run = 0; run != 1024; ++run)
    {
        Bytes input;
        std::vector<Record> expected;
        for (std::size_t i = 0; i != run % 9; ++i)
        {
            const auto name = bytes("item" + std::to_string(i));
            input.push_back('\'');
            input.insert(input.end(), name.begin(), name.end());
            input.push_back('\'');
            const auto& separation = separators[(run + i) % separators.size()];
            input.insert(input.end(), separation.begin(), separation.end());
            Record record{name, 0, {}, 0};
            if ((run + i) % 3 == 0)
            {
                const std::size_t selected = (run * 3 + i) % numeric.size();
                input.insert(input.end(), numeric[selected].begin(), numeric[selected].end());
                record.kind = 2;
                record.number = std::bit_cast<std::uint32_t>(numeric_values[selected]);
            }
            else if ((run + i) % 3 == 1)
            {
                record.kind = 1;
                record.text = {'v', static_cast<unsigned char>(run), 0, '\'', '\\'};
                if (record.text[1] == '"')
                {
                    record.text[1] = 0xff;
                }
                input.push_back('"');
                input.insert(input.end(), record.text.begin(), record.text.end());
                input.push_back('"');
            }
            else
            {
                const auto unknown = bytes("unclassified");
                input.insert(input.end(), unknown.begin(), unknown.end());
            }
            input.insert(input.end(), separation.begin(), separation.end());
            expected.push_back(record);
        }
        check(input, static_cast<int>(input.size()), &expected);
    }
}

void mixed_inputs()
{
    for (const Bytes& input : {bytes("'a' \"x\"\r'b' -3.0\r"),
             bytes("x/'hidden'\r'a' 1"),
             Bytes{'\'', 0xff, '\'', 0, '"', '\'', 0, '\\', '"', 0x80, '\'', '\''}})
    {
        for (std::size_t length = 0; length <= input.size(); ++length)
        {
            check(input, static_cast<int>(length));
        }
    }
    constexpr std::array<unsigned char, 19> alphabet{
        {0, 32, 33, 34, 39, 45, 46, 47, 48, 49, 57, 92, 97, 121, 122, 127, 128, 255, 13}};
    std::uint32_t state = 0x76c412aeu;
    for (std::size_t run = 0; run != 4096; ++run)
    {
        Bytes input(run % 97);
        for (auto& byte : input)
        {
            state = state * 1664525u + 1013904223u;
            byte = alphabet[(state >> 16) % alphabet.size()];
        }
        check(input);
    }
}
} // namespace

int main()
{
    try
    {
        direct_setters();
        literal_cases();
        byte_payloads();
        composed_records();
        mixed_inputs();
        expect(kinds[0] && kinds[1] && kinds[2] && unsafe_inputs && fractional_inputs,
            "Generated cases failed to cover a required class or excluded domain.");
        std::cout << "{\"cases\":" << cases << ",\"checks\":" << checks
                  << ",\"records\":" << records << ",\"kind_counts\":[" << kinds[0] << ','
                  << kinds[1] << ',' << kinds[2] << "],\"zero_count_cases\":" << zero_count_cases
                  << ",\"empty_cases\":" << empty_cases
                  << ",\"unsafe_inputs_excluded\":" << unsafe_inputs
                  << ",\"unqualified_numeric_inputs_excluded\":" << fractional_inputs
                  << ",\"direct_name_cases\":" << name_cases
                  << ",\"direct_value_cases\":" << value_cases
                  << ",\"direct_numeric_cases\":" << numeric_cases << "}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Parser fixture failure: " << error.what() << '\n';
        return 1;
    }
}
