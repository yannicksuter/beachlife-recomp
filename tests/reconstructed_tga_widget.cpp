#include "data/folder_source.h"
#include "renderer_queue_test_state.h"
#include "runtime/retail_files.h"
#include "startup_screen_manager.h"
#include "startup_screen_manager_auxiliary.h"
#include "startup_tga_screen_item.h"
#include "tga_image.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(int) == 4);
static_assert(!std::is_abstract_v<TgaScreenItem>);
static_assert(std::is_base_of_v<ScreenItem, TgaScreenItem>);
static_assert(std::extent_v<decltype(ScreenItem::unknown_31)> == 3);
static_assert(std::extent_v<decltype(TgaScreenItem::filename)> == 32);

namespace
{
struct RendererFixtureFailure
{
};

namespace data = beachlife::data;
namespace files = beachlife::runtime::retail_files;
using Bytes = std::vector<unsigned char>;
constexpr std::size_t guard_size = 32;
constexpr unsigned short untouched = 0x6b6b;
static_assert(guard_size % alignof(std::max_align_t) == 0);
std::size_t current_case;
std::size_t checks;
std::size_t lifetimes;
std::size_t enables;
std::size_t disables;
std::size_t draws;
std::size_t host_exceptions;

[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "case %zu: %s\n", current_case, message);
    throw RendererFixtureFailure{};
}

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        fail(message);
    }
}

template <class T> struct Guarded
{
    std::array<unsigned char, guard_size> before{};
    T value{};
    std::array<unsigned char, guard_size> after{};

    Guarded()
    {
        before.fill(0xa5);
        after.fill(0x5a);
    }
    void verify() const
    {
        for (std::size_t i = 0; i < guard_size; ++i)
        {
            require(before[i] == 0xa5 && after[i] == 0x5a, "external guard changed");
        }
    }
};

// Genuine renderer ownership; only method inputs are borrowed.
template <> struct Guarded<ScreenRequestRenderer>
{
    renderer_owner_test::Bootstrap renderer_bootstrap;
    std::array<unsigned char, guard_size> before{};
    ScreenRequestRenderer value{renderer_bootstrap.video(), 8, 4, 1};
    std::array<unsigned char, guard_size> after{};

    Guarded()
    {
        before.fill(0xa5);
        after.fill(0x5a);
    }
    void verify() const
    {
        for (std::size_t i = 0; i < guard_size; ++i)
        {
            require(before[i] == 0xa5 && after[i] == 0x5a, "external guard changed");
        }
    }
    renderer_owner_test::RestoreOwnedState renderer_restore{value, renderer_bootstrap};
};

struct Request
{
    std::size_t bytes;
    bool input = false;
    bool inject_failure = false;
};

struct Allocation
{
    unsigned char* backing = nullptr;
    unsigned char* returned = nullptr;
    std::size_t bytes = 0;
    bool live = false;
};

struct Ledger
{
    std::vector<Request> expected;
    std::span<const unsigned char> file;
    std::array<Allocation, 10> allocations{};
    std::array<std::size_t, 10> releases{};
    std::size_t attempts = 0;
    std::size_t release_count = 0;
    TgaScreenItem* widget = nullptr;
};

struct InjectedArrayFailure : std::bad_alloc
{
};
thread_local Ledger* active;

void allocation_guards(const Allocation& a)
{
    require(a.live, "checking a dead array allocation");
    for (std::size_t i = 0; i < guard_size; ++i)
    {
        require(a.backing[i] == 0xa5 && a.returned[a.bytes + i] == 0x5a,
            "array allocation guard changed");
    }
}

