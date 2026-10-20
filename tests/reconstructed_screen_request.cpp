#include "pool_chunk_acquire.h"
#include "pool_lock_acquire.h"
#include "pool_lock_guard.h"
#include "pool_return.h"
#include "renderer_queue_test_state.h"
#include "startup_screen_request.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <memory>
#include <new>
#include <type_traits>

namespace
{
static_assert(sizeof(void*) == 8 && sizeof(unsigned int) == 4);
static_assert(std::is_aggregate_v<ScreenManager>);
static_assert(std::is_aggregate_v<ScreenRequestGamePrefix>);
static_assert(std::is_aggregate_v<ScreenPlacementStore>);
static_assert(std::is_aggregate_v<ActiveScreenEntry>);
static_assert(std::is_trivially_destructible_v<ActiveScreenEntry>);
static_assert(std::is_trivially_destructible_v<ScreenSurfacePosition>);
static_assert(sizeof(SavedScreenPlacementNode) == sizeof(CircularListValueNode));

struct Failure
{
    const char* reason;
};
const char* group = "setup";
unsigned checks = 0, requests = 0, successes = 0;
unsigned dialog_owners = 0, dialog_releases = 0;
unsigned active_releases = 0, surface_releases = 0, placement_releases = 0;
unsigned exchanges = 0;
bool pool_live = false;

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
        before.fill(0x53);
        after.fill(0xa9);
    }
    void check() const
    {
        for (unsigned n = 0; n < before.size(); ++n)
        {
            require(before[n] == 0x53 && after[n] == 0xa9, "typed backing guard changed");
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
        require(!pool_live && pool_chunk_begin == nullptr && pool_chunk_end == nullptr &&
                    pool_heap_bytes == 0 && pool_lock_word == 0,
            "inherited pool state");
        for (auto* bucket : pool_chunk_buckets)
        {
            require(bucket == nullptr, "inherited free list");
        }
        before.fill(0xc7);
        after.fill(0x7c);
        std::fill(std::begin(bytes), std::end(bytes), 0x86);
        pool_chunk_begin = bytes;
        pool_chunk_end = bytes + sizeof(bytes);
        pool_live = true;
    }
    ~PoolScope()
    {
        // Every active node, saved node, surface table and genuine child-ring
        // owner has ended before this member's byte backing lifetime ends.
        for (auto& bucket : pool_chunk_buckets)
        {
            bucket = nullptr;
        }
        pool_chunk_begin = nullptr;
        pool_chunk_end = nullptr;
        pool_heap_bytes = 0;
        pool_live = false;
    }
    void check() const
    {
        require(
            pool_chunk_end == bytes + sizeof(bytes) && pool_heap_bytes == 0 && pool_lock_word == 0,
            "finite pool span escaped or retained a lock");
        for (unsigned n = 0; n < before.size(); ++n)
        {
            require(before[n] == 0xc7 && after[n] == 0x7c, "pool backing guard changed");
        }
    }
};

struct Environment
{
    // This first member outlives every real child-ring/node/table allocation.
    PoolScope pool;
    Guarded<ScreenRequestRenderer> renderer;
    Guarded<ScreenRequestRenderer> bounds_renderer;
    Guarded<ScreenManager> manager;
    Guarded<ScreenRequestGamePrefix> game;
    Guarded<ScreenPlacementStore> placements;
    Guarded<CircularListNode> active_head;
    Guarded<CircularListNode> saved_head;
    std::array<std::unique_ptr<ScreenChildLink>, 3> registry;
    unsigned registered = 0;

    Environment()
    {
        require(screen_request_renderer == nullptr && screen_request_game == nullptr &&
                    screen_manager == nullptr,
            "another borrowed game read model is active");
        renderer_queue_test::initialize(renderer.value, 0x21);
        renderer.value.software_mode = 1;
        renderer.value.width = 64;
        renderer.value.height = 48;
        renderer.value.surfaces = nullptr;
        renderer.value.surfaces_end = nullptr;
        renderer.value.surfaces_capacity = nullptr;
        renderer.value.draw = nullptr;
        renderer.value.current_surface = -1;
        renderer.value.fill_color = 0x45ab;
        renderer_queue_test::initialize(bounds_renderer.value, 0x62);
        bounds_renderer.value.width = 16;
        bounds_renderer.value.height = 12;
        active_head.value = {&active_head.value, &active_head.value};
        saved_head.value = {&saved_head.value, &saved_head.value};
        manager.value.renderer = &renderer.value;
        manager.value.active.head = &active_head.value;
        manager.value.mode = 6;
        manager.value.previous_mode = 6;
        placements.value.saved.head = &saved_head.value;
        game.value.placement = &placements.value;
        screen_request_renderer = &bounds_renderer.value;
        screen_request_game = &game.value;
        screen_manager = &manager.value;
    }

