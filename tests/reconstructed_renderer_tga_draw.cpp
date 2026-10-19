#include "renderer_queue_test_state.h"
#include "startup_screen_renderer.h"
#include "tga_image.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <type_traits>
#include <vector>

static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(int) == 4);
static_assert(sizeof(SurfaceResult) == 4 && INT_MAX == 2147483647);
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Lock) == 25 * sizeof(void*));
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Unlock) == 32 * sizeof(void*));

namespace
{
constexpr std::array<unsigned char, 12> source_pixels = {1, 0, 2, 3, 4, 5, 0, 6, 7, 8, 9, 10};
constexpr std::array<unsigned short, 11> colors = {
    0x1357, 0x9123, 0xa456, 0xb789, 0xcabc, 0xddef, 0xe012, 0xf345, 0x2468, 0x369c, 0x48ad};

struct Write
{
    std::size_t slot;
    unsigned short value;
};

enum class Access
{
    None,
    Software,
    Hardware,
    HardwareRoute,
    HardwareSoftware,
    HardwareNull,
    SoftwareNull,
    HardwareFail
};

struct Scenario
{
    const char* name = "";
    ScreenPoint destination{1, 1};
    ScreenRectangle bounds{1, 0, 4, 2};
    int mode = 0;
    unsigned int flags = 0;
    unsigned int mask = 0;
    RenderRectangle clip{0, 0, 6, 5};
    int clip_mode = 1;
    unsigned char unscaled = 0;
    bool publication = true;
    Access access = Access::None;
    bool alias_arguments = false;
    unsigned int repeats = 1;
    std::vector<ScreenRectangle> initial_queue;
    std::vector<ScreenRectangle> expected_queue{{1, 1, 4, 3}};
    std::vector<Write> direct;
    std::vector<Write> locked;
};

// Literal expected halfword slots; no fixture implementation of clip or blit.
const std::array<Scenario, 22> scenarios = []
{
    std::array<Scenario, 22> result{};
    result[0].name = "transparent last-row equality";
    result[0].destination = {0, 4};
    result[0].bounds = {0, 0, 1, 1};
    result[0].flags = 1;
    result[0].expected_queue = {{0, 4, 1, 5}};

    result[1].name = "opaque origins and index zero";
    result[1].direct = {
        {14, 0x1357}, {16, 0xa456}, {18, 0xb789}, {26, 0xddef}, {28, 0x1357}, {30, 0xe012}};
    result[2].name = "transparent index zero";
    result[2].flags = 0x81;
    result[2].direct = {{16, 0xa456}, {18, 0xb789}, {26, 0xddef}, {30, 0xe012}};
    result[3].name = "masked halfwords";
    result[3].mode = 10;
    result[3].mask = 0x0ff0;
    result[3].direct = {{16, 0x0450}, {18, 0x0780}, {26, 0x0de0}, {30, 0x0010}};

    for (std::size_t i = 4; i <= 10; ++i)
    {
        result[i].mode = 11;
        result[i].destination = {2, 1};
        result[i].expected_queue = {{2, 1, 5, 3}};
    }
    result[4].name = "software packed byte pitch and zero destination";
    result[4].access = Access::Software;
    result[4].locked = {{1, 0xa456}, {2, 0xb789}, {5, 0xddef}, {7, 0xe012}};
    result[5].name = "hardware lock and unlock";
    result[5].access = Access::Hardware;
    result[5].locked = result[4].locked;
    result[6].name = "post-lock table index pitch and unlock receiver";
    result[6].access = Access::HardwareRoute;
    result[6].locked = {{1, 0xa456}, {2, 0xb789}, {7, 0xddef}, {9, 0xe012}};
    result[7].name = "post-lock software switch skips unlock";
    result[7].access = Access::HardwareSoftware;
    result[7].locked = result[6].locked;
    result[8].name = "successful null lock keeps locked flag";
    result[8].access = Access::HardwareNull;
    result[9].name = "software null pointer keeps locked flag";
    result[9].access = Access::SoftwareNull;
    result[10].name = "failed lock uses actual error sink";
    result[10].access = Access::HardwareFail;

    result[11].name = "unsupported draw mode still publishes clip";
    result[11].mode = 99;
    result[12].name = "false clip publishes nothing";
    result[12].destination = {7, 1};
    result[12].expected_queue.clear();
    result[13].name = "right zero resets both endpoints";
    result[13].destination = {0, 0};
    result[13].bounds = {0, 0, 0, 1};
    result[13].expected_queue = {{0, 0, 4, 3}};
    result[13].direct = {{0, 0x9123},
        {2, 0x1357},
        {4, 0xa456},
        {6, 0xb789},
        {12, 0xcabc},
        {14, 0xddef},
        {16, 0x1357},
        {18, 0xe012},
        {24, 0xf345},
        {26, 0x2468},
        {28, 0x369c},
        {30, 0x48ad}};
    result[14] = result[13];
    result[14].name = "bottom zero resets both endpoints";
    result[14].bounds = {0, 0, 2, 0};
    result[15].name = "clipped negative destination";
    result[15].destination = {-1, -1};
    result[15].bounds = {0, 0, 4, 3};
    result[15].expected_queue = {{0, 0, 3, 2}};
    result[15].direct = {
        {0, 0xddef}, {2, 0x1357}, {4, 0xe012}, {12, 0x2468}, {14, 0x369c}, {16, 0x48ad}};
    result[16].name = "live point and queue-bound argument aliases";
    result[16].destination = {0, 0};
    result[16].bounds = {0, 0, 4, 3};
    result[16].clip = {0, 0, 2, 2};
    result[16].initial_queue = {{0, 0, 4, 3}};
    result[16].expected_queue = result[16].initial_queue;
    result[16].alias_arguments = true;
    result[16].direct = {{0, 0x9123}, {2, 0x1357}, {12, 0xcabc}, {14, 0xddef}};
    result[17] = result[1];
    result[17].name = "disabled publication keeps queue";
    result[17].publication = false;
    result[17].initial_queue = {{10, 10, 11, 11}};
    result[17].expected_queue = result[17].initial_queue;
    result[18] = result[1];
    result[18].name = "half-size clip mode";
    result[18].clip = {0, 0, 12, 10};
    result[18].clip_mode = 0;
    result[19] = result[1];
    result[19].name = "double-size clip mode";
    result[19].clip = {0, 0, 3, 3};
    result[19].clip_mode = 2;
    result[20] = result[1];
    result[20].name = "unscaled clip defines otherwise unsupported mode";
    result[20].clip_mode = 7;
    result[20].unscaled = 1;
    result[21] = result[1];
    result[21].name = "repeated draw and contained queue";
    result[21].repeats = 3;
    return result;
}();

template <class T> struct Guarded
{
    std::array<unsigned char, 16> before{};
    T value{};
    std::array<unsigned char, 16> after{};
};
using Bank = Guarded<std::array<unsigned short, 128>>;

struct Frame
{
    Guarded<ScreenRequestRenderer> renderer;
    std::array<Guarded<ScreenSurfacePosition>, 3> entries{};
    std::array<DirectDrawSurface7Prefix, 3> devices{};
    std::array<ScreenSurfacePosition*, 3> table_a{};
    std::array<ScreenSurfacePosition*, 3> table_b{};
    Bank direct;
    Bank locked;
    Bank other;
};

struct Context
{
    Frame* frame;
    TgaImage* image;
    const Scenario* scenario;
    std::array<unsigned short, 128> expected_locked;
    unsigned int lock_calls = 0;
    unsigned int unlock_calls = 0;
};

Context* active;
std::size_t current_case;
std::size_t checks;
std::size_t calls;
std::size_t image_lifetimes;
std::size_t lock_calls;
std::size_t unlock_calls;

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "case %zu: %s\n", current_case, message);
        std::exit(2);
    }
}

