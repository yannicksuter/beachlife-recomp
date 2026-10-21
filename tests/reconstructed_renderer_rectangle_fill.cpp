#include "renderer_queue_test_state.h"
#include "startup_screen_item.h"
#include "startup_screen_renderer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <vector>

static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(sizeof(int) == 4 && INT_MAX == 2147483647 && sizeof(SurfaceResult) == 4);
static_assert(std::endian::native == std::endian::little);
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Lock) == 25 * sizeof(void*));
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Unlock) == 32 * sizeof(void*));

namespace
{
struct RendererFixtureFailure
{
};

enum class Route
{
    Direct,
    Software,
    Hardware
};

enum class Mutation
{
    None,
    Reselect,
    Software,
    Failure
};

struct Scenario
{
    const char* name = "";
    ScreenRectangle bounds{1, 1, 4, 3};
    Route route = Route::Direct;
    Mutation mutation = Mutation::None;
    RenderRectangle clip{0, 0, 6, 5};
    ScreenPoint position{0, 0};
    unsigned int pitch = 12;
    unsigned int colour = 0xfedc5678;
    bool null_pixels = false;
    bool change_bounds = false;
    SurfaceResult lock_result = 0;
    unsigned char expected_locked = 0x7b;
    unsigned int expected_locks = 0;
    unsigned int expected_unlocks = 0;
    std::vector<std::size_t> direct_slots;
    std::vector<std::size_t> dense_slots;
};

// Literal halfword indices from the accepted rectangle plans. No reference
// clipping, fill loop, game method or game allocator is supplied by this test.
const std::array<Scenario, 22> scenarios = []
{
    std::array<Scenario, 22> s{};
    s[0].name = "preserve upper halfword";
    s[0].bounds = {1, 1, 2, 2};
    s[0].direct_slots = {14};
    s[1].name = "direct centre";
    s[1].direct_slots = {14, 16, 18, 26, 28, 30};
    s[2].name = "negative left and top crop";
    s[2].bounds = {-2, -1, 2, 2};
    s[2].direct_slots = {0, 2, 12, 14};
    s[3].name = "right and bottom crop";
    s[3].bounds = {4, 3, 9, 8};
    s[3].direct_slots = {44, 46, 56, 58};
    s[4] = s[1];
    s[4].name = "full buffer bounds ignore clip rectangle";
    s[4].clip = {-99, 99, -7, -8};
    s[5].name = "zero width direct null unused";
    s[5].bounds = {6, 0, 6, 2};
    s[5].null_pixels = true;
    s[6].name = "zero height direct";
    s[6].bounds = {1, 1, 3, 1};
    s[7].name = "negative direct width and height";
    s[7].bounds = {5, 4, 2, 2};

    s[8].name = "dense odd width and odd byte pitch";
    s[8].route = Route::Software;
    s[8].bounds = {2, 1, 5, 3};
    s[8].position = {1, 0};
    s[8].pitch = 11;
    s[8].expected_locked = 0;
    s[8].dense_slots = {6, 7, 8, 11, 12, 13};
    s[9].name = "dense even pairs";
    s[9].route = Route::Software;
    s[9].bounds = {1, 1, 5, 2};
    s[9].expected_locked = 0;
    s[9].dense_slots = {7, 8, 9, 10};
    s[10].name = "dense single odd tail";
    s[10].route = Route::Software;
    s[10].bounds = {0, 0, 1, 1};
    s[10].pitch = 10;
    s[10].expected_locked = 0;
    s[10].dense_slots = {0};
    s[11].name = "empty hardware span still unlocks";
    s[11].route = Route::Hardware;
    s[11].bounds = {1, 1, 3, 1};
    s[11].expected_locked = 0;
    s[11].expected_locks = 1;
    s[11].expected_unlocks = 1;
    s[12].name = "software null keeps access lock";
    s[12].route = Route::Software;
    s[12].bounds = {1, 1, 3, 3};
    s[12].null_pixels = true;
    s[12].expected_locked = 1;
    s[13] = s[12];
    s[13].name = "successful hardware null keeps access lock";
    s[13].route = Route::Hardware;
    s[13].expected_locks = 1;
    s[14].name = "failed lock uses actual error sink";
    s[14].route = Route::Hardware;
    s[14].bounds = {1, 1, 3, 3};
    s[14].lock_result = -1;
    s[14].expected_locks = 1;
    s[15].name = "live entry position pitch and unlock receiver";
    s[15].route = Route::Hardware;
    s[15].mutation = Mutation::Reselect;
    s[15].bounds = {2, 1, 5, 3};
    s[15].expected_locked = 0;
    s[15].expected_locks = 1;
    s[15].expected_unlocks = 1;
    s[15].dense_slots = {8, 9, 10, 15, 16, 17};
    s[16] = s[15];
    s[16].name = "post-lock software route skips unlock";
    s[16].mutation = Mutation::Software;
    s[16].expected_unlocks = 0;
    s[17] = s[11];
    s[17].name = "negative dense span still cleans up";
    s[17].bounds = {5, 4, 2, 2};

    s[18] = s[8];
    s[18].name = "positive nonzero HRESULT succeeds";
    s[18].route = Route::Hardware;
    s[18].lock_result = 37;
    s[18].expected_locks = 1;
    s[18].expected_unlocks = 1;
    s[19] = s[15];
    s[19].name = "pre-lock clamp and input values stay captured";
    s[19].bounds = {4, 3, 9, 8};
    s[19].change_bounds = true;
    s[19].dense_slots = {24, 25, 31, 32};
    s[20] = s[14];
    s[20].name = "failed lock preserves platform state mutation";
    s[20].mutation = Mutation::Failure;
    s[20].lock_result = INT_MIN;
    s[20].expected_locked = 0x66;
    s[21] = s[11];
    s[21].name = "zero width positive height still unlocks";
    s[21].bounds = {6, 0, 6, 2};
    s[21].position = {6, 0};
    s[21].pitch = 0;
    return s;
}();

template <class T> struct Guarded
{
    std::array<unsigned char, 16> before{};
    T value{};
    std::array<unsigned char, 16> after{};
};

// Genuine renderer ownership; only method inputs are borrowed.
template <> struct Guarded<ScreenRequestRenderer>
{
    renderer_owner_test::Bootstrap renderer_bootstrap;
    std::array<unsigned char, 16> before{};
    ScreenRequestRenderer value{renderer_bootstrap.video(), 8, 4, 1};
    std::array<unsigned char, 16> after{};
    renderer_owner_test::RestoreOwnedState renderer_restore{value, renderer_bootstrap};
};
using Words = std::array<unsigned short, 128>;
using Bank = Guarded<Words>;
using Table = Guarded<std::array<ScreenSurfacePosition*, 3>>;

struct Frame
{
    Guarded<ScreenRequestRenderer> renderer;
    Guarded<ScreenRectangle> bounds;
    std::array<Guarded<ScreenSurfacePosition>, 3> entries{};
    std::array<Guarded<DirectDrawSurface7Prefix>, 3> devices{};
    Table table_a;
    Table table_b;
    Bank direct;
    Bank dense;
    Bank other;
};

struct Context
{
    Frame* frame;
    const Scenario* scenario;
    const renderer_queue_test::Snapshot* initial_renderer;
    Words initial_dense;
    Words expected_direct;
    Words expected_dense;
    unsigned int locks = 0;
    unsigned int unlocks = 0;
};

Context* active;
std::size_t current_case;
std::size_t checks;
std::size_t calls;
std::size_t locks;
std::size_t unlocks;

void require(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        std::fprintf(stderr, "case %zu: %s\n", current_case, message);
        throw RendererFixtureFailure{};
    }
}