    ~Environment()
    {
        // RequestScreen allocates trivial entries and borrows the Dialog.
        // Clear is the real node-only operation; it must not own the payload.
        for (CircularListNode* node = active_head.value.next; node != &active_head.value;
            node = node->next)
        {
            ActiveScreenEntry* entry = static_cast<CircularListValueNode*>(node)->entry;
            renderer.value.CleanupSurface(entry->surface);
            delete entry;
            ++active_releases;
        }
        manager.value.active.Clear();
        for (CircularListNode* node = saved_head.value.next; node != &saved_head.value;
            node = node->next)
        {
            delete static_cast<SavedScreenPlacementNode*>(node)->placement;
            ++placement_releases;
        }
        placements.value.saved.Clear();

        auto& r = renderer.value;
        if (r.surfaces != nullptr)
        {
            for (auto** p = r.surfaces; p != r.surfaces_end; ++p)
            {
                // The real indexed cleanup already released each WORD array.
                // Entry storage came from scalar operator new, not an array.
                delete *p;
                ++surface_releases;
            }
            const unsigned bytes = static_cast<unsigned>(
                (r.surfaces_capacity - r.surfaces) * sizeof(ScreenSurfacePosition*));
            if (bytes > 128)
            {
                ::operator delete(r.surfaces);
            }
            else
            {
                ReturnPool(r.surfaces, bytes);
            }
            r.surfaces = r.surfaces_end = r.surfaces_capacity = nullptr;
        }
        manager.value.registry = nullptr;
        for (auto& link : registry)
        {
            if (link)
            {
                // The actual kind4 deleting path destroys the genuine Dialog,
                // owned name/Frame/base and child-ring sentinel.
                link.reset();
                ++dialog_releases;
            }
        }
        screen_request_renderer = nullptr;
        screen_request_game = nullptr;
        screen_manager = nullptr;
    }

    ScreenDialog& add(const char* name,
        ScreenRectangle rectangle,
        unsigned group_value,
        unsigned char restore = 0)
    {
        require(registered < registry.size(), "registry fixture capacity exceeded");
        auto dialog = std::make_unique<ScreenDialog>(name, rectangle, "nOnE", group_value, restore);
        auto link = std::make_unique<ScreenChildLink>(dialog.get());
        ScreenDialog& result = *dialog.release();
        ++dialog_owners;
        // Only fields untouched by the genuine constructors are initialized.
        result.word_2c = 0;
        std::fill(std::begin(result.unknown_31), std::end(result.unknown_31), 0x26);
        result.previous_x_50 = 0;
        result.previous_y_54 = 0;
        std::fill(std::begin(result.unknown_5a), std::end(result.unknown_5a), 0x35);
        std::fill(std::begin(result.unknown_62), std::end(result.unknown_62), 0x53);
        result.remaining_68 = 0.0f;
        result.duration_6c = 0.0f;
        result.word_70 = 0;
        result.child_44->word_2c = 0;
        std::fill(
            std::begin(result.child_44->unknown_31), std::end(result.child_44->unknown_31), 0);
        result.preparation_flag = 0;
        if (registered != 0)
        {
            registry[registered - 1]->next = link.get();
        }
        else
        {
            manager.value.registry = link.get();
        }
        registry[registered++] = std::move(link);
        return result;
    }

    bool request(const char* name, unsigned char flag)
    {
        // Both new and every old entry use the original Intro null-context domain.
        const bool result = manager.value.RequestScreen(name, nullptr, flag);
        ++requests;
        successes += result ? 1 : 0;
        return result;
    }

    ActiveScreenEntry& active(unsigned position)
    {
        CircularListNode* node = active_head.value.next;
        while (position != 0 && node != &active_head.value)
        {
            node = node->next;
            --position;
        }
        require(node != &active_head.value && position == 0, "active entry is absent");
        return *static_cast<CircularListValueNode*>(node)->entry;
    }

    void check_ring(unsigned expected)
    {
        CircularListNode* previous = &active_head.value;
        CircularListNode* node = previous->next;
        for (unsigned n = 0; n < expected; ++n)
        {
            require(node != &active_head.value && node->previous == previous,
                "active ring order or previous edge differs");
            require(static_cast<CircularListValueNode*>(node)->entry->context == nullptr,
                "null context publication differs");
            previous = node;
            node = node->next;
        }
        require(node == &active_head.value && active_head.value.previous == previous,
            "active ring extent differs");
    }

