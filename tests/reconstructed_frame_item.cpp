#include "child_ring_seed.h"
#include "data/folder_source.h"
#include "render_mask_setup.h"
#include "renderer_queue_test_state.h"
#include "runtime/resource_files.h"
#include "startup_screen_dialog_frame_file.h"
#include "startup_screen_dialog_resource.h"
#include "startup_screen_frame_item.h"
#include "startup_screen_frame_resources.h"
#include "startup_screen_manager.h"
#include "startup_screen_palette_modifiers.h"

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

struct Failure
{
    const char* reason;
};
unsigned checks = 0, pixel_checks = 0, frame_owners = 0;
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
constexpr std::array<const char*, 6> names{"UI3_combobox_textarea",
    "UI3_combobox_textarea_w",
    "UI3_combobox_textarea_b",
    "UI3_scrollbar_back",
    "UI3_SCROLLBAR_SLIDER",
    "dummy"};
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
    std::array<unsigned char, width * height> markers{};
    ScreenRequestRenderer renderer{};
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
        // owner and the mounted Session. No game teardown is replaced here.
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
            "Frame drawing changed borrowed surface state");
        check(renderer.buffer_130 == markers.data() && std::all_of(markers.begin(),
                                                           markers.end(),
                                                           [](unsigned char value)
                                                           {
                                                               return value == 0;
                                                           }),
            "unused marker owner/pointer changed");
        check(manager.renderer == &renderer && manager.item_134 == nullptr,
            "Frame input changed the borrowed manager read model");
        for (unsigned i = 0; i < 10; ++i)
        {
            check(manager.fonts_04[i] == nullptr && manager.fonts_2c[i] == nullptr,
                "Frame drawing changed borrowed font slots");
        }
    }
};

void typed_uninitialized_members(ScreenItem& item)
{
    // The true constructors leave these fields alone. Set only their actual
    // typed/opaque members, without touching a vptr or native ABI padding.
    item.word_2c = 0x23;
    for (auto& byte : item.unknown_31)
    {
        byte = 0x62;
    }
}
void owner_and_children()
{
    group = "genuine-frame-and-child-lifetimes";
    ScreenItem child({20, 20, 22, 22});
    child.kind_28 = 0;
    typed_uninitialized_members(child);
    {
        ScreenFrameItem parent({1, 1, 3, 3}, 0);
        ++frame_owners;
        typed_uninitialized_members(parent);
        check(parent.kind_28 == 11 && parent.word_44 == 0 && parent.word_48 == 0 &&
                  parent.children.head->next == parent.children.head,
            "genuine Frame constructor fields/ring differ");
        ScreenItem* dispatch = &parent;
        dispatch->Unresolved10(&child);
        check(parent.children.head->next->item == &child &&
                  parent.children.head->previous == parent.children.head->next,
            "real attachment did not retain the live borrowed child");
        child.preparation_flag = 0;
        dispatch->MarkPrepared();
        check(parent.preparation_flag == 1 && child.preparation_flag == 1,
            "real preparation did not traverse children");
        dispatch->Unresolved04(0.25f);
        dispatch->Unresolved1c();
        dispatch->Unresolved24(1);
        check(child.state_30 == 1 && child.elapsed_24 == 0.0f, "real child activation differs");
        dispatch->Unresolved24(0);
        check(child.state_30 == 0 && child.elapsed_24 == 0.0f, "real child deactivation differs");
        check(dispatch->TestRectangle(21, 21), "child geometry did not extend the Frame hit");
        check(dispatch->HandleTimedCoordinates(21, 21, 1) && parent.state_42 == 1 &&
                  parent.state_41 == 0 && parent.state_40 == 1,
            "Frame coordinate dispatch/own fallback differs");
        check(!dispatch->HandleTimedEvent(21, 21, 2), "real child event unexpectedly handled");
        dispatch->Unresolved0c();
        // The existing single-expression control leaves this flag at one.
        check(parent.preparation_flag == 0, "Frame draw did not clear preparation");
        check(child.last_x == 21 && child.last_y == 21 && child.owned_text == nullptr,
            "real base child publication differs");
    }
    check(child.kind_28 == 0 && child.rectangle.left == 20 && child.rectangle.bottom == 22,
        "Frame teardown destroyed or changed a borrowed payload");
}

