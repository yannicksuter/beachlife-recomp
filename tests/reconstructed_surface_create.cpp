#include "pool_chunk_acquire.h"
#include "pool_lock_acquire.h"
#include "pool_lock_guard.h"
#include "pool_return.h"
#include "renderer_queue_test_state.h"
#include "startup_screen_renderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <new>
#include <type_traits>

namespace
{
static_assert(sizeof(void*) == 8 && sizeof(unsigned int) == 4);
static_assert(std::is_aggregate_v<ScreenRequestRenderer>);
static_assert(std::is_aggregate_v<ScreenSurfacePosition>);
static_assert(std::is_trivially_destructible_v<ScreenSurfacePosition>);
static_assert(std::is_standard_layout_v<ScreenSurfacePosition>);
static_assert(offsetof(ScreenSurfacePosition, video_memory) == 24);
static_assert(offsetof(ScreenSurfacePosition, unknown_0d) == 25);
static_assert(offsetof(ScreenSurfacePosition, width) == 28);
static_assert(sizeof(ScreenSurfacePosition) == 56);
static_assert(offsetof(DirectDraw7VtablePrefix, CreateSurface) == 6 * sizeof(void*));
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Release) == 2 * sizeof(void*));
static_assert(offsetof(DirectDrawSurface7VtablePrefix, GetSurfaceDesc) == 22 * sizeof(void*));
static_assert(offsetof(DirectDrawSurface7VtablePrefix, SetColorKey) == 29 * sizeof(void*));
static_assert(offsetof(DirectDrawSurface7VtablePrefix, Unlock) == 32 * sizeof(void*));
static_assert(sizeof(DirectDrawSurface7VtablePrefix) == 33 * sizeof(void*));

struct Failure
{
    const char* reason;
};
const char* group = "setup";
unsigned checks = 0, create_calls = 0, initialize_calls = 0, cleanup_calls = 0;
unsigned owned_entries = 0, released_entries = 0, exchanges = 0;
bool pool_scope_live = false;

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        throw Failure{reason};
    }
}

template <class T> struct Guarded
{
    std::array<unsigned char, 16> before{};
    T value{};
    std::array<unsigned char, 16> after{};
    Guarded()
    {
        before.fill(0xa6);
        after.fill(0x6a);
    }
    void check() const
    {
        for (std::size_t n = 0; n < before.size(); ++n)
        {
            require(before[n] == 0xa6 && after[n] == 0x6a, "outside guard changed");
        }
    }
};

struct PoolScope
{
    std::array<unsigned char, 16> before{};
    alignas(8) unsigned char bytes[8192];
    std::array<unsigned char, 16> after{};
    PoolScope()
    {
        require(!pool_scope_live && pool_chunk_begin == nullptr && pool_chunk_end == nullptr &&
                    pool_heap_bytes == 0 && pool_lock_word == 0,
            "inherited pool state");
        for (auto* bucket : pool_chunk_buckets)
        {
            require(bucket == nullptr, "inherited free link");
        }
        before.fill(0x47);
        after.fill(0x74);
        std::fill(std::begin(bytes), std::end(bytes), 0x93);
        pool_chunk_begin = bytes;
        pool_chunk_end = bytes + sizeof(bytes);
        pool_scope_live = true;
    }
    ~PoolScope()
    {
        // All tables and entries are released first. Retire raw backing roots
        // before the actual byte-array storage lifetime ends.
        for (auto& bucket : pool_chunk_buckets)
        {
            bucket = nullptr;
        }
        pool_chunk_begin = nullptr;
        pool_chunk_end = nullptr;
        pool_heap_bytes = 0;
        pool_scope_live = false;
    }
    void check() const
    {
        require(
            pool_chunk_end == bytes + sizeof(bytes) && pool_heap_bytes == 0 && pool_lock_word == 0,
            "seeded pool escaped or retained lock");
        for (std::size_t n = 0; n < before.size(); ++n)
        {
            require(before[n] == 0x47 && after[n] == 0x74, "pool backing guard changed");
        }
    }
};