    void check_surface(const ActiveScreenEntry& entry, int index, ScreenRectangle rectangle)
    {
        require(entry.surface == index, "captured surface index differs");
        const auto* surface = renderer.value.surfaces[index];
        const unsigned width = static_cast<unsigned>(rectangle.right - rectangle.left);
        const unsigned height = static_cast<unsigned>(rectangle.bottom - rectangle.top);
        require(surface->active == 1 && surface->software == 1 && surface->prepared == 1 &&
                    surface->width == width && surface->height == height &&
                    surface->pitch == width * 2 && surface->position.x == rectangle.left &&
                    surface->position.y == rectangle.top,
            "real created/prepared/positioned surface differs");
        for (unsigned n = 0; n < width * height; ++n)
        {
            require(surface->pixels[n] == 0x45ab, "prepared literal WORD pixel differs");
        }
    }

    void check() const
    {
        require(manager.value.renderer == &renderer.value &&
                    screen_request_renderer == &bounds_renderer.value &&
                    game.value.placement == &placements.value &&
                    screen_request_game == &game.value && screen_manager == &manager.value,
            "borrowed owner/global identity changed");
        require(renderer.value.width == 64 && renderer.value.height == 48 &&
                    bounds_renderer.value.width == 16 && bounds_renderer.value.height == 12 &&
                    renderer.value.current_surface == -1 &&
                    renderer.value.dirty_rectangle_count == 2 &&
                    renderer.value.dirty_rectangles.values.size() == 2 &&
                    renderer_queue_test::same_rectangle(
                        renderer.value.dirty_rectangles.values[0], {-7, -3, -2, 1}) &&
                    renderer_queue_test::same_rectangle(
                        renderer.value.dirty_rectangles.values[1], {13, 17, 19, 23}),
            "request changed unrelated renderer state");
        renderer.check();
        bounds_renderer.check();
        manager.check();
        game.check();
        placements.check();
        active_head.check();
        saved_head.check();
        pool.check();
    }
};

void lookup_and_first_name()
{
    group = "case-sensitive-first-name";
    Environment environment;
    require(!environment.request("Exact", 1), "empty registry did not fail");
    ScreenDialog& first = environment.add("Exact", {2, 3, 6, 6}, 1);
    ScreenDialog& later = environment.add("Exact", {8, 1, 10, 3}, 2);
    const auto before = environment.renderer.value;
    require(!environment.request("exact", 1) && !environment.request("missing", 1),
        "case-sensitive missing lookup did not fail");
    require(environment.active_head.value.next == &environment.active_head.value &&
                renderer_queue_test::equal(environment.renderer.value, before),
        "failed lookup had an active/surface effect");
    require(environment.request("Exact", 0x7e), "present first name did not succeed");
    const auto& entry = environment.active(0);
    require(entry.item == &first && entry.state == 0 && first.state_59 == 0x7e &&
                first.preparation_flag == 1 && later.preparation_flag == 0 && later.state_59 == 0,
        "first equal name/flag/preparation publication differs");
    environment.check_surface(entry, 0, {2, 3, 6, 6});
    environment.check_ring(1);
    environment.check();
}

void null_context_replacement()
{
    group = "null-context-first-group";
    ScreenItem child({1, 2, 3, 4});
    child.kind_28 = 3;
    child.word_2c = 0;
    std::fill(std::begin(child.unknown_31), std::end(child.unknown_31), 0);
    child.preparation_flag = 0;
    {
        // The borrowed child outlives the genuine Dialog ring clearing.
        Environment environment;
        ScreenDialog& first = environment.add("first", {1, 1, 3, 3}, 7);
        ScreenDialog& second = environment.add("second", {6, 7, 8, 9}, 7);
        second.Unresolved10(&child);
        require(environment.request("first", 0x11), "first request failed");
        ActiveScreenEntry* old = &environment.active(0);
        require(environment.request("second", 0x22), "replacement request failed");
        require(old->state == 2, "first matching group was not replaced with state2");
        require(old->item == &first && environment.active(0).item == &second &&
                    environment.active(0).state == 0 && second.state_59 == 0x22 &&
                    child.preparation_flag == 1 &&
                    renderer_queue_test::same_rectangle(child.rectangle, {7, 9, 9, 11}),
            "replacement/real child forwarding differs");
        // A later same-group entry is deliberately distinct so the oracle proves
        // the original first-match-only replacement, not merely the final count.
        old->state = 9;
        ActiveScreenEntry* prior_first = &environment.active(0);
        require(environment.request("second", 0x33), "repeated null request failed");
        require(prior_first->state == 2 && old->state == 9 && environment.active(0).state == 0 &&
                    &environment.active(0) != prior_first && second.state_59 == 0x33,
            "null request suppression or first-group-only order differs");
        environment.check_surface(environment.active(0), 2, {6, 7, 8, 9});
        environment.check_surface(environment.active(1), 1, {6, 7, 8, 9});
        environment.check_surface(environment.active(2), 0, {1, 1, 3, 3});
        environment.check_ring(3);
        environment.check();
    }
    require(renderer_queue_test::same_rectangle(child.rectangle, {7, 9, 9, 11}),
        "borrowed child did not survive Dialog destruction");
}

