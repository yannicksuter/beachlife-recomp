#include "pool_chunk_acquire.h"
#include "pool_lock_acquire.h"
#include "pool_lock_guard.h"
#include "pool_return.h"
#include "renderer_queue_test_state.h"
#include "startup_screen_dialog_resource.h"
#include "startup_screen_frame_item.h"
#include "startup_screen_request.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <memory>
#include <new>
#include <type_traits>

namespace
{
static_assert(sizeof(void*) == 8 && sizeof(unsigned int) == 4);
static_assert(std::is_aggregate_v<ScreenManager>);
static_assert(std::is_trivially_destructible_v<ActiveScreenEntry>);
static_assert(std::is_trivially_destructible_v<ScreenSurfacePosition>);
static_assert(std::is_same_v<decltype(&ScreenManager::ActivateScreens), void (ScreenManager::*)()>);

struct Failure
{
    const char* reason;
};
unsigned checks = 0, calls = 0, setup_requests = 0, exchanges = 0;
unsigned dialogs = 0, dialog_releases = 0, frames = 0, frame_releases = 0;
unsigned active_releases = 0, surface_releases = 0, borrowed_survivors = 0;
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
    // Backing outlives genuine ring nodes, active payloads and the surface table.
    PoolScope pool;
    Guarded<ScreenRequestRenderer> renderer;
    Guarded<ScreenManager> receiver;
    Guarded<ScreenManager> global_manager;
    Guarded<CircularListNode> active_head;
    Guarded<CircularListNode> global_head;
    std::array<std::unique_ptr<ScreenChildLink>, 2> registration;
    std::array<std::unique_ptr<ScreenFrameItem>, 2> borrowed_frames;
    unsigned registered = 0;

    Environment()
    {
        require(screen_request_renderer == nullptr && screen_request_game == nullptr &&
                    screen_manager == nullptr && screen_dialog_resources == nullptr,
            "another borrowed read model is active");
        renderer_queue_test::initialize(renderer.value, 0x21);
        renderer.value.software_mode = 1;
        renderer.value.width = 64;
        renderer.value.height = 48;
        renderer.value.current_surface = -1;
        renderer.value.fill_color = 0x45ab;
        active_head.value = {&active_head.value, &active_head.value};
        global_head.value = {&global_head.value, &global_head.value};
        receiver.value.renderer = &renderer.value;
        receiver.value.active.head = &active_head.value;
        receiver.value.mode = 6;
        receiver.value.previous_mode = 6;
        global_manager.value.renderer = &renderer.value;
        global_manager.value.active.head = &global_head.value;
        global_manager.value.mode = 0x1234;
        global_manager.value.previous_mode = 0x5678;
        screen_request_renderer = &renderer.value;
        screen_manager = &global_manager.value;
    }

