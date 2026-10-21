#include "startup_frame_clock.h"
#include "startup_input_motion.h"

#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>

namespace
{
static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && sizeof(unsigned int) == 4);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(std::is_standard_layout_v<FrameClock> && !std::is_polymorphic_v<FrameClock>);
static_assert(sizeof(FrameClock) == 40);
static_assert(offsetof(FrameClock, start_ticks) == 0 && offsetof(FrameClock, sampled_frames) == 4);
static_assert(offsetof(FrameClock, frames) == 8 && offsetof(FrameClock, previous_frames) == 12);
static_assert(offsetof(FrameClock, seconds) == 16 && offsetof(FrameClock, previous_seconds) == 20);
static_assert(
    offsetof(FrameClock, delta_seconds) == 24 && offsetof(FrameClock, sample_seconds) == 28);
static_assert(offsetof(FrameClock, value_20) == 32 && offsetof(FrameClock, value_24) == 36);

struct Failure
{
    const char* message;
};

struct Counts
{
    unsigned int groups = 0;
    unsigned int constructions = 0;
    unsigned int destructions = 0;
    unsigned int updates = 0;
    unsigned int checks = 0;
} counts;

void require(bool condition, const char* message)
{
    ++counts.checks;
    if (!condition)
    {
        throw Failure{message};
    }
}

using Guard = std::array<unsigned char, 16>;
Guard guard(unsigned char first)
{
    Guard result{};
    for (std::size_t index = 0; index < result.size(); ++index)
    {
        result[index] = static_cast<unsigned char>(first + index * 7U);
    }
    return result;
}

// The holder never copies a FrameClock or reads its uninitialized sample word.
// Its destructor also runs after a deliberate assertion failure.
struct Owner
{
    Guard before = guard(0x19);
    std::optional<FrameClock> value;
    Guard after = guard(0x91);

    explicit Owner(int ticks)
    {
        value.emplace(ticks);
        ++counts.constructions;
    }
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
    ~Owner()
    {
        close();
    }
    void close()
    {
        if (value)
        {
            value.reset();
            ++counts.destructions;
        }
    }
    void check_guards() const
    {
        require(before == guard(0x19) && after == guard(0x91), "outside owner guards changed");
    }
};

void initialized(const FrameClock& clock, int ticks)
{
    require(clock.frames == 0, "constructor frame count differs");
    require(clock.start_ticks == ticks && clock.previous_frames == 0,
        "constructor initialized integers differ");
    require(clock.seconds == 0.0f && clock.previous_seconds == 0.0f &&
                clock.delta_seconds == 0.0f && clock.sample_seconds == 0.0f,
        "constructor initialized floating members differ");
    require(clock.value_20 == 0 && clock.value_24 == 0, "constructor tail words differ");
    // sampled_frames at +4 is intentionally not read or compared here.
}

void close_to(float value, float expected)
{
    require(std::isfinite(value) && std::fabs(value - expected) < 0.000001f,
        "finite update value differs");
}

void construction_and_scalar_lifetime()
{
    ++counts.groups;
    require(startup_frame_clock == nullptr, "unexpected initial clock publication");
    {
        Owner stack(-19);
        require(startup_frame_clock == &*stack.value, "constructor did not publish its owner");
        initialized(*stack.value, -19);
        stack.check_guards();
        stack.close();
        require(startup_frame_clock == nullptr, "ordinary destructor did not clear registration");
        stack.check_guards();
    }
    {
        auto heap = std::make_unique<FrameClock>(123456);
        ++counts.constructions;
        require(startup_frame_clock == heap.get(), "heap constructor publication differs");
        initialized(*heap, 123456);
        heap.reset();
        ++counts.destructions;
        require(
            startup_frame_clock == nullptr, "scalar owner destruction did not clear registration");
    }
}

void defined_update_path()
{
    ++counts.groups;
    Owner owner(1000);
    auto& clock = *owner.value;
    initialized(clock, 1000);
    // Tick differences fit signed int and stay away from the x87 1-second
    // comparison boundary. The first call writes sampled_frames before reading it.
    clock.Update(2500);
    ++counts.updates;
    require(clock.frames == 1 && clock.previous_frames == 1 && clock.sampled_frames == 1,
        "first defined sample differs");
    close_to(clock.seconds, 1.5f);
    close_to(clock.delta_seconds, 1.5f);
    require(clock.previous_seconds == 0.0f && clock.sample_seconds == 1.0f,
        "first sampling state differs");
    clock.Update(2750);
    ++counts.updates;
    require(clock.frames == 2 && clock.previous_frames == 1 && clock.sampled_frames == 1,
        "non-sampling update changed the defined sample");
    close_to(clock.seconds, 1.75f);
    close_to(clock.previous_seconds, 1.5f);
    close_to(clock.delta_seconds, 0.25f);
    clock.Update(4500);
    ++counts.updates;
    require(clock.frames == 3 && clock.previous_frames == 3 && clock.sampled_frames == 2 &&
                clock.sample_seconds == 2.0f,
        "single-step sampling accumulation differs");
    close_to(clock.seconds, 3.5f);
    close_to(clock.previous_seconds, 1.75f);
    close_to(clock.delta_seconds, 1.75f);
    require(clock.start_ticks == 1000 && clock.value_20 == 0 && clock.value_24 == 0 &&
                startup_frame_clock == &clock,
        "update changed unrelated initialized state");
    owner.check_guards();
    owner.close();
    require(startup_frame_clock == nullptr, "updated owner destruction did not clear registration");
    owner.check_guards();
}

void older_owner_clears_younger_registration()
{
    ++counts.groups;
    Owner older(17);
    Owner younger(29);
    require(startup_frame_clock == &*younger.value, "later owner was not published");
    initialized(*older.value, 17);
    initialized(*younger.value, 29);
    older.close();
    require(startup_frame_clock == nullptr && younger.value.has_value(),
        "older destruction did not unconditionally clear the younger registration");
    initialized(*younger.value, 29);
    older.check_guards();
    younger.check_guards();
    younger.close();
    require(startup_frame_clock == nullptr, "younger destruction changed null registration");
    younger.check_guards();
}
} // namespace

int main()
{
    try
    {
        require(startup_input_state == nullptr && startup_frame_clock == nullptr,
            "real global initial state differs");
        construction_and_scalar_lifetime();
        defined_update_path();
        older_owner_clears_younger_registration();
        require(counts.groups == 3 && counts.constructions == 5 && counts.destructions == 5 &&
                    counts.updates == 3,
            "actual owner or update count differs");
        require(startup_input_state == nullptr && startup_frame_clock == nullptr,
            "final real global state differs");
        std::printf("frame-clock: {\"groups\":%u,\"constructions\":%u,\"destructions\":%u,"
                    "\"updates\":%u,\"checks\":%u,\"clock_bytes\":%zu}\n",
            counts.groups,
            counts.constructions,
            counts.destructions,
            counts.updates,
            counts.checks,
            sizeof(FrameClock));
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "frame-clock: %s\n", failure.message);
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "frame-clock host failure: %s\n", error.what());
        return 3;
    }
}
