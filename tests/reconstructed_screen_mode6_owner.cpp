#include "startup_screen_manager.h"
#include "startup_screen_modes.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <typeinfo>

static_assert(CHAR_BIT == 8 && sizeof(unsigned int) == 4 && sizeof(float) == 4);
static_assert(std::is_aggregate_v<ScreenManager> && !std::is_polymorphic_v<ScreenManager>);
static_assert(std::is_abstract_v<ScreenModeController>);
static_assert(std::has_virtual_destructor_v<ScreenModeController>);
static_assert(std::is_base_of_v<ScreenModeController, ScreenMode6Controller>);
static_assert(sizeof(ScreenMode6Controller) == sizeof(ScreenModeController));

namespace
{
struct Failure
{
    const char* message;
};

struct Counters
{
    std::size_t checks = 0;
    std::size_t dispatch_sets = 0;
    std::size_t virtual_calls = 0;
    std::size_t constructions = 0;
    std::size_t destructions = 0;
    unsigned int groups = 0;
} counters;

void require(bool condition, const char* message)
{
    ++counters.checks;
    if (!condition)
    {
        // Fixture failure is raised only after retail calls return. Unwinding
        // destroys each genuine live controller, including the safe control.
        throw Failure{message};
    }
}

using Guard = std::array<unsigned char, 16>;

bool guard_is(const Guard& guard, unsigned char value)
{
    return std::all_of(guard.begin(),
        guard.end(),
        [value](auto byte)
        {
            return byte == value;
        });
}

template <class T> struct Guarded
{
    Guard before{};
    T value{};
    Guard after{};

    Guarded()
    {
        before.fill(0x36);
        after.fill(0xc9);
    }

    bool intact() const
    {
        return guard_is(before, 0x36) && guard_is(after, 0xc9);
    }
};

bool same_manager(const ScreenManager& a, const ScreenManager& b)
{
    // Compare declared members only: no ABI padding or opaque full-object
    // representation. No retail manager lifetime or callback is supplied.
    return a.renderer == b.renderer &&
           std::equal(std::begin(a.fonts_04), std::end(a.fonts_04), std::begin(b.fonts_04)) &&
           std::equal(std::begin(a.fonts_2c), std::end(a.fonts_2c), std::begin(b.fonts_2c)) &&
           std::equal(std::begin(a.unknown_54), std::end(a.unknown_54), std::begin(b.unknown_54)) &&
           a.registry == b.registry && a.item_90 == b.item_90 &&
           std::equal(std::begin(a.unknown_94), std::end(a.unknown_94), std::begin(b.unknown_94)) &&
           a.active.head == b.active.head &&
           std::equal(std::begin(a.unknown_c0), std::end(a.unknown_c0), std::begin(b.unknown_c0)) &&
           a.mode == b.mode && a.previous_mode == b.previous_mode &&
           a.auxiliary_130 == b.auxiliary_130 && a.item_134 == b.item_134 &&
           std::bit_cast<std::uint32_t>(a.value_138) == std::bit_cast<std::uint32_t>(b.value_138);
}

struct Borrowers
{
    // Actual current declaration, live as a C++ aggregate read model. Mode6
    // borrows its identity only; this does not construct a retail ScreenManager.
    Guarded<ScreenManager> first;
    Guarded<ScreenManager> second;
    CircularListNode first_head{};
    CircularListNode second_head{};
    ScreenManager first_before;
    ScreenManager second_before;

