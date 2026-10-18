#include "startup_screen_renderer.h"
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

static_assert(sizeof(SurfaceResult) == 4 && INT_MAX == 2147483647);
static_assert(std::is_standard_layout_v<ScreenRequestRenderer>);
static_assert(std::is_standard_layout_v<ScreenSurfacePosition>);

template <class T> struct Guarded
{
    unsigned char before[32];
    T value;
    unsigned char after[32];
};

struct Table
{
    ScreenSurfacePosition* slots[4];
};

struct Pixels
{
    unsigned short words[17];
};

struct Fixture
{
    Guarded<ScreenRequestRenderer> renderer;
    Guarded<ScreenSurfacePosition> entries[3];
    Guarded<DirectDrawSurface7Prefix> devices[3];
    Guarded<Table> tables[2];
    Guarded<Pixels> pixels[3];
};

using State = std::array<unsigned char, sizeof(Fixture)>;

struct Scenario
{
    unsigned int gate;
    unsigned int prepared;
    unsigned int locked;
    unsigned int slot;
    unsigned int base;
    unsigned int alias;
    unsigned int provider;
    unsigned int mutation;
    unsigned int output;
    bool write_output;
    SurfaceResult result;
};

struct Context
{
    Fixture* fixture;
    ScreenSurfacePosition* selected;
    DirectDrawSurface7Prefix* receiver;
    Scenario scenario;
    State before_provider;
    State after_provider;
    unsigned short* output;
    unsigned int calls;
    bool failed;
};