bool queue_matches(const ScreenRequestRenderer& r, const std::vector<ScreenRectangle>& expected)
{
    return r.dirty_rectangle_count == static_cast<int>(expected.size()) &&
           r.dirty_rectangles.values.size() == expected.size() &&
           std::equal(r.dirty_rectangles.values.begin(),
               r.dirty_rectangles.values.end(),
               expected.begin(),
               renderer_queue_test::same_rectangle);
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
    ++c.lock_calls;
    ++lock_calls;
    require(c.lock_calls == 1 && c.unlock_calls == 0, "lock order differs");
    require(receiver == &f.devices[0] && rectangle == nullptr && flags == 0 && event == nullptr,
        "lock ABI differs");
    require(f.entries[0].value.prepared == 0, "prepared publication precedes lock");
    require(queue_matches(r, c.scenario->expected_queue), "clip publication precedes lock");
    std::array<unsigned char, sizeof(SurfaceDescription)> expected{};
    const unsigned int size = sizeof(SurfaceDescription);
    std::memcpy(expected.data(), &size, sizeof(size));
    require(description != nullptr &&
                std::memcmp(
                    static_cast<const void*>(description), expected.data(), expected.size()) == 0,
        "lock descriptor differs");
    description->pixels =
        c.scenario->access == Access::HardwareNull ? nullptr : f.locked.value.data();
    description->pitch = 98; // DrawTga must reload the live entry pitch, not this descriptor.
    if (c.scenario->access == Access::HardwareRoute ||
        c.scenario->access == Access::HardwareSoftware)
    {
        r.surfaces = f.table_b.data();
        r.current_surface = 1;
        r.width = 11;
        c.image->width = 3; // The parent already captured the original source stride of four.
        if (c.scenario->access == Access::HardwareSoftware)
        {
            r.software_mode = 1;
        }
    }
    return c.scenario->access == Access::HardwareFail ? -1 : 0;
}

