#include "startup_screen_item.h"
#include "startup_screen_manager.h"
#include "startup_screen_manager_auxiliary.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

#if !defined(BEACHLIFE_DECOMP_SCREEN_ACTIVATION)
#error This fixture requires the experimental activation source profile.
#endif

static_assert(std::numeric_limits<unsigned char>::digits == 8);
static_assert(sizeof(unsigned int) == 4 && sizeof(int) == 4);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(
    std::is_same_v<decltype(&ScreenItem::Unresolved24), void (ScreenItem::*)(unsigned char)>);
static_assert(std::is_standard_layout_v<ScreenManager>);
static_assert(std::is_trivially_copyable_v<ScreenManager>);
static_assert(std::is_standard_layout_v<ScreenManagerAuxiliaryState>);
static_assert(offsetof(ScreenManagerAuxiliaryState, state_04) == 4);
static_assert(
    offsetof(ScreenManager, auxiliary_130) == offsetof(ScreenManager, previous_mode) + sizeof(int));
static_assert(offsetof(ScreenManager, item_134) ==
              offsetof(ScreenManager, auxiliary_130) + sizeof(ScreenManagerAuxiliaryState*));
static_assert(
    offsetof(ScreenManager, value_138) == offsetof(ScreenManager, item_134) + sizeof(ScreenItem*));
#if defined(BEACHLIFE_DECOMP_AUXILIARY_TEXT)
static_assert(sizeof(void*) != 8 || sizeof(ScreenManager) == 384);
static_assert(sizeof(void*) != 4 || offsetof(ScreenManager, fonts_2c) == 0x2c);
static_assert(sizeof(void*) != 4 || offsetof(ScreenManager, unknown_54) == 0x54);
static_assert(sizeof(void*) != 4 || sizeof(ScreenManagerAuxiliaryState) == 0x28);
static_assert(sizeof(void*) != 8 || sizeof(ScreenManagerAuxiliaryState) == 48);
#else
static_assert(sizeof(void*) != 8 || sizeof(ScreenManager) == 344);
#endif
static_assert(sizeof(void*) != 4 || sizeof(ScreenManager) == 0x13c);
static_assert(sizeof(void*) != 4 || offsetof(ScreenManager, auxiliary_130) == 0x130);
static_assert(sizeof(void*) != 4 || offsetof(ScreenManager, item_134) == 0x134);
static_assert(sizeof(void*) != 4 || offsetof(ScreenManager, value_138) == 0x138);