// The R119 guarded array boundary, with R116's actual file-buffer receipt.
// It supplies real default-aligned storage, never a reconstructed method.
// All bookkeeping during an actual operation is fixed-size and allocation-free.
void* allocate_array(std::size_t bytes)
{
    if (!active)
    {
        void* result = std::malloc(bytes ? bytes : 1);
        if (!result)
        {
            throw std::bad_alloc();
        }
        return result;
    }
    auto& ledger = *active;
    require(ledger.attempts < ledger.expected.size() && ledger.attempts < ledger.allocations.size(),
        "unexpected array request");
    const std::size_t index = ledger.attempts++;
    const auto expected = ledger.expected[index];
    require(bytes == expected.bytes, "literal array request differs");
    if (bytes == 512)
    {
        require(ledger.widget && ledger.widget->image &&
                    ledger.widget->image->converted_palette == nullptr,
            "conversion does not follow image publication");
    }
    if (expected.inject_failure)
    {
        throw InjectedArrayFailure();
    }
    auto* backing = static_cast<unsigned char*>(std::malloc(bytes + 2 * guard_size));
    if (!backing)
    {
        throw std::bad_alloc();
    }
    std::memset(backing, 0xa5, guard_size);
    std::memset(backing + guard_size, 0x6b, bytes);
    std::memset(backing + guard_size + bytes, 0x5a, guard_size);
    ledger.allocations[index] = {backing, backing + guard_size, bytes, true};
    return backing + guard_size;
}

void release_array(void* pointer) noexcept
{
    if (!pointer)
    {
        return;
    }
    if (!active)
    {
        std::free(pointer);
        return;
    }
    auto& ledger = *active;
    std::size_t index = 0;
    while (index < ledger.attempts &&
           !(ledger.allocations[index].live && ledger.allocations[index].returned == pointer))
    {
        ++index;
    }
    require(index < ledger.attempts, "unknown or duplicate array release");
    auto& allocation = ledger.allocations[index];
    allocation_guards(allocation);
    if (ledger.expected[index].input)
    {
        require(allocation.bytes == ledger.file.size() + 1, "input array extent differs");
        for (std::size_t byte = 0; byte < ledger.file.size(); ++byte)
        {
            require(allocation.returned[byte] == ledger.file[byte], "file input was changed");
        }
        require(allocation.returned[ledger.file.size()] == '.', "file terminator differs");
    }
    require(ledger.release_count < ledger.releases.size(), "release ledger exhausted");
    ledger.releases[ledger.release_count++] = index;
    allocation.live = false;
    std::free(allocation.backing);
    allocation.backing = nullptr;
    allocation.returned = nullptr;
}

struct Observation
{
    explicit Observation(Ledger& ledger)
    {
        require(active == nullptr, "nested observation");
        active = &ledger;
    }
    ~Observation()
    {
        active = nullptr;
    }
};

struct ImageFile
{
    const char* path;
    Bytes file;
    int width;
    int height;
    Bytes decoded;
    std::vector<Request> requests;
    std::vector<std::size_t> releases;
};

// Independent byte packets and decoded rows. Index zero deliberately has color.
const ImageFile small{"Data/Screens/Green.tga",
    {0,
        1,
        1,
        0,
        0,
        4,
        0,
        24,
        0,
        0,
        0,
        0,
        2,
        0,
        2,
        0,
        8,
        0,
        24,
        16,
        8,
        0,
        0,
        248,
        0,
        252,
        0,
        248,
        0,
        0,
        1,
        2,
        0,
        3},
    2,
    2,
    {0, 3, 1, 2},
    {{35, true}, {4}, {768}, {512}},
    {0, 1, 2, 3}};
const ImageFile rle{"Data/Screens/Rle.tga",
    {0,
        1,
        9,
        0,
        0,
        4,
        0,
        24,
        0,
        0,
        0,
        0,
        3,
        0,
        2,
        0,
        8,
        0,
        24,
        16,
        8,
        0,
        0,
        248,
        0,
        252,
        0,
        248,
        0,
        0,
        0x82,
        1,
        2,
        0,
        2,
        3},
    3,
    2,
    {0, 2, 3, 1, 1, 1},
    {{37, true}, {6}, {768}, {6}, {512}},
    {3, 0, 1, 2, 4}};
