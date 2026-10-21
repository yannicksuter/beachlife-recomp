#include "child_ring_seed.h"
#include "data/folder_source.h"
#include "render_mask_setup.h"
#include "renderer_queue_test_state.h"
#include "runtime/resource_files.h"
#include "startup_screen_dialog_frame_file.h"
#include "startup_screen_dialog_resource.h"
#include "startup_screen_frame_item.h"
#include "startup_screen_manager.h"
#include "startup_screen_palette_modifiers.h"
#include "startup_screen_tab_item.h"

#include <algorithm>
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
constexpr unsigned width = 16, height = 16;
using Pixels = std::array<unsigned int, width * height>;
using Seed = child_ring_fixture::Seed<8>;

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
#if defined(BEACHLIFE_DECOMP_RENDERER_SEA_STATE)
static_assert(sizeof(ScreenItem) == 80 && !std::is_copy_constructible_v<ScreenRequestRenderer>);
#else
static_assert(sizeof(ScreenItem) == 80 && !std::is_copy_constructible_v<ScreenRequestRenderer>);
#endif
static_assert(std::is_same_v<decltype(ScreenTabItem::word_44), unsigned int>);
static_assert(std::is_same_v<decltype(ScreenTabItem::word_48), unsigned int>);
static_assert(std::is_same_v<decltype(ScreenTabItem::resource), ScreenDialogResource*>);

struct Failure
{
    const char* reason;
};
unsigned checks = 0, pixel_checks = 0, tab_owners = 0, frame_owners = 0;
unsigned draws = 0, coordinate_calls = 0;
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
constexpr std::array<const char*, 1> names{"tab"};
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
            word(metadata, 1);
            metadata.push_back(2);
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
    std::array<unsigned char, width * height> markers{};
    renderer_owner_test::Bootstrap renderer_bootstrap;
    ScreenRequestRenderer renderer{renderer_bootstrap.video(), 8, 4, 1};
    renderer_owner_test::RestoreOwnedState renderer_restore{renderer, renderer_bootstrap};
    ScreenManager manager{};
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
        renderer.buffer_130 = markers.data();
        renderer.format_44 = 0;
        renderer.unscaled_clip_5c = 0;
        renderer.clip_flag_5d = 0;
        renderer.rectangle_enabled_4e = 1;
        renderer.rectangle_suppressed_4d = 0;
        renderer.rectangle_ready_4f = 1;
        renderer.pixels_locked = 0;
        renderer.current_surface = 0;
        screen_request_renderer = &renderer;
        manager.renderer = &renderer;
        screen_manager = &manager;
        SetupRenderMasks(canvas.words.data(), width, 0);
        reset(0);
    }
    ~Environment()
    {
        // Generated borrowed read models end after every genuine resource/item
        // owner and the mounted Session. The genuine renderer cleanup follows its view-restoration
        // guard.
        SetupRenderMasks(nullptr, 0, 0);
        screen_request_renderer = nullptr;
        screen_manager = nullptr;
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
            "Tab drawing changed borrowed surface state");
        check(renderer.buffer_130 == markers.data() && std::all_of(markers.begin(),
                                                           markers.end(),
                                                           [](unsigned char value)
                                                           {
                                                               return value == 0;
                                                           }),
            "unused marker owner/pointer changed");
        check(manager.renderer == &renderer && manager.item_134 == nullptr,
            "Tab input changed the borrowed manager read model");
        for (unsigned i = 0; i < 10; ++i)
        {
            check(manager.fonts_04[i] == nullptr && manager.fonts_2c[i] == nullptr,
                "Tab drawing changed borrowed font slots");
        }
    }
};

