#include "child_ring_seed.h"
#include "data/folder_source.h"
#include "renderer_queue_test_state.h"
#include "runtime/resource_files.h"
#include "startup_screen_dialog_frame_file.h"
#include "startup_screen_dialog_resource.h"
#include "startup_screen_image_item.h"
#include "startup_screen_manager.h"
#include "startup_screen_palette_modifiers.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
constexpr unsigned background = 0x12345678;
using Pixels = std::array<unsigned int, width * height>;
using Seed = child_ring_fixture::Seed<2>;

static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(std::endian::native == std::endian::little);
static_assert(std::is_base_of_v<ScreenItem, ScreenImageItem>);
static_assert(std::has_virtual_destructor_v<ScreenImageItem>);
static_assert(sizeof(ScreenImageItem) == 120);
#if defined(BEACHLIFE_DECOMP_RENDERER_SEA_STATE)
static_assert(sizeof(ScreenRequestRenderer) == 392 && sizeof(ScreenManager) == 424);
#else
static_assert(sizeof(ScreenRequestRenderer) == 384 && sizeof(ScreenManager) == 424);
#endif
static_assert(sizeof(ScreenDialogResourceNode) == sizeof(ScreenChildNode));
static_assert(alignof(ScreenDialogResourceNode) <= alignof(ScreenChildNode));

struct Failure
{
    const char* reason;
};
unsigned checks = 0, pixel_checks = 0, image_owners = 0, image_draw_calls = 0;
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
constexpr std::array<std::array<unsigned char, 3>, 4> colours{
    {{248, 0, 0}, {0, 252, 0}, {0, 0, 248}, {248, 252, 248}}};
constexpr std::array<unsigned char, 16> packets{3, 0, 1, 2, 3, 3, 0, 1, 3, 2, 3, 0, 3, 1, 2, 3};
constexpr std::array<unsigned int, 12> literal_pixels{0xfffff800,
    0xffff07e0,
    0xffff001f,
    0xffffffff,
    0xfffff800,
    0xffff07e0,
    0xffff001f,
    0xffffffff,
    0xfffff800,
    0xffff07e0,
    0xffff001f,
    0xffffffff};

// Manifest-backed generated files only. The real resource owner reads every
// record, palette, frame and packet through its qualified mounted-I/O bodies.
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
        dword(metadata, 1);
        name(metadata, "icon");
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
        metadata.push_back(1);
        word(metadata, 3);
        word(metadata, 4);
        word(metadata, 0);
        word(metadata, 0);
        dword(metadata, 24);
        dword(metadata, static_cast<unsigned>(packets.size()));
        word(metadata, 1);

        std::vector<unsigned char> bank(20, 0x21);
        dword(bank, 0xaabbccdd);
        word(bank, 0);
        dword(bank, 0);
        bank.insert(bank.end(), packets.begin(), packets.end());
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
    void reset()
    {
        before.fill(0x69a5);
        after.fill(0xc35a);
        for (unsigned i = 0; i < width * height; ++i)
        {
            std::memcpy(words.data() + i * 2, &background, sizeof(background));
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
        reset();
    }
    ~Environment()
    {
        // Only borrowed read models are cleared here, after real item/resource
        // lifetimes and the mounted Session. No game destructor is supplied.
        screen_request_renderer = nullptr;
        screen_manager = nullptr;
        screen_dialog_frame_file = -1;
        screen_dialog_frame_cursor = 0;
    }
    void reset()
    {
        canvas.reset();
        renderer.clip_mode_54 = 1;
        renderer.dirty_rectangles.values.clear();
        renderer.dirty_rectangle_count = 0;
    }
    void borrowed_state() const
    {
        check(renderer.width == width && renderer.height == height &&
                  renderer.pixels_1c == canvas.words.data() && renderer.format_44 == 0 &&
                  renderer.unscaled_clip_5c == 0 && renderer.pixels_locked == 0,
            "Image changed borrowed surface state");
        check(renderer.buffer_130 == markers.data() && std::all_of(markers.begin(),
                                                           markers.end(),
                                                           [](unsigned char value)
                                                           {
                                                               return value == 0;
                                                           }),
            "unused marker owner/pointer changed");
        check(manager.renderer == &renderer && manager.item_134 == nullptr &&
                  manager.auxiliary_130 == nullptr,
            "Image changed the borrowed manager read model");
    }
};

