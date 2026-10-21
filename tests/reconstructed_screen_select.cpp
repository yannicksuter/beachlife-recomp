#include "pool_chunk_acquire.h"
#include "pool_lock_acquire.h"
#include "pool_lock_guard.h"
#include "pool_return.h"
#include "renderer_queue_test_state.h"
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

struct Failure
{
    const char* reason;
};
unsigned checks = 0, selections = 0, activations = 0, exchanges = 0;
unsigned dialogs = 0, dialog_releases = 0, frames = 0, frame_releases = 0;
unsigned active_releases = 0, surface_releases = 0;
bool pool_live = false, borrowed_frame_survived = false;

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
    // The finite pool outlives every genuine ring, active node and surface table.
    PoolScope pool;
    Guarded<ScreenRequestRenderer> renderer;
    Guarded<ScreenManager> manager;
    Guarded<ScreenManager> ignored_receiver;
    Guarded<CircularListNode> active_head;
    std::unique_ptr<ScreenChildLink> registration;
    std::unique_ptr<ScreenFrameItem> borrowed_frame;

    Environment()
    {
        require(screen_request_renderer == nullptr && screen_request_game == nullptr &&
                    screen_manager == nullptr,
            "another borrowed read model is active");
        renderer_queue_test::initialize(renderer.value, 0x21);
        renderer.value.software_mode = 1;
        renderer.value.width = 64;
        renderer.value.height = 48;
        renderer.value.current_surface = -1;
        renderer.value.fill_color = 0x45ab;
        active_head.value = {&active_head.value, &active_head.value};
        manager.value.renderer = &renderer.value;
        manager.value.active.head = &active_head.value;
        manager.value.mode = 6;
        manager.value.previous_mode = 6;
        ignored_receiver.value.mode = 0x1234;
        ignored_receiver.value.previous_mode = 0x5678;
        screen_request_renderer = &renderer.value;
        screen_manager = &manager.value;
        // This generated Dialog uses the genuine no-resource branch and disables
        // saved placement. No full Game or resource-manager lifetime is supplied.
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
        manager.value.active.Clear();

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
        if (registration)
        {
            // The real kind4 deletion destroys the Dialog and its owned Frame.
            // Its child ring returns nodes but never deletes borrowed payloads.
            registration.reset();
            ++dialog_releases;
            ++frame_releases;
        }
        if (borrowed_frame)
        {
            borrowed_frame_survived =
                borrowed_frame->kind_28 == 11 && borrowed_frame->word_48 == 0x2468u &&
                borrowed_frame->children.head->next == borrowed_frame->children.head &&
                borrowed_frame->children.head->previous == borrowed_frame->children.head;
            borrowed_frame.reset();
            ++frame_releases;
        }
        screen_request_renderer = nullptr;
        screen_manager = nullptr;
    }

    ScreenDialog& create()
    {
        auto dialog = std::make_unique<ScreenDialog>(
            "InGameOptions", ScreenRectangle{2, 3, 6, 6}, "nOnE", 7, 0);
        auto link = std::make_unique<ScreenChildLink>(dialog.get());
        ScreenDialog& result = *dialog.release();
        registration = std::move(link);
        manager.value.registry = registration.get();
        ++dialogs;
        ++frames; // Genuine child_44 constructed by the Dialog itself.

        // Initialize only actual members untouched by the real constructors.
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

        borrowed_frame = std::make_unique<ScreenFrameItem>(ScreenRectangle{9, 10, 13, 14}, 0);
        ++frames;
        borrowed_frame->word_2c = 0;
        std::fill(std::begin(borrowed_frame->unknown_31), std::end(borrowed_frame->unknown_31), 0);
        borrowed_frame->word_48 = 0x2468u;
        result.Unresolved10(borrowed_frame.get());
        require(result.child_44 != borrowed_frame.get() &&
                    result.children.head->next->item == borrowed_frame.get() &&
                    result.children.head->next->next == result.children.head &&
                    borrowed_frame->children.head->next == borrowed_frame->children.head,
            "genuine borrowed child graph differs");
        return result;
    }

    void check() const
    {
        require(screen_manager == &manager.value && screen_request_renderer == &renderer.value &&
                    screen_request_game == nullptr && manager.value.renderer == &renderer.value &&
                    manager.value.mode == 6 && manager.value.previous_mode == 6 &&
                    ignored_receiver.value.renderer == nullptr &&
                    ignored_receiver.value.registry == nullptr &&
                    ignored_receiver.value.active.head == nullptr &&
                    ignored_receiver.value.mode == 0x1234 &&
                    ignored_receiver.value.previous_mode == 0x5678,
            "global or ignored receiver state differs");
        require(renderer.value.width == 64 && renderer.value.height == 48 &&
                    renderer.value.current_surface == -1 &&
                    renderer.value.dirty_rectangle_count == 2 &&
                    renderer.value.dirty_rectangles.values.size() == 2 &&
                    renderer_queue_test::same_rectangle(
                        renderer.value.dirty_rectangles.values[0], {-7, -3, -2, 1}) &&
                    renderer_queue_test::same_rectangle(
                        renderer.value.dirty_rectangles.values[1], {13, 17, 19, 23}),
            "selection changed unrelated renderer state");
        renderer.check();
        manager.check();
        ignored_receiver.check();
        active_head.check();
        pool.check();
    }
};

