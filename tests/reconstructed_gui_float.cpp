#include "startup_gui_parser.h"

#include <array>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <tuple>

namespace
{
std::size_t checks = 0;
std::size_t calls = 0;

struct Failure
{
    const char* message;
};

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw Failure{message};
    }
}

// Every retained member is a typed value. The parser's owning constructor and
// destructor are not selected here; this is the existing non-owning prefix.
auto retained(const GuiParserState& p)
{
    return std::tuple{p.manager,
        p.buffer,
        p.current_context,
        p.alternate_context_10,
        p.alternate_context_14,
        p.context_head,
        p.previous_context,
        p.last_registered_item,
        p.defined_items,
        p.registry_head,
        p.items_head,
        p.items_tail,
        p.registry_tail,
        p.word_38,
        p.word_3c};
}

struct GuardedParser
{
    std::array<unsigned int, 4> before{0x1a2b3c4d, 0x2b3c4d5e, 0x3c4d5e6f, 0x4d5e6f70};
    GuiParserState parser{};
    std::array<unsigned int, 4> after{0x50617283, 0x61728394, 0x728394a5, 0x8394a5b6};
};

struct Literal
{
    std::string_view input;
    float value;
    unsigned int cursor;
};

// Explicit cursor positions include the next unconsumed byte. In particular,
// whitespace before a comma leaves that comma for the caller; interior tabs
// belong to the copied token and are handled by the real CRT's atof.
constexpr std::array<Literal, 13> literals{{
    {"1.5)Q", 1.5f, 4},
    {" \t\r\n-2.25, \tQ", -2.25f, 12},
    {"+6.25e-2)Q", 0.0625f, 9},
    {".5,Q", 0.5f, 3},
    {"abc)Q", 0.0f, 4},
    {",Q", 0.0f, 1},
    {")Q", 0.0f, 1},
    {"12tail)Q", 12.0f, 7},
    {"1\t2)Q", 1.0f, 4},
    {"3.5 \r\n,Q", 3.5f, 6},
    {"7))Q", 7.0f, 2},
    {"2,\vQ", 2.0f, 2},
    {"0.125 \tQ", 0.125f, 7},
}};

void one(std::string_view input, float expected, unsigned int end, unsigned int start)
{
    std::string storage(start, '#');
    storage.append(input);
    storage.append("outside-input");
    const auto unchanged = storage;
    GuardedParser guarded;
    guarded.parser.cursor = start;
    guarded.parser.buffer = storage.c_str();
    guarded.parser.word_38 = 0x13579bdf;
    guarded.parser.word_3c = 0x2468ace0;
    const auto members = retained(guarded.parser);
    const auto before = guarded.before;
    const auto after = guarded.after;
    struct Output
    {
        unsigned int before = 0xabcddcba;
        float value = -123.5f;
        unsigned int after = 0x76543210;
    } output;
    guarded.parser.ReadFloat(output.value);
    ++calls;
    require(guarded.parser.cursor == start + end, "cursor differs");
    require(output.value == expected, "value differs");
    require(output.before == 0xabcddcba && output.after == 0x76543210, "output guards changed");
    require(retained(guarded.parser) == members, "unrelated parser member changed");
    require(guarded.before == before && guarded.after == after, "parser guards changed");
    require(storage == unchanged, "input changed");
}

void run()
{
    // The process starts in the C locale. Do not mutate process-global locale
    // or claim equivalence for the original CRT's nondecimal/overflow grammar.
    for (unsigned int start : {0u, 1u, 7u})
    {
        for (const auto& test : literals)
        {
            one(test.input, test.value, test.cursor, start);
        }
    }

    // Exactly 127 token bytes plus its terminator fit the original stack array.
    std::string longest(124, '0');
    longest += "1.5)Q";
    one(longest, 1.5f, 128, 0);

    GuiParserState parser{};
    parser.buffer = "  1.5 , 2)Q";
    float value = 0;
    parser.ReadFloat(value);
    ++calls;
    require(value == 1.5f && parser.cursor == 6, "first sequence step differs");
    parser.ReadFloat(value);
    ++calls;
    require(value == 0.0f && parser.cursor == 8, "empty comma sequence step differs");
    parser.ReadFloat(value);
    ++calls;
    require(value == 2.0f && parser.cursor == 10, "last sequence step differs");
}
} // namespace

int main()
{
    try
    {
        run();
        std::printf("gui-float: groups=3 calls=%zu checks=%zu\n", calls, checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "float parser fixture: %s\n", failure.message);
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "float parser host failure: %s\n", error.what());
        return 3;
    }
}
