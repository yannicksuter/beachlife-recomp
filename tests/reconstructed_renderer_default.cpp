#include "render_mask_setup.h"
#include "renderer_queue_test_state.h"
#include "startup_screen_renderer.h"

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tuple>

static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(sizeof(int) == 4 && INT_MAX == 2147483647 && sizeof(SurfaceResult) == 4);
static_assert(std::endian::native == std::endian::little);
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Lock) == 25 * sizeof(void*));
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Unlock) == 32 * sizeof(void*));

namespace
{
enum class Case
{
    Mode0565,
    Mode0555,
    ZeroColumns,
    PackedRows,
    PackedAlias,
    LiveLock,
    FailedLock,
    NullLock,
    NullSoftware,
    EmptyRows
};

constexpr std::array<const char*, 10> names{"mode0-565",
    "mode0-555",
    "zero-columns",
    "packed-rows",
    "packed-alias",
    "live-lock",
    "failed-lock",
    "null-lock",
    "null-software",
    "empty-rows"};
const char* current_case = "";
unsigned calls = 0;
unsigned checks = 0;
unsigned word_checks = 0;
unsigned lock_calls = 0;
unsigned unlock_calls = 0;

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "default-render failure: case=%s reason=%s\n", current_case, reason);
        std::exit(2);
    }
}

template <class T> struct Guarded
{
    std::array<unsigned char, 8> before{};
    T value{};
    std::array<unsigned char, 8> after{};

    Guarded()
    {
        before.fill(0x3c);
        after.fill(0xc3);
    }
};

using Words = std::array<unsigned short, 160>;
struct Fixture
{
    Guarded<ScreenRequestRenderer> renderer;
    std::array<Guarded<ScreenSurfacePosition>, 3> entries;
    std::array<Guarded<DirectDrawSurface7Prefix>, 3> devices;
    Guarded<std::array<ScreenSurfacePosition*, 3>> table;
    Guarded<Words> source;
    Guarded<Words> destination;
    Guarded<Words> other;
};

struct Context
{
    Fixture* fixture;
    Case kind;
    Words source_before;
    Words destination_before;
    Words source_expected;
    Words destination_expected;
    unsigned locks = 0;
    unsigned unlocks = 0;
};
Context* active;

auto mask_state()
{
    return std::tuple{render_mask_format,
        render_quarter_mask,
        render_half_mask,
        render_mask_pixels,
        render_full_mask,
        render_green_mask,
        render_sum_bias,
        render_quarter_bias,
        render_half_bias,
        render_green_levels,
        render_green_bits,
        render_red_shift,
        render_red_mask,
        render_mask_width,
        render_blue_mask};
}

template <class T> void check_guard(const Guarded<T>& object)
{
    for (auto byte : object.before)
    {
        require(byte == 0x3c, "leading-guard");
    }
    for (auto byte : object.after)
    {
        require(byte == 0xc3, "trailing-guard");
    }
}