const ImageFile wide = []
{
    ImageFile result{"Data/Screens/Wide.tga",
        {0,
            1,
            1,
            0,
            0,
            4,
            0,
            24,
            0,
            0,
            0,
            0,
            32,
            3,
            1,
            0,
            8,
            0,
            24,
            16,
            8,
            0,
            0,
            248,
            0,
            252,
            0,
            248,
            0,
            0},
        800,
        1,
        Bytes(800, 2),
        {{831, true}, {800}, {768}, {512}},
        {0, 1, 2, 3}};
    result.file.insert(result.file.end(), 800, 2);
    return result;
}();

struct Write
{
    std::size_t halfword;
    unsigned short value;
};

enum class Action
{
    Cache,
    Draw,
    Reload,
    ConstructorOnly,
    ConversionFailure
};

struct Case
{
    const char* name;
    const ImageFile* file;
    const char* filename;
    unsigned int format;
    Action action;
    RenderRectangle clip;
    std::vector<Write> writes;
    std::vector<ScreenRectangle> queue;
    bool publication = true;
    bool maximum_path = false;
    bool manager_matches = true;
};

const std::array<Case, 12> cases = []
{
    std::array<Case, 12> result{{
        {"cached RGB555 and actual loaded destruction",
            &small,
            "Green.tga",
            1,
            Action::Cache,
            {0, 0, 2, 2},
            {{0, 0x0443}, {2, 0x001f}, {4, 0x7c00}, {6, 0x03e0}},
            {{0, 0, 2, 2}}},
        {"full fixed width includes pixel799",
            &wide,
            "Wide.tga",
            1,
            Action::Draw,
            {0, 0, 800, 1},
            {},
            {{0, 0, 800, 1}}},
        {"real indexed RLE and RGB565",
            &rle,
            "Rle.tga",
            0,
            Action::Draw,
            {0, 0, 3, 2},
            {{0, 0x0883}, {2, 0x07e0}, {4, 0x001f}, {6, 0xf800}, {8, 0xf800}, {10, 0xf800}},
            {{0, 0, 3, 2}}},
        {"non-one format selects RGB565",
            &small,
            "Green.tga",
            2,
            Action::Draw,
            {0, 0, 2, 2},
            {{0, 0x0883}, {2, 0x001f}, {4, 0xf800}, {6, 0x07e0}},
            {{0, 0, 2, 2}}},
        {"actual VFS case and separator aliases",
            &small,
            "gReEn.TgA",
            1,
            Action::Draw,
            {0, 0, 2, 2},
            {{0, 0x0443}, {2, 0x001f}, {4, 0x7c00}, {6, 0x03e0}},
            {{0, 0, 2, 2}}},
        {"disable then reload",
            &small,
            "Green.tga",
            1,
            Action::Reload,
            {0, 0, 2, 2},
            {{0, 0x0443}, {2, 0x001f}, {4, 0x7c00}, {6, 0x03e0}},
            {{0, 0, 2, 2}}},
        {"31-byte filename and 127-byte combined path",
            &small,
            "abcdefghijklmnopqrstuvwxyz0.tga",
            1,
            Action::Draw,
            {0, 0, 2, 2},
            {{0, 0x0443}, {2, 0x001f}, {4, 0x7c00}, {6, 0x03e0}},
            {{0, 0, 2, 2}},
            true,
            true},
        {"empty inline filename with disable only",
            nullptr,
            "",
            1,
            Action::ConstructorOnly,
            {0, 0, 2, 2},
            {},
            {}},
        {"host conversion exception keeps published image",
            &small,
            "Green.tga",
            1,
            Action::ConversionFailure,
            {0, 0, 2, 2},
            {},
            {}},
        {"fixed draw fully outside clip",
            &small,
            "Green.tga",
            1,
            Action::Draw,
            {801, 0, 1, 2},
            {},
            {}},
        {"positive-left clip selects right source column",
            &small,
            "Green.tga",
            1,
            Action::Draw,
            {1, 0, 1, 2},
            {{2, 0x001f}, {6, 0x03e0}},
            {{1, 0, 2, 2}}},
        {"disabled dirty publication and nonmatching null manager item",
            &small,
            "Green.tga",
            1,
            Action::Draw,
            {0, 0, 2, 2},
            {{0, 0x0443}, {2, 0x001f}, {4, 0x7c00}, {6, 0x03e0}},
            {{9, 9, 10, 10}},
            false,
            false,
            false},
    }};
    // Literal run of 800 identical halfword values; no clip or blit oracle loop.
    for (std::size_t i = 0; i < 800; ++i)
    {
        result[1].writes.push_back({2 * i, 0x03e0});
    }
    return result;
}();

