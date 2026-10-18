#include "startup_screen_renderer.h"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <new>
#include <type_traits>

namespace
{
static_assert(sizeof(int) == 4 && std::numeric_limits<int>::digits == 31);
static_assert(sizeof(unsigned int) == 4);
static_assert(std::is_standard_layout_v<ScreenSurfacePosition>);
static_assert(std::is_trivially_copyable_v<ScreenSurfacePosition>);
static_assert(std::is_standard_layout_v<ScreenRequestRenderer>);
static_assert(std::is_trivially_copyable_v<ScreenRequestRenderer>);
static_assert(sizeof(ScreenPoint) == 2 * sizeof(int));
static_assert(std::is_same_v<decltype(&ScreenRequestRenderer::PositionSurface),
    void (ScreenRequestRenderer::*)(int, ScreenPoint)>);
static_assert(
    std::is_same_v<decltype(&ScreenSurfacePosition::Cleanup), void (ScreenSurfacePosition::*)()>);
static_assert(std::is_same_v<decltype(&ScreenRequestRenderer::CleanupSurface),
    void (ScreenRequestRenderer::*)(int)>);

// Native offsets follow these real types; the observed x86 prefix is not imposed
// on the pointer-bearing 64-bit objects exercised here.
enum class Method : unsigned
{
    member,
    indexed
};
std::uint64_t cases, checks, representation_checks, byte_checks;
std::uint64_t array_events, api_events, null_events;
std::uint64_t cleanup_cases[2]{}, cleanup_calls[2]{};
std::uint64_t position_cases, position_calls;
std::uint64_t case_number;

[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "case %llu: %s\n", static_cast<unsigned long long>(case_number), message);
    std::exit(2);
}

void check(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        fail(message);
    }
}

void check_bytes(const void* first, const void* second, std::size_t size, const char* message)
{
    ++representation_checks;
    byte_checks += size;
    check(std::memcmp(first, second, size) == 0, message);
}

struct Frame
{
    unsigned char before[32];
    ScreenSurfacePosition object;
    unsigned char prepared; // Extra trailing canary, separate from object.prepared.
    unsigned char after[32];
};

struct RendererFrame
{
    unsigned char before[32];
    ScreenRequestRenderer renderer;
    unsigned char after[32];
};

struct TableFrame
{
    unsigned char before[32];
    ScreenSurfacePosition* entries[8];
    unsigned char after[32];
};

struct Allocation
{
    unsigned char* raw;
    void* pointer;
    std::size_t bytes;
};
Allocation allocations[4]{};

Allocation* find_allocation(void* pointer)
{
    for (auto& allocation : allocations)
    {
        if (allocation.pointer == pointer && allocation.raw)
        {
            return &allocation;
        }
    }
    return nullptr;
}

void inspect_allocation(const Allocation& allocation)
{
    for (std::size_t n = 0; n < allocation.bytes + 64; ++n)
    {
        const auto expected = n < 32 || n >= allocation.bytes + 32 ? 0xa7 : 0x5c;
        if (allocation.raw[n] != expected)
        {
            fail("pixel allocation or guards changed before release");
        }
    }
    byte_checks += allocation.bytes + 64;
    check(true, "pixel allocation inspected");
}

struct Context
{
    Frame* actual;
    Frame initial;
    unsigned mask;
    unsigned route; // 0 inactive, 1 primitive array, 2 COM.
    unsigned result;
    unsigned events;
    void* original_pixels;
    DirectDrawSurface7Prefix* original_surface;
    DirectDrawSurface7Prefix* replacement_surface;
    unsigned short* replacement_pixels;
    RendererFrame* renderer;
    RendererFrame initial_renderer;
    TableFrame* table;
    TableFrame initial_table;
    ScreenSurfacePosition** alternate;
    ScreenSurfacePosition* decoy;
    unsigned slot;
    unsigned base;
};
Context* current;