namespace
{
Context* active;
std::size_t cases;
std::size_t provider_calls;
std::size_t byte_checks;

State snapshot(const Fixture& value)
{
    State result;
    std::memcpy(result.data(), &value, result.size());
    return result;
}

bool equal_state(const Fixture& fixture, const State& expected)
{
    byte_checks += expected.size();
    return std::memcmp(&fixture, expected.data(), expected.size()) == 0;
}

template <class T>
void expect_write(State& state, const Fixture& fixture, const T& field, const T& value)
{
    const auto offset = reinterpret_cast<const unsigned char*>(&field) -
                        reinterpret_cast<const unsigned char*>(&fixture);
    std::memcpy(state.data() + offset, &value, sizeof(value));
}

template <std::size_t N> void fill(unsigned char (&bytes)[N], unsigned int seed)
{
    for (std::size_t n = 0; n < N; ++n)
    {
        bytes[n] = static_cast<unsigned char>(seed + n * 37U);
    }
}

template <class T> void fill_guards(Guarded<T>& value, unsigned int seed)
{
    fill(value.before, seed);
    fill(value.after, seed ^ 0xd5U);
}

SurfaceResult lock_provider(unsigned int provider,
    DirectDrawSurface7Prefix* receiver,
    SurfaceApiRectangle* rectangle,
    SurfaceDescription* description,
    unsigned int flags,
    void* event);

SurfaceResult SURFACE_STDCALL lock_a(DirectDrawSurface7Prefix* receiver,
    SurfaceApiRectangle* rectangle,
    SurfaceDescription* description,
    unsigned int flags,
    void* event)
{
    return lock_provider(0, receiver, rectangle, description, flags, event);
}

SurfaceResult SURFACE_STDCALL lock_b(DirectDrawSurface7Prefix* receiver,
    SurfaceApiRectangle* rectangle,
    SurfaceDescription* description,
    unsigned int flags,
    void* event)
{
    return lock_provider(1, receiver, rectangle, description, flags, event);
}

const DirectDrawSurface7VtablePrefix table_a = []
{
    DirectDrawSurface7VtablePrefix result{};
    result.Lock = lock_a;
    return result;
}();
const DirectDrawSurface7VtablePrefix table_b = []
{
    DirectDrawSurface7VtablePrefix result{};
    result.Lock = lock_b;
    return result;
}();

SurfaceResult lock_provider(unsigned int provider,
    DirectDrawSurface7Prefix* receiver,
    SurfaceApiRectangle* rectangle,
    SurfaceDescription* description,
    unsigned int flags,
    void* event)
{
    Context& context = *active;
    Fixture& fixture = *context.fixture;
    ScreenRequestRenderer& renderer = fixture.renderer.value;
    ScreenSurfacePosition& selected = *context.selected;
    const Scenario& scenario = context.scenario;
    ++context.calls;
    ++provider_calls;
    std::array<unsigned char, sizeof(SurfaceDescription)> descriptor_expected{};
    const unsigned int size = sizeof(SurfaceDescription);
    std::memcpy(
        descriptor_expected.data() + offsetof(SurfaceDescription, size), &size, sizeof(size));
    if (context.calls != 1 || provider != scenario.provider || receiver != context.receiver ||
        rectangle != nullptr || event != nullptr || flags != 0 || description == nullptr ||
        !equal_state(fixture, context.before_provider) ||
        std::memcmp(description, descriptor_expected.data(), descriptor_expected.size()) != 0)
    {
        context.failed = true;
        return -1;
    }
    byte_checks += descriptor_expected.size();

    // Explicit platform-provider mutation recipe. No reconstructed method is replaced.
    const unsigned int mask = scenario.mutation;
    if (mask & 1U)
    {
        renderer.current_surface =
            static_cast<int>((scenario.slot + 1U) % 4U) - static_cast<int>(scenario.base);
    }
    if (mask & 2U)
    {
        renderer.surfaces = fixture.tables[1].value.slots + scenario.base;
    }
    if (mask & 4U)
    {
        renderer.software_mode = 77;
    }
    if (mask & 8U)
    {
        renderer.pixels_locked = 0xa3;
    }
    if (mask & 16U)
    {
        selected.prepared = 0xd2;
    }
    if (mask & 32U)
    {
        selected.pixels = fixture.pixels[2].value.words + 7;
    }
    if (mask & 64U)
    {
        selected.surface = &fixture.devices[2].value;
    }
    if (mask & 128U)
    {
        selected.width = 0xa187239dU;
        selected.height = 0x327815dcU;
        selected.pitch = 0xffffffe0U;
    }
    if (mask & 256U)
    {
        fixture.tables[0].value.slots[scenario.slot] = &fixture.entries[2].value;
    }
    if (mask & 512U)
    {
        receiver->vtable = scenario.provider ? &table_a : &table_b;
        selected.active = 0xcc;
    }
    description->flags = 0xfdef3201U;
    description->height = 999;
    description->width = 31;
    description->pitch = -32;
    fill(description->unknown_14, 0x2d);
    fill(description->unknown_28, 0x96);
    description->caps = 0x13579bdfU;
    fill(description->unknown_6c, 0x85);
    if (scenario.write_output)
    {
        description->pixels = context.output;
    }
    if (!equal_state(fixture, context.after_provider))
    {
        context.failed = true;
    }
    return scenario.result;
}

bool run(const Scenario& scenario)
{
    // All pointer and signed fields start with typed values. Only opaque arrays and
    // canaries receive byte patterns; no arbitrary bytes become pointer objects.
    Fixture fixture{};
    fill_guards(fixture.renderer, 0x31);
    ScreenRequestRenderer& renderer = fixture.renderer.value;
    renderer.software_mode = static_cast<unsigned char>(scenario.gate);
    fill(renderer.unknown_01, 17);
    renderer.width = 640;
    renderer.height = 480;
    fill(renderer.unknown_0c, 0x23);
    fill(renderer.unknown_6c, 0x93);
    renderer.current_surface = static_cast<int>(scenario.slot) - static_cast<int>(scenario.base);
    renderer.pixels_locked = static_cast<unsigned char>(scenario.locked);
    for (unsigned int n = 0; n < 3; ++n)
    {
        fill_guards(fixture.entries[n], n + 0x56);
        fill_guards(fixture.devices[n], n + 0x37);
        fill_guards(fixture.pixels[n], n + 0x88);
        ScreenSurfacePosition& entry = fixture.entries[n].value;
        entry.active = static_cast<unsigned char>(n + 0x27);
        entry.software = static_cast<unsigned char>(n + 0xb7);
        fill(entry.unknown_02, n + 0x16);
        entry.pixels =
            scenario.output == 0 ? nullptr : fixture.pixels[n].value.words + scenario.output;
        entry.surface = &fixture.devices[n].value;
        fill(entry.unknown_0c, n + 0xa3);
        entry.width = 100U + n;
        entry.height = 200U + n;
        entry.pitch = 300U + n;
        entry.position.x = -17 + static_cast<int>(n);
        entry.position.y = 23 - static_cast<int>(n);
        entry.prepared = static_cast<unsigned char>(scenario.prepared + n);
        fixture.devices[n].value.vtable = scenario.provider ? &table_b : &table_a;
        for (unsigned int word = 0; word < 17; ++word)
        {
            fixture.pixels[n].value.words[word] = static_cast<unsigned short>(word * 257U + n);
        }
    }
    for (unsigned int table = 0; table < 2; ++table)
    {
        fill_guards(fixture.tables[table], 0xa8 + table);
        for (unsigned int slot = 0; slot < 4; ++slot)
        {
            const unsigned int index =
                scenario.alias == 3 ? 0 : (slot + table + scenario.alias) % 3U;
            fixture.tables[table].value.slots[slot] = &fixture.entries[index].value;
        }
    }
    renderer.surfaces = fixture.tables[0].value.slots + scenario.base;
    ScreenSurfacePosition& selected = *fixture.tables[0].value.slots[scenario.slot];
    DirectDrawSurface7Prefix* const receiver = selected.surface;
    unsigned short* const software_pixels = selected.pixels;
    unsigned short* output = nullptr;
    if (scenario.output == 1)
    {
        output = fixture.pixels[0].value.words;
    }
    else if (scenario.output == 2)
    {
        output = fixture.pixels[1].value.words + 3;
    }
    else if (scenario.output == 3)
    {
        output = fixture.pixels[2].value.words + 17; // Valid one-past identity; never dereferenced.
    }

    Context context{
        &fixture, &selected, receiver, scenario, snapshot(fixture), {}, output, 0, false};
    expect_write(
        context.before_provider, fixture, selected.prepared, static_cast<unsigned char>(0));
    context.after_provider = context.before_provider;
    if (scenario.gate == 0)
    {
        // Independent byte-level expected writes for the documented provider recipe.
        State& state = context.after_provider;
        const unsigned int mask = scenario.mutation;
        if (mask & 1U)
        {
            expect_write(state,
                fixture,
                renderer.current_surface,
                static_cast<int>((scenario.slot + 1U) % 4U) - static_cast<int>(scenario.base));
        }
        if (mask & 2U)
        {
            expect_write(
                state, fixture, renderer.surfaces, fixture.tables[1].value.slots + scenario.base);
        }
        if (mask & 4U)
        {
            expect_write(state, fixture, renderer.software_mode, static_cast<unsigned char>(77));
        }
        if (mask & 8U)
        {
            expect_write(state, fixture, renderer.pixels_locked, static_cast<unsigned char>(0xa3));
        }
        if (mask & 16U)
        {
            expect_write(state, fixture, selected.prepared, static_cast<unsigned char>(0xd2));
        }
        if (mask & 32U)
        {
            expect_write(state, fixture, selected.pixels, fixture.pixels[2].value.words + 7);
        }
        if (mask & 64U)
        {
            expect_write(state, fixture, selected.surface, &fixture.devices[2].value);
        }
        if (mask & 128U)
        {
            expect_write(state, fixture, selected.width, 0xa187239dU);
            expect_write(state, fixture, selected.height, 0x327815dcU);
            expect_write(state, fixture, selected.pitch, 0xffffffe0U);
        }
        if (mask & 256U)
        {
            expect_write(state,
                fixture,
                fixture.tables[0].value.slots[scenario.slot],
                &fixture.entries[2].value);
        }
        if (mask & 512U)
        {
            expect_write(state, fixture, receiver->vtable, scenario.provider ? &table_a : &table_b);
            expect_write(state, fixture, selected.active, static_cast<unsigned char>(0xcc));
        }
    }
    State expected_final = context.after_provider;
    const bool success = (static_cast<std::uint32_t>(scenario.result) & 0x80000000U) == 0;
    unsigned short* const expected_result = scenario.gate                      ? software_pixels
                                            : success && scenario.write_output ? output
                                                                               : nullptr;
    if (scenario.gate || success)
    {
        expect_write(
            expected_final, fixture, renderer.pixels_locked, static_cast<unsigned char>(1));
    }
    active = &context;
    unsigned short* const actual = renderer.AccessPixels();
    active = nullptr;
    const bool state_matches = equal_state(fixture, expected_final);
    if (context.failed || context.calls != (scenario.gate ? 0U : 1U) || actual != expected_result ||
        !state_matches)
    {
        const char* const reason = context.failed ? "provider state or arguments"
                                   : context.calls != (scenario.gate ? 0U : 1U) ? "call count"
                                   : actual != expected_result                  ? "pixel result"
                                                                                : "write set";
        std::fprintf(stderr,
            "case %zu failed: gate=%u result=%d mask=%u calls=%u: %s\n",
            cases,
            scenario.gate,
            scenario.result,
            scenario.mutation,
            context.calls,
            reason);
        return false;
    }
    ++cases;
    return true;
}
} // namespace