    ~Environment()
    {
        for (CircularListNode* node = active_head.value.next; node != &active_head.value;
            node = node->next)
        {
            ActiveScreenEntry* entry = static_cast<CircularListValueNode*>(node)->entry;
            renderer.value.CleanupSurface(entry->surface);
            delete entry;
            ++active_releases;
        }
        receiver.value.active.Clear();

        auto& r = renderer.value;
        if (r.surfaces != nullptr)
        {
            for (auto** p = r.surfaces; p != r.surfaces_end; ++p)
            {
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

        receiver.value.registry = nullptr;
        for (auto& link : registration)
        {
            if (link)
            {
                // Kind4 really deletes the Dialog, its owned Frame and its
                // borrowed-child ring nodes. It does not delete link->next.
                link.reset();
                ++dialog_releases;
                ++frame_releases;
            }
        }
        for (auto& frame : borrowed_frames)
        {
            if (frame)
            {
                if (frame->kind_28 == 11 && frame->word_48 == 0x2468u &&
                    frame->children.head->next == frame->children.head &&
                    frame->children.head->previous == frame->children.head)
                {
                    ++borrowed_survivors;
                }
                frame.reset();
                ++frame_releases;
            }
        }
        screen_request_renderer = nullptr;
        screen_manager = nullptr;
    }

    ScreenDialog& create(const char* name, unsigned group)
    {
        require(registered < registration.size(), "finite registration limit");
        auto dialog =
            std::make_unique<ScreenDialog>(name, ScreenRectangle{2, 3, 6, 6}, "nOnE", group, 0);
        auto link = std::make_unique<ScreenChildLink>(dialog.get());
        ScreenDialog& result = *dialog.release();
        const unsigned index = registered++;
        link->next = receiver.value.registry;
        registration[index] = std::move(link);
        receiver.value.registry = registration[index].get();
        ++dialogs;
        ++frames; // Real child_44, distinct from the separately borrowed Frame.

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

        auto& frame = borrowed_frames[index];
        frame = std::make_unique<ScreenFrameItem>(ScreenRectangle{9, 10, 13, 14}, 0);
        ++frames;
        frame->word_2c = 0;
        std::fill(std::begin(frame->unknown_31), std::end(frame->unknown_31), 0);
        frame->word_48 = 0x2468u;
        result.Unresolved10(frame.get());
        require(result.child_44 != frame.get() && result.children.head->next->item == frame.get() &&
                    result.children.head->next->next == result.children.head &&
                    frame->children.head->next == frame->children.head,
            "genuine borrowed child graph differs");
        // Preserve R532's real constructor observation, then establish only the
        // intended preparation transition with valid typed member writes.
        require(result.preparation_flag == 1 && frame->preparation_flag == 1,
            "real constructor preparation differs");
        result.preparation_flag = 0;
        frame->preparation_flag = 0;
        require(result.state_30 == 0 && result.flag_60 == 0 && result.resource_4c == nullptr,
            "initial genuine Dialog state differs");
        return result;
    }

    void check() const
    {
        require(
            screen_manager == &global_manager.value && screen_request_renderer == &renderer.value &&
                screen_request_game == nullptr && screen_dialog_resources == nullptr &&
                receiver.value.renderer == &renderer.value && receiver.value.mode == 6 &&
                receiver.value.previous_mode == 6 &&
                global_manager.value.renderer == &renderer.value &&
                global_manager.value.registry == nullptr &&
                global_manager.value.active.head == &global_head.value &&
                global_head.value.next == &global_head.value &&
                global_head.value.previous == &global_head.value &&
                global_manager.value.mode == 0x1234 && global_manager.value.previous_mode == 0x5678,
            "actual receiver or separate global read model changed");
        require(renderer.value.width == 64 && renderer.value.height == 48 &&
                    renderer.value.current_surface == -1 &&
                    renderer.value.dirty_rectangle_count == 2 &&
                    renderer.value.dirty_rectangles.values.size() == 2 &&
                    renderer_queue_test::same_rectangle(
                        renderer.value.dirty_rectangles.values[0], {-7, -3, -2, 1}) &&
                    renderer_queue_test::same_rectangle(
                        renderer.value.dirty_rectangles.values[1], {13, 17, 19, 23}),
            "activation changed unrelated renderer state");
        renderer.check();
        receiver.check();
        global_manager.check();
        active_head.check();
        global_head.check();
        pool.check();
    }
};

void activate_case(const char* existing)
{
    Environment environment;
    ScreenDialog& pause = environment.create("PauseBox", 7);
    ScreenDialog* blocker = nullptr;
    if (existing != nullptr)
    {
        blocker = &environment.create(existing, 8);
        require(environment.receiver.value.RequestScreen(existing, nullptr, 0),
            "real suppression setup request failed");
        ++setup_requests;
    }

    CircularListNode* head = &environment.active_head.value;
    CircularListNode* before = head->next;
    const unsigned before_exchanges = exchanges;
    ScreenSurfacePosition** before_table = environment.renderer.value.surfaces;
    environment.receiver.value.ActivateScreens();
    ++calls;
    if (blocker != nullptr)
    {
        require(head->next == before && head->previous == before && before != head &&
                    before->next == head && before->previous == head &&
                    exchanges == before_exchanges &&
                    environment.renderer.value.surfaces == before_table,
            "scenario guard did not suppress PauseBox");
        const auto* entry = static_cast<CircularListValueNode*>(before)->entry;
        require(entry->item == blocker && entry->context == nullptr && entry->state == 0 &&
                    entry->surface == 0 && blocker->state_59 == 0 &&
                    blocker->preparation_flag == 1 && pause.state_59 == 0 &&
                    pause.preparation_flag == 0 &&
                    environment.borrowed_frames[0]->preparation_flag == 0 &&
                    environment.borrowed_frames[1]->preparation_flag == 1,
            "scenario guard changed genuine entry or preparation state");
    }
    else
    {
        // Reject the omitted-request control before dereferencing a sentinel.
        require(before == head && head->next != head, "ActivateScreens did not request PauseBox");
        CircularListNode* node = head->next;
        require(node->next == head && node->previous == head && head->previous == node,
            "one-entry PauseBox active ring differs");
        const auto* entry = static_cast<CircularListValueNode*>(node)->entry;
        require(entry->item == &pause && entry->context == nullptr && entry->state == 0 &&
                    entry->surface == 0 && pause.state_59 == 1 && pause.preparation_flag == 1 &&
                    environment.borrowed_frames[0]->preparation_flag == 1,
            "PauseBox receiver, null context, literal flag1 or preparation differs");
    }

    const auto& renderer = environment.renderer.value;
    require(renderer.surfaces != nullptr && renderer.surfaces_end == renderer.surfaces + 1,
        "real single surface publication differs");
    const auto* surface = renderer.surfaces[0];
    require(surface->active == 1 && surface->software == 1 && surface->prepared == 1 &&
                surface->width == 4 && surface->height == 3 && surface->pitch == 8 &&
                surface->position.x == 2 && surface->position.y == 3,
        "real prepared software surface differs");
    for (unsigned n = 0; n < 12; ++n)
    {
        require(surface->pixels[n] == 0x45ab, "prepared literal WORD pixel differs");
    }
    require(renderer_queue_test::same_rectangle(pause.rectangle, {2, 3, 6, 6}) &&
                renderer_queue_test::same_rectangle(
                    environment.borrowed_frames[0]->rectangle, {9, 10, 13, 14}) &&
                environment.borrowed_frames[0]->word_48 == 0x2468u,
        "real request changed unrelated geometry or child scalar");
    environment.check();
}
} // namespace

// Existing qualified, uncontended pool OS boundary; no game allocator/provider.
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
        activate_case("ScenarioWon");
        activate_case("ScenarioLost");
        activate_case(nullptr);
        require(calls == 3 && setup_requests == 2 && dialogs == 5 && dialog_releases == 5 &&
                    frames == 10 && frame_releases == 10 && active_releases == 3 &&
                    surface_releases == 3 && borrowed_survivors == 5,
            "real owner totals or borrowed Frame lifetime differ");
        require(screen_request_renderer == nullptr && screen_request_game == nullptr &&
                    screen_manager == nullptr && screen_dialog_resources == nullptr && !pool_live &&
                    pool_chunk_begin == nullptr && pool_chunk_end == nullptr &&
                    pool_heap_bytes == 0 && pool_lock_word == 0,
            "final borrowed state remains");
        std::printf("screen-activate groups=3 calls=%u setup_requests=%u dialogs=%u releases=%u "
                    "frames=%u frame_releases=%u active=%u surfaces=%u borrowed=%u exchanges=%u "
                    "checks=%u pass\n",
            calls,
            setup_requests,
            dialogs,
            dialog_releases,
            frames,
            frame_releases,
            active_releases,
            surface_releases,
            borrowed_survivors,
            exchanges,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "screen-activate: %s\n", failure.reason);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "screen-activate host failure: %s\n", failure.what());
        return 3;
    }
}