void layout(const ScreenRequestRenderer& r)
{
    const auto offset = [&](const void* p)
    {
        return reinterpret_cast<std::uintptr_t>(p) - reinterpret_cast<std::uintptr_t>(&r);
    };
    require(offset(r.unknown_20) == 40 && sizeof(r.unknown_20) == 24 && offset(&r.draw) == 64 &&
                offset(r.unknown_3c) == 72 && sizeof(r.unknown_3c) == 8,
        "draw-pointer native layout differs");
    require(offset(&r.surfaces) == 120 && offset(&r.surfaces_end) == 128 &&
                offset(&r.surfaces_capacity) == 136 && offset(&r.current_surface) == 144,
        "table endpoint native layout differs");
    require(offset(&r.word_60) == 108 && offset(&r.fill_color) == 150 &&
                offset(&r.dirty_rectangles) == 152 && sizeof(RendererRectangleQueue) == 80 &&
                offset(&r.buffer_130) == 376 && sizeof(r) == 384,
        "retained native suffix or size differs");
}

void initialize_renderer(ScreenRequestRenderer& r)
{
    renderer_queue_test::initialize(r, 0x31);
    r.software_mode = 1;
    r.surfaces = r.surfaces_end = r.surfaces_capacity = nullptr;
    r.draw = nullptr; // The complete hardware branch exists but is not called.
    layout(r);
}

void pixels(ScreenSurfacePosition& entry, int width, int height)
{
    require(entry.software == 1 && entry.pixels != nullptr &&
                entry.width == static_cast<unsigned>(width) &&
                entry.height == static_cast<unsigned>(height) &&
                entry.pitch == static_cast<unsigned>(width * 2) && entry.prepared == 0,
        "software initialization fields differ");
    for (int n = 0; n < width * height; ++n)
    {
        entry.pixels[n] = static_cast<unsigned short>(0x1200 + n);
    }
    for (int n = 0; n < width * height; ++n)
    {
        require(entry.pixels[n] == static_cast<unsigned short>(0x1200 + n), "owned pixel differs");
    }
}

struct BorrowedEntries
{
    std::array<Guarded<ScreenSurfacePosition>, 3> entries;
    ~BorrowedEntries()
    {
        for (auto& e : entries)
        {
            if (e.value.pixels != nullptr)
            {
                // External receipt cleanup also handles the safe inactive-byte
                // control: restore only its ownership gate, then use real cleanup.
                e.value.active = 1;
                e.value.Cleanup();
                ++cleanup_calls;
            }
        }
    }
};

void reused_first_inactive()
{
    group = "reuse-first-inactive";
    Guarded<ScreenRequestRenderer> renderer;
    initialize_renderer(renderer.value);
    BorrowedEntries owners;
    auto& e = owners.entries;
    for (unsigned n = 0; n < e.size(); ++n)
    {
        e[n].value.active = n == 0 ? 1 : 0;
        e[n].value.software = 1;
        e[n].value.position = {17 + static_cast<int>(n), -9};
        e[n].value.prepared = 0x71;
        e[n].value.video_memory = 0xa3;
        renderer_queue_test::pattern(e[n].value.unknown_02, 0x26);
        renderer_queue_test::pattern(e[n].value.unknown_0d, 0x4a);
    }
    e[0].value.pixels = new unsigned short[1]{0x7463};
    Guarded<std::array<ScreenSurfacePosition*, 3>> table;
    table.value = {&e[0].value, &e[1].value, &e[2].value};
    auto& r = renderer.value;
    r.surfaces = table.value.data();
    r.surfaces_end = r.surfaces_capacity = table.value.data() + table.value.size();
    const auto before = r;
    const auto inactive = e[1].value;
    const int index = r.CreateSurface(3, 2, 0x100);
    ++create_calls;
    require(index == 1 && e[1].value.active == 1, "first inactive slot not activated");
    pixels(e[1].value, 3, 2);
    require(e[0].value.pixels[0] == 0x7463 && e[0].value.active == 1 && e[2].value.active == 0 &&
                e[2].value.pixels == nullptr,
        "other slot changed");
    require(e[1].value.position.x == 18 && e[1].value.position.y == -9 &&
                e[1].value.surface == nullptr &&
                renderer_queue_test::same_bytes(e[1].value.unknown_02, inactive.unknown_02) &&
                renderer_queue_test::same_entry_extension(e[1].value, inactive),
        "reuse changed retained entry state");
    require(renderer_queue_test::equal(r, before), "reuse changed renderer/table endpoints");
    renderer.check();
    table.check();
    for (const auto& entry : e)
    {
        entry.check();
    }
}