    Borrowers()
    {
        first_head.next = first_head.previous = &first_head;
        second_head.next = second_head.previous = &second_head;
        first.value.active.head = &first_head;
        second.value.active.head = &second_head;
        first.value.mode = 6;
        first.value.previous_mode = -7;
        first.value.value_138 = -0.25F;
        second.value.mode = 10;
        second.value.previous_mode = INT_MAX;
        second.value.value_138 = 1.5F;
        std::fill(std::begin(first.value.unknown_54), std::end(first.value.unknown_54), 0x23);
        std::fill(std::begin(first.value.unknown_94), std::end(first.value.unknown_94), 0x45);
        std::fill(std::begin(first.value.unknown_c0), std::end(first.value.unknown_c0), 0x67);
        std::fill(std::begin(second.value.unknown_54), std::end(second.value.unknown_54), 0x89);
        std::fill(std::begin(second.value.unknown_94), std::end(second.value.unknown_94), 0xab);
        std::fill(std::begin(second.value.unknown_c0), std::end(second.value.unknown_c0), 0xcd);
        first_before = first.value;
        second_before = second.value;
    }

    void check() const
    {
        require(first.intact() && second.intact(), "borrowed manager guard changed");
        require(
            same_manager(first.value, first_before) && same_manager(second.value, second_before),
            "borrowed manager state changed");
        require(first_head.next == &first_head && first_head.previous == &first_head &&
                    second_head.next == &second_head && second_head.previous == &second_head,
            "borrowed sentinel changed");
    }
};

struct Arguments
{
    float elapsed;
    std::array<unsigned int, 4> words;
};

// Finite float values only. Signed-boundary word patterns are passed as the
// genuine unsigned words; these idle methods neither interpret nor dereference
// them. No arithmetic on extreme float/word values is required.
constexpr std::array<Arguments, 7> arguments{{
    {0.0F, {0, 0, 0, 0}},
    {1.0F / 60.0F, {1, 0x7fffffffU, 0x80000000U, 0xffffffffU}},
    {-1.0F, {0xffffffffU, 0, 0x80000000U, 0x7fffffffU}},
    {std::numeric_limits<float>::max(), {17, 23, 31, 47}},
    {-std::numeric_limits<float>::max(), {0xaaaaaaaaU, 0x55555555U, 0, 1}},
    {-0.0F, {0x80000000U, 0x80000000U, 0xffffffffU, 0xffffffffU}},
    {std::numeric_limits<float>::min(), {5, 4, 3, 2}},
}};

void exercise(ScreenModeController& value,
    ScreenManager* expected,
    const Arguments& input,
    const Borrowers& owners)
{
    const Arguments before = input;
    const auto check = [&]()
    {
        require(value.manager == expected, "controller lost borrowed manager identity");
        require(typeid(value) == typeid(ScreenMode6Controller), "live dynamic type is not Mode6");
        require(input.words == before.words && std::bit_cast<std::uint32_t>(input.elapsed) ==
                                                   std::bit_cast<std::uint32_t>(before.elapsed),
            "caller argument storage changed");
        owners.check();
    };

    check();
    value.Enter();
    ++counters.virtual_calls;
    check();
    value.Update(input.elapsed);
    ++counters.virtual_calls;
    check();
    value.RenderScreens();
    ++counters.virtual_calls;
    check();
    value.RenderPointer();
    ++counters.virtual_calls;
    check();
    const unsigned char three =
        value.HandleThreeWords(input.words[0], input.words[1], input.words[2]);
    ++counters.virtual_calls;
    require(three == 0, "idle three-word input returned true");
    check();
    const unsigned char four =
        value.HandleFourWords(input.words[0], input.words[1], input.words[2], input.words[3]);
    ++counters.virtual_calls;
    require(four == 0, "idle four-word input returned true");
    check();
    value.Exit();
    ++counters.virtual_calls;
    check();
    ++counters.dispatch_sets;
}

// An opaque fixture call keeps exercise's receiver dynamic even in the
// optimized build. There is no retail method/provider replacement here.
using Dispatch = void (*)(
    ScreenModeController&, ScreenManager*, const Arguments&, const Borrowers&);
Dispatch volatile dispatch = &exercise;

struct PlacementSlot
{
    Guard before{};
    alignas(ScreenMode6Controller) std::array<std::byte, sizeof(ScreenMode6Controller)> storage;
    Guard after{};
    ScreenMode6Controller* live = nullptr;