int main()
{
    if (!run({0, 0xfe, 0xd3, 0, 0, 0, 0, 0, 1, true, 0}))
    {
        return 2;
    }
    for (unsigned int gate = 0; gate < 256; ++gate)
    {
        for (unsigned int variant = 0; variant < 32; ++variant)
        {
            if (!run({gate,
                    gate,
                    255U - gate,
                    variant % 4U,
                    variant & 2U,
                    (variant >> 2) % 4U,
                    variant & 1U,
                    (variant * 31U) % 1024U,
                    (variant >> 3) % 4U,
                    (variant & 4U) != 0,
                    -1}))
            {
                return 2;
            }
        }
    }
    const int results[] = {0, 1, INT_MAX, -1, INT_MIN, -2005532292};
    for (unsigned int mask = 0; mask < 1024; ++mask)
    {
        for (int result : results)
        {
            for (unsigned int output = 0; output < 4; ++output)
            {
                for (unsigned int write = 0; write < 2; ++write)
                {
                    if (!run({0,
                            mask % 256U,
                            (mask * 19U) % 256U,
                            mask % 4U,
                            mask & 2U,
                            (mask >> 2) % 4U,
                            mask & 1U,
                            mask,
                            output,
                            write != 0,
                            result}))
                    {
                        return 2;
                    }
                }
            }
        }
    }
    std::printf(
        "cases=%zu provider_calls=%zu byte_checks=%zu\n", cases, provider_calls, byte_checks);
    return 0;
}
