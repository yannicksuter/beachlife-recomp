#include "startup_config_scan.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
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
} // namespace

int main()
{
    try
    {
        preconditions_and_known_results();
        exhaustive_small_inputs();
        bounded_runs();
        prefixes_and_mixed_inputs();
        std::cout << checks << " reconstructed configuration checks passed across " << cases
                  << " generated lexer cases\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