bool rectangle(const ScreenRectangle& value, int left, int top, int right, int bottom)
{
    return value.left == left && value.top == top && value.right == right && value.bottom == bottom;
}
void initialize_preserved_members(ScreenImageItem& item)
{
    // Set only real members after genuine construction. No pre-lifetime byte
    // assumption, vptr/padding write, or constructor opaque-preservation claim.
    item.word_2c = 0x13579bdf;
    std::fill(std::begin(item.unknown_31), std::end(item.unknown_31), 0x62);
    std::fill(item.resource_name + std::strlen(item.resource_name) + 1,
        std::end(item.resource_name),
        static_cast<char>(0x5a));
}
void constructed(const ScreenImageItem& item,
    const ScreenDialogResourceRecord& record,
    const char* requested_name)
{
    // First assertion is the single constructor kind10->11 control witness.
    check(item.kind_28 == 10, "Image constructor kind is not 10");
    check(rectangle(item.rectangle, 5, 7, 8, 11) && rectangle(item.saved_rectangle, 5, 7, 8, 11),
        "real selected-frame dimensions did not replace both right/bottom pairs");
    check(std::strcmp(item.resource_name, requested_name) == 0 && item.resource != nullptr &&
              item.resource->record == &record &&
              std::strcmp(item.resource->name, requested_name) == 0 &&
              item.resource->name != item.resource_name,
        "Image inline name and genuine separately owned resource wrapper differ");
    check(item.elapsed_24 == 0.0f && item.state_30 == 0 && item.last_x == 0 && item.last_y == 0 &&
              item.owned_text == nullptr && item.state_40 == 0 && item.state_41 == 0 &&
              item.state_42 == 0 && item.preparation_flag == 1,
        "real base initial state differs");
}

void draw(ScreenItem& dispatch)
{
    ++image_draw_calls;
    dispatch.Unresolved0c();
}
void drawing(Environment& environment, ScreenImageItem& item)
{
    group = "activation-and-real-resource-drawing";
    auto& frame = *item.resource->record->frames[0];
    ScreenItem& dispatch = item;
    Pixels expected;
    expected.fill(background);
    item.preparation_flag = 0;
    draw(dispatch);
    environment.canvas.equal(expected);
    check(frame.loaded_08 == 0 && screen_dialog_frame_cursor == 24 &&
              environment.renderer.dirty_rectangle_count == 0 &&
              environment.renderer.dirty_rectangles.values.empty(),
        "unprepared Image performed a resource draw/load/queue publication");

    item.elapsed_24 = 3.0f;
    dispatch.Unresolved04(0.25f);
    check(item.elapsed_24 == 3.0f, "actual base update forwarding changed elapsed state");
    dispatch.Unresolved24(0x80);
    check(item.state_30 == 0x80 && item.preparation_flag == 1 && item.elapsed_24 == 3.0f,
        "nonzero Image activation/base/preparation order differs");
    dispatch.Unresolved24(0);
    check(item.state_30 == 0 && item.elapsed_24 == 0.0f && item.preparation_flag == 1,
        "deactivation did not keep preparation while resetting elapsed state");
    draw(dispatch);
    for (unsigned row = 0; row < 4; ++row)
    {
        for (unsigned column = 0; column < 3; ++column)
        {
            expected[(7 + row) * width + 5 + column] = literal_pixels[row * 3 + column];
        }
    }
    environment.canvas.equal(expected);
    check(item.preparation_flag == 0 && item.resource->word_14 == 0 &&
              item.resource->word_04 == 0.0f && frame.time_04 == 999999.0f &&
              frame.loaded_08 == 1 && frame.whole_data_48 == 1 && frame.indices_44[0] == 0 &&
              frame.offsets_70[0] == 0 &&
              std::memcmp(frame.data_4c, packets.data(), packets.size()) == 0 &&
              screen_dialog_frame_cursor == 46,
        "real image time, lazy bank load or post-draw publication differs");
    check(environment.renderer.dirty_rectangle_count == 1 &&
              environment.renderer.dirty_rectangles.values.size() == 1 &&
              rectangle(environment.renderer.dirty_rectangles.values.front(), 5, 7, 8, 11),
        "real Image draw queue bounds differ");
    draw(dispatch);
    environment.canvas.equal(expected);
    check(environment.renderer.dirty_rectangle_count == 1 && screen_dialog_frame_cursor == 46,
        "cleared preparation repeated the resource draw");

    environment.reset();
    dispatch.MoveRectangle(9, -1);
    dispatch.MarkPrepared();
    draw(dispatch);
    expected.fill(background);
    for (unsigned row = 0; row < 4; ++row)
    {
        expected[(6 + row) * width + 14] = literal_pixels[row * 3];
        expected[(6 + row) * width + 15] = literal_pixels[row * 3 + 1];
    }
    environment.canvas.equal(expected);
    check(item.preparation_flag == 0 && environment.renderer.dirty_rectangle_count == 1 &&
              environment.renderer.dirty_rectangles.values.size() == 1 &&
              rectangle(environment.renderer.dirty_rectangles.values.front(), 14, 6, 16, 10) &&
              screen_dialog_frame_cursor == 46,
        "right-clipped Image or resident source reuse differs");
    dispatch.MoveRectangle(-9, 1);
    check(rectangle(item.rectangle, 5, 7, 8, 11) && rectangle(item.saved_rectangle, 5, 7, 8, 11),
        "real move changed the saved rectangle or failed to restore the live rectangle");
}

