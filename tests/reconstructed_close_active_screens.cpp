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
unsigned checks = 0, requests = 0, successes = 0, closes = 0, erases = 0;
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

// Genuine renderer ownership; only method inputs are borrowed.
template <> struct Guarded<ScreenRequestRenderer>
{
    renderer_owner_test::Bootstrap renderer_bootstrap;
    std::array<unsigned char, 16> before{};
    ScreenRequestRenderer value{renderer_bootstrap.video(), 8, 4, 1};
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
    renderer_owner_test::RestoreOwnedState renderer_restore{value, renderer_bootstrap};
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
        renderer.value.surface_storage.clear();
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
#if defined(BEACHLIFE_DECOMP_GAME_PROGRESS)
        for (auto& value : game.value.unknown_58)
        {
            value = 0x6d;
        }
        for (auto& value : game.value.words_70)
        {
            value = 0x13579bdfu;
        }
        game.value.scenario_a0 = 0x2468ace0u;
        game.value.byte_a4 = 0x5a;
#endif
        screen_request_renderer = &bounds_renderer.value;
        screen_request_game = &game.value;
        screen_manager = &manager.value;
    }

    ~Environment()
    {
        // Closing only marks entries. End their externally owned lifetimes using
        // the real indexed surface cleanup and node-only Erase before Dialogs.
        CircularListIterator cursor = manager.value.active.Begin();
        const CircularListIterator end = manager.value.active.End();
        while (cursor.node != end.node)
        {
            ActiveScreenEntry* entry = static_cast<CircularListValueNode*>(cursor.node)->entry;
            renderer.value.CleanupSurface(entry->surface);
            cursor = manager.value.active.Erase(cursor);
            ++erases;
            delete entry;
            ++active_releases;
        }
        for (CircularListNode* node = saved_head.value.next; node != &saved_head.value;
            node = node->next)
        {
            delete static_cast<SavedScreenPlacementNode*>(node)->placement;
            ++placement_releases;
        }
        placements.value.saved.Clear();

        auto& r = renderer.value;
        // The actual indexed cleanup above released each WORD allocation.
        // Entry payloads remain scalar-owned; vector backing is real host storage.
        for (auto* entry : r.surface_storage)
        {
            delete entry;
            ++surface_releases;
        }
        r.surface_storage.clear();
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

    void close()
    {
        manager.value.CloseActiveScreens();
        ++closes;
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
            "close changed unrelated renderer state or queue canaries");
        renderer.check();
        bounds_renderer.check();
        manager.check();
        game.check();
#if defined(BEACHLIFE_DECOMP_GAME_PROGRESS)
        for (auto value : game.value.unknown_58)
        {
            require(value == 0x6d, "unused game opaque tail changed");
        }
        for (auto value : game.value.words_70)
        {
            require(value == 0x13579bdfu, "unused game progress word changed");
        }
        require(game.value.scenario_a0 == 0x2468ace0u && game.value.byte_a4 == 0x5a,
            "unused game scenario or mode changed");
#endif
        placements.check();
        active_head.check();
        saved_head.check();
        pool.check();
    }
};

void empty_active_ring()
{
    group = "empty-ring";
    Environment environment;
    environment.manager.value.mode = 3;
    environment.manager.value.previous_mode = 4;
    environment.close();
    environment.check_ring(0);
    require(environment.saved_head.value.next == &environment.saved_head.value &&
                environment.saved_head.value.previous == &environment.saved_head.value &&
                environment.renderer.value.surface_storage.empty(),
        "empty close changed placement or surface ownership");
    environment.check();
}