template <class T> void guard(Guarded<T>& value)
{
    value.before.fill(0x3c);
    value.after.fill(0xc3);
}

template <class T> void check_guard(const Guarded<T>& value)
{
    for (auto byte : value.before)
    {
        require(byte == 0x3c, "leading guard differs");
    }
    for (auto byte : value.after)
    {
        require(byte == 0xc3, "trailing guard differs");
    }
}

bool equal_entry(const ScreenSurfacePosition& a, const ScreenSurfacePosition& b)
{
    return a.active == b.active && a.software == b.software &&
           renderer_queue_test::same_bytes(a.unknown_02, b.unknown_02) && a.pixels == b.pixels &&
           a.surface == b.surface && renderer_queue_test::same_entry_extension(a, b) &&
           a.width == b.width && a.height == b.height && a.pitch == b.pitch &&
           a.position.x == b.position.x && a.position.y == b.position.y && a.prepared == b.prepared;
}

SurfaceResult SURFACE_STDCALL lock_surface(DirectDrawSurface7Prefix* receiver,
    SurfaceApiRectangle* rectangle,
    SurfaceDescription* description,
    unsigned int flags,
    void* event)
{
    require(active != nullptr, "lock has no active fixture");
    auto& c = *active;
    auto& f = *c.frame;
    auto& r = f.renderer.value;
    ++c.locks;
    ++locks;
    require(c.locks == 1 && c.unlocks == 0, "lock order differs");
    require(
        receiver == &f.devices[0].value && rectangle == nullptr && flags == 0 && event == nullptr,
        "lock ABI differs");
    require(f.entries[0].value.prepared == 0, "prepared publication precedes lock");
    require(renderer_queue_test::equal(r, *c.initial_renderer), "state before lock differs");
    require(f.dense.value == c.initial_dense && f.direct.value == c.expected_direct,
        "pixels changed before lock");
    std::array<unsigned char, sizeof(SurfaceDescription)> expected{};
    const unsigned int size = sizeof(SurfaceDescription);
    std::memcpy(expected.data(), &size, sizeof(size));
    require(description != nullptr &&
                std::memcmp(
                    static_cast<const void*>(description), expected.data(), expected.size()) == 0,
        "lock descriptor differs");
    description->pixels = c.scenario->null_pixels ? nullptr : f.dense.value.data();
    description->pitch = 98; // The real fill must read the selected entry's pitch.
    if (c.scenario->mutation != Mutation::None)
    {
        // Same-sized borrowed slot replacement; retain the genuine vector backing.
        std::copy(f.table_b.value.begin(), f.table_b.value.end(), r.surface_storage.begin());
        r.current_surface = 1;
        r.width = 11;
        r.height = 9;
        f.entries[1].value.pitch = 14;
        f.entries[1].value.position = {1, 0};
    }
    if (c.scenario->mutation == Mutation::Software)
    {
        r.software_mode = 1;
    }
    if (c.scenario->mutation == Mutation::Failure)
    {
        r.software_mode = 0x7b;
        r.pixels_locked = 0x66;
        f.entries[0].value.prepared = 0x9a;
    }
    if (c.scenario->change_bounds)
    {
        f.bounds.value = {0, 0, 6, 5};
    }
    return c.scenario->lock_result;
}

