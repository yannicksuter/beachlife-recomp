#include "startup_screen_list_box_find.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdio>

namespace
{

static_assert(CHAR_BIT == 8 && sizeof(int) == 4);

struct GuardedWords
{
    std::array<int, 2> before;
    std::array<int, 11> values;
    std::array<int, 2> after;
};

static_assert(offsetof(GuardedWords, values) == 2 * sizeof(int));
static_assert(offsetof(GuardedWords, after) == 13 * sizeof(int));

const GuardedWords expected{
    {0x13579bdf, 0x2468ace0},
    {INT_MIN, -11, 13, -17, 19, INT_MAX, 29, -11, 13, -17, 0},
    {-0x01234567, -0x07654321},
};

unsigned int checks = 0;
unsigned int calls = 0;

bool check(bool condition, const char* label)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "selection find: %s\n", label);
    }
    return condition;
}

bool retain(const GuardedWords& words)
{
    bool passed = true;
    for (std::size_t index = 0; index < words.before.size(); ++index)
    {
        passed = check(words.before[index] == expected.before[index], "leading guard") && passed;
        passed = check(words.after[index] == expected.after[index], "trailing guard") && passed;
    }
    for (std::size_t index = 0; index < words.values.size(); ++index)
    {
        passed =
            check(words.values[index] == expected.values[index], "input word unchanged") && passed;
    }
    return passed;
}

bool call(
    GuardedWords& words, int* first, int* last, const int& value, int* wanted, const char* label)
{
    const ScreenListBoxRandomAccessTag tag{};
    const int before = value;
    ++calls;
    int* actual = FindListBoxSelection(first, last, value, tag);
    bool passed = check(actual == wanted, label);
    passed = check(value == before, "value reference unchanged") && passed;
    return retain(words) && passed;
}

struct Literal
{
    std::size_t first;
    std::size_t last;
    int value;
    std::size_t expected;
    const char* label;
};

constexpr std::array<Literal, 8> literals{{
    {0, 11, INT_MIN, 0, "first matching word"},
    {0, 11, -11, 1, "first duplicate wins"},
    {0, 11, -17, 3, "fourth unrolled lane"},
    {0, 11, 19, 4, "next four-word group"},
    {0, 11, 0, 10, "last word"},
    {0, 11, 99, 11, "miss returns exact end"},
    {8, 11, -17, 9, "three-word tail middle"},
    {8, 11, 0, 10, "three-word tail last"},
}};

} // namespace

int main()
{
    GuardedWords words = expected;
    int* data = words.values.data();
    const int empty_value = INT_MAX;

    // A real value reference remains valid even when both range pointers are null.
    if (!call(words, nullptr, nullptr, empty_value, nullptr, "null empty range") ||
        !call(words, data + 4, data + 4, empty_value, data + 4, "same-array empty range"))
    {
        return 2;
    }

    for (const Literal& literal : literals)
    {
        if (!call(words,
                data + literal.first,
                data + literal.last,
                literal.value,
                data + literal.expected,
                literal.label))
        {
            return 2;
        }
    }

    if (!call(words, data + 8, data + 11, data[8], data + 8, "tail value aliases first") ||
        !call(words, data, data + 11, data[7], data + 1, "later alias finds earlier duplicate"))
    {
        return 2;
    }

    std::printf("selection find: 3 groups, %u calls, %u checks, word=%zu pointer=%zu\n",
        calls,
        checks,
        sizeof(int),
        sizeof(int*));
    return 0;
}