struct GrowingOwner
{
    Guarded<ScreenRequestRenderer> renderer;
    std::array<ScreenSurfacePosition*, 33> entries{};
    unsigned count = 0;
    GrowingOwner()
    {
        initialize_renderer(renderer.value);
    }
    ~GrowingOwner()
    {
        auto& r = renderer.value;
        for (unsigned n = 0; n < count; ++n)
        {
            if (entries[n]->pixels != nullptr)
            {
                entries[n]->active = 1;
                entries[n]->Cleanup();
                ++cleanup_calls;
            }
            delete entries[n]; // Actual scalar entry allocation; trivial lifetime.
            ++released_entries;
        }
        if (r.surfaces != nullptr)
        {
            const auto bytes = static_cast<unsigned>(
                (r.surfaces_capacity - r.surfaces) * sizeof(ScreenSurfacePosition*));
            if (bytes > 128)
            {
                ::operator delete(r.surfaces);
            }
            else
            {
                ReturnPool(r.surfaces, bytes);
            }
        }
        r.surfaces = r.surfaces_end = r.surfaces_capacity = nullptr;
    }
};

void growth_and_ownership()
{
    group = "growth-pool-and-scalar";
    PoolScope pool;
    {
        GrowingOwner owner;
        auto& r = owner.renderer.value;
        const auto before = r;
        constexpr std::array<unsigned, 33> capacities{1,
            2,
            4,
            4,
            8,
            8,
            8,
            8,
            16,
            16,
            16,
            16,
            16,
            16,
            16,
            16,
            32,
            32,
            32,
            32,
            32,
            32,
            32,
            32,
            32,
            32,
            32,
            32,
            32,
            32,
            32,
            32,
            64};
        for (unsigned n = 0; n < capacities.size(); ++n)
        {
            auto** old_table = r.surfaces;
            const int index = r.CreateSurface(2, 1, 0);
            ++create_calls;
            // Record the actual newly published owner before any oracle can throw.
            owner.entries[n] = r.surfaces[n];
            owner.count = n + 1;
            ++owned_entries;
            require(index == static_cast<int>(n) && r.surfaces_end == r.surfaces + n + 1 &&
                        r.surfaces_capacity == r.surfaces + capacities[n],
                "literal append index or capacity differs");
            if (n != 0 && capacities[n] == capacities[n - 1])
            {
                require(r.surfaces == old_table, "spare-capacity append replaced storage");
            }
            require(owner.entries[n]->active == 1 && owner.entries[n]->position.x == 0 &&
                        owner.entries[n]->position.y == 0,
                "new entry publication differs");
            pixels(*owner.entries[n], 2, 1);
            // Fresh allocations leave API/opaque members uninitialized; do not
            // inspect or copy them. Only source-written members are observed.
            for (unsigned j = 0; j <= n; ++j)
            {
                require(r.surfaces[j] == owner.entries[j] &&
                            owner.entries[j]->pixels[0] == 0x1200 &&
                            owner.entries[j]->pixels[1] == 0x1201,
                    "existing entry order or payload lost during table growth");
            }
            auto expected = before;
            expected.surfaces = r.surfaces;
            expected.surfaces_end = r.surfaces_end;
            expected.surfaces_capacity = r.surfaces_capacity;
            require(renderer_queue_test::equal(r, expected), "growth changed unrelated renderer");
        }
        // Native pointer width makes capacities1/2/4/8/16 pooled, then32/64 scalar.
        // The complete growth path frees the previous256-byte scalar table at33.
        require(pool_chunk_begin == pool.bytes + 4960, "literal real refill consumption differs");
        for (unsigned bucket : {0U, 1U, 3U, 7U, 15U})
        {
            require(pool_chunk_buckets[bucket] != nullptr, "pooled old table was not returned");
        }
        owner.renderer.check();
        pool.check();
    }
    require(owned_entries == 33 && released_entries == 33, "entry owners did not close");
    pool.check();
}

