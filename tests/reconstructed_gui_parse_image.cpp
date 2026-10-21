#include "child_ring_seed.h"
#include "data/folder_source.h"
#include "render_mask_setup.h"
#include "renderer_queue_test_state.h"
#include "runtime/resource_files.h"
#include "startup_gui_parser.h"
#include "startup_screen_child_link.h"
#include "startup_screen_dialog_frame_file.h"
#include "startup_screen_dialog_resource.h"
#include "startup_screen_frame_item.h"
#include "startup_screen_image_item.h"
#include "startup_screen_manager.h"
#include "startup_screen_palette_modifiers.h"
#include "startup_screen_text.h"
#include "text_lookup_owner_test_data.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <clocale>
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
using Seed = child_ring_fixture::Seed<4>;
static_assert(CHAR_BIT == 8 && sizeof(void*) == 8 && sizeof(unsigned int) == 4);
static_assert(sizeof(unsigned short) == 2 && std::endian::native == std::endian::little);
static_assert(std::is_aggregate_v<GuiParserState> && std::is_standard_layout_v<GuiParserState>);
static_assert(std::is_aggregate_v<ScreenManager>);
static_assert(sizeof(GuiParserState) == 120 && offsetof(GuiParserState, defined_items) == 72);
static_assert(
    offsetof(GuiParserState, registry_head) == 80 && offsetof(GuiParserState, word_38) == 112);
static_assert(sizeof(ScreenDialogResourceNode) == sizeof(ScreenChildNode));

struct Failure
{
    const char* reason;
};
unsigned checks = 0, calls = 0, released_items = 0, draws = 0, pixel_checks = 0;
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
template <class T> struct Guarded
{
    std::array<unsigned char, 16> before{};
    T value{};
    std::array<unsigned char, 16> after{};
    Guarded()
    {
        before.fill(0x26);
        after.fill(0x62);
    }
    void verify() const
    {
        for (unsigned i = 0; i < before.size(); ++i)
        {
            check(before[i] == 0x26 && after[i] == 0x62, "outside typed guard changed");
        }
    }
};

// A genuine owner with a scoped borrowed table view for these method cases.
template <> struct Guarded<ScreenTextLookup>
{
    text_owner_test::Bootstrap bootstrap;
    std::array<unsigned char, 16> before{};
    ScreenTextLookup value{"owner.txt"};
    std::array<unsigned char, 16> after{};
    Guarded()
    {
        before.fill(0x26);
        after.fill(0x62);
    }
    void verify() const
    {
        for (unsigned i = 0; i < before.size(); ++i)
        {
            check(before[i] == 0x26 && after[i] == 0x62, "outside typed guard changed");
        }
    }
    text_owner_test::RestoreOwnedTable restore{value, bootstrap};
};
void word(std::vector<unsigned char>& out, unsigned value)
{
    out.push_back(static_cast<unsigned char>(value));
    out.push_back(static_cast<unsigned char>(value >> 8));
}
void dword(std::vector<unsigned char>& out, unsigned value)
{
    word(out, value);
    word(out, value >> 16);
}
void name(std::vector<unsigned char>& out, const char* text)
{
    const auto length = std::strlen(text) + 1;
    require(length < 256, "generated name length exceeds its byte");
    out.push_back(static_cast<unsigned char>(length));
    out.insert(out.end(), text, text + length);
}