struct WidgetStorage
{
    std::array<unsigned char, guard_size> before;
    alignas(TgaScreenItem) std::array<unsigned char, sizeof(TgaScreenItem)> storage;
    std::array<unsigned char, guard_size> after;
    TgaScreenItem* live = nullptr;

    explicit WidgetStorage(const char* filename)
    {
        before.fill(0xa5);
        // Observable writes to the backing array before any widget exists.
        // Construction installs its own vptr; no live object or padding is patterned.
        volatile unsigned char* backing = storage.data();
        for (std::size_t i = 0; i < storage.size(); ++i)
        {
            backing[i] = 0x36;
        }
        after.fill(0x5a);
        const ScreenRectangle initial{17, -3, 19, 8};
        live = ::new (storage.data()) TgaScreenItem(initial, filename);
        ++lifetimes;
        verify_layout();
        require(live->kind_28 == 16, "constructor kind differs");
        require(renderer_queue_test::same_rectangle(live->rectangle, initial) &&
                    renderer_queue_test::same_rectangle(live->saved_rectangle, initial),
            "constructor rectangle differs");
        require(live->image == nullptr && live->owned_text == nullptr && live->elapsed_24 == 0.0f &&
                    live->state_30 == 0 && live->last_x == 0 && live->last_y == 0 &&
                    live->state_40 == 0 && live->state_41 == 0 && live->state_42 == 0 &&
                    live->preparation_flag == 1,
            "constructor base or image state differs");
        const std::size_t length = std::strlen(filename);
        require(length <= 31, "fixture filename is outside the defined domain");
        for (std::size_t i = 0; i <= length; ++i)
        {
            require(live->filename[i] == filename[i], "inline filename differs");
        }
        for (std::size_t i = length + 1; i < 32; ++i)
        {
            require(static_cast<unsigned char>(live->filename[i]) == 0x36,
                "constructor changes bytes after filename terminator");
        }
        for (unsigned char byte : live->unknown_31)
        {
            require(byte == 0x36, "constructor changes base opaque bytes");
        }
        live->word_2c = 0x12345678U;
    }

    void verify_layout() const
    {
        const auto address = [](const void* pointer)
        {
            return reinterpret_cast<std::uintptr_t>(pointer);
        };
        const auto start = address(storage.data());
        const auto contains = [&](const void* member, std::size_t bytes)
        {
            const auto member_start = address(member);
            return member_start >= start && bytes <= storage.size() &&
                   member_start - start <= storage.size() - bytes;
        };
        require(address(live) == start && address(static_cast<const ScreenItem*>(live)) == start &&
                    address(before.data()) + before.size() == start &&
                    start + storage.size() == address(after.data()),
            "widget backing extents are not contiguous");
        require(
            contains(live->unknown_31, sizeof(live->unknown_31)) &&
                address(live->unknown_31) == address(&live->state_30) + sizeof(live->state_30) &&
                address(live->unknown_31) + sizeof(live->unknown_31) == address(&live->last_x),
            "widget opaque member extent is not contiguous");
        require(contains(&live->image, sizeof(live->image)) &&
                    contains(live->filename, sizeof(live->filename)) &&
                    address(&live->image) >=
                        address(&live->preparation_flag) + sizeof(live->preparation_flag) &&
                    address(live->filename) == address(&live->image) + sizeof(live->image),
            "widget image and filename extents are not contiguous");
    }