void direct_initializer_and_indexed_cleanup()
{
    group = "direct-initialize-empty-and-live";
    Guarded<ScreenRequestRenderer> renderer;
    initialize_renderer(renderer.value);
    BorrowedEntries owners;
    auto& entry = owners.entries[0].value;
    Guarded<std::array<ScreenSurfacePosition*, 1>> table;
    table.value[0] = &entry;
    auto& r = renderer.value;
    r.surfaces = table.value.data();
    r.surfaces_end = r.surfaces_capacity = r.surfaces + 1;
    constexpr std::array<std::array<int, 2>, 2> sizes{{{0, 3}, {2, 2}}};
    for (const auto size : sizes)
    {
        entry.active = 0;
        entry.position = {-7, 11};
        entry.video_memory = 0xd3;
        renderer_queue_test::pattern(entry.unknown_02, 0x53);
        renderer_queue_test::pattern(entry.unknown_0d, 0x84);
        const auto before = entry;
        const auto renderer_before = r;
        r.InitializeSurface(size[0], size[1], &entry, 0xff);
        ++initialize_calls;
        require(entry.active == 0 && entry.position.x == -7 && entry.position.y == 11 &&
                    entry.surface == nullptr &&
                    renderer_queue_test::same_bytes(entry.unknown_02, before.unknown_02) &&
                    renderer_queue_test::same_entry_extension(entry, before),
            "initializer changed fields outside its write set");
        pixels(entry, size[0], size[1]);
        require(renderer_queue_test::equal(r, renderer_before), "initializer changed renderer");
        entry.active = 1; // Caller owns activation; direct initializer does not.
        r.CleanupSurface(0);
        ++cleanup_calls;
        require(entry.active == 0 && entry.pixels == nullptr && entry.width == 0 &&
                    entry.height == 0 && entry.pitch == 0 && entry.position.x == -7 &&
                    entry.position.y == 11 && entry.video_memory == 0xd3,
            "real indexed cleanup differs");
    }
    renderer.check();
    table.check();
    owners.entries[0].check();
}
} // namespace

// Existing R162/R187 test-only uncontended OS boundary. No game, allocator,
// DirectDraw or surface callback is supplied by this fixture.
extern "C" LONG InterlockedExchange(LONG* target, LONG value)
{
    volatile LONG* const word = target;
    require(pool_scope_live && target == &pool_lock_word && value == 1 && *word == 0,
        "unexpected uncontended exchange");
    const LONG previous = *word;
    *word = value;
    ++exchanges;
    return previous;
}
extern "C" void Sleep(DWORD)
{
    require(false, "unexpected wait in finite uncontended fixture");
}

int main()
{
    try
    {
        reused_first_inactive();
        growth_and_ownership();
        direct_initializer_and_indexed_cleanup();
        require(create_calls == 34 && initialize_calls == 2 && cleanup_calls == 37 &&
                    owned_entries == 33 && released_entries == 33 && exchanges == 10,
            "literal operation/ownership totals differ");
        require(pool_chunk_begin == nullptr && pool_chunk_end == nullptr && pool_heap_bytes == 0 &&
                    pool_lock_word == 0 && !pool_scope_live,
            "final pool state remains");
        std::printf("surface-create groups=3 create=%u direct-initialize=%u cleanup=%u owners=%u "
                    "releases=%u exchanges=%u checks=%u renderer-size=%zu entry-size=%zu pass\n",
            create_calls,
            initialize_calls,
            cleanup_calls,
            owned_entries,
            released_entries,
            exchanges,
            checks,
            sizeof(ScreenRequestRenderer),
            sizeof(ScreenSurfacePosition));
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "surface-create %s: %s\n", group, failure.reason);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "surface-create host failure: %s\n", failure.what());
        return 3;
    }
}