void ordered_placement_and_borrowed_owners()
{
    group = "ordered-placement";
    Environment environment;
    ScreenDialog& first = environment.add("first", {2, 3, 6, 6}, 7);
    ScreenDialog& second = environment.add("second", {8, 5, 11, 7}, 7);
    require(environment.request("first", 0x31) && environment.request("second", 0x42),
        "real initial requests failed");
    ActiveScreenEntry* const newest = &environment.active(0);
    ActiveScreenEntry* const oldest = &environment.active(1);
    require(newest->item == &second && oldest->item == &first && newest->context == nullptr &&
                oldest->context == nullptr && first.child_44 != nullptr &&
                second.child_44 != nullptr,
        "genuine Dialog/Frame or active order differs");
    newest->state = 9;
    oldest->state = 17;
    auto* const first_frame = first.child_44;
    auto* const second_frame = second.child_44;
    auto* const newest_surface = environment.renderer.value.surface_storage[newest->surface];
    auto* const oldest_surface = environment.renderer.value.surface_storage[oldest->surface];
    auto* const newest_pixels = newest_surface->pixels;
    auto* const oldest_pixels = oldest_surface->pixels;
    CircularListNode* const first_node = environment.active_head.value.next;
    require(environment.saved_head.value.next == &environment.saved_head.value,
        "request setup unexpectedly saved a placement");
    environment.manager.value.mode = 0;
    environment.manager.value.previous_mode = 0;

    environment.close();

    require(newest->state == 2 && oldest->state == 2,
        "closing state2 was not published for every entry");
    require(environment.active_head.value.next == first_node && &environment.active(0) == newest &&
                &environment.active(1) == oldest && newest->item == &second &&
                oldest->item == &first && first.child_44 == first_frame &&
                second.child_44 == second_frame && first.state_59 == 0x31 &&
                second.state_59 == 0x42,
        "close erased/reordered an entry or changed borrowed Dialog ownership");
    require(environment.renderer.value.surface_storage[newest->surface] == newest_surface &&
                environment.renderer.value.surface_storage[oldest->surface] == oldest_surface &&
                newest_surface->active == 1 && oldest_surface->active == 1 &&
                newest_surface->pixels == newest_pixels &&
                oldest_surface->pixels == oldest_pixels && newest_pixels[0] == 0x45ab &&
                newest_pixels[5] == 0x45ab && oldest_pixels[0] == 0x45ab &&
                oldest_pixels[11] == 0x45ab,
        "close prematurely cleaned a genuine software surface");
    CircularListNode* const saved = environment.saved_head.value.next;
    require(saved != &environment.saved_head.value &&
                saved->next == &environment.saved_head.value &&
                saved->previous == &environment.saved_head.value &&
                environment.saved_head.value.previous == saved,
        "same-group close did not reuse one saved placement");
    const auto* placement = static_cast<SavedScreenPlacementNode*>(saved)->placement;
    // Request inserts at the front. The older tail is visited last, so its
    // literal coordinates must overwrite the newer head's same-group record.
    require(placement->group == 7 && placement->x == 2 && placement->y == 3,
        "live forward order or final same-group coordinates differ");
    require(renderer_queue_test::same_rectangle(first.rectangle, {2, 3, 6, 6}) &&
                renderer_queue_test::same_rectangle(second.rectangle, {8, 5, 11, 7}),
        "close moved a Dialog");
    environment.check_ring(2);
    environment.check();
}

void either_mode_suppresses_placement()
{
    group = "mode6-suppression";
    Environment environment;
    ScreenDialog& dialog = environment.add("mode", {4, 2, 8, 5}, 9);
    require(environment.request("mode", 0x52), "mode request failed");
    ActiveScreenEntry& entry = environment.active(0);
    entry.state = 7;
    environment.manager.value.mode = 6;
    environment.manager.value.previous_mode = 0;
    environment.close();
    require(entry.state == 2 && environment.saved_head.value.next == &environment.saved_head.value,
        "current mode6 did not suppress placement while marking close");
    entry.state = 11;
    environment.manager.value.mode = 0;
    environment.manager.value.previous_mode = 6;
    environment.close();
    require(entry.state == 2 && entry.item == &dialog && entry.context == nullptr &&
                environment.saved_head.value.next == &environment.saved_head.value &&
                environment.saved_head.value.previous == &environment.saved_head.value &&
                environment.renderer.value.surface_storage[entry.surface]->active == 1,
        "previous mode6 did not suppress placement or retained ownership changed");
    environment.check_ring(1);
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
        empty_active_ring();
        ordered_placement_and_borrowed_owners();
        either_mode_suppresses_placement();
        require(closes == 4 && requests == 3 && successes == 3 && dialog_owners == 3 &&
                    dialog_releases == 3 && active_releases == 3 && erases == 3 &&
                    surface_releases == 3 && placement_releases == 1,
            "literal close and ownership totals differ");
        require(screen_request_renderer == nullptr && screen_request_game == nullptr &&
                    screen_manager == nullptr && !pool_live && pool_chunk_begin == nullptr &&
                    pool_chunk_end == nullptr && pool_heap_bytes == 0 && pool_lock_word == 0,
            "final borrowed state remains");
        std::printf("close-active groups=3 calls=%u requests=%u dialogs=%u releases=%u active=%u "
                    "erases=%u surfaces=%u placements=%u exchanges=%u checks=%u pass\n",
            closes,
            requests,
            dialog_owners,
            dialog_releases,
            active_releases,
            erases,
            surface_releases,
            placement_releases,
            exchanges,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "close-active %s: %s\n", group, failure.reason);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "close-active host failure: %s\n", failure.what());
        return 3;
    }
}