void rectangle(Pixels& expected, int left, int top, int right, int bottom, unsigned short colour)
{
    for (int y = top; y < bottom; ++y)
    {
        for (int x = left; x < right; ++x)
        {
            expected[static_cast<unsigned>(y) * width + static_cast<unsigned>(x)] =
                0x12340000u | colour;
        }
    }
}
void cap(Pixels& expected, unsigned x, unsigned y, bool second)
{
    constexpr std::array<unsigned int, 4> first{0xfffff800, 0xffff07e0, 0xffff001f, 0xffffffff};
    constexpr std::array<unsigned int, 4> last{0xffffffff, 0xffff001f, 0xffff07e0, 0xfffff800};
    const auto& pixels = second ? last : first;
    expected[y * width + x] = pixels[0];
    expected[y * width + x + 1] = pixels[1];
    expected[(y + 1) * width + x] = pixels[2];
    expected[(y + 1) * width + x + 1] = pixels[3];
}
void queue_pair(const Environment& environment, ScreenRectangle first, ScreenRectangle second)
{
    const auto& renderer = environment.renderer;
    check(renderer.dirty_rectangle_count == 2 && renderer.dirty_rectangles.values.size() == 2,
        "real clip/queue publication count differs");
    check(renderer_queue_test::same_rectangle(renderer.dirty_rectangles.values[0], first) &&
              renderer_queue_test::same_rectangle(renderer.dirty_rectangles.values[1], second),
        "real clip/queue literal bounds differ");
}
void resource_styles(Environment& environment, ScreenDialogResourceLookup& owner)
{
    group = "resource-backed-frame-pixels";
    constexpr std::array<unsigned, 3> styles{1, 4, 5};
    for (unsigned style : styles)
    {
        environment.reset(0x12340000);
        Pixels expected;
        expected.fill(0x12340000);
        // Independent literal memberships for the fixed rectangle{2,2,20,20}.
        // The oracle calls no reconstructed colour, line, fill or raster method.
        if (style == 1)
        {
            rectangle(expected, 4, 2, 21, 3, 0x6b50);
            rectangle(expected, 4, 3, 21, 4, 0x9cf6);
            rectangle(expected, 4, 4, 21, 5, 0xd6bc);
            rectangle(expected, 4, 5, 19, 15, 0xef5f);
            rectangle(expected, 4, 15, 21, 16, 0xef7f);
            rectangle(expected, 4, 16, 21, 17, 0xf79f);
            rectangle(expected, 4, 17, 21, 18, 0xf79f);
            cap(expected, 2, 2, false);
            cap(expected, 19, 2, true);
        }
        else if (style == 4)
        {
            rectangle(expected, 2, 2, 3, 20, 0xb67f);
            rectangle(expected, 3, 2, 4, 20, 0xb65f);
            rectangle(expected, 4, 2, 5, 20, 0xa5ff);
            rectangle(expected, 5, 2, 13, 19, 0x959f);
            rectangle(expected, 13, 2, 14, 20, 0x851f);
            rectangle(expected, 14, 2, 15, 20, 0x5bde);
            rectangle(expected, 15, 2, 16, 20, 0x2a9d);
            cap(expected, 2, 2, false);
            cap(expected, 2, 18, true);
        }
        else
        {
            rectangle(expected, 2, 4, 3, 19, 0x6d2f);
            rectangle(expected, 3, 4, 4, 19, 0x44eb);
            rectangle(expected, 4, 4, 5, 19, 0x3dac);
            rectangle(expected, 5, 4, 11, 18, 0x462e);
            rectangle(expected, 11, 4, 12, 19, 0x7ed3);
            rectangle(expected, 12, 4, 13, 19, 0xb759);
            rectangle(expected, 13, 4, 14, 19, 0x6d2f);
            cap(expected, 2, 2, false);
            cap(expected, 2, 18, true);
        }
        {
            ScreenFrameItem frame({2, 2, 20, 20}, style);
            ++frame_owners;
            typed_uninitialized_members(frame);
            ScreenItem* dispatch = &frame;
            dispatch->Unresolved0c();
            check(frame.preparation_flag == 0, "resource style did not clear Frame preparation");
            environment.canvas.equal(expected);
            const ScreenRectangle second =
                style == 1 ? ScreenRectangle{19, 2, 21, 4} : ScreenRectangle{2, 18, 4, 20};
            queue_pair(environment, {2, 2, 4, 4}, second);
            dispatch->Unresolved0c();
            environment.canvas.equal(expected);
            queue_pair(environment, {2, 2, 4, 4}, second);
        }
        for (int i = 0; i < owner.record_count; ++i)
        {
            check(owner.records[i]->reference_count == 0,
                "style did not release its real resource wrapper");
        }
    }
}

