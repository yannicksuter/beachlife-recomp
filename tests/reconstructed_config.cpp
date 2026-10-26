#include "startup_config_scan.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
using Bytes = std::vector<unsigned char>;
std::size_t cases = 0;
std::size_t checks = 0;

// This table defines the observed byte set independently of the helpers' signed
// comparisons. In particular, 'z' (122), NUL and every high-bit byte are outside.
constexpr auto accepted = []
{
    std::array<bool, 256> result{};
    for (unsigned int byte = 33; byte != 122; ++byte)
    {
        result[byte] = true;
    }
    return result;
}();

struct Expected
{
    int separator_extent;
    int has_name_delimiters;
    int name_extent;
    int value_extent;
};

void expect(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

Expected oracle(std::span<const unsigned char> bytes, int size)
{
    const auto counted = bytes.first(static_cast<std::size_t>(std::max(size, 0)));
    const auto first_accepted = std::find_if(counted.begin(),
        counted.end(),
        [](unsigned char byte)
        {
            return accepted[byte];
        });
    const auto first_rejected = std::find_if_not(counted.begin(),
        counted.end(),
        [](unsigned char byte)
        {
            return accepted[byte];
        });
    const auto quoted_extent = [&](unsigned char delimiter)
    {
        if (size <= 1)
        {
            return 1;
        }
        const auto closing = std::find(counted.begin() + 1, counted.end(), delimiter);
        return static_cast<int>(closing - counted.begin()) + (closing != counted.end() ? 1 : 0);
    };
    return {static_cast<int>(first_accepted - counted.begin()),
        std::count(counted.begin(), counted.end(), 0x27) >= 2 ? 1 : 0,
        quoted_extent(0x27),
        bytes.front() == 0x22 ? quoted_extent(0x22)
                              : static_cast<int>(first_rejected - counted.begin())};
}

void check(const Bytes& bytes, int size, const Expected& expected)
{
    // The value helper reads byte zero even when size is nonpositive. Never
    // grant it a null buffer or empty allocation. Other null cases are below.
    if (bytes.empty() || (size > 0 && static_cast<std::size_t>(size) > bytes.size()))
    {
        throw std::logic_error("Generated fixture violates the readable-extent precondition.");
    }
    const auto allocation = static_cast<std::size_t>(std::max(size, 1));
    std::unique_ptr<char[]> storage(new char[allocation]);
    std::memcpy(storage.get(), bytes.data(), allocation);
    const auto unchanged = [&]
    {
        expect(std::memcmp(storage.get(), bytes.data(), allocation) == 0,
            "A configuration lexer helper changed its input.");
    };

    expect(SkipConfigSeparators(storage.get(), size) == expected.separator_extent,
        "Separator extent differs from the accepted-byte set.");
    unchanged();
    expect(HasConfigNameDelimiters(storage.get(), size) == expected.has_name_delimiters,
        "Name delimiter predicate differs from the two-apostrophe count.");
    unchanged();
    expect(GetConfigNameExtent(storage.get(), size) == expected.name_extent,
        "Name extent differs from the first apostrophe after byte zero.");
    unchanged();
    expect(GetConfigValueExtent(storage.get(), size) == expected.value_extent,
        "Value extent differs from the quoted or unquoted byte policy.");
    unchanged();
    ++cases;
}

void check(const Bytes& bytes, int size)
{
    if (bytes.empty() || (size > 0 && static_cast<std::size_t>(size) > bytes.size()))
    {
        throw std::logic_error("Generated oracle input has an invalid extent.");
    }
    check(bytes, size, oracle(bytes, size));
}

void check(const Bytes& bytes)
{
    check(bytes, static_cast<int>(bytes.size()));
}

void known(const Bytes& bytes, int size, const Expected& expected)
{
    const auto generated = oracle(bytes, size);
    expect(generated.separator_extent == expected.separator_extent &&
               generated.has_name_delimiters == expected.has_name_delimiters &&
               generated.name_extent == expected.name_extent &&
               generated.value_extent == expected.value_extent,
        "Generated expectations differ from a literal lexer case.");
    check(bytes, size, expected);
}

void preconditions_and_known_results()
{
    for (const int size : {0, -1, -2, -31, std::numeric_limits<int>::min()})
    {
        expect(SkipConfigSeparators(nullptr, size) == 0,
            "Nonpositive separator extents must return zero without a read.");
        expect(HasConfigNameDelimiters(nullptr, size) == 0,
            "Nonpositive name delimiter extents must return zero without a read.");
        expect(GetConfigNameExtent(nullptr, size) == 1,
            "Nonpositive name extents must return one without a read.");
    }
    expect(GetConfigNameExtent(nullptr, 1) == 1,
        "A name extent of one must not read the opening byte.");

    // These literal cases also keep the generated oracle honest. The helpers
    // implement byte rules; they do not validate syntax, escapes or text encoding.
    known({'y'}, 1, {0, 0, 1, 1});
    known({'z'}, 1, {1, 0, 1, 0});
    known({' ', 'z', 0x80, 'y', 'a'}, 5, {3, 0, 5, 0});
    known({0, '\'', 0, '\''}, 4, {1, 1, 2, 0});
    known({'\'', '\'', 'x'}, 3, {0, 1, 2, 3});
    known({'\\', '\'', 'x', '\''}, 4, {0, 1, 2, 4});
    known({'"', 0, '\\', '"', 'x'}, 5, {0, 0, 5, 4});
    known({'"', '\'', '"', '\'', 0}, 5, {0, 1, 2, 3});
    known({'a', '"', 'b', ' '}, 4, {0, 0, 4, 3});
    known({'/', '/', '\'', '\r', '\''}, 5, {0, 1, 3, 3});
    known({'"'}, 0, {0, 0, 1, 1});
    known({'a'}, 0, {0, 0, 1, 0});
}

void exhaustive_small_inputs()
{
    for (unsigned int first = 0; first != 256; ++first)
    {
        const auto byte = static_cast<unsigned char>(first);
        for (const int size : {1, 0, -1, -2, -31, std::numeric_limits<int>::min()})
        {
            check({byte}, size);
        }
        for (unsigned int second = 0; second != 256; ++second)
        {
            const auto next = static_cast<unsigned char>(second);
            check({byte, next});
            check({'\'', byte, next});
            check({'"', byte, next});
            check({'a', byte, next});
        }
    }
}

void bounded_runs()
{
    for (const std::size_t size : {2, 3, 7, 16, 31, 64, 255, 1024})
    {
        for (const unsigned char fill : {0, 32, 39, 47, 92, 122, 127, 128, 255})
        {
            Bytes bytes(size, fill);
            check(bytes);
            for (std::size_t position = 0; position < size; ++position)
            {
                for (const unsigned char inserted : {33, 34, 39, 121})
                {
                    bytes[position] = inserted;
                    check(bytes);
                }
                bytes[position] = fill;
            }
        }
        for (const unsigned char opening : {static_cast<unsigned char>('a'),
                 static_cast<unsigned char>('"'),
                 static_cast<unsigned char>('\'')})
        {
            Bytes bytes(size, 'a');
            bytes.front() = opening;
            check(bytes);
            for (std::size_t position = 1; position < size; ++position)
            {
                for (const unsigned char delimiter : {0, 32, 34, 39, 122, 128, 255})
                {
                    bytes[position] = delimiter;
                    check(bytes);
                }
                bytes[position] = 'a';
            }
        }
    }

    // The predicate only needs two apostrophes; adjacency and later occurrences
    // do not change it. Exercise every pair in a bounded run with other quotes.
    for (const std::size_t size : {2, 3, 7, 16, 31, 64})
    {
        Bytes bytes(size, '"');
        for (std::size_t first = 0; first < size; ++first)
        {
            bytes[first] = '\'';
            check(bytes);
            for (std::size_t second = first + 1; second < size; ++second)
            {
                bytes[second] = '\'';
                check(bytes);
                bytes[second] = '"';
            }
            bytes[first] = '"';
        }
    }
}

void prefixes_and_mixed_inputs()
{
    for (const Bytes& bytes : {Bytes{'"', 'a', 0, '\\', '"', 'z', '\''},
             Bytes{'\'', 0x80, '\\', '\'', '\'', ' '},
             Bytes{'a', 'b', 'z', '\'', '\'', 0},
             Bytes{0, 32, 128, 121, 'a', '"'}})
    {
        for (std::size_t size = 0; size <= bytes.size(); ++size)
        {
            check(bytes, static_cast<int>(size));
        }
    }
    std::uint32_t state = 0x617e3ad9;
    for (std::size_t run = 0; run != 1024; ++run)
    {
        Bytes bytes(1 + run % 513);
        for (auto& byte : bytes)
        {
            state = state * 1664525u + 1013904223u;
            byte = static_cast<unsigned char>(state >> 24);
        }
        if (run % 3 == 0)
        {
            bytes.front() = '"';
        }
        else if (run % 3 == 1)
        {
            bytes.front() = '\'';
        }
        check(bytes);
    }
}

// The seeker reads one byte beyond a positive declared extent. Its result can
// be modeled independently by a forward slash/CR state machine over payload only.
int quote_oracle(std::span<const unsigned char> bytes)
{
    bool skipping = false;
    for (std::size_t position = 0; position < bytes.size(); ++position)
    {
        const unsigned char byte = bytes[position];
        if (skipping)
        {
            if (byte == '\r')
            {
                skipping = false;
            }
        }
        else if (position != 0 && byte == '/')
        {
            skipping = true;
        }
        else if (byte == '\'')
        {
            return static_cast<int>(position);
        }
    }
    return static_cast<int>(bytes.size());
}

struct UnsafeCounterInput
{
};

std::optional<int> count_oracle(std::span<const unsigned char> storage, int size)
{
    // Absolute cursors and bounded searches deliberately differ from the
    // reconstructed helper calls. Reject unsafe composed domains before running
    // the unchecked counter: one lookahead byte alone does not make it safe.
    const int allocated = static_cast<int>(storage.size());
    if (allocated == 0 || size >= allocated)
    {
        throw std::logic_error("Counter oracle requires one readable lookahead byte.");
    }
    const auto pointer = [&](int position)
    {
        if (position < 0 || position > allocated)
        {
            throw UnsafeCounterInput{};
        }
    };
    const auto at = [&](int position)
    {
        if (position < 0 || position >= allocated)
        {
            throw UnsafeCounterInput{};
        }
        return storage[static_cast<std::size_t>(position)];
    };
    const auto span = [&](int position)
    {
        pointer(position);
        return storage.subspan(static_cast<std::size_t>(position),
            static_cast<std::size_t>(std::max(size - position, 0)));
    };
    const auto skip = [&](int position)
    {
        const auto remaining = span(position);
        const auto first = std::find_if(remaining.begin(),
            remaining.end(),
            [](unsigned char byte)
            {
                return accepted[byte];
            });
        return position + static_cast<int>(first - remaining.begin());
    };
    const auto through_quote = [&](int position, unsigned char quote)
    {
        // Both extent helpers consume at least one byte when the remaining
        // size is exhausted; preserve that quirk in the model, then check bounds.
        if (position + 1 >= size)
        {
            return position + 1;
        }
        const auto after_opening = span(position + 1);
        const auto found = std::find(after_opening.begin(), after_opening.end(), quote);
        return position + 1 + static_cast<int>(found - after_opening.begin()) +
               (found != after_opening.end() ? 1 : 0);
    };
    try
    {
        int cursor = skip(0);
        int count = 0;
        for (;;)
        {
            const auto remaining = span(cursor);
            if (std::count(remaining.begin(), remaining.end(), '\'') < 2)
            {
                return count;
            }
            cursor += quote_oracle(remaining);
            cursor = skip(through_quote(cursor, '\''));
            if (at(cursor) == '"')
            {
                cursor = through_quote(cursor, '"');
            }
            else
            {
                const auto value = span(cursor);
                const auto end = std::find_if_not(value.begin(),
                    value.end(),
                    [](unsigned char byte)
                    {
                        return accepted[byte];
                    });
                cursor += static_cast<int>(end - value.begin());
            }
            cursor = skip(cursor);
            ++count;
            if (count > allocated)
            {
                throw std::logic_error("Counter oracle unexpectedly failed to advance.");
            }
        }
    }
    catch (const UnsafeCounterInput&)
    {
        return std::nullopt;
    }
}

std::size_t seeker_cases = 0;
std::size_t counter_cases = 0;
std::size_t counter_excluded = 0;

Bytes padded(const Bytes& bytes, unsigned char sentinel)
{
    Bytes storage = bytes;
    storage.push_back(sentinel);
    return storage;
}

void check_scanners(const Bytes& bytes, unsigned char sentinel = 0)
{
    const Bytes expected_storage = padded(bytes, sentinel);
    const int size = static_cast<int>(bytes.size());
    const auto expected_count = count_oracle(expected_storage, size);
    // Exact allocation: sanitizers catch a second lookahead byte. Existing
    // four-helper checks above retain their stricter unpadded allocation bounds.
    std::unique_ptr<char[]> storage(new char[expected_storage.size()]);
    std::memcpy(storage.get(), expected_storage.data(), expected_storage.size());
    const auto unchanged = [&]
    {
        expect(std::memcmp(storage.get(), expected_storage.data(), expected_storage.size()) == 0,
            "A configuration scanner changed its input or lookahead byte.");
    };
    expect(SeekConfigQuotedName(storage.get(), size) == quote_oracle(bytes),
        "Quote seeker differs from the independent slash/CR model.");
    unchanged();
    ++seeker_cases;
    if (expected_count)
    {
        expect(CountConfigTokens(storage.get(), size) == *expected_count,
            "Token count differs from the independent bounded byte model.");
        unchanged();
        ++counter_cases;
    }
    else
    {
        ++counter_excluded;
    }
}

void known_scanners(const Bytes& bytes, int quote, std::optional<int> count)
{
    expect(quote_oracle(bytes) == quote &&
               count_oracle(padded(bytes, 0), static_cast<int>(bytes.size())) == count,
        "Scanner oracle differs from a literal result or known unsafe domain.");
    check_scanners(bytes);
}

Bytes bytes_from(std::string_view text)
{
    return Bytes(text.begin(), text.end());
}

void scanner_preconditions_and_known_results()
{
    for (const int size : {0, -1, -2, -31, std::numeric_limits<int>::min()})
    {
        expect(SeekConfigQuotedName(nullptr, size) == size,
            "Nonpositive quote seeking must return its size without reading.");
        ++seeker_cases;
        // Count forms buffer+0 even when its children do not read; use a valid
        // sentinel pointer instead of making a null-pointer arithmetic claim.
        const char sentinel = 0;
        expect(count_oracle(Bytes{0}, size) == 0 && CountConfigTokens(&sentinel, size) == 0,
            "Nonpositive token counting must return zero.");
        ++counter_cases;
    }
    known_scanners({}, 0, 0);
    known_scanners(bytes_from("abc"), 3, 0);
    known_scanners(bytes_from("'unterminated"), 0, 0);
    known_scanners(bytes_from("''"), 0, 1);
    known_scanners(bytes_from("'a'"), 0, 1);
    known_scanners(bytes_from("'a' 1 'b' 2"), 0, 2);
    known_scanners(bytes_from("'a''b'"), 0, 1);
    known_scanners(bytes_from("'a' \"x 'junk'\" 'b' 3"), 0, 2);
    known_scanners(bytes_from("'a' zzz 'b' 3"), 0, 1);
    known_scanners(bytes_from("x/'ignored'\r'a' 1"), 12, 1);
    known_scanners(bytes_from("/'a' 1"), 1, 1);
    known_scanners(bytes_from("'a' \"unfinished"), 0, 1);
    known_scanners({0, 255, '\'', 'a', '\'', 128, '\t', '1'}, 2, 1);
    // Both quotes can be present inside the span that the seeker skips. The
    // name helper then advances beyond the extent, and the value read is unsafe.
    known_scanners(bytes_from("x/'a'"), 5, std::nullopt);
    known_scanners(bytes_from("x/'a'\r"), 6, std::nullopt);
    known_scanners(bytes_from("x/'a'\n'b'1"), 10, std::nullopt);
}

void exhaustive_scanner_inputs()
{
    for (unsigned int first = 0; first != 256; ++first)
    {
        for (unsigned int second = 0; second != 256; ++second)
        {
            check_scanners({static_cast<unsigned char>(first)}, static_cast<unsigned char>(second));
            check_scanners(
                {static_cast<unsigned char>(first), static_cast<unsigned char>(second)}, '\'');
        }
    }
    const std::array<unsigned char, 8> alphabet{0, '\r', ' ', '"', '\'', '/', 'A', 'z'};
    std::size_t combinations = 1;
    for (std::size_t length = 0; length <= 5; ++length)
    {
        for (std::size_t encoded = 0; encoded < combinations; ++encoded)
        {
            Bytes bytes(length);
            std::size_t value = encoded;
            for (auto& byte : bytes)
            {
                byte = alphabet[value % alphabet.size()];
                value /= alphabet.size();
            }
            check_scanners(bytes, '\r');
        }
        combinations *= alphabet.size();
    }
    for (const Bytes& bytes : {bytes_from("x/"),
             bytes_from("x/'a'\r'b' 1"),
             bytes_from("x/'a'\n'b' 1"),
             bytes_from("'a'"),
             bytes_from("'a'\""),
             Bytes{0, '/', '\r', '/', '\n', '\'', '\r', '\''}})
    {
        for (unsigned int sentinel = 0; sentinel != 256; ++sentinel)
        {
            check_scanners(bytes, static_cast<unsigned char>(sentinel));
        }
    }
    for (const std::size_t size : {1, 2, 3, 7, 31, 255, 1024})
    {
        for (const unsigned char fill : {0, 10, 13, 32, 47, 128, 255})
        {
            Bytes bytes(size, fill);
            check_scanners(bytes, '/');
            for (std::size_t position = 0; position < size; ++position)
            {
                bytes[position] = '\'';
                check_scanners(bytes, '\'');
                bytes[position] = fill;
            }
        }
    }
}

void constructed_tokens_and_mixed_scanners()
{
    std::uint32_t state = 0x67d031b9;
    const auto random = [&]
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    const std::array<Bytes, 6> separators{Bytes{' '},
        Bytes{'\r', '\n'},
        Bytes{0, '\t'},
        Bytes{'z', 'z'},
        Bytes{128, 255},
        Bytes{1, 'z', 127}};
    const Bytes name_bytes{0, 9, 32, 34, 47, 65, 121, 122, 128, 255};
    const Bytes quoted_bytes{0, 13, 32, 39, 47, 65, 122, 128, 255};
    const Bytes unquoted_bytes{33, 35, 39, 47, 65, 90, 121};
    for (unsigned int run = 0; run < 4096; ++run)
    {
        const int records = static_cast<int>(run % 13);
        Bytes bytes;
        const auto append = [&](const Bytes& part)
        {
            bytes.insert(bytes.end(), part.begin(), part.end());
        };
        append(separators[random() % separators.size()]);
        for (int record = 0; record < records; ++record)
        {
            if (random() % 4 == 0)
            {
                append(bytes_from("x/'ignored'\ntext\r"));
            }
            bytes.push_back('\'');
            const auto name_size = random() % 8;
            for (unsigned int index = 0; index < name_size; ++index)
            {
                bytes.push_back(name_bytes[random() % name_bytes.size()]);
            }
            bytes.push_back('\'');
            append(separators[random() % separators.size()]);
            const bool quoted = random() % 2 != 0;
            const auto& values = quoted ? quoted_bytes : unquoted_bytes;
            if (quoted)
            {
                bytes.push_back('"');
            }
            const auto value_size = random() % 9 + (quoted ? 0u : 1u);
            for (unsigned int index = 0; index < value_size; ++index)
            {
                bytes.push_back(values[random() % values.size()]);
            }
            if (quoted)
            {
                bytes.push_back('"');
            }
            append(separators[random() % separators.size()]);
        }
        // The expected count comes from construction, independently of scanning.
        expect(count_oracle(padded(bytes, 0), static_cast<int>(bytes.size())) == records,
            "Byte model disagrees with the number of generated complete tokens.");
        check_scanners(bytes);
    }
    const std::array<unsigned char, 8> alphabet{0, '\r', '\n', ' ', '"', '\'', '/', 'z'};
    for (unsigned int run = 0; run < 8192; ++run)
    {
        Bytes bytes(random() % 129);
        for (auto& byte : bytes)
        {
            byte = run % 2 == 0 ? static_cast<unsigned char>(random())
                                : alphabet[random() % alphabet.size()];
        }
        check_scanners(bytes, static_cast<unsigned char>(random()));
    }
    expect(counter_excluded != 0, "Malformed counter inputs were not screened out.");
}

// These value helpers have distinct domains: only the quoted-extent helper can
// receive null for a nonpositive size. The classifier always reads byte zero.
struct ValueExpected
{
    int quoted_extent;
    int kind;
};

std::size_t value_cases = 0;
std::size_t value_null_cases = 0;
std::size_t value_nonpositive_cases = 0;
std::size_t value_numeric_precedence_cases = 0;

ValueExpected value_oracle(std::span<const unsigned char> bytes, int size)
{
    const auto counted = bytes.first(static_cast<std::size_t>(std::max(size, 0)));
    const auto first = std::find(counted.begin(), counted.end(), '"');
    const auto second =
        first == counted.end() ? counted.end() : std::find(first + 1, counted.end(), '"');
    const int extent = second == counted.end() ? 0 : static_cast<int>(second - counted.begin()) + 1;
    constexpr std::string_view numeric_prefixes = "0123456789.-";
    const bool numeric =
        numeric_prefixes.find(static_cast<char>(bytes.front())) != std::string_view::npos;
    // Full delimiter counting is independent of the original countdown loop.
    const bool quoted = std::count(counted.begin(), counted.end(), '"') >= 2;
    return {extent, numeric ? 2 : (quoted ? 1 : 0)};
}

void check_values(const Bytes& bytes, int size)
{
    if (bytes.empty() || (size > 0 && static_cast<std::size_t>(size) > bytes.size()))
    {
        throw std::logic_error("Value fixtures require byte zero and their positive extent.");
    }
    const auto expected = value_oracle(bytes, size);
    const auto allocation = static_cast<std::size_t>(std::max(size, 1));
    std::unique_ptr<char[]> storage(new char[allocation]);
    std::memcpy(storage.get(), bytes.data(), allocation);
    const auto unchanged = [&]
    {
        expect(std::memcmp(storage.get(), bytes.data(), allocation) == 0,
            "A configuration value helper modified its input.");
    };
    expect(GetConfigQuotedValueExtent(storage.get(), size) == expected.quoted_extent,
        "Quoted value extent differs from the independent delimiter-position model.");
    unchanged();
    expect(ClassifyConfigValue(storage.get(), size) == expected.kind,
        "Value classifier differs from the numeric-prefix and quote-count model.");
    unchanged();
    ++value_cases;
    if (size <= 0)
    {
        ++value_nonpositive_cases;
    }
    if (expected.kind == 2 && expected.quoted_extent != 0)
    {
        ++value_numeric_precedence_cases;
    }
}

void known_values(const Bytes& bytes, int size, const ValueExpected& expected)
{
    const auto generated = value_oracle(bytes, size);
    expect(generated.quoted_extent == expected.quoted_extent && generated.kind == expected.kind,
        "Generated value expectations differ from a literal case.");
    check_values(bytes, size);
}

void value_preconditions_and_known_results()
{
    for (const int size : {0, -1, -2, -31, std::numeric_limits<int>::min()})
    {
        expect(GetConfigQuotedValueExtent(nullptr, size) == 0,
            "Nonpositive quoted value extents must not read the buffer.");
        ++value_null_cases;
    }
    known_values({'-'}, std::numeric_limits<int>::min(), {0, 2});
    known_values({'+'}, 0, {0, 0});
    known_values({'.'}, 0, {0, 2});
    known_values({'0'}, -1, {0, 2});
    known_values({'"'}, 0, {0, 0});
    known_values({'"'}, 1, {0, 0});
    known_values({'"', '"'}, 2, {2, 1});
    known_values({'a', '"', 'b', '"', '"'}, 5, {4, 1});
    known_values({'0', '"', '"'}, 3, {3, 2});
    known_values({'-', '"', '"'}, 3, {3, 2});
    known_values({'.', '"', '"'}, 3, {3, 2});
    known_values({'+', '"', '"'}, 3, {3, 1});
    known_values({' ', '1', '"', '"'}, 4, {4, 1});
    known_values({'\\', '"', '\\', '"'}, 4, {4, 1});
    known_values({0, '"', 0x80, '"'}, 4, {4, 1});
    known_values({'\'', '\''}, 2, {0, 0});
    known_values({'x', '"', '"'}, 2, {0, 0});
    known_values({'x', '"', '"'}, 3, {3, 1});
}

void exhaustive_value_inputs()
{
    for (unsigned int first = 0; first != 256; ++first)
    {
        const auto byte = static_cast<unsigned char>(first);
        for (const int size : {std::numeric_limits<int>::min(), -1024, -31, -1, 0, 1})
        {
            check_values({byte}, size);
        }
        for (unsigned int second = 0; second != 256; ++second)
        {
            const auto next = static_cast<unsigned char>(second);
            check_values({byte, next}, 1);
            check_values({byte, next}, 2);
            check_values({byte, next, '"'}, 3);
        }
        check_values({byte, '"', 0, 0x80, '"', '\\'}, 6);
    }
}

void bounded_value_quotes_and_mixtures()
{
    for (const std::size_t size : {2, 3, 4, 7, 16, 31, 64, 129})
    {
        for (const unsigned char fill : {97, 0, 128, 92})
        {
            Bytes bytes(size, fill);
            check_values(bytes, static_cast<int>(size));
            for (std::size_t first = 0; first < size; ++first)
            {
                bytes[first] = '"';
                check_values(bytes, static_cast<int>(size));
                for (std::size_t second = first + 1; second < size; ++second)
                {
                    bytes[second] = '"';
                    // The second quote is respectively outside and inside the
                    // declared range. Allocations stop at that exact boundary.
                    check_values(bytes, static_cast<int>(second));
                    check_values(bytes, static_cast<int>(second + 1));
                    bytes[second] = fill;
                }
                bytes[first] = fill;
            }
        }
    }
    std::uint32_t state = 0x4ec4a0u;
    for (unsigned int run = 0; run != 4096; ++run)
    {
        const auto size = static_cast<std::size_t>(1 + run % 257);
        Bytes bytes(size);
        for (auto& byte : bytes)
        {
            state = state * 1664525u + 1013904223u;
            byte = static_cast<unsigned char>(state >> 24);
        }
        for (std::size_t index = run % 11; index < size; index += 11)
        {
            bytes[index] = '"';
        }
        check_values(bytes, static_cast<int>(size));
        check_values(bytes, static_cast<int>(run % (size + 1)));
    }
}

} // namespace

int main()
{
    try
    {
        preconditions_and_known_results();
        exhaustive_small_inputs();
        bounded_runs();
        prefixes_and_mixed_inputs();
        scanner_preconditions_and_known_results();
        exhaustive_scanner_inputs();
        constructed_tokens_and_mixed_scanners();
        std::cout << checks << " reconstructed configuration checks passed across " << cases
                  << " generated lexer cases, " << seeker_cases << " seeker cases and "
                  << counter_cases << " safe counter cases; " << counter_excluded
                  << " unsafe counter inputs excluded\n";
        const auto previous_checks = checks;
        value_preconditions_and_known_results();
        exhaustive_value_inputs();
        bounded_value_quotes_and_mixtures();
        std::cout << checks - previous_checks << " value-helper checks passed across "
                  << value_cases << " generated cases and " << value_null_cases
                  << " null extent cases (" << value_nonpositive_cases << " nonpositive extents, "
                  << value_numeric_precedence_cases << " numeric-prefix precedence cases); "
                  << checks << " total checks\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