void check_words(const Words& actual, const Words& expected)
{
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        ++word_checks;
        require(actual[i] == expected[i], "pixel-word");
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

// Offsets are relative to the live halfword span beginning at word 4.
void put_dword(Words& storage, std::size_t offset, unsigned int value)
{
    std::memcpy(
        reinterpret_cast<unsigned char*>(storage.data() + 4) + offset, &value, sizeof(value));
}

SurfaceResult SURFACE_STDCALL lock_surface(DirectDrawSurface7Prefix* receiver,
    SurfaceApiRectangle* rectangle,
    SurfaceDescription* description,
    unsigned int flags,
    void* event)
{
    require(active != nullptr, "unexpected-lock");
    auto& c = *active;
    auto& f = *c.fixture;
    auto& r = f.renderer.value;
    ++c.locks;
    ++lock_calls;
    require(c.locks == 1 && c.unlocks == 0, "lock-order");
    require(
        receiver == &f.devices[0].value && rectangle == nullptr && flags == 0 && event == nullptr,
        "lock-arguments");
    require(r.current_surface == 0 && f.entries[0].value.prepared == 0 && r.pixels_locked == 0x7b,
        "access-publication");
    require(f.source.value == c.source_before && f.destination.value == c.destination_before,
        "pixels-before-lock");
    std::array<unsigned char, sizeof(SurfaceDescription)> expected{};
    const unsigned int size = sizeof(SurfaceDescription);
    std::memcpy(expected.data(), &size, sizeof(size));
    require(
        description != nullptr && std::memcmp(description, expected.data(), expected.size()) == 0,
        "lock-description");
    description->pixels = f.destination.value.data() + 4;
    description->pitch = 222; // The method must use the live entry's pitch.
    if (c.kind == Case::LiveLock)
    {
        r.current_surface = 1;
        r.width = 32;
        r.height = 3;
        r.word_60 = 1;
        r.clip_mode_54 = -1;
        r.pixels_1c = f.source.value.data() + 4;
        f.entries[1].value.pitch = 70;
        return 37;
    }
    if (c.kind == Case::FailedLock)
    {
        return INT_MIN;
    }
    require(c.kind == Case::NullLock, "lock-case");
    description->pixels = nullptr;
    return 0;
}

SurfaceResult SURFACE_STDCALL unlock_surface(
    DirectDrawSurface7Prefix* receiver, SurfaceApiRectangle* rectangle)
{
    require(active != nullptr, "unexpected-unlock");
    auto& c = *active;
    auto& f = *c.fixture;
    auto& r = f.renderer.value;
    ++c.unlocks;
    ++unlock_calls;
    require(c.kind == Case::LiveLock && c.locks == 1 && c.unlocks == 1, "unlock-order");
    require(receiver == &f.devices[1].value && rectangle == nullptr && r.current_surface == 1,
        "live-unlock-receiver");
    require(f.source.value == c.source_expected && f.destination.value == c.destination_expected,
        "pixels-before-unlock");
    require(r.pixels_locked == 1, "clear-after-unlock");
    r.current_surface = 2;
    r.pixels_locked = 0xa5;
    return -37;
}

const DirectDrawSurface7VtablePrefix surface_table = []
{
    DirectDrawSurface7VtablePrefix result{};
    result.Lock = lock_surface;
    result.Unlock = unlock_surface;
    return result;
}();

void run(Case kind)
{
    current_case = names[static_cast<std::size_t>(kind)];
    Fixture f{};
    auto& r = f.renderer.value;
    renderer_queue_test::initialize(r, 23);
    r.current_surface = 2;
    r.pixels_locked = 0x7b;
    r.software_mode = 1;
    r.clip_mode_54 = 0;
    r.width = 5;
    r.height = 6;
    r.word_60 = 1;
    r.pixels_1c = f.source.value.data() + 4;
    r.surfaces = f.table.value.data();
    f.source.value.fill(0xb6b6);
    f.destination.value.fill(0xa55a);
    f.other.value.fill(0xc33c);
    for (unsigned i = 0; i < 64; ++i)
    {
        put_dword(f.source.value, i * 4, 0xface0000U | (0x1000U + i));
    }
    for (std::size_t i = 0; i < f.entries.size(); ++i)
    {
        auto& e = f.entries[i].value;
        e.active = static_cast<unsigned char>(0x41 + i);
        e.software = static_cast<unsigned char>(0x51 + i);
        renderer_queue_test::pattern(e.unknown_02, static_cast<unsigned>(71 + i));
        renderer_queue_test::initialize_entry_extension(e, static_cast<unsigned>(91 + i));
        e.pixels = i == 0 ? f.destination.value.data() + 4 : f.other.value.data() + 4;
        e.surface = &f.devices[i].value;
        e.width = 80;
        e.height = 2;
        e.pitch = i == 0 ? 14 : 76;
        e.position = {3, 7};
        e.prepared = static_cast<unsigned char>(0x31 + i);
        f.devices[i].value.vtable = &surface_table;
        f.table.value[i] = &e;
    }
    if (kind == Case::Mode0565 || kind == Case::Mode0555)
    {
        constexpr std::array<std::size_t, 6> positions{0, 2, 4, 10, 12, 14};
        constexpr std::array<unsigned short, 6> values{
            0xf801, 0x07e1, 0x4321, 0x001f, 0x7bef, 0x8765};
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            f.source.value[4 + positions[i]] = values[i];
        }
    }
    else if (kind == Case::ZeroColumns)
    {
        r.width = 1;
        r.height = 3;
    }
    else
    {
        r.clip_mode_54 = -1;
        r.width = 33;
        r.height = 3;
        f.entries[0].value.pitch = 70;
        if (kind == Case::PackedAlias)
        {
            r.width = 32;
            r.height = 1;
            r.word_60 = 0;
            f.entries[0].value.pixels = f.source.value.data() + 4;
        }
        else if (kind == Case::LiveLock)
        {
            r.software_mode = 0;
            r.clip_mode_54 = 0;
            r.width = 1;
            r.height = 0;
            r.word_60 = 0;
            r.pixels_1c = nullptr;
        }
        else if (kind == Case::FailedLock || kind == Case::NullLock)
        {
            r.software_mode = 0;
            r.pixels_1c = nullptr;
        }
        else if (kind == Case::NullSoftware)
        {
            f.entries[0].value.pixels = nullptr;
            r.pixels_1c = nullptr;
        }
        else if (kind == Case::EmptyRows)
        {
            r.height = r.word_60;
            r.pixels_1c = nullptr;
        }
    }

    Words expected_source = f.source.value;
    Words expected_destination = f.destination.value;
    const Words expected_other = f.other.value;
    constexpr std::array<unsigned int, 32> packed{0x10011000,
        0x10031002,
        0x10051004,
        0x10071006,
        0x10091008,
        0x100b100a,
        0x100d100c,
        0x100f100e,
        0x10111010,
        0x10131012,
        0x10151014,
        0x10171016,
        0x10191018,
        0x101b101a,
        0x101d101c,
        0x101f101e,
        0x10211020,
        0x10231022,
        0x10251024,
        0x10271026,
        0x10291028,
        0x102b102a,
        0x102d102c,
        0x102f102e,
        0x10311030,
        0x10331032,
        0x10351034,
        0x10371036,
        0x10391038,
        0x103b103a,
        0x103d103c,
        0x103f103e};
    if (kind == Case::Mode0565 || kind == Case::Mode0555)
    {
        constexpr std::array<std::size_t, 8> offsets{0, 4, 12, 16, 24, 28, 36, 40};
        constexpr std::array<unsigned int, 8> words565{0xf801f000,
            0x07e107c0,
            0xf801f000,
            0x07e107c0,
            0x001f001e,
            0x7bef73ce,
            0x001f001e,
            0x7bef73ce};
        constexpr std::array<unsigned int, 8> words555{0xf8017800,
            0x07e103c0,
            0xf8017800,
            0x07e103c0,
            0x001f001e,
            0x7bef7bce,
            0x001f001e,
            0x7bef7bce};
        const auto& output = kind == Case::Mode0565 ? words565 : words555;
        for (std::size_t i = 0; i < offsets.size(); ++i)
        {
            put_dword(expected_destination, offsets[i], output[i]);
        }
    }
    else if (kind == Case::PackedRows || kind == Case::LiveLock)
    {
        for (unsigned i = 0; i < 16; ++i)
        {
            put_dword(expected_destination, i * 4, packed[i]);
            put_dword(expected_destination, 68 + i * 4, packed[16 + i]);
        }
    }
    else if (kind == Case::PackedAlias)
    {
        for (unsigned i = 0; i < 16; ++i)
        {
            put_dword(expected_source, i * 4, packed[i]);
        }
    }

    auto expected_renderer = r;
    expected_renderer.current_surface = 0;
    expected_renderer.pixels_locked = 0;
    std::array<ScreenSurfacePosition, 3> expected_entries{
        f.entries[0].value, f.entries[1].value, f.entries[2].value};
    expected_entries[0].prepared = 0;
    if (kind == Case::LiveLock)
    {
        expected_renderer.current_surface = 2;
        expected_renderer.width = 32;
        expected_renderer.height = 3;
        expected_renderer.word_60 = 1;
        expected_renderer.clip_mode_54 = -1;
        expected_renderer.pixels_1c = f.source.value.data() + 4;
        expected_entries[1].pitch = 70;
    }
    else if (kind == Case::FailedLock)
    {
        expected_renderer.pixels_locked = 0x7b;
    }
    else if (kind == Case::NullLock || kind == Case::NullSoftware)
    {
        expected_renderer.pixels_locked = 1;
    }

    SetupRenderMasks(nullptr, 0, kind == Case::Mode0555 ? 1 : 0);
    const auto expected_masks = mask_state();
    Context c{&f, kind, f.source.value, f.destination.value, expected_source, expected_destination};
    active = &c;
    r.RenderDefault();
    active = nullptr;
    ++calls;
    require(r.pixels_locked == expected_renderer.pixels_locked, "locked-state");
    require(renderer_queue_test::equal(r, expected_renderer), "renderer-state");
    require(mask_state() == expected_masks, "mask-state");
    require(c.locks == (kind == Case::LiveLock || kind == Case::FailedLock || kind == Case::NullLock
                               ? 1U
                               : 0U),
        "lock-count");
    require(c.unlocks == (kind == Case::LiveLock ? 1U : 0U), "unlock-count");
    check_words(f.source.value, expected_source);
    check_words(f.destination.value, expected_destination);
    check_words(f.other.value, expected_other);
    check_guard(f.renderer);
    check_guard(f.table);
    check_guard(f.source);
    check_guard(f.destination);
    check_guard(f.other);
    for (std::size_t i = 0; i < f.entries.size(); ++i)
    {
        require(equal_entry(f.entries[i].value, expected_entries[i]), "entry-state");
        require(f.devices[i].value.vtable == &surface_table, "surface-vtable");
        require(f.table.value[i] == &f.entries[i].value, "surface-table");
        check_guard(f.entries[i]);
        check_guard(f.devices[i]);
    }
}
} // namespace

int main()
{
    for (unsigned i = 0; i < names.size(); ++i)
    {
        run(static_cast<Case>(i));
    }
    std::printf("{\"groups\":3,\"calls\":%u,\"checks\":%u,\"word_checks\":%u,"
                "\"lock_calls\":%u,\"unlock_calls\":%u,\"pointer_bytes\":%zu}\n",
        calls,
        checks,
        word_checks,
        lock_calls,
        unlock_calls,
        sizeof(void*));
    return 0;
}