void mutate(Frame& frame, const Context& context)
{
    auto& object = frame.object;
    if (context.mask & 1)
    {
        object.pixels = context.replacement_pixels;
    }
    if (context.mask & 2)
    {
        object.surface = context.replacement_surface;
    }
    if (context.mask & 4)
    {
        object.width = 0x89abcdefu;
        object.height = 0xfedcba98u;
        object.pitch = 0x76543210u;
    }
    if (context.mask & 8)
    {
        object.active = 0xe9;
    }
    if (context.mask & 16)
    {
        object.software = 0x35;
    }
    if (context.mask & 32)
    {
        frame.prepared = 0x83;
    }
    if (context.mask & 64)
    {
        object.position = ScreenPoint{-1234567, 2345678};
    }
    if (context.mask & 128)
    {
        object.unknown_02[0] = 0x42;
        object.unknown_0c[3] = 0x61;
    }
}

void provider_event(unsigned route)
{
    check(current != nullptr, "provider called without a labelled context");
    check(current->route == route, "wrong ownership provider selected");
    check(current->events++ == 0, "provider invoked more than once");
    check_bytes(current->actual,
        &current->initial,
        sizeof(Frame),
        "object changed before provider returned");
    check_bytes(current->renderer,
        &current->initial_renderer,
        sizeof(RendererFrame),
        "renderer changed before provider returned");
    check_bytes(current->table,
        &current->initial_table,
        sizeof(TableFrame),
        "table changed before provider returned");
    mutate(*current->actual, *current);
    if (current->mask & 256)
    {
        current->renderer->renderer.surfaces = current->alternate + current->base;
    }
    if (current->mask & 512)
    {
        current->table->entries[current->slot] = current->decoy;
    }
}

unsigned int SURFACE_STDCALL release_provider(DirectDrawSurface7Prefix* receiver)
{
    check(current != nullptr && receiver == current->original_surface, "COM receiver differs");
    provider_event(2);
    ++api_events;
    return current->result;
}

template <class T> void expected_write(Frame& frame, std::size_t offset, const T& value)
{
    std::memcpy(reinterpret_cast<unsigned char*>(&frame.object) + offset, &value, sizeof(value));
}