SurfaceResult SURFACE_STDCALL unlock_surface(
    DirectDrawSurface7Prefix* receiver, SurfaceApiRectangle* rectangle)
{
    require(active != nullptr, "unlock has no active fixture");
    auto& c = *active;
    auto& f = *c.frame;
    ++c.unlock_calls;
    ++unlock_calls;
    require(c.lock_calls == 1 && c.unlock_calls == 1 && rectangle == nullptr,
        "unlock ABI/order differs");
    const std::size_t slot = c.scenario->access == Access::HardwareRoute ? 1 : 0;
    require(receiver == &f.devices[slot], "unlock receiver did not reload current surface");
    require(f.locked.value == c.expected_locked, "unlock precedes packed writes");
    require(f.renderer.value.pixels_locked == 1, "locked flag cleared before unlock");
    if (c.scenario->access == Access::HardwareRoute)
    {
        f.renderer.value.current_surface = 2;
    }
    f.renderer.value.pixels_locked = 0xa5;
    return -37; // The real parent ignores this HRESULT and clears its flag after return.
}

const DirectDrawSurface7VtablePrefix surface_table = []
{
    DirectDrawSurface7VtablePrefix table{};
    table.Lock = lock_surface;
    table.Unlock = unlock_surface;
    return table;
}();

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