void real_placement_and_edge_rejection()
{
    group = "saved-placement-and-edge";
    Environment environment;
    environment.manager.value.mode = 0;
    environment.manager.value.previous_mode = 0;
    ScreenDialog& moved = environment.add("moved", {2, 3, 6, 6}, 10, 1);
    ScreenDialog& rejected = environment.add("edge", {1, 2, 5, 5}, 11, 1);
    environment.placements.value.WritePlacement(5, 6, 10);
    environment.placements.value.WritePlacement(12, 1, 11);
    require(environment.request("moved", 0), "restored request failed");
    require(renderer_queue_test::same_rectangle(moved.rectangle, {5, 6, 9, 9}) &&
                renderer_queue_test::same_rectangle(moved.child_44->rectangle, {5, 6, 9, 36}) &&
                moved.preparation_flag == 1 && moved.state_59 == 0,
        "real Dialog/Frame movement or final flag differs");
    environment.check_surface(environment.active(0), 0, {5, 6, 9, 9});
    int x = -1, y = -1;
    require(environment.placements.value.FindPlacement(10, &x, &y) && x == 5 && y == 6,
        "real Move-Save-Write placement differs");
    require(environment.request("edge", 0), "edge-rejected request failed");
    require(renderer_queue_test::same_rectangle(rejected.rectangle, {1, 2, 5, 5}) &&
                renderer_queue_test::same_rectangle(rejected.child_44->rectangle, {1, 2, 5, 32}) &&
                environment.active(1).state == 0,
        "equality-at-global-edge was accepted or another group replaced");
    environment.check_surface(environment.active(0), 1, {1, 2, 5, 5});
    require(environment.placements.value.FindPlacement(11, &x, &y) && x == 12 && y == 1,
        "rejected placement changed its saved record");
    environment.check_ring(2);
    environment.check();
}
} // namespace

// Existing qualified pool test boundary only: finite, uncontended and not an
// actual OS atomicity or production contract. No game/API/allocator provider.
extern "C" LONG InterlockedExchange(LONG* target, LONG value)
{
    volatile LONG* const word = target;
    require(pool_live && target == &pool_lock_word && value == 1 && *word == 0,
        "unexpected uncontended exchange");
    const LONG previous = *word;
    *word = value;
    ++exchanges;
    return previous;
}
extern "C" void Sleep(DWORD)
{
    require(false, "unexpected wait in finite fixture");
}

int main()
{
    try
    {
        lookup_and_first_name();
        null_context_replacement();
        real_placement_and_edge_rejection();
        require(requests == 9 && successes == 6 && dialog_owners == 6 && dialog_releases == 6 &&
                    active_releases == 6 && surface_releases == 6 && placement_releases == 2,
            "literal request/ownership totals differ");
        require(screen_request_renderer == nullptr && screen_request_game == nullptr &&
                    screen_manager == nullptr && !pool_live && pool_chunk_begin == nullptr &&
                    pool_chunk_end == nullptr && pool_heap_bytes == 0 && pool_lock_word == 0,
            "final borrowed state remains");
        std::printf("screen-request groups=3 calls=%u success=%u dialogs=%u releases=%u active=%u "
                    "surfaces=%u placements=%u exchanges=%u checks=%u pass\n",
            requests,
            successes,
            dialog_owners,
            dialog_releases,
            active_releases,
            surface_releases,
            placement_releases,
            exchanges,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "screen-request %s: %s\n", group, failure.reason);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "screen-request host failure: %s\n", failure.what());
        return 3;
    }
}