void run_cleanup(Method method,
    unsigned active,
    unsigned software,
    unsigned mask,
    unsigned result,
    bool null_pixels,
    unsigned payload_words,
    unsigned pattern,
    unsigned slot,
    unsigned base,
    bool alias)
{
    case_number = cases++;
    ++cleanup_cases[static_cast<unsigned>(method)];
    DirectDrawSurface7VtablePrefix vtable{};
    vtable.Release = release_provider;
    DirectDrawSurface7Prefix primary{&vtable}, replacement{&vtable};
    const auto original_vtable = vtable;
    const auto original_primary = primary;
    const auto original_replacement = replacement;
    unsigned short replacement_pixels[4]{17, 33, 65, 129};
    const unsigned short saved_replacement_pixels[4]{17, 33, 65, 129};
    auto* owned = null_pixels ? nullptr : new unsigned short[payload_words];

    Frame actual;
    std::memset(&actual, static_cast<int>(pattern & 255), sizeof(actual));
    actual.object.active = static_cast<unsigned char>(active);
    actual.object.software = static_cast<unsigned char>(software);
    actual.object.pixels = owned;
    actual.object.surface = active && !software ? &primary : nullptr;
    actual.object.width = 0xffffffffu ^ (active * 0x01010101u);
    actual.object.height = software * 0x01010101u;
    actual.object.pitch = 0x80000001u ^ pattern;
    actual.object.position = ScreenPoint{-1234, 5678};
    actual.prepared = static_cast<unsigned char>(pattern);

    Frame decoy;
    std::memcpy(&decoy, &actual, sizeof(decoy));
    decoy.object.active = 0xb3;
    decoy.object.software = 0;
    decoy.object.pixels = nullptr;
    decoy.object.surface = &replacement;
    Frame saved_decoy;
    std::memcpy(&saved_decoy, &decoy, sizeof(decoy));
    TableFrame table, alternate;
    RendererFrame renderer;
    std::memset(&table, 0x67, sizeof(table));
    std::memset(&alternate, 0xd5, sizeof(alternate));
    std::memset(&renderer, 0x96, sizeof(renderer));
    for (unsigned n = 0; n < 8; ++n)
    {
        table.entries[n] = &decoy.object;
        alternate.entries[n] = &decoy.object;
    }
    table.entries[slot] = &actual.object;
    if (alias)
    {
        table.entries[(slot + 3) % 8] = &actual.object;
    }
    renderer.renderer.surfaces = table.entries + base;
    const int index = static_cast<int>(slot) - static_cast<int>(base);
    TableFrame original_table, expected_table, saved_alternate;
    RendererFrame original_renderer, expected_renderer;
    std::memcpy(&original_table, &table, sizeof(table));
    std::memcpy(&expected_table, &table, sizeof(table));
    std::memcpy(&saved_alternate, &alternate, sizeof(alternate));
    std::memcpy(&original_renderer, &renderer, sizeof(renderer));
    std::memcpy(&expected_renderer, &renderer, sizeof(renderer));

    Context context{};
    context.actual = &actual;
    std::memcpy(&context.initial, &actual, sizeof(actual));
    context.mask = mask;
    context.route = active == 0 ? 0u : software == 0 ? 2u : 1u;
    context.result = result;
    context.original_pixels = owned;
    context.original_surface = &primary;
    context.replacement_surface = &replacement;
    context.replacement_pixels = replacement_pixels;
    context.renderer = &renderer;
    std::memcpy(&context.initial_renderer, &renderer, sizeof(renderer));
    context.table = &table;
    std::memcpy(&context.initial_table, &table, sizeof(table));
    context.alternate = alternate.entries;
    context.decoy = &decoy.object;
    context.slot = slot;
    context.base = base;
    Frame expected;
    std::memcpy(&expected, &actual, sizeof(expected));
    const bool observable_provider = context.route == 2 || (context.route == 1 && owned);
    if (observable_provider)
    {
        mutate(expected, context);
        if (mask & 256)
        {
            expected_renderer.renderer.surfaces = alternate.entries + base;
        }
        if (mask & 512)
        {
            expected_table.entries[slot] = &decoy.object;
        }
    }
    // An independent write-set oracle uses the entry-state route, then applies
    // provider mutations and only the documented post-provider publication set.
    if (active)
    {
        const unsigned char clear_byte = 0;
        const unsigned int clear_word = 0;
        expected_write(expected, offsetof(ScreenSurfacePosition, active), clear_byte);
        for (auto offset : {offsetof(ScreenSurfacePosition, width),
                 offsetof(ScreenSurfacePosition, height),
                 offsetof(ScreenSurfacePosition, pitch)})
        {
            expected_write(expected, offset, clear_word);
        }
        if (software)
        {
            unsigned short* clear_pointer = nullptr;
            expected_write(expected, offsetof(ScreenSurfacePosition, pixels), clear_pointer);
        }
    }
    current = &context;
    ++cleanup_calls[static_cast<unsigned>(method)];
    if (method == Method::member)
    {
        actual.object.Cleanup();
    }
    else
    {
        renderer.renderer.CleanupSurface(index);
    }
    current = nullptr;
    check(context.events == static_cast<unsigned>(observable_provider),
        "ownership event count differs");
    check_bytes(&actual, &expected, sizeof(Frame), "object write set or surrounding guards differ");
    check_bytes(&vtable, &original_vtable, sizeof(vtable), "COM vtable changed");
    check_bytes(&primary, &original_primary, sizeof(primary), "COM object changed");
    check_bytes(
        &replacement, &original_replacement, sizeof(replacement), "replacement COM object changed");
    check_bytes(replacement_pixels,
        saved_replacement_pixels,
        sizeof(replacement_pixels),
        "replacement pixel buffer changed");
    check_bytes(&renderer, &expected_renderer, sizeof(renderer), "renderer write set differs");
    check_bytes(&table, &expected_table, sizeof(table), "table write set differs");
    check_bytes(&alternate, &saved_alternate, sizeof(alternate), "alternate table changed");
    check_bytes(&decoy, &saved_decoy, sizeof(decoy), "unselected entry changed");
    // Restore only generated table bindings so a repeat selects the same live
    // object, including through an aliased entry. Provider mutations were
    // already checked against the independent expected state above.
    std::memcpy(&renderer, &original_renderer, sizeof(renderer));
    std::memcpy(&table, &original_table, sizeof(table));
    // A second invocation must observe the cleared gate and leave everything.
    std::memcpy(&context.initial, &actual, sizeof(actual));
    context.route = 0;
    context.events = 0;
    current = &context;
    const unsigned repeat_slot = alias ? (slot + 3) % 8 : slot;
    ++cleanup_calls[static_cast<unsigned>(method)];
    if (method == Method::member)
    {
        actual.object.Cleanup();
    }
    else
    {
        renderer.renderer.CleanupSurface(static_cast<int>(repeat_slot) - static_cast<int>(base));
    }
    current = nullptr;
    check(context.events == 0, "inactive repeat invoked a provider");
    check_bytes(&actual, &expected, sizeof(Frame), "inactive repeat changed object");
    check_bytes(
        &renderer, &original_renderer, sizeof(renderer), "inactive repeat changed renderer");
    check_bytes(&table, &original_table, sizeof(table), "inactive repeat changed table");
    check_bytes(
        &alternate, &saved_alternate, sizeof(alternate), "inactive repeat changed alternate");
    check_bytes(&decoy, &saved_decoy, sizeof(decoy), "inactive repeat changed unselected entry");

    if (owned)
    {
        const bool was_released = active && software;
        auto* allocation = find_allocation(owned);
        check((allocation == nullptr) == was_released, "allocation ownership differs");
        if (allocation)
        {
            inspect_allocation(*allocation);
            delete[] owned;
        }
    }
    for (const auto& allocation : allocations)
    {
        check(!allocation.raw, "generated owner leaked an allocation");
    }
}
template <typename T> struct Guarded
{
    std::array<unsigned char, 32> before;
    T value;
    std::array<unsigned char, 32> after;
};