    ~WidgetStorage()
    {
        if (live)
        {
            destroy();
        }
    }

    void destroy()
    {
        require(live != nullptr, "duplicate widget destruction");
        live->~TgaScreenItem();
        live = nullptr;
    }

    void verify_guards() const
    {
        for (std::size_t i = 0; i < guard_size; ++i)
        {
            require(before[i] == 0xa5 && after[i] == 0x5a, "widget backing guard changed");
        }
    }
};

static_assert(std::is_standard_layout_v<WidgetStorage>);
static_assert(sizeof(std::array<unsigned char, sizeof(TgaScreenItem)>) == sizeof(TgaScreenItem));
static_assert(offsetof(WidgetStorage, storage) == guard_size);
static_assert(offsetof(WidgetStorage, after) == guard_size + sizeof(TgaScreenItem));

void verify_image(const TgaImage& image, const ImageFile& file, bool rgb555)
{
    constexpr std::array<unsigned char, 12> palette{8, 16, 24, 248, 0, 0, 0, 252, 0, 0, 0, 248};
    constexpr std::array<unsigned short, 4> colors555{0x0443, 0x7c00, 0x03e0, 0x001f};
    constexpr std::array<unsigned short, 4> colors565{0x0883, 0xf800, 0x07e0, 0x001f};
    require(image.width == file.width && image.height == file.height && image.bits_per_pixel == 8 &&
                image.bytes_per_pixel == 1 && image.pixels && image.palette &&
                image.converted_palette,
        "loaded image fields differ");
    for (std::size_t i = 0; i < file.decoded.size(); ++i)
    {
        require(image.pixels[i] == file.decoded[i], "decoded image differs");
    }
    for (std::size_t i = 0; i < 768; ++i)
    {
        require(image.palette[i] == (i < palette.size() ? palette[i] : 0), "RGB palette differs");
    }
    for (std::size_t i = 0; i < 256; ++i)
    {
        const auto expected = i < 4 ? (rgb555 ? colors555[i] : colors565[i]) : 0;
        require(image.converted_palette[i] == expected, "converted palette differs");
    }
}

