#include "startup_screen_item.h"
#include "startup_screen_manager.h"
#include "startup_screen_manager_auxiliary.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <tuple>
#include <type_traits>

static_assert(std::is_same_v<decltype(&ScreenItem::ClearManagerReference), void (ScreenItem::*)()>);
static_assert(std::is_trivially_copyable_v<ScreenManager>);
static_assert(std::is_trivially_copyable_v<ScreenManagerAuxiliaryState>);

namespace
{
unsigned checks = 0;

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        throw reason;
    }
}

template <class T> struct Guarded
{
    std::array<unsigned char, 16> before{};
    T value{};
    std::array<unsigned char, 16> after{};

    Guarded()
    {
        before.fill(0x37);
        after.fill(0x6b);
    }

    void check() const
    {
        require(std::all_of(before.begin(),
                    before.end(),
                    [](auto v)
                    {
                        return v == 0x37;
                    }),
            "leading guard changed");
        require(std::all_of(after.begin(),
                    after.end(),
                    [](auto v)
                    {
                        return v == 0x6b;
                    }),
            "trailing guard changed");
    }
};

auto item_state(const ScreenItem& item)
{
    return std::make_tuple(item.rectangle.left,
        item.rectangle.top,
        item.rectangle.right,
        item.rectangle.bottom,
        item.saved_rectangle.left,
        item.saved_rectangle.top,
        item.saved_rectangle.right,
        item.saved_rectangle.bottom,
        item.elapsed_24,
        item.kind_28,
        item.word_2c,
        item.state_30,
        item.unknown_31[0],
        item.unknown_31[1],
        item.unknown_31[2],
        item.last_x,
        item.last_y,
        item.owned_text,
        item.state_40,
        item.state_41,
        item.state_42,
        item.preparation_flag);
}

void check_manager(const ScreenManager& actual, const ScreenManager& expected)
{
    require(actual.renderer == expected.renderer && actual.registry == expected.registry &&
                actual.item_90 == expected.item_90 && actual.active.head == expected.active.head,
        "manager pointer changed");
    require(actual.mode == expected.mode && actual.previous_mode == expected.previous_mode &&
                actual.auxiliary_130 == expected.auxiliary_130 &&
                actual.item_134 == expected.item_134 && actual.value_138 == expected.value_138,
        "manager state differs");
    require(std::equal(std::begin(actual.fonts_04),
                std::end(actual.fonts_04),
                std::begin(expected.fonts_04)) &&
                std::equal(std::begin(actual.fonts_2c),
                    std::end(actual.fonts_2c),
                    std::begin(expected.fonts_2c)) &&
                std::equal(std::begin(actual.unknown_54),
                    std::end(actual.unknown_54),
                    std::begin(expected.unknown_54)) &&
                std::equal(std::begin(actual.unknown_94),
                    std::end(actual.unknown_94),
                    std::begin(expected.unknown_94)) &&
                std::equal(std::begin(actual.unknown_c0),
                    std::end(actual.unknown_c0),
                    std::begin(expected.unknown_c0)),
        "manager retained member changed");
}

auto auxiliary_state(const ScreenManagerAuxiliaryState& value)
{
    return std::make_tuple(value.word_00,
        value.state_04,
        value.unknown_05[0],
        value.unknown_05[1],
        value.unknown_05[2],
        value.text_08,
        value.text_width_0c,
        value.text_height_10,
        value.width_14,
        value.height_18,
        value.x_1c,
        value.y_20,
        value.frame_24);
}

struct Binding
{
    ScreenManager* prior = screen_manager;
    ~Binding()
    {
        screen_manager = prior;
    }
};

void run()
{
    ScreenItem first({-3, 5, 21, 31}), second({8, 9, 41, 52});
    first.kind_28 = 21;
    second.kind_28 = 22;
    first.word_2c = 0x12345678;
    second.word_2c = 0x89abcdef;
    first.unknown_31[0] = second.unknown_31[0] = 0x21;
    first.unknown_31[1] = second.unknown_31[1] = 0x43;
    first.unknown_31[2] = second.unknown_31[2] = 0x65;
    first.SetOwnedText("radio first");
    second.SetOwnedText("radio second");
    const auto first_before = item_state(first), second_before = item_state(second);
    std::array<Guarded<ScreenManager>, 2> managers;
    std::array<Guarded<ScreenManagerAuxiliaryState>, 2> auxiliaries;
    Binding binding;
    for (unsigned test = 0; test < 4; ++test)
    {
        for (unsigned i = 0; i < 2; ++i)
        {
            managers[i].value.auxiliary_130 = &auxiliaries[i].value;
            managers[i].value.item_134 = i == 0 ? &first : &second;
            managers[i].value.value_138 = -3.25f - static_cast<float>(i);
            auxiliaries[i].value.word_00 = 0x13579bdf + i;
            auxiliaries[i].value.state_04 = 0x79;
        }
        const unsigned current = test == 2 ? 1 : 0;
        ScreenItem& receiver = test == 1 ? second : first;
        if (test == 2)
        {
            managers[1].value.item_134 = &first;
        }
        if (test == 3)
        {
            managers[0].value.item_134 = nullptr;
            managers[0].value.auxiliary_130 = nullptr;
        }
        std::array expected{managers[0].value, managers[1].value};
        std::array expected_aux{auxiliaries[0].value, auxiliaries[1].value};
        if (test == 0 || test == 2)
        {
            expected[current].item_134 = nullptr;
            expected[current].value_138 = 2.5f;
            expected_aux[current].state_04 = 0;
        }
        screen_manager = &managers[current].value;
        receiver.ClearManagerReference();
        require(screen_manager == &managers[current].value, "global manager changed");
        require(item_state(first) == first_before && item_state(second) == second_before,
            "real item member changed");
        require(
            first.owned_text[0] == 'r' && second.owned_text[0] == 'r', "owned item text changed");
        for (unsigned i = 0; i < 2; ++i)
        {
            check_manager(managers[i].value, expected[i]);
            require(auxiliary_state(auxiliaries[i].value) == auxiliary_state(expected_aux[i]),
                "auxiliary member differs");
            managers[i].check();
            auxiliaries[i].check();
        }
    }
}
} // namespace

int main()
{
    ScreenManager* const prior = screen_manager;
    try
    {
        run();
        require(screen_manager == prior, "manager binding not restored");
        std::printf("item-clear-manager cases=4 calls=4 real_items=2 checks=%u pass\n", checks);
        return 0;
    }
    catch (const char* reason)
    {
        std::fprintf(stderr, "item-clear-manager: %s\n", reason);
        return 2;
    }
}