    PlacementSlot()
    {
        before.fill(0x36);
        after.fill(0xc9);
        // No bytes inside the controller's storage are patterned. Mode6 has
        // no opaque members, and no vptr/padding representation is asserted.
    }

    ~PlacementSlot()
    {
        destroy();
    }

    ScreenMode6Controller& construct(ScreenManager* owner)
    {
        require(!live, "placement slot already has a live object");
        require(
            reinterpret_cast<std::uintptr_t>(storage.data()) % alignof(ScreenMode6Controller) == 0,
            "placement slot alignment differs");
        require(reinterpret_cast<const unsigned char*>(storage.data()) ==
                        before.data() + before.size() &&
                    after.data() ==
                        reinterpret_cast<const unsigned char*>(storage.data()) + storage.size(),
            "placement guard extent differs");
        live = ::new (static_cast<void*>(storage.data())) ScreenMode6Controller(owner);
        ++counters.constructions;
        return *live;
    }

    void destroy()
    {
        if (live)
        {
            std::destroy_at(live);
            live = nullptr;
            ++counters.destructions;
        }
    }

    void check() const
    {
        require(guard_is(before, 0x36) && guard_is(after, 0xc9), "placement outside guard changed");
    }
};

void run()
{
    Borrowers owners;
    // This genuine allocation is already live when the changed-source control
    // is detected in the stack group, so its unique_ptr must also unwind.
    std::unique_ptr<ScreenModeController> heap(new ScreenMode6Controller(&owners.second.value));
    ++counters.constructions;
    {
        ScreenMode6Controller value(&owners.first.value);
        ++counters.constructions;
        require(
            reinterpret_cast<const char*>(&value.manager) - reinterpret_cast<const char*>(&value) ==
                static_cast<std::ptrdiff_t>(sizeof(void*)),
            "native manager member offset differs");
        for (std::size_t index = 0; index < 3; ++index)
        {
            dispatch(value, &owners.first.value, arguments[index], owners);
        }
    }
    ++counters.destructions;
    owners.check();
    ++counters.groups;

    {
        // Real host scalar new/delete and actual compiler virtual deletion.
        // Both ordinary destructors come from the genuine source archive.
        dispatch(*heap, &owners.second.value, arguments[3], owners);
        dispatch(*heap, &owners.second.value, arguments[4], owners);
        heap.reset();
        ++counters.destructions;
    }
    owners.check();
    ++counters.groups;

    {
        PlacementSlot slot;
        auto& first = slot.construct(&owners.first.value);
        dispatch(first, &owners.first.value, arguments[5], owners);
        slot.check();
        slot.destroy();
        slot.check();
        owners.check();
        // Begin a new genuine lifetime in the same storage; never inspect an
        // ended object's members or its old virtual pointer.
        auto& second = slot.construct(&owners.second.value);
        dispatch(second, &owners.second.value, arguments[6], owners);
        slot.check();
        slot.destroy();
        slot.check();
        owners.check();
    }
    ++counters.groups;
    require(counters.constructions == 4 && counters.destructions == 4,
        "fixture lifetime accounting differs");
}
} // namespace

int main()
{
    try
    {
        run();
        std::printf("{\"groups\":%u,\"dispatch_sets\":%zu,\"virtual_calls\":%zu,"
                    "\"constructions\":%zu,\"destructions\":%zu,\"checks\":%zu,"
                    "\"mode_bytes\":%zu,\"base_bytes\":%zu}\n",
            counters.groups,
            counters.dispatch_sets,
            counters.virtual_calls,
            counters.constructions,
            counters.destructions,
            counters.checks,
            sizeof(ScreenMode6Controller),
            sizeof(ScreenModeController));
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "screen-mode6: %s\n", failure.message);
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "screen-mode6 host failure: %s\n", error.what());
        return 3;
    }
}