// The same real mounted owner route as the existing Frame fixture, reduced to
// one generated format1 record, one animation index/plane and six packet bytes.
class Generated
{
  public:
    explicit Generated(fs::path root)
        : root_(std::move(root))
    {
        require(!fs::exists(root_), "generated directory already exists");
        fs::create_directories(root_ / "Data/Sprites");
        std::vector<unsigned char> metadata(20, 0x42), bank(20, 0x21);
        dword(metadata, 0x12345678);
        dword(metadata, 1);
        dword(bank, 0xaabbccdd);
        name(metadata, "icon");
        dword(metadata, 1);
        dword(metadata, 1);
        constexpr std::array<std::array<unsigned char, 3>, 4> rgb{
            {{248, 0, 0}, {0, 252, 0}, {0, 0, 248}, {248, 252, 248}}};
        for (unsigned i = 0; i < 256; ++i)
        {
            const auto colour = i < rgb.size() ? rgb[i] : std::array<unsigned char, 3>{};
            metadata.insert(metadata.end(), colour.begin(), colour.end());
        }
        dword(metadata, 1);
        name(metadata, "idle");
        word(metadata, 1);
        metadata.push_back(1);
        word(metadata, 2);
        word(metadata, 2);
        word(metadata, 0);
        word(metadata, 0);
        dword(metadata, 24);
        dword(metadata, 6);
        word(metadata, 1);
        word(bank, 0);
        dword(bank, 0);
        bank.insert(bank.end(), {2, 0, 1, 2, 2, 3});
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

struct Environment
{
    Guarded<std::array<unsigned short, width * height * 2>> canvas;
    Guarded<std::array<unsigned char, width * height>> markers;
    Guarded<ScreenRequestRenderer> renderer;
    Guarded<ScreenManager> manager;
    Environment()
    {
        check(screen_request_renderer == nullptr && screen_manager == nullptr &&
                  screen_dialog_resources == nullptr &&
                  screen_dialog_palette_modifiers == nullptr && screen_dialog_frame_file == -1 &&
                  screen_dialog_frame_cursor == 0,
            "another read model or resource owner is active");
        auto& r = renderer.value;
        renderer_queue_test::initialize(r, 31);
        r.software_mode = 1;
        r.width = width;
        r.height = height;
        r.clip_rectangle = {0, 0, static_cast<int>(width), static_cast<int>(height)};
        r.pixels_1c = canvas.value.data();
        r.buffer_130 = markers.value.data();
        r.format_44 = 0;
        r.unscaled_clip_5c = 0;
        r.clip_flag_5d = 0;
        r.rectangle_enabled_4e = 1;
        r.rectangle_suppressed_4d = 0;
        r.rectangle_ready_4f = 1;
        r.pixels_locked = 0;
        r.current_surface = 0;
        r.clip_mode_54 = 1;
        r.dirty_rectangles.values.clear();
        r.dirty_rectangle_count = 0;
        constexpr unsigned int background = 0x12345678;
        for (unsigned i = 0; i < width * height; ++i)
        {
            std::memcpy(canvas.value.data() + i * 2, &background, sizeof(background));
        }
        manager.value.renderer = &r;
        screen_request_renderer = &r;
        screen_manager = &manager.value;
        SetupRenderMasks(canvas.value.data(), width, 0);
    }
    ~Environment()
    {
        SetupRenderMasks(nullptr, 0, 0);
        screen_request_renderer = nullptr;
        screen_manager = nullptr;
        screen_dialog_frame_file = -1;
        screen_dialog_frame_cursor = 0;
    }
    void literal_pixels() const
    {
        for (unsigned y = 0; y < height; ++y)
        {
            for (unsigned x = 0; x < width; ++x)
            {
                unsigned int expected = 0x12345678;
                if (y == 2 && x == 1)
                {
                    expected = 0xfffff800;
                }
                if (y == 2 && x == 2)
                {
                    expected = 0xffff07e0;
                }
                if (y == 3 && x == 1)
                {
                    expected = 0xffff001f;
                }
                if (y == 3 && x == 2)
                {
                    expected = 0xffffffff;
                }
                unsigned int actual;
                std::memcpy(&actual, canvas.value.data() + (y * width + x) * 2, sizeof(actual));
                ++pixel_checks;
                check(actual == expected, "parsed Image literal pixel differs");
            }
        }
        const auto& r = renderer.value;
        check(r.dirty_rectangle_count == 1 && r.dirty_rectangles.values.size() == 1 &&
                  renderer_queue_test::same_rectangle(
                      r.dirty_rectangles.values.front(), {1, 2, 3, 4}),
            "parsed Image draw queue differs");
        check(r.pixels_1c == canvas.value.data() && r.buffer_130 == markers.value.data() &&
                  r.width == width && r.height == height && r.format_44 == 0 &&
                  r.pixels_locked == 0 && manager.value.renderer == &renderer.value,
            "borrowed renderer/manager state changed");
        check(std::all_of(markers.value.begin(),
                  markers.value.end(),
                  [](auto x)
                  {
                      return x == 0;
                  }),
            "unused marker storage changed");
        canvas.verify();
        markers.verify();
        renderer.verify();
        manager.verify();
    }
};

// The real localization owner temporarily borrows these typed records. Its
// constructor-owned table is restored before genuine destruction.
struct Localization
{
    Guarded<ScreenTextLookup> lookup;
    Guarded<std::array<ScreenTextBucket, 2>> buckets;
    Guarded<ScreenTextValue> value;
    Guarded<std::array<unsigned short, 3>> text;
    ScreenTextLookup* previous = screen_text_lookup;
    Localization()
    {
        text.value = {0, 76, 0};
        value.value = {"LABEL", text.value.data() + 1};
        buckets.value = {text_owner_test::bucket("LABEL", &value.value, 0),
            text_owner_test::bucket(nullptr, nullptr, 1)};
        lookup.value.end_index = 1;
        lookup.value.bucket_count = 1;
        lookup.value.capacity_04 = 2;
        lookup.value.buckets = buckets.value.data();
        screen_text_lookup = &lookup.value;
    }
    ~Localization()
    {
        screen_text_lookup = previous;
    }
    void verify() const
    {
        check(screen_text_lookup == &lookup.value && lookup.value.end_index == 1 &&
                  lookup.value.bucket_count == 1 && lookup.value.buckets == buckets.value.data() &&
                  buckets.value[0].value == &value.value && buckets.value[0].unavailable == 0 &&
                  std::strcmp(buckets.value[0].key, "LABEL") == 0 &&
                  value.value.text == text.value.data() + 1 &&
                  text.value == std::array<unsigned short, 3>{0, 76, 0},
            "optional text lookup changed borrowed state");
        lookup.verify();
        buckets.verify();
        value.verify();
        text.verify();
    }
};

struct ParseSession
{
    Guarded<GuiParserState> parser;
    explicit ParseSession(ScreenManager& manager)
    {
        parser.value.manager = &manager;
        parser.value.word_3c = 0x2468ace0;
    }
    ~ParseSession()
    {
        clear();
    }
    void clear()
    {
        auto& p = parser.value;
        // The control creates extra borrowed registry references. The item
        // chain remains the sole owner, including on the first failed oracle.
        while (p.registry_head)
        {
            auto* node = p.registry_head;
            p.registry_head = node->next;
            node->item = nullptr;
            delete node;
        }
        p.registry_tail = nullptr;
        while (p.items_head)
        {
            auto* node = p.items_head;
            p.items_head = node->next;
            delete node; // Genuine kind10 link -> Image -> resource Release/base.
            ++released_items;
        }
        p.items_tail = nullptr;
        p.last_registered_item = nullptr;
    }
};
void preserved(const GuiParserState& p, const GuiParserState& before)
{
    check(p.manager == before.manager && p.buffer == before.buffer &&
              p.current_context == before.current_context &&
              p.alternate_context_10 == before.alternate_context_10 &&
              p.alternate_context_14 == before.alternate_context_14 &&
              p.context_head == before.context_head &&
              p.previous_context == before.previous_context &&
              p.defined_items == before.defined_items && p.word_38 == before.word_38 &&
              p.word_3c == before.word_3c,
        "GUI handler changed unrelated parser/context state");
}
ScreenImageItem& image(ScreenChildLink* link,
    ScreenRectangle rectangle,
    ScreenDialogResourceRecord* record,
    unsigned int word_value)
{
    check(link != nullptr, "real image link absent");
    auto* result = dynamic_cast<ScreenImageItem*>(link->item);
    check(result != nullptr, "registered item is not a genuine Image");
    check(result->kind_28 == 10 && result->word_2c == word_value &&
              renderer_queue_test::same_rectangle(result->rectangle, rectangle) &&
              renderer_queue_test::same_rectangle(result->saved_rectangle, rectangle) &&
              std::strcmp(result->resource_name, "icon") == 0 && result->resource != nullptr &&
              result->resource->record == record && result->preparation_flag == 1,
        "real image construction/registration differs");
    check(result->elapsed_24 == 0.0f && result->state_30 == 0 && result->last_x == 0 &&
              result->last_y == 0,
        "real base construction differs");
    return *result;
}
void normal_groups(Environment& environment, ScreenDialogResourceLookup& owner)
{
    ParseSession session(environment.manager.value);
    auto& p = session.parser.value;
    auto* record = owner.records[0];
    group = "unnamed-image-and-pixels";
    constexpr char first[] = "(1,2,30,40,icon)TAIL";
    p.buffer = first;
    p.word_38 = 0x13579bdf;
    const auto before_first = p;
    p.ParseImage();
    ++calls;
    check(p.registry_head == nullptr && p.registry_tail == nullptr, "unexpected named registry");
    check(p.cursor == 16 && std::strcmp(p.buffer + p.cursor, "TAIL") == 0 &&
              p.items_head == p.items_tail && p.items_head && p.items_head->next == nullptr,
        "first cursor/link differs");
    auto* first_link = p.items_head;
    auto& first_item = image(first_link, {1, 2, 3, 4}, record, 0x13579bdf);
    check(p.last_registered_item == &first_item && first_item.owned_text == nullptr &&
              record->reference_count == 1,
        "first publication/resource reference differs");
    preserved(p, before_first);
    ScreenItem* dispatch = &first_item;
    dispatch->Unresolved0c();
    ++draws;
    check(first_item.preparation_flag == 0 && first_item.resource->word_14 == 0,
        "genuine parsed Image draw did not finish");
    environment.literal_pixels();

    group = "parent-relative-owned-text";
    {
        ScreenFrameItem parent({10, 20, 80, 90}, 0);
        parent.word_2c = 0x57;
        Localization localization;
        constexpr char second[] = "xx( \t1,2,30,40,icon,\tLABEL)TAIL";
        p.buffer = second;
        p.cursor = 2;
        p.current_context = &parent;
        p.word_38 = 0xfedcba98;
        const auto before_second = p;
        p.ParseImage();
        ++calls;
        check(
            p.registry_head == nullptr && p.registry_tail == nullptr, "unexpected named registry");
        check(p.cursor == 27 && std::strcmp(p.buffer + p.cursor, "TAIL") == 0 &&
                  p.items_head == first_link && first_link->next == p.items_tail && p.items_tail &&
                  p.items_tail != first_link && p.items_tail->next == nullptr,
            "optional cursor/append order differs");
        auto& second_item = image(p.items_tail, {11, 22, 13, 24}, record, 0xfedcba98);
        check(p.last_registered_item == &second_item && &first_item != &second_item &&
                  first_link->item == &first_item && first_item.word_2c == 0x13579bdf &&
                  first_item.owned_text == nullptr && second_item.owned_text != nullptr &&
                  std::strcmp(second_item.owned_text, "LABEL") == 0 &&
                  static_cast<const void*>(second_item.owned_text) !=
                      localization.text.value.data() + 1 &&
                  record->reference_count == 2,
            "optional owned key or retained item/reference differs");
        auto* const child = parent.children.head->next;
        check(child != parent.children.head && child->item == &second_item &&
                  child->next == parent.children.head && child->previous == parent.children.head &&
                  parent.children.head->previous == child,
            "real context attachment differs");
        preserved(p, before_second);
        localization.verify();
        p.current_context = nullptr;
        // The genuine Frame destructor returns its borrowed child node without
        // destroying the Image still owned by the parser's all-items chain.
    }
    check(record->reference_count == 2 && first_link->next == p.items_tail &&
              p.items_tail->item->kind_28 == 10,
        "parent teardown destroyed a borrowed Image");
    session.clear();
    check(record->reference_count == 0 && p.items_head == nullptr && p.registry_head == nullptr,
        "genuine image/link cleanup retained a resource reference");
    session.parser.verify();
}
void icon_groups(Environment& environment, ScreenDialogResourceLookup& owner)
{
    ParseSession session(environment.manager.value);
    auto& p = session.parser.value;
    auto* record = owner.records[0];
    group = "icon-and-pixels";
    constexpr char first[] = "(\r\n1,2,30,40,icon,0)TAIL";
    p.buffer = first;
    p.word_38 = 0x13579bdf;
    const auto before_first = p;
    p.ParseIcon();
    ++calls;
    check(p.registry_head == nullptr && p.registry_tail == nullptr, "unexpected named registry");
    check(p.cursor == 20 && std::strcmp(p.buffer + p.cursor, "TAIL") == 0 &&
              p.items_head == p.items_tail && p.items_head && p.items_head->next == nullptr,
        "first icon cursor/link differs");
    auto* first_link = p.items_head;
    auto& first_item = image(first_link, {1, 2, 3, 4}, record, 0x13579bdf);
    check(first_item.resource->word_08 == 3 && first_item.resource->word_14 == 0,
        "icon mode/frame differs");
    check(p.last_registered_item == &first_item && first_item.owned_text == nullptr &&
              record->reference_count == 1,
        "first icon publication/resource reference differs");
    preserved(p, before_first);
    first_item.Unresolved0c();
    ++draws;
    check(first_item.preparation_flag == 0 && first_item.resource->word_14 == 0,
        "genuine parsed Icon draw did not finish");
    environment.literal_pixels();

    group = "parent-relative-icon";
    {
        ScreenFrameItem parent({10, 20, 80, 90}, 0);
        parent.word_2c = 0x57;
        constexpr char second[] = "xx( \t1,2,30,40,icon,0)TAIL";
        p.buffer = second;
        p.cursor = 2;
        p.current_context = &parent;
        p.word_38 = 0xfedcba98;
        const auto before_second = p;
        p.ParseIcon();
        ++calls;
        check(p.cursor == 22 && std::strcmp(p.buffer + p.cursor, "TAIL") == 0 &&
                  p.items_head == first_link && first_link->next == p.items_tail &&
                  p.items_tail != first_link && p.items_tail->next == nullptr &&
                  p.registry_head == nullptr && p.registry_tail == nullptr,
            "second icon cursor/append differs");
        auto& second_item = image(p.items_tail, {11, 22, 13, 24}, record, 0xfedcba98);
        check(second_item.resource->word_08 == 3 && second_item.resource->word_14 == 0 &&
                  second_item.owned_text == nullptr && p.last_registered_item == &second_item &&
                  first_link->item == &first_item && &first_item != &second_item &&
                  first_item.word_2c == 0x13579bdf && record->reference_count == 2,
            "second icon state/ownership differs");
        auto* child = parent.children.head->next;
        check(child != parent.children.head && child->item == &second_item &&
                  child->next == parent.children.head && child->previous == parent.children.head &&
                  parent.children.head->previous == child,
            "real icon parent attachment differs");
        preserved(p, before_second);
        p.current_context = nullptr;
    }
    check(record->reference_count == 2 && first_link->next == p.items_tail,
        "parent teardown destroyed a borrowed Icon");
    session.clear();
    check(record->reference_count == 0 && p.items_head == nullptr && p.registry_head == nullptr,
        "genuine icon/link cleanup retained a resource reference");
    session.parser.verify();
}

void returned_pool(Seed& seed)
{
    std::array<bool, 4> seen{};
    auto* link = pool_chunk_buckets[2];
    for (unsigned n = 0; n < seen.size(); ++n)
    {
        check(link != nullptr, "genuine pool owner retained a slot");
        unsigned id = 0;
        while (id < seen.size() && reinterpret_cast<std::uintptr_t>(link) != seed.address(id))
        {
            ++id;
        }
        check(id < seen.size() && !seen[id], "returned pool slot identity differs");
        seen[id] = true;
        link = link->next; // Only the live returned PoolChunkLink is read.
    }
    check(link == nullptr, "returned pool has an extra slot");
    seed.check_common();
    seed.check_guards();
}
} // namespace

int main(int argc, char** argv)
{
    const bool icon_mode = argc == 3 && std::strcmp(argv[1], "--icon") == 0;
    const bool icon_fatal = argc == 2 && std::strcmp(argv[1], "--icon-fatal-open") == 0;
    const char* prefix = icon_mode || icon_fatal ? "gui-parse-icon" : "gui-parse-image";
    if (icon_fatal || (argc == 2 && std::strcmp(argv[1], "--fatal-open") == 0))
    {
        GuiParserState parser{};
        parser.buffer = "xx!";
        parser.cursor = 2;
        std::fprintf(stderr, "%s fatal-before\n", prefix);
        std::fflush(stderr);
        if (icon_fatal)
        {
            parser.ParseIcon();
        }
        else
        {
            parser.ParseImage();
        } // Actual StartupFatal exits0 before any owner exists.
        std::fprintf(stderr, "%s fatal-returned\n", prefix);
        return 3;
    }
    try
    {
        require(argc == (icon_mode ? 3 : 2), "expected generated directory argument");
        require(std::setlocale(LC_CTYPE, "C") != nullptr, "C locale unavailable");
        check(screen_text_lookup == nullptr, "another text lookup is active");
        Generated generated(icon_mode ? argv[2] : argv[1]);
        Seed seed(check);
        Environment environment;
        std::weak_ptr<const data::FileSource> borrowed_mount;
        {
            auto mount = generated.mount();
            borrowed_mount = mount;
            files::Session files_session(std::move(mount));
            int descriptor;
            files::Stream* metadata;
            {
                ScreenDialogResourceLookup owner;
                check(screen_dialog_resources == &owner && owner.record_count == 1 &&
                          std::strcmp(owner.records[0]->name, "icon") == 0 &&
                          owner.records[0]->frame_count == 1 &&
                          owner.records[0]->reference_count == 0,
                    "genuine mounted resource graph differs");
                descriptor = owner.data_file;
                metadata = owner.metadata_file;
                if (icon_mode)
                {
                    icon_groups(environment, owner);
                }
                else
                {
                    normal_groups(environment, owner);
                }
                check(owner.records[0]->reference_count == 0 && screen_text_lookup == nullptr,
                    "completed parser scope retained ownership");
            }
            check(screen_dialog_resources == nullptr && screen_dialog_palette_modifiers == nullptr,
                "genuine resource teardown retained its registration");
            check(files::close_stream(metadata) == -1, "retained closed metadata identity changed");
            unsigned char byte = 0xa5;
            check(files::read_descriptor(descriptor, &byte, 1) == -1 && byte == 0xa5,
                "genuine resource owner did not close its descriptor");
            returned_pool(seed);
        }
        check(borrowed_mount.expired(), "mounted source outlived its real Session");
        generated.unchanged();
        check(calls == 2 && released_items == 2 && draws == 1, "call/ownership totals differ");
        std::printf("%s groups=2 calls=%u released=%u draws=%u pixels=%u checks=%u pass\n",
            prefix,
            calls,
            released_items,
            draws,
            pixel_checks,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "%s %s: %s\n", prefix, group, failure.reason);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "%s setup: %s\n", prefix, failure.what());
        return 3;
    }
}