using Renderer = Guarded<ScreenRequestRenderer>;
using Surface = Guarded<ScreenSurfacePosition>;
using Table = Guarded<std::array<ScreenSurfacePosition*, 8>>;
using Surfaces = std::array<Surface, 8>;

template <typename T> std::array<unsigned char, sizeof(T)> snapshot(const T& value)
{
    std::array<unsigned char, sizeof(T)> bytes;
    std::memcpy(bytes.data(), &value, sizeof(value));
    return bytes;
}

template <typename T>
void unchanged(
    const T& value, const std::array<unsigned char, sizeof(T)>& expected, const char* name)
{
    check_bytes(&value, expected.data(), sizeof(value), name);
}

template <typename T> void patterned(T& value, unsigned int salt)
{
    auto* bytes = reinterpret_cast<unsigned char*>(&value);
    for (std::size_t i = 0; i < sizeof(value); ++i)
    {
        bytes[i] = static_cast<unsigned char>(i * 37 + salt);
    }
}

// Every object and pointer is valid before the source executes. The independent
// representation oracle replaces only the two selected coordinate subobjects.
void run_position(unsigned int layout, unsigned int base_index, unsigned int index, int x, int y)
{
    case_number = cases++;
    ++position_cases;
    Renderer renderer;
    Table table;
    Surfaces surfaces;
    patterned(renderer, 0x35);
    patterned(table, 0x67);
    patterned(surfaces, 0x98);
    unsigned short borrowed_pixels[4]{1, 2, 3, 4};
    DirectDrawSurface7VtablePrefix vtable{};
    vtable.Release = release_provider;
    DirectDrawSurface7Prefix interface{&vtable};
    const auto expected_pixels = snapshot(borrowed_pixels);
    const auto expected_vtable = snapshot(vtable);
    const auto expected_interface = snapshot(interface);
    unsigned n = 0;
    for (auto& entry : surfaces)
    {
        entry.value.active = static_cast<unsigned char>(n * 37);
        entry.value.software = static_cast<unsigned char>(n * 63);
        entry.value.pixels = n % 2 ? borrowed_pixels : nullptr;
        entry.value.surface = n % 3 ? &interface : nullptr;
        entry.value.width = n * 5;
        entry.value.height = n * 7;
        entry.value.pitch = n * 11;
        entry.value.position = ScreenPoint{static_cast<int>(n) - 1234, static_cast<int>(n) + 5678};
        ++n;
    }
    renderer.value.width = 800;
    renderer.value.height = 600;
    for (unsigned int i = 0; i < table.value.size(); ++i)
    {
        // Three bijections and an aliasing table exercise selected-index identity.
        const unsigned int selected = layout == 3 ? i % 3 : (i * (2 * layout + 1) + layout) % 8;
        table.value[i] = &surfaces[selected].value;
    }
    renderer.value.surfaces = table.value.data() + base_index;
    auto expected_surfaces = snapshot(surfaces);
    const auto expected_table = snapshot(table);
    const auto expected_renderer = snapshot(renderer);
    const auto* base = reinterpret_cast<const unsigned char*>(&surfaces);
    const auto x_offset =
        reinterpret_cast<const unsigned char*>(&table.value[index]->position.x) - base;
    const auto y_offset =
        reinterpret_cast<const unsigned char*>(&table.value[index]->position.y) - base;
    std::memcpy(expected_surfaces.data() + x_offset, &x, sizeof(x));
    std::memcpy(expected_surfaces.data() + y_offset, &y, sizeof(y));

    const ScreenPoint point = {x, y};
    const auto expected_point = snapshot(point);
    renderer.value.PositionSurface(static_cast<int>(index) - static_cast<int>(base_index), point);
    unchanged(point, expected_point, "by-value argument");
    unchanged(surfaces, expected_surfaces, "surfaces");
    unchanged(table, expected_table, "table");
    unchanged(renderer, expected_renderer, "renderer");
    ++position_calls;

    // A second call must overwrite both fields, including an aliased table slot.
    const unsigned int again = layout == 3 && index < 5 ? index + 3 : index;
    const int next_x = std::bit_cast<int>(~std::bit_cast<std::uint32_t>(y));
    const int next_y = std::bit_cast<int>(~std::bit_cast<std::uint32_t>(x));
    std::memcpy(expected_surfaces.data() + x_offset, &next_x, sizeof(next_x));
    std::memcpy(expected_surfaces.data() + y_offset, &next_y, sizeof(next_y));
    const ScreenPoint next_point = {next_x, next_y};
    renderer.value.PositionSurface(
        static_cast<int>(again) - static_cast<int>(base_index), next_point);
    unchanged(surfaces, expected_surfaces, "surfaces");
    unchanged(table, expected_table, "table");
    unchanged(renderer, expected_renderer, "renderer");
    ++position_calls;

    // Passing an actual stored position by value covers same-object and other-
    // object argument aliases without fabricating byte-overlapping C++ objects.
    for (unsigned int from = 0; from < table.value.size(); ++from)
    {
        const auto before = snapshot(surfaces);
        const auto* source = table.value[from];
        auto expected = before;
        std::memcpy(expected.data() + x_offset, &source->position.x, sizeof(int));
        std::memcpy(expected.data() + y_offset, &source->position.y, sizeof(int));
        renderer.value.PositionSurface(
            static_cast<int>(index) - static_cast<int>(base_index), source->position);
        unchanged(surfaces, expected, "by-value source alias");
        unchanged(table, expected_table, "alias table");
        unchanged(renderer, expected_renderer, "alias renderer");
        ++position_calls;
    }
    unchanged(borrowed_pixels, expected_pixels, "borrowed pixels");
    unchanged(vtable, expected_vtable, "borrowed vtable");
    unchanged(interface, expected_interface, "borrowed interface");
}

} // namespace