void initialized_members(ScreenItem& item)
{
    // Actual constructors deliberately leave these members alone. Initialize
    // only their real representations after construction, never the vptr or
    // ABI padding. This fixture makes no pre-construction preservation claim.
    item.word_2c = 0x23;
    for (auto& byte : item.unknown_31)
    {
        byte = 0x62;
    }
}
void attach(ScreenItem& parent, ScreenItem& child)
{
    parent.Unresolved10(&child);
}
void ownership_and_forwarding(ScreenDialogResourceLookup& owner)
{
    group = "ownership-and-forwarding";
    ScreenItem child({8, 9, 12, 13});
    child.kind_28 = 0;
    initialized_members(child);
    child.SetOwnedText("borrowed child");
    const char* text_owner = child.owned_text;
    {
        // Real derived construction and deletion through the actual base table.
        std::unique_ptr<ScreenItem> owner_pointer(new ScreenTabItem({2, 3, 6, 7}, "tab"));
        ++tab_owners;
        ScreenItem& dispatch = *owner_pointer;
        auto& tab = static_cast<ScreenTabItem&>(dispatch);
        initialized_members(tab);
        tab.word_48 = 71;
        tab.SetOwnedText("owned Tab");
        check(tab.kind_28 == 9 && tab.word_44 == 0 && tab.resource != nullptr &&
                  tab.resource->record == owner.records[0] &&
                  owner.records[0]->reference_count == 1 &&
                  tab.children.head->next == tab.children.head,
            "real Tab construction/resource/ring differs");
        attach(dispatch, child);
        check(tab.children.head->next->item == &child &&
                  tab.children.head->previous == tab.children.head->next,
            "real Tab insertion lost its borrowed child");
        child.preparation_flag = 0;
        tab.preparation_flag = 0;
        dispatch.MarkPrepared();
        // The retained safe control changes this real cache assignment to 2.
        check(tab.word_48 == 3, "inactive prepare cache differs");
        check(child.preparation_flag == 0 && tab.preparation_flag == 0,
            "inactive Tab prepared a child or called base preparation");

        dispatch.ApplyPosition(3, 4);
        check(renderer_queue_test::same_rectangle(tab.rectangle, {5, 7, 9, 11}) &&
                  renderer_queue_test::same_rectangle(child.rectangle, {11, 13, 15, 17}),
            "real position forwarding differs");
        dispatch.MoveRectangle(-1, 2);
        check(renderer_queue_test::same_rectangle(tab.rectangle, {4, 9, 8, 13}) &&
                  renderer_queue_test::same_rectangle(child.rectangle, {10, 15, 14, 19}) &&
                  renderer_queue_test::same_rectangle(tab.saved_rectangle, {2, 3, 6, 7}) &&
                  renderer_queue_test::same_rectangle(child.saved_rectangle, {8, 9, 12, 13}),
            "real incremental movement changed saved rectangles");
        dispatch.Unresolved24(0x81);
        check(tab.state_30 == 0x81 && tab.word_44 == 1 && child.state_30 == 0x81,
            "activation lost the actual byte or Boolean Tab state");
        dispatch.MarkPrepared();
        check(tab.word_48 == 3 && tab.preparation_flag == 0 && child.preparation_flag == 1,
            "active preparation did not reach the real child");
        dispatch.Unresolved04(0.25f);
        dispatch.Unresolved08();
        dispatch.Unresolved1c();
        check(!dispatch.HandleInput38(10, 15, 2), "real base child falsely handled input38");
        tab.elapsed_24 = 1.5f;
        child.elapsed_24 = 2.5f;
        dispatch.Unresolved24(0);
        check(tab.state_30 == 0 && tab.word_44 == 0 && tab.elapsed_24 == 0.0f &&
                  child.state_30 == 0 && child.elapsed_24 == 0.0f,
            "real deactivation/base reset differs");
        check(tab.word_2c == 0x23 && tab.unknown_31[0] == 0x62 && tab.unknown_31[1] == 0x62 &&
                  tab.unknown_31[2] == 0x62,
            "Tab forwarding changed unrelated initialized members");
    }
    check(owner.records[0]->reference_count == 0, "Tab did not release its real wrapper");
    check(child.owned_text == text_owner && std::strcmp(child.owned_text, "borrowed child") == 0 &&
              child.kind_28 == 0 && child.rectangle.left == 10 && child.rectangle.bottom == 19,
        "Tab teardown destroyed or changed its borrowed child");
}