SurfaceResult SURFACE_STDCALL unlock_surface(
    DirectDrawSurface7Prefix* receiver, SurfaceApiRectangle* rectangle)
{
    require(active != nullptr, "unlock has no active fixture");
    auto& c = *active;
    auto& f = *c.frame;
    ++c.unlocks;
    ++unlocks;
    require(c.locks == 1 && c.unlocks == 1 && rectangle == nullptr, "unlock ABI/order differs");
    const std::size_t index = c.scenario->mutation == Mutation::Reselect ? 1 : 0;
    require(receiver == &f.devices[index].value, "unlock receiver did not reload live entry");
    require(f.dense.value == c.expected_dense && f.direct.value == c.expected_direct,
        "unlock precedes expected writes");
    require(f.renderer.value.pixels_locked == 1, "locked flag cleared before unlock");
    if (c.scenario->mutation == Mutation::Reselect)
    {
        f.renderer.value.current_surface = 2;
    }
    f.renderer.value.pixels_locked = 0xa5;
    return -37; // The original ignores this HRESULT, then clears the locked byte.
}

const DirectDrawSurface7VtablePrefix surface_table = []
{
    DirectDrawSurface7VtablePrefix table{};
    table.Lock = lock_surface;
    table.Unlock = unlock_surface;
    return table;
}();

