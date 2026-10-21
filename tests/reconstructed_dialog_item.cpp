#include "child_ring_seed.h"
#include "data/folder_source.h"
#include "render_mask_setup.h"
#include "renderer_queue_test_state.h"
#include "runtime/resource_files.h"
#include "startup_screen_dialog.h"
#include "startup_screen_dialog_frame_file.h"
#include "startup_screen_dialog_resource.h"
#include "startup_screen_frame_item.h"
#include "startup_screen_frame_resources.h"
#include "startup_screen_manager.h"
#include "startup_screen_palette_modifiers.h"
#include "window_activation.h"

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;
namespace data = beachlife::data;
namespace files = beachlife::runtime::resource_files;
constexpr unsigned width = 32, height = 32;
using Pixels = std::array<unsigned int, width * height>;
using Seed = child_ring_fixture::Seed<12>;

static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(std::endian::native == std::endian::little);
static_assert(std::is_same_v<decltype(ScreenDialogResource::word_04), float>);
static_assert(std::is_same_v<decltype(ScreenDialogResource::word_0c), int>);
static_assert(std::is_same_v<decltype(ScreenDialogResource::word_14), int>);
static_assert(std::is_same_v<decltype(ScreenDialogResource::word_2c), const unsigned short*>);
static_assert(offsetof(ScreenDialogResource, word_04) == sizeof(void*));
static_assert(offsetof(ScreenDialogResource, word_2c) == (sizeof(void*) == 8 ? 48 : 44));
static_assert(offsetof(ScreenDialogResource, record) == (sizeof(void*) == 8 ? 56 : 48));
static_assert(sizeof(ScreenDialogResource) == (sizeof(void*) == 8 ? 72 : 56));
static_assert(sizeof(ScreenDialogResourceNode) == sizeof(ScreenChildNode));
static_assert(alignof(ScreenDialogResourceNode) <= alignof(ScreenChildNode));
static_assert(sizeof(ScreenDialog) == (sizeof(void*) == 8 ? 160 : 128));
static_assert(offsetof(WindowDeactivationState, time_158) == 0x158);
static_assert(offsetof(WindowDeactivationState, deactivation_requested) == 0x439);
static_assert(sizeof(WindowDeactivationState) == 0x43c);
static_assert(sizeof(SurfaceColorKey) == 8);
static_assert(offsetof(DirectDrawSurface7VtablePrefix, SetColorKey) == 29 * sizeof(void*));
static_assert(sizeof(DirectDrawSurface7VtablePrefix) == 33 * sizeof(void*));
static_assert(sizeof(void*) == 8);
static_assert(!std::is_copy_constructible_v<ScreenRequestRenderer>);
static_assert(std::is_same_v<decltype(ScreenRequestRenderer::fill_color), unsigned short>);
static_assert(std::is_same_v<decltype(ScreenRequestRenderer::word_60), unsigned int>);

struct Failure
{
    const char* reason;
};
unsigned checks = 0, pixel_checks = 0, dialog_owners = 0;
const char* group = "setup";
void check(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        throw Failure{reason};
    }
}
void require(bool condition, const char* reason)
{
    if (!condition)
    {
        throw std::runtime_error(reason);
    }
}
void word(std::vector<unsigned char>& bytes, unsigned value)
{
    bytes.push_back(static_cast<unsigned char>(value));
    bytes.push_back(static_cast<unsigned char>(value >> 8));
}
void dword(std::vector<unsigned char>& bytes, unsigned value)
{
    word(bytes, value);
    word(bytes, value >> 16);
}
void name(std::vector<unsigned char>& bytes, const char* text)
{
    const auto length = std::strlen(text) + 1;
    require(length < 256, "generated name exceeds the real length byte");
    bytes.push_back(static_cast<unsigned char>(length));
    bytes.insert(bytes.end(), text, text + length);
}
constexpr std::array<const char*, 2> names{"dialog", "dummy"};
constexpr std::array<std::array<unsigned char, 3>, 4> colours{
    {{248, 0, 0}, {0, 252, 0}, {0, 0, 248}, {248, 252, 248}}};