void hits(ScreenImageItem& item)
{
    group = "inclusive-geometry-and-timed-input";
    ScreenItem& dispatch = item;
    check(dispatch.TestRectangle(5, 7) && item.state_40 == 1, "left/top equality is not a hit");
    check(
        dispatch.TestRectangle(8, 11) && item.state_40 == 1, "right/bottom equality is not a hit");
    check(!dispatch.TestRectangle(9, 11) && item.state_40 == 0,
        "point after the right edge is a hit");
    check(!dispatch.TestRectangle(8, 12) && item.state_40 == 0,
        "point after the bottom edge is a hit");
    struct Case
    {
        int x, y, value;
        unsigned char state40, state41, state42;
    };
    constexpr std::array<Case, 5> cases{{{8, 11, 1, 1, 0, 1},
        {8, 11, 2, 1, 1, 0},
        {8, 11, 3, 0, 0, 0},
        {8, 11, 7, 1, 0, 0},
        {9, 11, 1, 0, 0, 0}}};
    for (const auto& test : cases)
    {
        item.state_41 = 0x51;
        item.state_42 = 0x62;
        check(!dispatch.HandleTimedCoordinates(test.x, test.y, test.value),
            "Image timed input returned true");
        check(item.state_40 == test.state40 && item.state_41 == test.state41 &&
                  item.state_42 == test.state42 && item.last_x == test.x && item.last_y == test.y,
            "literal Image states or real base coordinate publication differ");
    }
    check(item.owned_text == nullptr && item.word_2c == 0x13579bdf &&
              std::all_of(std::begin(item.unknown_31),
                  std::end(item.unknown_31),
                  [](unsigned char byte)
                  {
                      return byte == 0x62;
                  }) &&
              std::all_of(item.resource_name + 5,
                  std::end(item.resource_name),
                  [](char byte)
                  {
                      return byte == 0x5a;
                  }),
        "Image operations changed initialized preserved members");
}

void image_groups(Environment& environment, ScreenDialogResourceLookup& owner)
{
    group = "genuine-image-and-resource-ownership";
    auto& record = *owner.records[0];
    check(record.reference_count == 0, "record has an unexpected initial reference");
    {
        ScreenImageItem image({5, 7, 99, 111}, "icon");
        ++image_owners;
        constructed(image, record, "icon");
        initialize_preserved_members(image);
        check(record.reference_count == 1, "Image constructor did not acquire one reference");
        {
            auto* second = new ScreenImageItem({5, 7, -9, -11}, "ICON");
            std::unique_ptr<ScreenItem> second_owner(second);
            ++image_owners;
            constructed(*second, record, "ICON");
            initialize_preserved_members(*second);
            check(second->resource != image.resource && record.reference_count == 2,
                "case-insensitive Find did not create distinct genuine wrappers");
            second_owner->SetOwnedText("owned text released by the actual base destructor");
            check(std::strcmp(
                      second->owned_text, "owned text released by the actual base destructor") == 0,
                "real base owned text assignment differs");
        }
        check(record.reference_count == 1 && image.resource->record == &record,
            "virtual Image destruction released the record or retained its reference");
        drawing(environment, image);
        hits(image);
    }
    check(record.reference_count == 0 && record.frames[0]->loaded_08 == 1,
        "Image teardown retained a wrapper or destroyed the borrowed record/frame");
    environment.borrowed_state();
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
                          std::strcmp(owner.records[0]->name, "icon") == 0 &&
                          owner.records[0]->format_18 == 1 &&
                          owner.records[0]->palette_count == 1 &&
                          owner.records[0]->frame_count == 1 &&
                          owner.records[0]->frames[0]->width_0e == 3 &&
                          owner.records[0]->frames[0]->height_10 == 4 &&
                          owner.records[0]->palettes[0][0] == 0xf800 &&
                          owner.records[0]->palettes[0][1] == 0x07e0 &&
                          owner.records[0]->palettes[0][2] == 0x001f &&
                          owner.records[0]->palettes[0][3] == 0xffff,
                    "genuine resource graph/metadata/palette load differs");
                descriptor = owner.data_file;
                metadata = owner.metadata_file;
                image_groups(environment, owner);
            }
            check(screen_dialog_resources == nullptr && screen_dialog_palette_modifiers == nullptr,
                "genuine resource teardown retained a registration");
            check(files::close_stream(metadata) == -1, "closed metadata identity changed");
            unsigned char byte = 0xa5;
            check(files::read_descriptor(descriptor, &byte, 1) == -1 && byte == 0xa5,
                "genuine resource owner did not close its data descriptor");
            seed.check_initial_order();
        }
        check(borrowed_mount.expired(), "mounted source outlived the real Session");
        generated.unchanged();
        std::printf("{\"groups\":3,\"image_owners\":%u,\"image_draw_calls\":%u,"
                    "\"checks\":%u,\"pixel_checks\":%u}\n",
            image_owners,
            image_draw_calls,
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