void select_generated_options()
{
    Environment environment;
    ScreenDialog& dialog = environment.create();
    require(dialog.preparation_flag == 1 && environment.borrowed_frame->preparation_flag == 1,
        "real constructor preparation differs");
    // Establish the preparation transition after the genuine constructors.
    dialog.preparation_flag = 0;
    environment.borrowed_frame->preparation_flag = 0;
    require(dialog.preparation_flag == 0 && environment.borrowed_frame->preparation_flag == 0 &&
                dialog.state_30 == 0 && dialog.flag_60 == 0 && dialog.resource_4c == nullptr,
        "initial real Dialog state differs");
    environment.ignored_receiver.value.ScreenSelect();
    ++selections;

    CircularListNode* head = &environment.active_head.value;
    // This check also rejects the omitted-request control without dereferencing
    // the sentinel or inventing an observable implementation of the empty slot4c.
    require(head->next != head, "ScreenSelect did not request InGameOptions");
    CircularListNode* node = head->next;
    require(node->next == head && node->previous == head && head->previous == node,
        "one-entry active ring differs");
    const auto* entry = static_cast<CircularListValueNode*>(node)->entry;
    require(entry->item == &dialog && entry->context == nullptr && entry->state == 0 &&
                entry->surface == 0 && dialog.state_59 == 0 && dialog.preparation_flag == 1 &&
                environment.borrowed_frame->preparation_flag == 1 &&
                environment.borrowed_frame->word_48 == 0x2468u &&
                renderer_queue_test::same_rectangle(dialog.rectangle, {2, 3, 6, 6}) &&
                renderer_queue_test::same_rectangle(
                    environment.borrowed_frame->rectangle, {9, 10, 13, 14}),
        "request, real child preparation or slot4c preservation differs");

    const auto& renderer = environment.renderer.value;
    require(renderer.surface_storage.size() == 1, "real surface table publication differs");
    const auto* surface = renderer.surface_storage[0];
    require(surface->active == 1 && surface->software == 1 && surface->prepared == 1 &&
                surface->width == 4 && surface->height == 3 && surface->pitch == 8 &&
                surface->position.x == 2 && surface->position.y == 3,
        "real prepared software surface differs");
    for (unsigned n = 0; n < 12; ++n)
    {
        require(surface->pixels[n] == 0x45ab, "prepared literal WORD pixel differs");
    }

    // Separate genuine lifecycle calls; ScreenSelect itself performs word4c,
    // not this activation. The real Frame override forwards without self-state.
    dialog.word_74 = 0x5a5au;
    entry->item->Unresolved24(0x80);
    ++activations;
    require(
        dialog.state_30 == 0x80 && dialog.word_74 == 0 && environment.borrowed_frame->state_30 == 0,
        "real Dialog/Frame activation differs");
    dialog.elapsed_24 = 1.25f;
    entry->item->Unresolved24(0);
    ++activations;
    require(dialog.state_30 == 0 && dialog.elapsed_24 == 0.0f &&
                environment.borrowed_frame->word_48 == 0x2468u,
        "real Dialog deactivation differs");
    environment.check();
}
} // namespace

// Existing qualified, uncontended pool platform boundary. These are not game
// allocation methods and establish no concurrent or production Win32 semantics.
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
        select_generated_options();
        require(selections == 1 && activations == 2 && dialogs == 1 && dialog_releases == 1 &&
                    frames == 2 && frame_releases == 2 && active_releases == 1 &&
                    surface_releases == 1 && borrowed_frame_survived,
            "real owner totals or borrowed Frame lifetime differ");
        require(screen_request_renderer == nullptr && screen_request_game == nullptr &&
                    screen_manager == nullptr && !pool_live && pool_chunk_begin == nullptr &&
                    pool_chunk_end == nullptr && pool_heap_bytes == 0 && pool_lock_word == 0,
            "final borrowed state remains");
        std::printf(
            "screen-select groups=1 calls=%u activations=%u dialogs=%u releases=%u "
            "frames=%u frame_releases=%u active=%u surfaces=%u exchanges=%u checks=%u pass\n",
            selections,
            activations,
            dialogs,
            dialog_releases,
            frames,
            frame_releases,
            active_releases,
            surface_releases,
            exchanges,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "screen-select: %s\n", failure.reason);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "screen-select host failure: %s\n", failure.what());
        return 3;
    }
}