namespace
{
struct Counts
{
    unsigned cases = 0;
    unsigned activation_calls = 0;
    unsigned manager_calls = 0;
    unsigned auxiliary_calls = 0;
    unsigned constructions = 0;
    unsigned destructions = 0;
    unsigned long long checks = 0;
    unsigned long long byte_checks = 0;
} counts;

void require(bool value, const char* reason)
{
    ++counts.checks;
    if (!value)
    {
        std::fprintf(stderr, "case %u: %s\n", counts.cases, reason);
        std::fflush(stderr);
        std::_Exit(2);
    }
}

// Only unresolved interfaces have generated fail-fast observers. Activation
// and every already reconstructed virtual body are inherited without overrides.
struct GeneratedItem final : ScreenItem
{
    explicit GeneratedItem(ScreenRectangle initial)
        : ScreenItem(initial)
    {
        ++counts.constructions;
    }
    ~GeneratedItem() override
    {
        ++counts.destructions;
    }
    void Unresolved04() override
    {
        require(false, "unobserved item slot called");
    }
    void Unresolved08() override
    {
        require(false, "unobserved item slot called");
    }
    void Unresolved0c() override
    {
        require(false, "unobserved item slot called");
    }
    void Unresolved10(ScreenItem*) override
    {
        require(false, "unobserved item slot called");
    }
    void Unresolved1c() override
    {
        require(false, "unobserved item slot called");
    }
    void Unresolved20() override
    {
        require(false, "unobserved item slot called");
    }
    void Unresolved28() override
    {
        require(false, "unobserved item slot called");
    }
};

template <typename T> struct Guarded
{
    std::array<unsigned char, 32> before{};
    T value{};
    std::array<unsigned char, 32> after{};
};

struct ItemFrame
{
    std::array<unsigned char, 32> before{};
    GeneratedItem value;
    std::array<unsigned char, 32> after{};
    explicit ItemFrame(ScreenRectangle initial)
        : value(initial)
    {
        before.fill(0x63);
        after.fill(0xd9);
        // The actual constructor leaves these observed fields untouched.
        // Give the generated live object valid, explicit values before reads.
        value.kind_28 = 17;
        value.word_2c = 0x76543210U;
        std::memset(value.unknown_31, 0xa6, sizeof(value.unknown_31));
    }
};

// Snapshot live representations, comparing actual members/opaque bytes and
// guards. Native padding is deliberately not treated as a game field.
template <typename T> struct Snapshot
{
    const T& object;
    std::array<unsigned char, sizeof(T)> expected{};
    std::array<bool, sizeof(T)> watched{};

    explicit Snapshot(const T& value)
        : object(value)
    {
        std::memcpy(expected.data(), static_cast<const void*>(&value), sizeof(T));
    }
    template <typename Field> std::size_t offset(const Field& field) const
    {
        const auto start = reinterpret_cast<std::uintptr_t>(&object);
        const auto at = reinterpret_cast<std::uintptr_t>(&field);
        require(at >= start && at - start <= sizeof(T) - sizeof(Field),
            "snapshot member is outside its live object");
        return at - start;
    }
    template <typename Field> void watch(const Field& field)
    {
        const auto begin = offset(field);
        for (std::size_t i = begin; i < begin + sizeof(Field); ++i)
        {
            watched[i] = true;
        }
    }
    template <typename Field> void expect(const Field& field, const Field& value)
    {
        const auto begin = offset(field);
        for (std::size_t i = begin; i < begin + sizeof(Field); ++i)
        {
            require(watched[i], "expected member was not watched");
        }
        std::memcpy(expected.data() + begin, &value, sizeof(Field));
    }
    void check(const char* reason) const
    {
        const auto* actual = reinterpret_cast<const unsigned char*>(&object);
        for (std::size_t i = 0; i < sizeof(T); ++i)
        {
            if (watched[i])
            {
                ++counts.byte_checks;
                require(actual[i] == expected[i], reason);
            }
        }
    }
};

void watch(Snapshot<ItemFrame>& s)
{
    const auto& o = s.object;
    s.watch(o.before);
    s.watch(o.after);
    s.watch(o.value.rectangle);
    s.watch(o.value.saved_rectangle);
    s.watch(o.value.elapsed_24);
    s.watch(o.value.kind_28);
    s.watch(o.value.word_2c);
    s.watch(o.value.state_30);
    s.watch(o.value.unknown_31);
    s.watch(o.value.last_x);
    s.watch(o.value.last_y);
    s.watch(o.value.owned_text);
    s.watch(o.value.state_40);
    s.watch(o.value.state_41);
    s.watch(o.value.state_42);
    s.watch(o.value.preparation_flag);
}

void watch(Snapshot<Guarded<ScreenManager>>& s)
{
    const auto& o = s.object;
    s.watch(o.before);
    s.watch(o.after);
    s.watch(o.value.renderer);
    s.watch(o.value.unknown_04);
#if defined(BEACHLIFE_DECOMP_AUXILIARY_TEXT)
    s.watch(o.value.fonts_2c);
    s.watch(o.value.unknown_54);
#endif
    s.watch(o.value.registry);
    s.watch(o.value.unknown_90);
    s.watch(o.value.active.head);
    s.watch(o.value.unknown_c0);
    s.watch(o.value.mode);
    s.watch(o.value.previous_mode);
    s.watch(o.value.auxiliary_130);
    s.watch(o.value.item_134);
    s.watch(o.value.value_138);
}

void watch(Snapshot<Guarded<ScreenManagerAuxiliaryState>>& s)
{
    s.watch(s.object.before);
    s.watch(s.object.after);
    s.watch(s.object.value.word_00);
    s.watch(s.object.value.state_04);
#if defined(BEACHLIFE_DECOMP_AUXILIARY_TEXT)
    s.watch(s.object.value.unknown_05);
    s.watch(s.object.value.text_08);
    s.watch(s.object.value.text_width_0c);
    s.watch(s.object.value.text_height_10);
    s.watch(s.object.value.width_14);
    s.watch(s.object.value.height_18);
    s.watch(s.object.value.x_1c);
    s.watch(s.object.value.y_20);
    s.watch(s.object.value.frame_24);
#endif
}

#if defined(BEACHLIFE_DECOMP_AUXILIARY_TEXT)
void initialize_auxiliary_tail(ScreenManagerAuxiliaryState& value)
{
    std::memset(value.unknown_05, 0x6d, sizeof(value.unknown_05));
    value.text_08 = nullptr;
    value.text_width_0c = 0x10203040U;
    value.text_height_10 = 0x50607080U;
    value.width_14 = 0x90a0b0c0U;
    value.height_18 = 0xd0e0f001U;
    value.x_1c = -321;
    value.y_20 = 654;
    value.frame_24 = nullptr;
}
#endif

struct ManagerBinding
{
    ScreenManager* previous = screen_manager;
    explicit ManagerBinding(ScreenManager* value)
    {
        screen_manager = value;
    }
    ~ManagerBinding()
    {
        screen_manager = previous;
    }
};

enum class Item
{
    None,
    First,
    Second
};
enum class Route
{
    Activation,
    Manager
};
struct Literal
{
    Route route;
    unsigned manager;
    bool null_global;
    bool null_auxiliary;
    unsigned char argument;
    Item stored;
    Item manager_argument;
    unsigned char initial_state;
    float initial_elapsed;
    unsigned char initial_auxiliary;
    float initial_value;
    unsigned char expected_state;
    float expected_elapsed;
    unsigned char expected_auxiliary;
    Item expected_stored;
    float expected_value;
};

constexpr Literal matched{Route::Activation,
    0,
    false,
    false,
    0,
    Item::First,
    Item::First,
    0x93,
    -7.25f,
    0xfe,
    -9.0f,
    0,
    0.0f,
    0,
    Item::None,
    2.5f};
constexpr std::array<Literal, 7> literals{{
    matched,
    {Route::Activation,
        0,
        false,
        false,
        0,
        Item::Second,
        Item::First,
        0x11,
        13.5f,
        0xa7,
        -4.0f,
        0,
        0.0f,
        0xa7,
        Item::Second,
        -4.0f},
    {Route::Activation,
        0,
        true,
        false,
        2,
        Item::First,
        Item::First,
        0x82,
        -3.25f,
        0x45,
        -6.0f,
        2,
        -3.25f,
        0x45,
        Item::First,
        -6.0f},
    {Route::Activation,
        1,
        false,
        false,
        0xff,
        Item::First,
        Item::First,
        0x16,
        -0.0f,
        0x91,
        1.25f,
        0xff,
        -0.0f,
        0x91,
        Item::First,
        1.25f},
    {Route::Activation,
        1,
        false,
        false,
        0,
        Item::First,
        Item::First,
        0x27,
        7.5f,
        0xff,
        2.0f,
        0,
        0.0f,
        0,
        Item::None,
        2.5f},
    {Route::Manager,
        0,
        false,
        false,
        0,
        Item::None,
        Item::None,
        0x38,
        11.0f,
        0x88,
        7.0f,
        0x38,
        11.0f,
        0,
        Item::None,
        2.5f},
    {Route::Manager,
        0,
        false,
        true,
        0,
        Item::Second,
        Item::None,
        0x49,
        -1.5f,
        0x71,
        4.5f,
        0x49,
        -1.5f,
        0x71,
        Item::Second,
        4.5f},
}};

struct Fixture
{
    ItemFrame first{{-3, 5, 17, 29}};
    ItemFrame second{{31, -7, 61, 43}};
    std::array<Guarded<ScreenManager>, 2> managers{};
    std::array<Guarded<ScreenManagerAuxiliaryState>, 2> auxiliaries{};