void run(const Scenario& s)
{
    Frame f{};
    guard(f.renderer);
    guard(f.direct);
    guard(f.locked);
    guard(f.other);
    f.direct.value.fill(0x6a6a);
    f.locked.value.fill(0x7b7b);
    f.other.value.fill(0x8c8c);
    auto& r = f.renderer.value;
    renderer_queue_test::initialize(r, 23);
    r.software_mode = s.access == Access::Software || s.access == Access::SoftwareNull;
    r.width = 6;
    r.height = 5;
    r.clip_rectangle = s.clip;
    r.clip_mode_54 = s.clip_mode;
    r.unscaled_clip_5c = s.unscaled;
    r.clip_flag_5d = 0;
    r.pixels_1c = f.direct.value.data();
    r.rectangle_suppressed_4d = 0;
    r.rectangle_enabled_4e = s.publication;
    r.rectangle_ready_4f = 1;
    r.pixels_locked = 0x7b;
    r.dirty_rectangles.values.assign(s.initial_queue.begin(), s.initial_queue.end());
    r.dirty_rectangle_count = static_cast<int>(s.initial_queue.size());
    for (std::size_t i = 0; i < f.entries.size(); ++i)
    {
        guard(f.entries[i]);
        auto& entry = f.entries[i].value;
        entry.active = 1;
        entry.software = 0;
        entry.pixels = i == 0 ? f.locked.value.data() : f.other.value.data();
        entry.surface = &f.devices[i];
        entry.width = 6;
        entry.height = 5;
        entry.pitch = 10 + static_cast<unsigned int>(i) * 4;
        entry.position = s.destination;
        entry.prepared = static_cast<unsigned char>(0x31 + i);
        f.devices[i].vtable = &surface_table;
    }
    if (s.access == Access::SoftwareNull)
    {
        f.entries[0].value.pixels = nullptr;
    }
    f.table_a = {&f.entries[0].value, &f.entries[2].value, &f.entries[1].value};
    f.table_b = {&f.entries[2].value, &f.entries[1].value, &f.entries[0].value};
    const auto original_table_a = f.table_a;
    const auto original_table_b = f.table_b;
    r.surfaces = f.table_a.data();
    r.current_surface = 0;

    // Real allocating constructor and destructor own both typed arrays. The
    // fixture supplies their generated contents, not a replacement lifetime.
    TgaImage image(4, 3, 8, 0);
    ++image_lifetimes;
    require(
        image.pixels != nullptr && image.palette == nullptr && image.converted_palette == nullptr,
        "real image construction differs");
    std::copy(source_pixels.begin(), source_pixels.end(), image.pixels);
    image.converted_palette = new unsigned short[256];
    std::fill_n(image.converted_palette, 256, 0x5e5e);
    std::copy(colors.begin(), colors.end(), image.converted_palette);
    const auto original_pixels = image.pixels;
    const auto original_palette = image.converted_palette;
    auto expected_direct = f.direct.value;
    auto expected_locked = f.locked.value;
    for (const auto& write : s.direct)
    {
        expected_direct.at(write.slot) = write.value;
    }
    for (const auto& write : s.locked)
    {
        expected_locked.at(write.slot) = write.value;
    }

    for (unsigned int repeat = 0; repeat < s.repeats; ++repeat)
    {
        ScreenRequestRenderer expected_renderer = r;
        expected_renderer.dirty_rectangles.values.assign(
            s.expected_queue.begin(), s.expected_queue.end());
        expected_renderer.dirty_rectangle_count = static_cast<int>(s.expected_queue.size());
        decltype(f.entries) expected_entries{};
        static_assert(std::is_trivially_copyable_v<decltype(expected_entries)>);
        std::memcpy(static_cast<void*>(&expected_entries),
            static_cast<const void*>(&f.entries),
            sizeof(f.entries));
        if (s.mode == 11)
        {
            expected_entries[0].value.prepared = 0;
            expected_renderer.pixels_locked =
                s.access == Access::HardwareFail
                    ? 0x7b
                    : (s.access == Access::HardwareNull || s.access == Access::SoftwareNull ? 1
                                                                                            : 0);
        }
        if (s.access == Access::HardwareRoute || s.access == Access::HardwareSoftware)
        {
            expected_renderer.surfaces = f.table_b.data();
            expected_renderer.current_surface = s.access == Access::HardwareRoute ? 2 : 1;
            expected_renderer.width = 11;
            expected_renderer.software_mode = s.access == Access::HardwareSoftware;
        }
        Context context{&f, &image, &s, expected_locked};
        active = &context;
        const ScreenPoint& destination =
            s.alias_arguments ? f.entries[0].value.position : s.destination;
        const ScreenRectangle& bounds =
            s.alias_arguments ? r.dirty_rectangles.values.front() : s.bounds;
        r.DrawTga(destination, bounds, &image, s.mode, s.flags, s.mask);
        ++calls;
        active = nullptr;
        require(f.direct.value == expected_direct, "direct buffer differs");
        require(f.locked.value == expected_locked, "locked buffer differs");
        require(std::all_of(f.other.value.begin(),
                    f.other.value.end(),
                    [](unsigned short value)
                    {
                        return value == 0x8c8c;
                    }),
            "unselected buffer differs");
        require(renderer_queue_test::equal(r, expected_renderer), "renderer/queue state differs");
        require(std::memcmp(static_cast<const void*>(&f.entries),
                    static_cast<const void*>(&expected_entries),
                    sizeof(f.entries)) == 0,
            "surface entry state differs");
        const bool hardware = s.access == Access::Hardware || s.access == Access::HardwareRoute ||
                              s.access == Access::HardwareSoftware ||
                              s.access == Access::HardwareNull || s.access == Access::HardwareFail;
        require(
            context.lock_calls == static_cast<unsigned int>(hardware), "lock call count differs");
        require(
            context.unlock_calls == static_cast<unsigned int>(s.access == Access::Hardware ||
                                                              s.access == Access::HardwareRoute),
            "unlock call count differs");
        require(image.width ==
                        (s.access == Access::HardwareRoute || s.access == Access::HardwareSoftware
                                ? 3
                                : 4) &&
                    image.height == 3 && image.bits_per_pixel == 8 && image.bytes_per_pixel == 1 &&
                    image.pixels == original_pixels &&
                    image.converted_palette == original_palette && image.palette == nullptr,
            "image fields differ");
        for (std::size_t i = 0; i < source_pixels.size(); ++i)
        {
            require(image.pixels[i] == source_pixels[i], "source pixel changed");
        }
        for (std::size_t i = 0; i < 256; ++i)
        {
            require(image.converted_palette[i] == (i < colors.size() ? colors[i] : 0x5e5e),
                "palette changed");
        }
        for (auto byte : image.unknown_1c)
        {
            require(byte == 0, "image opaque field changed");
        }
        for (std::size_t i = 0; i < f.devices.size(); ++i)
        {
            require(f.devices[i].vtable == &surface_table, "COM table changed");
        }
        require(f.table_a == original_table_a && f.table_b == original_table_b,
            "surface pointer table changed");
        check_guard(f.renderer);
        check_guard(f.direct);
        check_guard(f.locked);
        check_guard(f.other);
        for (const auto& entry : f.entries)
        {
            check_guard(entry);
        }
    }
}
} // namespace

int main()
{
    for (current_case = 0; current_case < scenarios.size(); ++current_case)
    {
        run(scenarios[current_case]);
    }
    std::printf("renderer TGA draw: %zu sessions, %zu calls, %zu image lifetimes, %zu locks, %zu "
                "unlocks, %zu checks\n",
        scenarios.size(),
        calls,
        image_lifetimes,
        lock_calls,
        unlock_calls,
        checks);
}