void run_case(const Case& item, const std::string& maximum_prefix)
{
    Guarded<ScreenRequestRenderer> renderer;
    renderer_queue_test::initialize(renderer.value, 23);
    auto& r = renderer.value;
    r.width = static_cast<unsigned int>(item.file ? item.file->width : 2);
    r.height = static_cast<unsigned int>(item.file ? item.file->height : 2);
    r.clip_rectangle = item.clip;
    r.clip_mode_54 = 1;
    r.unscaled_clip_5c = 0;
    r.clip_flag_5d = 0;
    r.rectangle_enabled_4e = item.publication ? 1 : 0;
    r.rectangle_suppressed_4d = 0;
    r.rectangle_ready_4f = 1;
    r.format_44 = item.format;
    r.dirty_rectangles.values.clear();
    r.dirty_rectangle_count = 0;
    if (!item.publication)
    {
        r.dirty_rectangles.values = {{9, 9, 10, 10}};
        r.dirty_rectangle_count = 1;
    }
    Guarded<std::array<unsigned short, 1600>> pixels;
    pixels.value.fill(untouched);
    r.pixels_1c = pixels.value.data();
    renderer_queue_test::Snapshot expected_renderer = r;
    expected_renderer.dirty_rectangles.values.assign(item.queue.begin(), item.queue.end());
    expected_renderer.dirty_rectangle_count = static_cast<int>(item.queue.size());
    auto expected_pixels = pixels.value;
    for (const auto& write : item.writes)
    {
        require(write.halfword < expected_pixels.size(), "literal pixel slot out of range");
        expected_pixels[write.halfword] = write.value;
    }

    Guarded<ScreenManager> manager;
    Guarded<ScreenManagerAuxiliaryState> auxiliary;
    auxiliary.value.state_04 = 0x52;
    manager.value.auxiliary_130 = &auxiliary.value;
    manager.value.value_138 = 0.5f;
    manager.value.renderer = &r;
    std::fill(std::begin(manager.value.fonts_04), std::end(manager.value.fonts_04), nullptr);
    renderer_queue_test::pattern(manager.value.unknown_54, 0x38);
    manager.value.item_90 = nullptr;
    renderer_queue_test::pattern(manager.value.unknown_94, 0x49);
    renderer_queue_test::pattern(manager.value.unknown_c0, 0x5a);
    renderer_queue_test::pattern(auxiliary.value.unknown_05, 0x6b);
    const auto manager_before = manager.value;
    const auto auxiliary_before = auxiliary.value;
    const auto old_manager = screen_manager;
    const auto old_renderer = screen_request_renderer;
    const auto old_prefix = tga_screen_path_prefix;
    screen_manager = &manager.value;
    screen_request_renderer = &r;
    // Borrowed test string remains alive until every real widget has died.
    if (item.maximum_path)
    {
        require(maximum_prefix.size() == 96 && std::strlen(item.filename) == 31,
            "combined path boundary differs");
        tga_screen_path_prefix = maximum_prefix.c_str();
    }

    Ledger ledger;
    if (item.file)
    {
        ledger.expected = item.file->requests;
        ledger.file = item.file->file;
    }
    std::vector<std::size_t> expected_releases =
        item.file ? item.file->releases : std::vector<std::size_t>{};
    if (item.action == Action::Reload)
    {
        ledger.expected.insert(
            ledger.expected.end(), item.file->requests.begin(), item.file->requests.end());
        expected_releases = {0, 1, 2, 3, 4, 5, 6, 7};
    }
    if (item.action == Action::ConversionFailure)
    {
        ledger.expected.back().inject_failure = true;
        expected_releases = {0, 1, 2};
    }
    // Bookkeeping, renderer/deque and borrowed-prefix setup finish before binding.
    Observation observation(ledger);
    WidgetStorage storage(item.filename);
    auto* widget = storage.live;
    ledger.widget = widget;
    manager.value.item_134 = item.manager_matches ? widget : nullptr;
    widget->SetOwnedText("base cleanup");
    widget->elapsed_24 = 1.25f;
    const auto original_rectangle = widget->rectangle;
    const auto original_saved = widget->saved_rectangle;
    std::array<char, 32> filename_before{};
    std::memcpy(filename_before.data(), widget->filename, filename_before.size());

    if (item.action != Action::ConstructorOnly)
    {
        bool caught = false;
        try
        {
            ++enables;
            widget->Unresolved24(0x81);
        }
        catch (const InjectedArrayFailure&)
        {
            caught = true;
            ++host_exceptions;
        }
        require(
            caught == (item.action == Action::ConversionFailure), "host exception path differs");
        require(widget->state_30 == 0x81 && widget->elapsed_24 == 1.25f,
            "activation byte or elapsed differs");
        require(widget->image != nullptr, "image was not published");
        if (caught)
        {
            require(widget->image->pixels && widget->image->palette &&
                        !widget->image->converted_palette && ledger.release_count == 1 &&
                        ledger.releases[0] == 0,
                "conversion failure publication or input cleanup differs");
            ++disables;
            widget->Unresolved24(0);
            require(widget->image == nullptr && widget->state_30 == 0 && widget->elapsed_24 == 0.0f,
                "disable after conversion failure differs");
        }
        else
        {
            verify_image(*widget->image, *item.file, item.format == 1);
            if (item.action == Action::Cache)
            {
                const auto* image_before = widget->image;
                const auto* converted_before = widget->image->converted_palette;
                const auto attempts = ledger.attempts;
                // A cache hit reads neither this missing prefix nor the new format.
                tga_screen_path_prefix = "missing/";
                r.format_44 = 0;
                expected_renderer.format_44 = 0;
                ++enables;
                widget->Unresolved24(0x7f);
                require(widget->image == image_before &&
                            widget->image->converted_palette == converted_before &&
                            widget->state_30 == 0x7f && ledger.attempts == attempts,
                    "cached activation reloads, reconverts or normalizes the byte");
                tga_screen_path_prefix = old_prefix;
                verify_image(*widget->image, *item.file, true);
            }
            if (item.action == Action::Reload)
            {
                ++disables;
                widget->Unresolved24(0);
                require(widget->image == nullptr && widget->state_30 == 0 &&
                            widget->elapsed_24 == 0.0f && ledger.release_count == 4,
                    "loaded disable differs");
                ++enables;
                widget->Unresolved24(2);
                require(widget->state_30 == 2, "reload normalizes the byte");
                verify_image(*widget->image, *item.file, true);
            }
            ++draws;
            widget->Unresolved0c();
        }
    }
    else
    {
        ++disables;
        widget->Unresolved24(0);
        require(widget->image == nullptr && widget->elapsed_24 == 0.0f, "unloaded disable differs");
    }

    require(renderer_queue_test::equal(r, expected_renderer), "renderer or literal queue differs");
    for (std::size_t i = 0; i < pixels.value.size(); ++i)
    {
        require(pixels.value[i] == expected_pixels[i], "literal draw pixel differs");
    }
    require(renderer_queue_test::same_rectangle(widget->rectangle, original_rectangle) &&
                renderer_queue_test::same_rectangle(widget->saved_rectangle, original_saved) &&
                widget->kind_28 == 16 && widget->word_2c == 0x12345678U &&
                std::memcmp(widget->filename, filename_before.data(), filename_before.size()) == 0,
        "widget preserved fields differ");
    storage.verify_guards();
    TgaImage* image_receipt = widget->image;
    // Ordinary destruction ends the widget lifetime. No field is read afterward.
    storage.destroy();
    ledger.widget = nullptr;
    bool all_released = true;
    for (const auto& allocation : ledger.allocations)
    {
        all_released &= !allocation.live;
    }
    if (!all_released && image_receipt)
    {
        // The exact dtor control retains the entire loaded image. Reclaim that
        // separate live owner before rejecting; this is not widget cleanup credit.
        const auto remaining = std::count_if(ledger.allocations.begin(),
            ledger.allocations.end(),
            [](const Allocation& allocation)
            {
                return allocation.live;
            });
        require(remaining == 3, "unexpected partial image cleanup");
        delete image_receipt;
    }
    require(all_released, "widget destructor retained image arrays");
    require(ledger.attempts == ledger.expected.size() &&
                ledger.release_count == expected_releases.size(),
        "literal allocation or release count differs");
    for (std::size_t i = 0; i < expected_releases.size(); ++i)
    {
        require(ledger.releases[i] == expected_releases[i], "literal release order differs");
    }
    require(manager.value.item_134 == nullptr &&
                manager.value.value_138 == (item.manager_matches ? 2.5f : 0.5f) &&
                auxiliary.value.state_04 == (item.manager_matches ? 0 : 0x52),
        "actual base-disable manager effects differ");
    const auto& m = manager.value;
    const auto& a = auxiliary.value;
    require(m.renderer == manager_before.renderer && m.registry == manager_before.registry &&
                m.active.head == manager_before.active.head && m.mode == manager_before.mode &&
                m.previous_mode == manager_before.previous_mode &&
                m.auxiliary_130 == manager_before.auxiliary_130 &&
                std::equal(std::begin(m.fonts_2c),
                    std::end(m.fonts_2c),
                    std::begin(manager_before.fonts_2c)) &&
                std::equal(std::begin(m.fonts_04),
                    std::end(m.fonts_04),
                    std::begin(manager_before.fonts_04)) &&
                renderer_queue_test::same_bytes(m.unknown_54, manager_before.unknown_54) &&
                m.item_90 == manager_before.item_90 &&
                renderer_queue_test::same_bytes(m.unknown_94, manager_before.unknown_94) &&
                renderer_queue_test::same_bytes(m.unknown_c0, manager_before.unknown_c0),
        "unrelated manager fields changed");
    require(a.word_00 == auxiliary_before.word_00 && a.text_08 == auxiliary_before.text_08 &&
                a.text_width_0c == auxiliary_before.text_width_0c &&
                a.text_height_10 == auxiliary_before.text_height_10 &&
                a.width_14 == auxiliary_before.width_14 &&
                a.height_18 == auxiliary_before.height_18 && a.x_1c == auxiliary_before.x_1c &&
                a.y_20 == auxiliary_before.y_20 && a.frame_24 == auxiliary_before.frame_24 &&
                renderer_queue_test::same_bytes(a.unknown_05, auxiliary_before.unknown_05),
        "unrelated auxiliary fields changed");
    storage.verify_guards();
    manager.verify();
    auxiliary.verify();
    renderer.verify();
    pixels.verify();
    screen_manager = old_manager;
    screen_request_renderer = old_renderer;
    tga_screen_path_prefix = old_prefix;
}