    Fixture()
    {
        for (unsigned i = 0; i < managers.size(); ++i)
        {
            auto& m = managers[i];
            m.before.fill(0x72);
            m.after.fill(0xb4);
            std::memset(m.value.unknown_04, 0x29 + i, sizeof(m.value.unknown_04));
#if defined(BEACHLIFE_DECOMP_AUXILIARY_TEXT)
            for (auto& font : m.value.fonts_2c)
            {
                font = nullptr;
            }
            std::memset(m.value.unknown_54, 0x29 + i, sizeof(m.value.unknown_54));
#endif
            std::memset(m.value.unknown_90, 0xa3 + i, sizeof(m.value.unknown_90));
            std::memset(m.value.unknown_c0, 0x5e + i, sizeof(m.value.unknown_c0));
            m.value.mode = -75;
            m.value.previous_mode = 123;
            m.value.auxiliary_130 = &auxiliaries[i].value;
            m.value.item_134 = &second.value;
            m.value.value_138 = -2.25f;
            auxiliaries[i].before.fill(0x37);
            auxiliaries[i].after.fill(0xc6);
            auxiliaries[i].value.word_00 = 0x98765432U + i;
            auxiliaries[i].value.state_04 = 0x81;
#if defined(BEACHLIFE_DECOMP_AUXILIARY_TEXT)
            initialize_auxiliary_tail(auxiliaries[i].value);
#endif
        }
    }