void literal_plane(Pixels& expected, bool second)
{
    constexpr std::array<unsigned int, 4> first{0xfffff800, 0xffff07e0, 0xffff001f, 0xffffffff};
    constexpr std::array<unsigned int, 4> last{0xffffffff, 0xffff001f, 0xffff07e0, 0xfffff800};
    const auto& pixels = second ? last : first;
    expected[3 * width + 2] = pixels[0];
    expected[3 * width + 3] = pixels[1];
    expected[4 * width + 2] = pixels[2];
    expected[4 * width + 3] = pixels[3];
}
void queue(const Environment& environment)
{
    const auto& renderer = environment.renderer;
    check(renderer.dirty_rectangle_count == 1 && renderer.dirty_rectangles.values.size() == 1,
        "real Tab resource drawing queue count differs");
    // Actual AccumulateRectangle absorbs later identical bounds.
    for (const auto& rectangle : renderer.dirty_rectangles.values)
    {
        check(renderer_queue_test::same_rectangle(rectangle, {2, 3, 4, 5}),
            "real Tab resource drawing queue bounds differ");
    }
}
void draw(ScreenItem& item)
{
    ++draws;
    item.Unresolved0c();
}
void cached_plane_pixels(Environment& environment, ScreenDialogResourceLookup& owner)
{
    group = "cached-plane-pixels";
    environment.reset(0x12345678);
    Pixels expected;
    expected.fill(0x12345678);
    ScreenFrameItem child({8, 8, 10, 10}, 0);
    ++frame_owners;
    initialized_members(child);
    {
        ScreenTabItem tab({2, 3, 4, 5}, "tab");
        ++tab_owners;
        initialized_members(tab);
        tab.word_48 = 3;
        ScreenItem& dispatch = tab;
        attach(dispatch, child);
        auto& frame = *owner.records[0]->frames[0];
        check(frame.count_0a == 1 && frame.plane_count_0c == 2 && frame.data_4c == nullptr,
            "real generated two-plane Frame differs");
        draw(dispatch);
        literal_plane(expected, false);
        environment.canvas.equal(expected);
        queue(environment);
        check(tab.word_48 == 0 && tab.resource->word_18 == 0 && tab.resource->word_14 == 0 &&
                  frame.time_04 == 999999.0f && frame.data_4c != nullptr &&
                  screen_dialog_frame_cursor == 48 && child.preparation_flag == 1,
            "inactive first plane/read/publication or child gate differs");
        draw(dispatch);
        environment.canvas.equal(expected);
        queue(environment);

        dispatch.Unresolved24(1);
        draw(dispatch);
        literal_plane(expected, true);
        environment.canvas.equal(expected);
        queue(environment);
        check(tab.word_48 == 1 && tab.resource->word_18 == 1 && child.preparation_flag == 0,
            "active second plane or real Frame child draw differs");
        child.preparation_flag = 1;
        draw(dispatch);
        environment.canvas.equal(expected);
        queue(environment);
        check(child.preparation_flag == 0, "cached active Tab skipped its real Frame child");

        tab.word_44 = 7;
        child.preparation_flag = 1;
        draw(dispatch);
        environment.canvas.equal(expected);
        queue(environment);
        check(tab.resource->word_18 == 1 && tab.word_48 == 7 && child.preparation_flag == 1 &&
                  screen_dialog_frame_cursor == 48,
            "positive out-of-range state did not clamp only the plane");
        // State 7 is retained; drawing never repairs it to the last valid plane.
    }
    check(owner.records[0]->reference_count == 0 && child.kind_28 == 11 &&
              child.children.head->next == child.children.head,
        "Tab draw lifetime consumed its borrowed Frame child");
}