class Generated
{
  public:
    explicit Generated(fs::path root)
        : root_(std::move(root))
    {
        require(!fs::exists(root_), "generated directory already exists");
        fs::create_directories(root_ / "Data/Sprites");
        std::vector<unsigned char> metadata(20, 0x42);
        dword(metadata, 0x12345678);
        dword(metadata, static_cast<unsigned>(names.size()));
        std::vector<unsigned char> bank(20, 0x21);
        dword(bank, 0xaabbccdd);
        for (const char* resource_name : names)
        {
            name(metadata, resource_name);
            dword(metadata, 1);
            dword(metadata, 1);
            for (unsigned colour = 0; colour < 256; ++colour)
            {
                const auto rgb =
                    colour < colours.size() ? colours[colour] : std::array<unsigned char, 3>{};
                metadata.insert(metadata.end(), rgb.begin(), rgb.end());
            }
            dword(metadata, 1);
            name(metadata, "idle");
            word(metadata, 2);
            metadata.push_back(1);
            word(metadata, 2);
            word(metadata, 2);
            word(metadata, 0);
            word(metadata, 0);
            dword(metadata, static_cast<unsigned>(bank.size()));
            dword(metadata, 12);
            word(metadata, 2);
            word(bank, 0);
            word(bank, 1);
            dword(bank, 0);
            dword(bank, 6);
            bank.insert(bank.end(), {2, 0, 1, 2, 2, 3, 2, 3, 2, 2, 1, 0});
        }
        add("Data/Sprites/BLSprite.hdr", std::move(metadata));
        add("Data/Sprites/BLSprite.Dat", std::move(bank));
        add("Data/Sprites/PaletteModifiers.bin", std::vector<unsigned char>(12 * 64 * 3));
    }
    ~Generated()
    {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }
    std::shared_ptr<const data::FileSource> mount() const
    {
        return std::make_shared<data::FolderSource>(root_, entries_);
    }
    void unchanged() const
    {
        for (const auto& [path, expected] : inputs_)
        {
            check(fs::file_size(root_ / path) == expected.size(), "mounted input size changed");
            std::vector<unsigned char> actual(expected.size());
            std::ifstream stream(root_ / path, std::ios::binary);
            stream.read(reinterpret_cast<char*>(actual.data()),
                static_cast<std::streamsize>(actual.size()));
            require(stream.gcount() == static_cast<std::streamsize>(actual.size()),
                "generated input reread failed");
            check(actual == expected, "mounted input bytes changed");
        }
    }

