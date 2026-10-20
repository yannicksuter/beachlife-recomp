#include "startup_screen_item.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>

namespace
{
static_assert(sizeof(int) == 4 && std::numeric_limits<int>::digits == 31);

struct Failure
{
    std::size_t group;
    const char* message;
};

std::size_t group = 0;
std::size_t checks = 0;
std::size_t calls = 0;

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw Failure{group, message};
    }
}

struct GuardedItem
{
    std::array<std::uint64_t, 2> before{0x123456789abcdef0ULL, 0xfedcba9876543210ULL};
    ScreenItem item;
    std::array<std::uint64_t, 2> after{0x1122334455667788ULL, 0x8877665544332211ULL};

    explicit GuardedItem(ScreenRectangle initial)
        : item(initial)
    {
        item.elapsed_24 = 1.25f;
        item.kind_28 = 0x1234567;
        item.word_2c = 0xa5c39e71U;
        item.state_30 = 0x63;
        item.unknown_31[0] = 0x12;
        item.unknown_31[1] = 0x34;
        item.unknown_31[2] = 0x56;
        item.last_x = -307;
        item.last_y = 811;
        item.state_40 = 0x29;
        item.state_41 = 0x37;
        item.state_42 = 0x53;
        item.preparation_flag = 0x61;
    }
};

struct Case
{
    ScreenRectangle initial;
    int x;
    int y;
    ScreenRectangle expected;
};

constexpr Case cases[]{
    {{13, 17, 113, 57}, 13, 499, {13, 499, 113, 539}},
    {{-7, -9, 23, 21}, -10, -20, {-10, -20, 20, 10}},
    {{10, 5, -2, 5}, 3, -4, {3, -4, -9, -4}},
};
constexpr ScreenRectangle aliased_expected{-20, -10, 10, 20};

void observe(
    const GuardedItem& frame, const ScreenRectangle& initial, const ScreenRectangle& expected)
{
    const ScreenItem& item = frame.item;
    require(item.rectangle.left == expected.left, "left");
    require(item.rectangle.top == expected.top, "top");
    require(item.rectangle.right == expected.right, "right");
    require(item.rectangle.bottom == expected.bottom, "bottom");
    require(item.saved_rectangle.left == initial.left, "saved left");
    require(item.saved_rectangle.top == initial.top, "saved top");
    require(item.saved_rectangle.right == initial.right, "saved right");
    require(item.saved_rectangle.bottom == initial.bottom, "saved bottom");
    require(item.elapsed_24 == 1.25f, "elapsed");
    require(item.kind_28 == 0x1234567, "kind");
    require(item.word_2c == 0xa5c39e71U, "word");
    require(item.state_30 == 0x63, "state30");
    require(item.unknown_31[0] == 0x12, "opaque0");
    require(item.unknown_31[1] == 0x34, "opaque1");
    require(item.unknown_31[2] == 0x56, "opaque2");
    require(item.last_x == -307, "last x");
    require(item.last_y == 811, "last y");
    require(item.owned_text == nullptr, "owned text");
    require(item.state_40 == 0x29, "state40");
    require(item.state_41 == 0x37, "state41");
    require(item.state_42 == 0x53, "state42");
    require(item.preparation_flag == 0x61, "preparation");
    require(frame.before[0] == 0x123456789abcdef0ULL, "before0");
    require(frame.before[1] == 0xfedcba9876543210ULL, "before1");
    require(frame.after[0] == 0x1122334455667788ULL, "after0");
    require(frame.after[1] == 0x8877665544332211ULL, "after1");
}

void run()
{
    for (group = 0; group < std::size(cases); ++group)
    {
        const Case& test = cases[group];
        GuardedItem frame(test.initial);
        frame.item.SetRectanglePosition(test.x, test.y);
        ++calls;
        observe(frame, test.initial, test.expected);
        if (group == 1)
        {
            frame.item.SetRectanglePosition(frame.item.rectangle.top, frame.item.rectangle.left);
            ++calls;
            observe(frame, test.initial, aliased_expected);
        }
    }
}
} // namespace

int main()
{
    try
    {
        run();
        std::printf("{\"groups\":3,\"calls\":%zu,\"checks\":%zu,\"item_bytes\":%zu}\n",
            calls,
            checks,
            sizeof(ScreenItem));
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "rectangle-position case %zu: %s\n", failure.group, failure.message);
        return 2;
    }
}