bool coordinates(ScreenTabItem& tab, int x, int y, int value, unsigned char selected)
{
    ++coordinate_calls;
    return tab.HandleTabCoordinates(x, y, value, selected);
}
void selected_coordinates(ScreenDialogResourceLookup& owner)
{
    group = "selected-coordinates";
    ScreenFrameItem first({2, 2, 4, 4}, 0);
    ++frame_owners;
    initialized_members(first);
    ScreenItem last({2, 2, 4, 4});
    last.kind_28 = 0;
    initialized_members(last);
    {
        ScreenTabItem tab({0, 0, 10, 10}, "tab");
        ++tab_owners;
        initialized_members(tab);
        tab.word_48 = 3;
        attach(tab, first);
        attach(tab, last);
        check(
            tab.owned_text == nullptr && first.owned_text == nullptr && last.owned_text == nullptr,
            "timed input unexpectedly requires the optional tooltip owner");
        check(coordinates(tab, 3, 3, 1, 0) && tab.state_40 == 1 && tab.last_x == 3 &&
                  tab.last_y == 3 && first.state_42 == 0 && last.last_x == 0,
            "unselected own hit traversed children or lost base coordinates");
        check(coordinates(tab, 3, 3, 1, 0x80) && first.state_42 == 1 && first.state_41 == 0 &&
                  last.last_x == 0 && last.last_y == 0,
            "selected real Frame handling did not short-circuit the tail");
        check(!coordinates(tab, 8, 8, 1, 1) && tab.state_40 == 1 && first.state_40 == 0 &&
                  first.state_42 == 0 && last.last_x == 8 && last.last_y == 8,
            "unhandled selected traversal lost the real final child call");
        check(!coordinates(tab, 3, 3, 3, 0) && tab.state_40 == 0 && tab.last_x == 3 &&
                  tab.last_y == 3 && last.last_x == 8,
            "value3 did not suppress the own hit without visiting children");
        check(coordinates(tab, 3, 3, 2, 1) && first.state_41 == 1 && first.state_42 == 0 &&
                  last.last_x == 8,
            "selected value2 did not reach genuine Frame state publication");
        check(!coordinates(tab, 20, 20, 1, 0) && tab.state_40 == 0 && tab.last_x == 20 &&
                  tab.last_y == 20 && tab.word_44 == 0 && tab.word_48 == 3,
            "outside input changed selection/cache or lost base coordinates");
    }
    check(owner.records[0]->reference_count == 0 && first.kind_28 == 11 && last.kind_28 == 0,
        "coordinate owner teardown consumed borrowed payloads");
}

void all_pool_slots_returned(Seed& seed)
{
    std::array<bool, 8> found{};
    PoolChunkLink* link = pool_chunk_buckets[2];
    for (unsigned n = 0; n < found.size(); ++n)
    {
        unsigned id = 0;
        while (id < found.size() && reinterpret_cast<std::uintptr_t>(link) != seed.address(id))
        {
            ++id;
        }
        check(id < found.size(), "owner returned an unknown pool slot");
        check(!found[id], "owner returned the same pool slot twice");
        found[id] = true;
        // ReturnPool ended each node's lifetime. Read only its actual new link.
        link = link->next;
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
                check(screen_dialog_resources == &owner && owner.record_count == 1 &&
                          std::strcmp(owner.records[0]->name, "tab") == 0 &&
                          owner.records[0]->frame_count == 1 &&
                          owner.records[0]->reference_count == 0,
                    "genuine mounted resource owner differs");
                descriptor = owner.data_file;
                metadata = owner.metadata_file;
                ownership_and_forwarding(owner);
                cached_plane_pixels(environment, owner);
                selected_coordinates(owner);
                environment.borrowed_state();
                check(owner.records[0]->reference_count == 0, "live Tab wrapper reference remains");
            }
            check(screen_dialog_resources == nullptr && screen_dialog_palette_modifiers == nullptr,
                "genuine resource teardown left a registration");
            check(files::close_stream(metadata) == -1, "retained closed metadata identity changed");
            unsigned char byte = 0xa5;
            check(files::read_descriptor(descriptor, &byte, 1) == -1 && byte == 0xa5,
                "genuine owner did not close its nonzero data descriptor");
            all_pool_slots_returned(seed);
        }
        check(borrowed_mount.expired(), "mounted source outlived the real Session");
        generated.unchanged();
        std::printf("{\"groups\":3,\"tab_owners\":%u,\"frame_owners\":%u,\"draw_calls\":%u,"
                    "\"coordinate_calls\":%u,\"checks\":%u,\"pixel_checks\":%u}\n",
            tab_owners,
            frame_owners,
            draws,
            coordinate_calls,
            checks,
            pixel_checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "gui-tab-item %s: %s\n", group, failure.reason);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "fixture setup: %s\n", failure.what());
        return 3;
    }
}