  private:
    void add(const char* path, std::vector<unsigned char> bytes)
    {
        std::ofstream stream(root_ / path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        stream.close();
        require(bool(stream), "generated input write failed");
        entries_.push_back({path, bytes.size()});
        inputs_.emplace_back(path, std::move(bytes));
    }
    fs::path root_;
    std::vector<data::SourceEntry> entries_;
    std::vector<std::pair<std::string, std::vector<unsigned char>>> inputs_;
};

struct Canvas
{
    std::array<unsigned short, 8> before{};
    std::array<unsigned short, width * height * 2> words{};
    std::array<unsigned short, 8> after{};
    void reset(unsigned int pixel)
    {
        before.fill(0x69a5);
        after.fill(0xc35a);
        for (unsigned i = 0; i < width * height; ++i)
        {
            std::memcpy(words.data() + i * 2, &pixel, sizeof(pixel));
        }
    }
    void equal(const Pixels& expected) const
    {
        for (unsigned i = 0; i < expected.size(); ++i)
        {
            unsigned int actual;
            std::memcpy(&actual, words.data() + i * 2, sizeof(actual));
            ++pixel_checks;
            check(actual == expected[i], "literal full-buffer pixel differs");
        }
        for (unsigned i = 0; i < before.size(); ++i)
        {
            check(before[i] == 0x69a5 && after[i] == 0xc35a, "destination owner guard changed");
        }
    }
};
struct Environment
{
    Canvas canvas;
    renderer_owner_test::Bootstrap renderer_bootstrap;
    ScreenRequestRenderer renderer{renderer_bootstrap.video(), 8, 4, 1};
    renderer_owner_test::RestoreOwnedState renderer_restore{renderer, renderer_bootstrap};
    ScreenManager manager{};
    CircularListNode active_head{};
    ScreenSurfacePosition surface{};
    ScreenSurfacePosition* surfaces[1]{&surface};
    std::array<unsigned short, 26> surface_words{};
    WindowDeactivationState clock{};
    Environment()
    {
        check(screen_request_renderer == nullptr && screen_manager == nullptr &&
                  screen_dialog_resources == nullptr &&
                  screen_dialog_palette_modifiers == nullptr && screen_dialog_frame_file == -1 &&
                  screen_dialog_frame_cursor == 0,
            "another game read model is active");
        renderer_queue_test::initialize(renderer, 31);
        renderer.software_mode = 1;
        renderer.width = width;
        renderer.height = height;
        renderer.clip_rectangle = {0, 0, static_cast<int>(width), static_cast<int>(height)};
        renderer.pixels_1c = canvas.words.data();
        renderer.format_44 = 0;
        renderer.unscaled_clip_5c = 0;
        renderer.clip_flag_5d = 0;
        renderer.rectangle_enabled_4e = 1;
        renderer.rectangle_suppressed_4d = 0;
        renderer.rectangle_ready_4f = 1;
        renderer.pixels_locked = 0;
        renderer.current_surface = 0;
        renderer.fill_color = 0x45ab;
        renderer.surface_storage.assign(std::begin(surfaces), std::end(surfaces));
        surface.active = 1;
        surface.software = 1;
        surface.width = 3;
        surface.height = 2;
        surface.pitch = 10;
        surface.pixels = surface_words.data() + 8;
        surface_words.fill(0xc67a);
        active_head = {&active_head, &active_head};
        manager.active.head = &active_head;
        manager.mode = 6;
        clock.time_158 = 3.25f;
        check(window_deactivation_state == nullptr, "another window read model is active");
        window_deactivation_state = &clock;
        screen_request_renderer = &renderer;
        manager.renderer = &renderer;
        screen_manager = &manager;
        SetupRenderMasks(canvas.words.data(), width, 0);
        reset(0);
    }
    ~Environment()
    {
        // Borrowed bindings end after genuine resource/item owners. The renderer
        // guard restores its original ownership before the real destructor.
        SetupRenderMasks(nullptr, 0, 0);
        screen_request_renderer = nullptr;
        screen_manager = nullptr;
        window_deactivation_state = nullptr;
        screen_dialog_frame_file = -1;
        screen_dialog_frame_cursor = 0;
    }
    void reset(unsigned int pixel)
    {
        canvas.reset(pixel);
        renderer.clip_mode_54 = 1;
        renderer.dirty_rectangles.values.clear();
        renderer.dirty_rectangle_count = 0;
    }
    void borrowed_state() const
    {
        check(renderer.width == width && renderer.height == height &&
                  renderer.pixels_1c == canvas.words.data() && renderer.format_44 == 0 &&
                  renderer.unscaled_clip_5c == 0 && renderer.pixels_locked == 0,
            "Frame drawing changed borrowed surface state");
        check(manager.renderer == &renderer && manager.item_134 == nullptr,
            "Frame input changed the borrowed manager read model");
        for (unsigned i = 0; i < 10; ++i)
        {
            check(manager.fonts_04[i] == nullptr && manager.fonts_2c[i] == nullptr,
                "Frame drawing changed borrowed font slots");
        }
    }
};

void initialize_untouched(ScreenDialog& dialog)
{
    // Genuine constructors have already established the vptrs/owned members.
    // Initialize only the actual fields they leave untouched, with typed values.
    dialog.word_2c = 23;
    std::memset(dialog.unknown_31, 0x62, sizeof(dialog.unknown_31));
    dialog.previous_x_50 = 0;
    dialog.previous_y_54 = 0;
    std::memset(dialog.unknown_5a, 0x37, sizeof(dialog.unknown_5a));
    std::memset(dialog.unknown_62, 0x73, sizeof(dialog.unknown_62));
    dialog.remaining_68 = 0.0f;
    dialog.duration_6c = 0.0f;
    dialog.word_70 = 0;
    dialog.child_44->word_2c = 0;
    std::memset(dialog.child_44->unknown_31, 0, sizeof(dialog.child_44->unknown_31));
}
void same_rect(const ScreenRectangle& a, ScreenRectangle b, const char* reason)
{
    check(renderer_queue_test::same_rectangle(a, b), reason);
}
void owner_and_virtuals(Environment& environment)
{
    group = "genuine-dialog-lifetimes-and-virtuals";
    environment.reset(0x12345678);
    Pixels expected;
    expected.fill(0x12345678);
    ScreenItem child({20, 20, 22, 22});
    child.kind_28 = 0;
    child.word_2c = 0;
    std::memset(child.unknown_31, 0, sizeof(child.unknown_31));
    const char label[] = "clear-dialog";
    {
        ScreenDialog dialog(label, {2, 3, 8, 9}, "cLeAr", 7, 1);
        ++dialog_owners;
        initialize_untouched(dialog);
        check(dialog.name != label && std::strcmp(dialog.name, label) == 0 && dialog.kind_28 == 4 &&
                  dialog.resource_4c == nullptr && dialog.clear_5c == 1 && dialog.group_7c == 7 &&
                  dialog.flag_60 == 1 && dialog.state_58 == 0 && dialog.state_59 == 0 &&
                  dialog.state_61 == 0 && dialog.word_64 == 0 && dialog.word_74 == 0,
            "genuine Dialog owned name/constructor fields differ");
        check(dialog.child_44 != nullptr && dialog.child_44->kind_28 == 11,
            "genuine Dialog did not own a real Frame");
        same_rect(dialog.child_44->rectangle, {2, 3, 8, 33}, "owned title Frame bounds differ");
        ScreenItem* dispatch = &dialog;
        dispatch->Unresolved10(&child);
        check(dialog.children.head->next->item == &child &&
                  dialog.children.head->next == dialog.children.head->previous,
            "genuine Dialog attachment differs");
        child.preparation_flag = 0;
        dispatch->MarkPrepared();
        check(dialog.preparation_flag == 1 && child.preparation_flag == 1,
            "Dialog preparation missed a real borrowed child");
        dialog.word_74 = 91;
        dispatch->Unresolved24(1);
        check(dialog.state_30 == 1 && child.state_30 == 1 && dialog.word_74 == 0,
            "real activation/reset/child route differs");
        dispatch->Unresolved04(0.25f);
        dispatch->Unresolved08();
        dispatch->Unresolved1c();
        dispatch->Unresolved4c(0x12345678);
        dispatch->ApplyPosition(90, 91);
        check(!dispatch->HandleInput38(1, 2, 3), "real base child unexpectedly handled input38");
        dispatch->Unresolved0c();
        // The sole source control keeps this flag at one; all owners still unwind.
        check(dialog.preparation_flag == 0, "Dialog draw did not clear preparation");
        environment.canvas.equal(expected);
        check(environment.renderer.dirty_rectangle_count == 0, "CLEAR published a rectangle");
        check(dispatch->TestRectangle(3, 4), "resource-free base hit route differs");
        check(!dispatch->TestRectangle(90, 90) && dialog.state_40 == 0,
            "Dialog failed hit did not publish zero");
        dialog.word_74 = 73;
        dispatch->Unresolved24(0);
        check(dialog.state_30 == 0 && child.state_30 == 0 && dialog.word_74 == 73,
            "disable changed the nonzero-only word reset");
    }
    check(child.kind_28 == 0 && child.owned_text == nullptr,
        "Dialog destruction consumed its borrowed payload");
    same_rect(child.rectangle, {20, 20, 22, 22}, "Dialog changed a skipped child rectangle");
    {
        ScreenDialog dialog("blank-dialog", {2, 3, 8, 9}, "nOnE", 0, 0);
        ++dialog_owners;
        initialize_untouched(dialog);
        check(dialog.clear_5c == 0 && dialog.resource_4c == nullptr,
            "NONE constructor branch differs");
        dialog.Unresolved0c();
        for (int y = 3; y != 8; ++y)
        {
            for (int x = 2; x != 7; ++x)
            {
                expected[static_cast<unsigned>(y) * width + static_cast<unsigned>(x)] = 0x1234ffff;
            }
        }
        environment.canvas.equal(expected);
        check(dialog.preparation_flag == 0, "blank draw did not clear preparation");
        dialog.Unresolved0c();
        environment.canvas.equal(expected);
    }
}
struct PickProjection
{
    ScreenDialogResourceRecord& record;
    unsigned int old_format;
    const char* old_name;
    explicit PickProjection(ScreenDialogResourceRecord& value)
        : record(value)
        , old_format(value.format_18)
        , old_name(value.frames[0]->name)
    {
        // A bounded typed mutation of a genuine loaded owner. The selected
        // row-zero token is complete, and the actual owned name is restored.
        record.format_18 = 5;
        record.frames[0]->name = const_cast<char*>("pick");
    }
    ~PickProjection()
    {
        record.format_18 = old_format;
        record.frames[0]->name = old_name;
    }
};
void resource_pixels(Environment& environment, ScreenDialogResourceLookup& owner)
{
    group = "real-resource-hit-and-pixels";
    environment.reset(0x12345678);
    Pixels expected;
    expected.fill(0x12345678);
    {
        ScreenDialog dialog("resource-dialog", {4, 5, 12, 13}, "dialog", 3, 0);
        ++dialog_owners;
        initialize_untouched(dialog);
        check(dialog.resource_4c != nullptr && dialog.resource_4c->record == owner.records[0] &&
                  owner.records[0]->reference_count == 1,
            "Dialog constructor did not retain the actual resource record");
        dialog.Unresolved0c();
        expected[5 * width + 4] = 0xfffff800;
        expected[5 * width + 5] = 0xffff07e0;
        expected[6 * width + 4] = 0xffff001f;
        expected[6 * width + 5] = 0xffffffff;
        environment.canvas.equal(expected);
        check(dialog.preparation_flag == 0 && owner.records[0]->frames[0]->time_04 == 999999.0f &&
                  dialog.resource_4c->word_14 == 0 &&
                  environment.renderer.dirty_rectangle_count == 1,
            "Dialog Draw/resource timing/clip publication differs");
        same_rect(environment.renderer.dirty_rectangles.values[0],
            {4, 5, 6, 7},
            "resource clip queue rectangle differs");
        check(dialog.TestRectangle(5, 6), "actual packet hit inside the resource failed");
        check(!dialog.TestRectangle(4, 6) && !dialog.TestRectangle(6, 6) &&
                  !dialog.TestRectangle(5, 7) && dialog.state_40 == 0,
            "resource strict-positive/width/height edges differ");
        dialog.Unresolved0c();
        environment.canvas.equal(expected);
        check(environment.renderer.dirty_rectangle_count == 1,
            "unprepared repeat entered the resource draw");
        {
            PickProjection pick(*owner.records[0]);
            check(dialog.resource_4c->TestPixel(1, 0) &&
                      owner.records[0]->frames[0]->time_04 == 3.25f,
                "actual pick-name/timestamp boundary differs");
        }
        dialog.MoveRectangle(40, 40);
        dialog.MarkPrepared();
        owner.records[0]->frames[0]->time_04 = 17.0f;
        dialog.Unresolved0c();
        environment.canvas.equal(expected);
        check(dialog.preparation_flag == 0 && owner.records[0]->frames[0]->time_04 == 17.0f &&
                  environment.renderer.dirty_rectangle_count == 1,
            "false clip changed late resource timing or queue");
    }
    check(owner.records[0]->reference_count == 0, "Dialog destructor did not release its wrapper");
}
struct ActiveBorrow
{
    Environment& environment;
    ActiveScreenEntry entry;
    CircularListValueNode node;
    ActiveBorrow(Environment& value, ScreenDialog& dialog)
        : environment(value)
        , entry{&dialog, 0, 1, nullptr}
    {
        node.next = &environment.active_head;
        node.previous = &environment.active_head;
        node.entry = &entry;
        environment.active_head.next = &node;
        environment.active_head.previous = &node;
    }
    ~ActiveBorrow()
    {
        environment.active_head.next = &environment.active_head;
        environment.active_head.previous = &environment.active_head;
    }
};
void movement_and_completion(Environment& environment)
{
    group = "drag-transition-and-real-manager-children";
    environment.reset(0);
    ScreenDialog dialog("moving", {2, 2, 24, 24}, "CLEAR", 7, 1);
    ++dialog_owners;
    initialize_untouched(dialog);
    ActiveBorrow active(environment, dialog);
    check(dialog.HandleTimedCoordinates(3, 3, 1) && dialog.state_58 == 1,
        "real title Frame did not arm dragging");
    check(dialog.HandleTimedCoordinates(4, 4, 2), "first drag publication was not handled");
    same_rect(dialog.rectangle, {2, 2, 24, 24}, "zero previous-coordinate guard moved Dialog");
    check(dialog.HandleTimedCoordinates(6, 7, 2), "second drag was not handled");
    same_rect(dialog.rectangle, {4, 5, 26, 27}, "drag delta/order differs");
    same_rect(dialog.child_44->rectangle, {4, 5, 26, 35}, "real owned Frame did not move");
    check(dialog.preparation_flag == 1, "real PrepareNamedScreen did not mark Dialog");
    check(dialog.HandleTimedCoordinates(-300, 999, 2) && dialog.previous_x_50 == 6 &&
              dialog.previous_y_54 == 7,
        "one -300 input did not replace both coordinates");
    same_rect(dialog.rectangle, {4, 5, 26, 27}, "sentinel drag moved Dialog");
    dialog.HandleTimedCoordinates(6, 7, 3);
    check(dialog.state_58 == 0 && dialog.previous_x_50 == 0 && dialog.previous_y_54 == 0,
        "default input did not clear dragging and previous coordinates");

    dialog.preparation_flag = 0;
    environment.manager.PrepareNamedScreen("moving", 1);
    check(dialog.preparation_flag == 1 && environment.surface.prepared == 1,
        "real manager/PrepareSurface/preparation order differs");
    for (unsigned i = 0; i < environment.surface_words.size(); ++i)
    {
        check(environment.surface_words[i] == (i >= 8 && i < 18 ? 0x45ab : 0xc67a),
            "software PrepareSurface pitch/height or guards differ");
    }
    environment.surface_words[8] = 0x9876;
    environment.manager.PrepareNamedScreen("moving", 1);
    check(environment.surface_words[8] == 0x9876, "prepared surface was filled again");

    dialog.BeginTransition(1, 2.0f, 0);
    same_rect(dialog.rectangle, {34, 5, 56, 27}, "mode1 captured-width movement differs");
    dialog.Unresolved04(1.0f);
    same_rect(dialog.rectangle, {18, 5, 40, 27}, "half transition delta differs");
    check(dialog.state_61 == 1 && dialog.remaining_68 == 1.0f, "finite transition cleared early");
    dialog.Unresolved04(1.0f);
    same_rect(dialog.rectangle, {2, 5, 24, 27}, "complete transition position differs");
    check(dialog.state_61 == 0 && dialog.word_64 == 0 && dialog.remaining_68 == 0.0f,
        "transition completion did not clear its state");
    dialog.BeginTransition(2, 1.0f, 0);
    dialog.Unresolved04(1.0f);
    same_rect(dialog.rectangle, {132, 5, 154, 27}, "mode2 offscreen movement differs");
    check(active.entry.state == 2 && active.entry.context == nullptr && dialog.state_61 == 0 &&
              dialog.word_64 == 0 && dialog.preparation_flag == 1,
        "real CloseScreen/completion state differs");
    check(!environment.manager.CloseScreen("missing"), "absent named close unexpectedly succeeded");
    check(environment.manager.mode == 6 && screen_request_game == nullptr,
        "bounded mode6 route touched an absent game placement owner");
}
void all_pool_slots_returned(Seed& seed)
{
    std::array<bool, 12> found{};
    PoolChunkLink* link = pool_chunk_buckets[2];
    for (unsigned n = 0; n < found.size(); ++n)
    {
        unsigned id = 0;
        while (id < found.size() && reinterpret_cast<std::uintptr_t>(link) != seed.address(id))
        {
            ++id;
        }
        check(id < found.size() && !found[id], "pool returned an unknown or duplicate slot");
        found[id] = true;
        link = link->next; // The actual return began a live PoolChunkLink lifetime.
    }
    check(link == nullptr, "returned pool chain has an extra node");
    seed.check_common();
    seed.check_guards();
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 3;
    }
    try
    {
        Generated generated(argv[1]);
        Seed seed(check);
        Environment environment;
        std::weak_ptr<const data::FileSource> borrowed_mount;
        {
            auto mount = generated.mount();
            borrowed_mount = mount;
            files::Session session(std::move(mount));
            int descriptor;
            files::Stream* metadata;
            {
                ScreenDialogResourceLookup owner;
                check(screen_dialog_resources == &owner && owner.record_count == 2,
                    "genuine resource owner did not load the generated graph");
                descriptor = owner.data_file;
                metadata = owner.metadata_file;
                InitializeFrameStyleNames();
                owner_and_virtuals(environment);
                resource_pixels(environment, owner);
                movement_and_completion(environment);
                environment.borrowed_state();
                for (int i = 0; i < owner.record_count; ++i)
                {
                    check(owner.records[i]->reference_count == 0, "live wrapper reference remains");
                }
            }
            check(screen_dialog_resources == nullptr && screen_dialog_palette_modifiers == nullptr,
                "genuine resource teardown retained a registration");
            check(files::close_stream(metadata) == -1, "closed metadata identity changed");
            unsigned char byte = 0xa5;
            check(files::read_descriptor(descriptor, &byte, 1) == -1 && byte == 0xa5,
                "genuine owner did not close the data descriptor");
            all_pool_slots_returned(seed);
        }
        check(borrowed_mount.expired(), "mounted source outlived the real Session");
        generated.unchanged();
        std::printf("{\"groups\":3,\"dialog_owners\":%u,\"checks\":%u,\"pixel_checks\":%u}\n",
            dialog_owners,
            checks,
            pixel_checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "%s: %s\n", group, failure.reason);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "fixture setup: %s\n", failure.what());
        return 3;
    }
}