struct Handle
{
    ScreenDialogResourceLookup& owner;
    ScreenDialogResource* value;
    ~Handle()
    {
        owner.Release(value);
    }
};
struct Format
{
    ScreenDialogResourceRecord& record;
    unsigned old;
    explicit Format(ScreenDialogResourceRecord& value)
        : record(value)
        , old(value.format_18)
    {
        // A bounded typed state change on a genuinely loaded owner. The real
        // parser's format12 palette gap is not hidden by a fake parser body.
        record.format_18 = 12;
    }
    ~Format()
    {
        record.format_18 = old;
    }
};
void depth_and_clipping(Environment& environment, ScreenDialogResourceLookup& owner)
{
    group = "real-depth-routes-and-clipping";
    environment.reset(0);
    Handle handle{owner, owner.Lookup(names[0])};
    Format format(*handle.value->record);
    auto& resource = *handle.value;
    resource.word_08 = 3;
    resource.word_14 = 0;
    std::array<unsigned short, 64> palette{};
    palette[0] = 0x1111;
    palette[1] = 0x2222;
    palette[2] = 0x3333;
    palette[3] = 0x4444;
    resource.word_2c = palette.data();
    resource.Draw(4, 4, 0, 5.0f);
    Pixels expected{};
    expected[4 * width + 4] = 0x00f21111;
    expected[4 * width + 5] = 0x00f22222;
    expected[5 * width + 4] = 0x01023333;
    expected[5 * width + 5] = 0x01024444;
    environment.canvas.equal(expected);
    check(resource.word_2c == nullptr && resource.word_04 == 0.0f && resource.word_14 == 0 &&
              resource.record->frames[0]->time_04 == 5.0f,
        "true resource timestamp/palette-pointer publication differs");
    resource.word_2c = palette.data();
    resource.Draw(100, 4, 0, 9.0f);
    check(resource.word_2c == palette.data() && resource.record->frames[0]->time_04 == 5.0f &&
              environment.renderer.dirty_rectangle_count == 1,
        "rejected clip changed late resource state or queue");
    environment.canvas.equal(expected);
    environment.renderer.clip_mode_54 = 2;
    resource.Draw(12, 12, 0, 6.0f);
    expected[6 * width + 6] = 0x01023333;
    environment.canvas.equal(expected);
    check(resource.word_2c == nullptr && resource.record->frames[0]->time_04 == 6.0f,
        "mode2 real dispatcher did not clear the palette override/update time");
    queue_pair(environment, {4, 4, 6, 6}, {12, 12, 14, 14});
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
        // Only the live PoolChunkLink is read after real ReturnPool. The old
        // resource/child node lifetime has ended; no dead node guard is read.
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
                check(screen_dialog_resources == &owner && owner.record_count == 6,
                    "genuine resource construction/load failed");
                descriptor = owner.data_file;
                metadata = owner.metadata_file;
                for (unsigned i = 0; i < names.size(); ++i)
                {
                    check(std::strcmp(owner.records[i]->name, names[i]) == 0 &&
                              owner.records[i]->frame_count == 1 &&
                              owner.records[i]->reference_count == 0,
                        "genuine resource record graph differs");
                }
                for (unsigned i = 0; i < 3; ++i)
                {
                    check(screen_frame_style1_names[i] == nullptr,
                        "native selected-use style slot was not initially null");
                }
                InitializeFrameStyleNames();
                for (unsigned i = 0; i < 3; ++i)
                {
                    check(std::strcmp(screen_frame_style1_names[i], names[i]) == 0,
                        "real style initializer published another name");
                }
                check(std::strcmp(screen_frame_style4_name, names[3]) == 0 &&
                          std::strcmp(screen_frame_style5_name, names[4]) == 0,
                    "actual mutable style-name owners differ");
                owner_and_children();
                resource_styles(environment, owner);
                depth_and_clipping(environment, owner);
                environment.borrowed_state();
                for (int i = 0; i < owner.record_count; ++i)
                {
                    check(owner.records[i]->reference_count == 0, "live wrapper reference remains");
                }
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
        std::printf("{\"groups\":3,\"frame_owners\":%u,\"checks\":%u,\"pixel_checks\":%u}\n",
            frame_owners,
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