    ScreenItem* item(Item which)
    {
        switch (which)
        {
        case Item::None:
            return nullptr;
        case Item::First:
            return &first.value;
        case Item::Second:
            return &second.value;
        }
        require(false, "invalid literal item");
        return nullptr;
    }

    void initialize(const Literal& value)
    {
        first.value.state_30 = value.initial_state;
        first.value.elapsed_24 = value.initial_elapsed;
        auto& manager = managers[value.manager].value;
        manager.item_134 = item(value.stored);
        manager.auxiliary_130 = value.null_auxiliary ? nullptr : &auxiliaries[value.manager].value;
        manager.value_138 = value.initial_value;
        auxiliaries[value.manager].value.state_04 = value.initial_auxiliary;
    }

    void invoke(const Literal& value)
    {
        Snapshot<ItemFrame> first_before(first), second_before(second);
        watch(first_before);
        watch(second_before);
        std::array manager_before{Snapshot(managers[0]), Snapshot(managers[1])};
        std::array auxiliary_before{Snapshot(auxiliaries[0]), Snapshot(auxiliaries[1])};
        for (auto& snapshot : manager_before)
        {
            watch(snapshot);
        }
        for (auto& snapshot : auxiliary_before)
        {
            watch(snapshot);
        }
        auto& manager = managers[value.manager].value;
        auto& auxiliary = auxiliaries[value.manager].value;
        first_before.expect(first.value.state_30, value.expected_state);
        first_before.expect(first.value.elapsed_24, value.expected_elapsed);
        ScreenItem* expected_item = item(value.expected_stored);
        manager_before[value.manager].expect(manager.item_134, expected_item);
        manager_before[value.manager].expect(manager.value_138, value.expected_value);
        auxiliary_before[value.manager].expect(auxiliary.state_04, value.expected_auxiliary);
        ScreenManager* const bound = value.null_global ? nullptr : &manager;
        ScreenManager* const prior = screen_manager;
        {
            ManagerBinding binding(bound);
            if (value.route == Route::Activation)
            {
                ScreenItem& receiver = first.value;
                receiver.Unresolved24(value.argument);
                ++counts.activation_calls;
            }
            else
            {
                manager.ClearItemReference(item(value.manager_argument));
                ++counts.manager_calls;
            }
            require(manager.value_138 == value.expected_value, "manager clear value differs");
            require(std::bit_cast<std::uint32_t>(first.value.elapsed_24) ==
                        std::bit_cast<std::uint32_t>(value.expected_elapsed),
                "elapsed float representation differs");
            require(screen_manager == bound, "activation changed the manager global");
            first_before.check("item field or guard differs");
            second_before.check("borrowed peer field or guard changed");
            for (const auto& snapshot : manager_before)
            {
                snapshot.check("manager field or guard differs");
            }
            for (const auto& snapshot : auxiliary_before)
            {
                snapshot.check("auxiliary field or guard differs");
            }
        }
        require(screen_manager == prior, "fixture did not restore manager binding");
        ++counts.cases;
    }
};

void repeated_disable()
{
    Fixture fixture;
    fixture.initialize(matched);
    fixture.invoke(matched);
    // This is the same live object after its real first clear. No item pointer
    // is republished; the second call must retain these newly seeded values.
    fixture.first.value.elapsed_24 = 6.75f;
    fixture.auxiliaries[0].value.state_04 = 0x5a;
    fixture.managers[0].value.value_138 = 9.0f;
    constexpr Literal again{Route::Activation,
        0,
        false,
        false,
        0,
        Item::None,
        Item::First,
        0,
        6.75f,
        0x5a,
        9.0f,
        0,
        0.0f,
        0x5a,
        Item::None,
        9.0f};
    fixture.invoke(again);
}

void all_activation_bytes()
{
    {
        Fixture fixture;
        fixture.initialize(matched);
        fixture.invoke(matched);
    }
    for (unsigned byte = 1; byte <= 255; ++byte)
    {
        const auto argument = static_cast<unsigned char>(byte);
        const Literal enabled{Route::Activation,
            0,
            true,
            false,
            argument,
            Item::First,
            Item::First,
            0x96,
            -12.5f,
            0xe3,
            -5.25f,
            argument,
            -12.5f,
            0xe3,
            Item::First,
            -5.25f};
        Fixture fixture;
        fixture.initialize(enabled);
        fixture.invoke(enabled);
    }
}

void all_auxiliary_bytes()
{
    for (unsigned byte = 0; byte <= 255; ++byte)
    {
        Guarded<ScreenManagerAuxiliaryState> object{};
        object.before.fill(0x87);
        object.after.fill(0xb9);
        object.value.word_00 = 0xa1b2c3d4U;
        object.value.state_04 = static_cast<unsigned char>(byte);
#if defined(BEACHLIFE_DECOMP_AUXILIARY_TEXT)
        initialize_auxiliary_tail(object.value);
#endif
        Snapshot before(object);
        watch(before);
        const unsigned char zero = 0;
        before.expect(object.value.state_04, zero);
        object.value.ClearState();
        ++counts.auxiliary_calls;
        before.check("auxiliary clear changed retained bytes");
        object.value.ClearState();
        ++counts.auxiliary_calls;
        before.check("repeated auxiliary clear changed retained bytes");
        ++counts.cases;
    }
}
} // namespace

int main()
{
    require(screen_manager == nullptr, "real manager global is not initially null");
    for (const auto& value : literals)
    {
        Fixture fixture;
        fixture.initialize(value);
        fixture.invoke(value);
    }
    repeated_disable();
    all_activation_bytes();
    all_auxiliary_bytes();
    require(counts.cases == 521 && counts.activation_calls == 263 && counts.manager_calls == 2 &&
                counts.auxiliary_calls == 512 && counts.constructions == 528,
        "literal and exhaustive call totals differ");
    require(counts.constructions == counts.destructions, "generated item lifetime imbalance");
    require(screen_manager == nullptr, "manager global was not restored");
    std::printf("reconstructed screen activation: %u cases, %u activation calls, "
                "%u manager calls, %u auxiliary calls, %u item lifetimes, "
                "%llu checks, %llu byte checks; manager %zu, auxiliary %zu\n",
        counts.cases,
        counts.activation_calls,
        counts.manager_calls,
        counts.auxiliary_calls,
        counts.destructions,
        counts.checks,
        counts.byte_checks,
        sizeof(ScreenManager),
        sizeof(ScreenManagerAuxiliaryState));
}