void run(const Scenario& s)
{
    Frame f{};
    guard(f.renderer);
    guard(f.bounds);
    guard(f.table_a);
    guard(f.table_b);
    guard(f.direct);
    guard(f.dense);
    guard(f.other);
    for (std::size_t i = 0; i < f.direct.value.size(); ++i)
    {
        f.direct.value[i] = i & 1U ? 0xabcd : 0x1111;
    }
    f.dense.value.fill(0x7b7b);
    f.other.value.fill(0x8c8c);
    f.bounds.value = s.bounds;
    auto& r = f.renderer.value;
    renderer_queue_test::initialize(r, 23);
    r.software_mode = s.route == Route::Software ? 1 : 0;
    r.width = 6;
    r.height = 5;
    r.clip_rectangle = s.clip;
    r.unscaled_clip_5c = s.route == Route::Direct ? 0 : 1;
    r.pixels_1c = s.route == Route::Direct && s.null_pixels ? nullptr : f.direct.value.data();
    r.pixels_locked = 0x7b;
    for (std::size_t i = 0; i < f.entries.size(); ++i)
    {
        guard(f.entries[i]);
        guard(f.devices[i]);
        auto& e = f.entries[i].value;
        e.active = static_cast<unsigned char>(0x41 + i);
        e.software = static_cast<unsigned char>(0x51 + i);
        renderer_queue_test::pattern(e.unknown_02, static_cast<unsigned int>(71 + i));
        e.pixels = i == 0 ? f.dense.value.data() : f.other.value.data();
        e.surface = &f.devices[i].value;
        renderer_queue_test::initialize_entry_extension(e, static_cast<unsigned int>(91 + i));
        e.width = 6;
        e.height = 5;
        e.pitch = i == 0 ? s.pitch : 18;
        e.position = i == 0 ? s.position : ScreenPoint{3, 2};
        e.prepared = static_cast<unsigned char>(0x31 + i);
        f.devices[i].value.vtable = &surface_table;
    }
    if (s.route == Route::Software && s.null_pixels)
    {
        f.entries[0].value.pixels = nullptr;
    }
    f.table_a.value = {&f.entries[0].value, &f.entries[2].value, &f.entries[1].value};
    f.table_b.value = {&f.entries[2].value, &f.entries[1].value, &f.entries[0].value};
    r.surface_storage.assign(f.table_a.value.begin(), f.table_a.value.end());
    r.current_surface = 0;

    const renderer_queue_test::Snapshot initial_renderer = r;
    renderer_queue_test::Snapshot expected_renderer = r;
    auto expected_entries = f.entries;
    const auto expected_table_a = f.table_a.value;
    const auto expected_table_b = f.table_b.value;
    auto expected_bounds = s.bounds;
    auto expected_direct = f.direct.value;
    auto expected_dense = f.dense.value;
    const auto expected_other = f.other.value;
    for (auto slot : s.direct_slots)
    {
        expected_direct.at(slot) = static_cast<unsigned short>(s.colour);
    }
    for (auto slot : s.dense_slots)
    {
        expected_dense.at(slot) = static_cast<unsigned short>(s.colour);
    }
    if (s.route != Route::Direct)
    {
        expected_entries[0].value.prepared = 0;
    }
    expected_renderer.pixels_locked = s.expected_locked;
    if (s.mutation != Mutation::None)
    {
        expected_renderer.surface_storage.assign(f.table_b.value.begin(), f.table_b.value.end());
        expected_renderer.current_surface = s.mutation == Mutation::Reselect ? 2 : 1;
        expected_renderer.width = 11;
        expected_renderer.height = 9;
        expected_entries[1].value.pitch = 14;
        expected_entries[1].value.position = {1, 0};
    }
    if (s.mutation == Mutation::Software)
    {
        expected_renderer.software_mode = 1;
    }
    if (s.mutation == Mutation::Failure)
    {
        expected_renderer.software_mode = 0x7b;
        expected_entries[0].value.prepared = 0x9a;
    }
    if (s.change_bounds)
    {
        expected_bounds = {0, 0, 6, 5};
    }

    Context context{&f, &s, &initial_renderer, f.dense.value, expected_direct, expected_dense};
    active = &context;
    r.FillRectangle(f.bounds.value, s.colour);
    ++calls;
    active = nullptr;
    require(f.direct.value == expected_direct, "direct buffer differs");
    require(f.dense.value == expected_dense, "dense buffer differs");
    require(f.other.value == expected_other, "unselected buffer differs");
    require(
        renderer_queue_test::equal(r, expected_renderer), "renderer/opaque/queue state differs");
    require(renderer_queue_test::same_rectangle(f.bounds.value, expected_bounds),
        "input bounds differ");
    require(context.locks == s.expected_locks && context.unlocks == s.expected_unlocks,
        "COM call counts differ");
    require(f.table_a.value == expected_table_a && f.table_b.value == expected_table_b,
        "surface pointer tables differ");
    for (std::size_t i = 0; i < f.entries.size(); ++i)
    {
        require(
            equal_entry(f.entries[i].value, expected_entries[i].value), "surface fields differ");
        require(f.devices[i].value.vtable == &surface_table, "COM table pointer differs");
        check_guard(f.entries[i]);
        check_guard(f.devices[i]);
    }
    check_guard(f.renderer);
    check_guard(f.bounds);
    check_guard(f.table_a);
    check_guard(f.table_b);
    check_guard(f.direct);
    check_guard(f.dense);
    check_guard(f.other);
}
} // namespace

int main()
{
    try
    {
        for (current_case = 0; current_case < scenarios.size(); ++current_case)
        {
            run(scenarios[current_case]);
        }
        std::printf("renderer rectangle fill: %zu sessions, %zu calls, %zu locks, %zu unlocks, %zu "
                    "checks\n",
            scenarios.size(),
            calls,
            locks,
            unlocks,
            checks);
    }
    catch (const RendererFixtureFailure&)
    {
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "renderer fixture host failure: %s\n", error.what());
        return 3;
    }
}