void write_file(const std::filesystem::path& path, const Bytes& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(bool(output), "cannot write generated file");
}
} // namespace

void* operator new[](std::size_t bytes)
{
    return allocate_array(bytes);
}
void operator delete[](void* pointer) noexcept
{
    release_array(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept
{
    release_array(pointer);
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
        {
            return 2;
        }
        try
        {
            require(std::strcmp(tga_screen_path_prefix, "data\\screens\\") == 0,
                "actual initial screen prefix differs");
            const std::filesystem::path root(argv[1]);
            const std::string maximum_prefix = std::string(95, 'p') + '/';
            const std::string maximum_path = maximum_prefix + cases[6].filename;
            std::vector<data::SourceEntry> entries;
            for (const auto* file : {&small, &rle, &wide})
            {
                write_file(root / file->path, file->file);
                entries.push_back({file->path, file->file.size()});
            }
            write_file(root / maximum_path, small.file);
            entries.push_back({maximum_path, small.file.size()});
            auto source = std::make_shared<data::FolderSource>(root, entries);
            {
                files::Session session(source);
                for (current_case = 0; current_case < cases.size(); ++current_case)
                {
                    run_case(cases[current_case], maximum_prefix);
                }
            }
            require(active == nullptr && screen_manager == nullptr &&
                        screen_request_renderer == nullptr,
                "borrowed globals or observer remain bound");
            require(lifetimes == 12 && enables == 13 && disables == 3 && draws == 10 &&
                        host_exceptions == 1,
                "literal operation counts differ");
            std::printf(
                "Reconstructed TGA widget checks passed: %zu cases, %zu lifetimes, %zu enables, "
                "%zu disables, %zu draws, %zu host exceptions, %zu checks\n",
                cases.size(),
                lifetimes,
                enables,
                disables,
                draws,
                host_exceptions,
                checks);
            return 0;
        }
        catch (const std::exception& error)
        {
            std::fprintf(stderr, "generated widget fixture: %s\n", error.what());
            return 2;
        }
    }
    catch (const RendererFixtureFailure&)
    {
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "renderer fixture host failure: %s\n", error.what());
        return 3;
    }
}