// Explicit test providers: all generated primitive-array allocations use this
// guarded arena; production source and comparison builds retain normal CRT use.
void* operator new[](std::size_t bytes)
{
    auto* raw = static_cast<unsigned char*>(std::malloc(bytes + 64));
    if (!raw)
    {
        throw std::bad_alloc();
    }
    std::memset(raw, 0xa7, bytes + 64);
    std::memset(raw + 32, 0x5c, bytes);
    for (auto& allocation : allocations)
    {
        if (!allocation.raw)
        {
            allocation = Allocation{raw, raw + 32, bytes};
            return allocation.pointer;
        }
    }
    fail("generated allocation registry exhausted");
}

void operator delete[](void* memory) noexcept
{
    if (!memory)
    {
        // Modern C++ compilers may omit a null delete[] call. The original
        // unconditional VC6 dispatch is qualified by the target byte audit.
        ++null_events;
        return;
    }
    auto* allocation = find_allocation(memory);
    check(allocation != nullptr, "unknown or double-freed array pointer");
    inspect_allocation(*allocation);
    if (current)
    {
        check(memory == current->original_pixels, "wrong pixel allocation released");
        provider_event(1);
        ++array_events;
    }
    std::free(allocation->raw);
    *allocation = Allocation{};
}

void operator delete[](void* memory, std::size_t) noexcept
{
    ::operator delete[](memory);
}

int main()
{
    for (auto method : {Method::member, Method::indexed})
    {
        // Each possible flag byte is tested independently against both routes.
        // The representative cross product below also covers their interaction.
        for (unsigned byte = 0; byte < 256; ++byte)
        {
            for (unsigned other : {0u, 1u})
            {
                run_cleanup(method, byte, other, 0, 0, false, byte % 33, byte, byte % 8, 3, true);
                run_cleanup(method, other, byte, 0, 0, false, byte % 33, byte, byte % 8, 0, false);
            }
        }
        for (unsigned active : {0u, 1u, 128u, 255u})
        {
            for (unsigned mode : {0u, 1u, 128u, 255u})
            {
                for (unsigned base : {0u, 3u})
                {
                    for (unsigned slot = 0; slot < 8; ++slot)
                    {
                        for (bool alias : {false, true})
                        {
                            run_cleanup(method,
                                active,
                                mode,
                                0,
                                mode * 0x01010101u,
                                false,
                                (active + mode + slot) % 33,
                                active ^ mode,
                                slot,
                                base,
                                alias);
                        }
                    }
                }
            }
        }
        // Null primitive arrays have a valid state contract. Modern compilers
        // may omit the deallocator call; exact VC6 call timing is not claimed.
        for (unsigned active : {1u, 128u, 255u})
        {
            for (unsigned mode : {1u, 128u, 255u})
            {
                for (unsigned base : {0u, 3u})
                {
                    for (unsigned slot = 0; slot < 8; ++slot)
                    {
                        run_cleanup(method, active, mode, 0, 0, true, 0, 0x72, slot, base, true);
                    }
                }
            }
        }
        for (unsigned mode : {0u, 1u})
        {
            for (unsigned base : {0u, 3u})
            {
                for (unsigned mask = 0; mask < 1024; ++mask)
                {
                    constexpr unsigned results[] = {0u, 1u, 0x80000000u, 0xffffffffu};
                    run_cleanup(method,
                        0x91,
                        mode,
                        mask,
                        results[mask % 4],
                        false,
                        17,
                        0xb5,
                        mask % 8,
                        base,
                        (mask & 1) != 0);
                }
            }
        }
    }
    constexpr std::array<int, 9> values{0,
        1,
        -1,
        800,
        -600,
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max(),
        std::bit_cast<int>(0x55555555u),
        std::bit_cast<int>(0xaaaaaaaau)};
    for (unsigned layout = 0; layout < 4; ++layout)
    {
        for (unsigned slot = 0; slot < 8; ++slot)
        {
            for (unsigned base : {0u, 3u})
            {
                for (int x : values)
                {
                    for (int y : values)
                    {
                        run_position(layout, base, slot, x, y);
                    }
                }
                for (unsigned bit = 0; bit < 32; ++bit)
                {
                    const auto word = std::uint32_t{1} << bit;
                    run_position(
                        layout, base, slot, std::bit_cast<int>(word), std::bit_cast<int>(~word));
                }
            }
        }
    }
    std::printf("surface subsystem checks passed: {\"member_cases\":%llu,\"indexed_cases\":%llu,"
                "\"position_cases\":%llu,\"member_calls\":%llu,\"indexed_calls\":%llu,"
                "\"position_calls\":%llu,\"assertions\":%llu,\"representation_checks\":%llu,"
                "\"inspected_bytes\":%llu,\"array_events\":%llu,\"release_events\":%llu,"
                "\"null_delete_events\":%llu}\n",
        static_cast<unsigned long long>(cleanup_cases[0]),
        static_cast<unsigned long long>(cleanup_cases[1]),
        static_cast<unsigned long long>(position_cases),
        static_cast<unsigned long long>(cleanup_calls[0]),
        static_cast<unsigned long long>(cleanup_calls[1]),
        static_cast<unsigned long long>(position_calls),
        static_cast<unsigned long long>(checks),
        static_cast<unsigned long long>(representation_checks),
        static_cast<unsigned long long>(byte_checks),
        static_cast<unsigned long long>(array_events),
        static_cast<unsigned long long>(api_events),
        static_cast<unsigned long long>(null_events));
}
