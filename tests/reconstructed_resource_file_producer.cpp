#include "data/folder_source.h"
#include "pool_chunk_acquire.h"
#include "pool_lock_guard.h"
#include "renderer_queue_test_state.h"
#include "runtime/resource_files.h"
#include "startup_screen_dialog_frame_file.h"
#include "startup_screen_dialog_resource.h"
#include "startup_screen_palette_modifiers.h"

#include <array>
#include <bit>
#include <climits>
#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using Record = ScreenDialogResourceRecord;
using Frame = ScreenDialogResourceFrame;
using Lookup = ScreenDialogResourceLookup;
namespace fs = std::filesystem;
namespace files = beachlife::runtime::resource_files;
namespace data = beachlife::data;
static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && sizeof(unsigned int) == 4);
static_assert(sizeof(unsigned short) == 2 && sizeof(float) == 4);
static_assert(std::endian::native == std::endian::little);
static_assert(!std::is_aggregate_v<Lookup> && std::is_standard_layout_v<Lookup>);
static_assert(sizeof(void*) == 8 && sizeof(Lookup) == 456);
static_assert(offsetof(Lookup, record_count) == 24 && offsetof(Lookup, records) == 32);
static_assert(std::is_standard_layout_v<Record> && std::is_standard_layout_v<Frame>);
static_assert(!std::is_polymorphic_v<Record> && !std::is_polymorphic_v<Frame>);
static_assert(sizeof(void*) == 4 || sizeof(void*) == 8);
static_assert(sizeof(Record) == (sizeof(void*) == 8 ? 56 : 36));
static_assert(sizeof(Frame) == (sizeof(void*) == 8 ? 176 : 120));

namespace
{
constexpr std::size_t guard = alignof(std::max_align_t);
struct Totals
{
    std::size_t sessions = 0, calls = 0, records = 0, frames = 0, palettes = 0;
    std::size_t allocations = 0, releases = 0, graph_cleanups = 0, closed_descriptors = 0;
    std::size_t metadata_bytes = 0, data_bytes = 0, checks = 0;
    std::size_t owner_constructions = 0, owner_destructions = 0, bootstrap_cleanups = 0;
    std::size_t owner_palette_releases = 0, io_forwardings = 0;
} totals;
char label[64] = "setup";
const char* first_error = nullptr;
struct FixtureFailure
{
};

[[noreturn]] void fail(const char* reason)
{
    std::fprintf(stderr, "resource file producer %s: %s\n", label, reason);
    std::exit(2);
}

void necessary(bool condition, const char* reason)
{
    if (!condition)
    {
        fail(reason);
    }
}

void expect(bool condition, const char* reason)
{
    ++totals.checks;
    if (!condition && !first_error)
    {
        first_error = reason;
    }
}

long long position(int descriptor)
{
    return files::seek_descriptor(descriptor, 0, SEEK_CUR);
}

int open_readonly(const char* path)
{
    return files::open_descriptor(path, 0x8000);
}

void close_descriptor(int descriptor)
{
    necessary(descriptor >= 0, "cleanup received invalid descriptor");
    necessary(files::close_descriptor(descriptor) == 0, "descriptor close failed");
    ++totals.closed_descriptors;
}

enum class Phase
{
    outside,
    load,
    destruction
};
Phase phase = Phase::outside;
struct Trace
{
    Phase previous;
    explicit Trace(Phase next)
        : previous(phase)
    {
        phase = next;
    }
    ~Trace()
    {
        phase = previous;
    }
};

enum class Role
{
    record_table,
    record,
    name,
    palette_table,
    palette,
    frame_table,
    frame,
    frame_name,
    marks
};
struct Request
{
    Role role;
    std::size_t bytes;
    int record;
};
struct Allocation
{
    unsigned char* raw;
    std::uintptr_t address;
    std::size_t bytes, capacity;
    Role role;
    bool live;
};
std::array<Request, 64> requests;
std::size_t request_count = 0, request_next = 0;
std::array<Allocation, 128> allocations;
std::size_t allocation_count = 0;
Lookup* observed_lookup = nullptr;
std::array<unsigned char, sizeof(files::Stream*)> closed_stream_representation{};
ScreenDialogPaletteModifiers* bootstrap_palette = nullptr;
bool stream_captured = false;
std::array<int, 4> expected_marks{};
int expected_record_count = 0, prior_global_descriptor = -1;
unsigned int prior_cursor = 0;
long long expected_data_position = 0;
ScreenRequestRenderer* second_renderer = nullptr;

void allocation_guards(const Allocation& a)
{
    necessary(a.live, "guard check after allocation release");
    for (std::size_t i = 0; i < guard; ++i)
    {
        expect(a.raw[i] == 0xa5, "allocation prefix changed");
        expect(a.raw[guard + a.capacity + i] == 0x5a, "allocation suffix changed");
    }
    if (a.bytes == 0)
    {
        expect(a.raw[guard] == 0x6b, "zero-size allocation changed");
    }
}

Allocation& owner(const void* pointer, Role role, std::size_t bytes)
{
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        Allocation& a = allocations[i];
        if (a.live && a.address == address)
        {
            necessary(a.role == role && a.bytes == bytes, "owner role/extent differs");
            allocation_guards(a);
            return a;
        }
    }
    fail("owner is not a live recorded allocation");
}

// Fixture-only scalar boundary; all storage comes from real malloc/free.
// Volatile payload stores precede genuine Record/Frame construction.
void* allocate(std::size_t bytes)
{
    if (phase == Phase::outside)
    {
        void* value = std::malloc(bytes ? bytes : 1);
        if (!value)
        {
            throw std::bad_alloc();
        }
        return value;
    }
    necessary(phase == Phase::load && observed_lookup, "unexpected allocating phase");
    necessary(request_next < request_count, "unexpected scalar allocation");
    const Request r = requests[request_next++];
    necessary(bytes == r.bytes && bytes <= 4096, "scalar allocation extent differs");
    if (r.role == Role::record_table)
    {
        necessary(
            observed_lookup->metadata_file, "metadata stream not published before allocation");
        std::uint64_t cursor = 0;
        expect(files::stream_position(observed_lookup->metadata_file, cursor) && cursor == 28,
            "metadata header consumed length differs");
        std::memcpy(closed_stream_representation.data(),
            &observed_lookup->metadata_file,
            sizeof(files::Stream*));
        stream_captured = true;
    }
    if (r.role == Role::record && r.record == 1 && second_renderer)
    {
        // A genuine allocator callback changes the live renderer publication.
        // No parser, renderer method or retail lifetime is replaced here.
        screen_request_renderer = second_renderer;
    }
    necessary(allocation_count < allocations.size(), "allocation ledger full");
    const std::size_t capacity = bytes ? bytes : 1;
    auto* raw = static_cast<unsigned char*>(std::malloc(capacity + 2 * guard));
    if (!raw)
    {
        throw std::bad_alloc();
    }
    std::memset(raw, 0xa5, guard);
    volatile unsigned char* payload = raw + guard;
    for (std::size_t i = 0; i < capacity; ++i)
    {
        payload[i] = 0x6b;
    }
    std::memset(raw + guard + capacity, 0x5a, guard);
    allocations[allocation_count++] = {
        raw, reinterpret_cast<std::uintptr_t>(raw + guard), bytes, capacity, r.role, true};
    ++totals.allocations;
    totals.records += r.role == Role::record;
    totals.frames += r.role == Role::frame;
    totals.palettes += r.role == Role::palette;
    return raw + guard;
}

void release(void* pointer) noexcept
{
    if (!pointer)
    {
        return;
    }
    if (phase == Phase::outside)
    {
        std::free(pointer);
        return;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        Allocation& a = allocations[i];
        if (a.address != address || !a.live)
        {
            continue;
        }
        allocation_guards(a);
        if (phase == Phase::load)
        {
            necessary(a.role == Role::marks, "Load released a published graph owner");
            expect(a.bytes == static_cast<std::size_t>(expected_record_count) * sizeof(int),
                "mark storage extent differs");
            for (int j = 0; j < expected_record_count; ++j)
            {
                int mark;
                std::memcpy(
                    &mark, a.raw + guard + static_cast<std::size_t>(j) * sizeof(int), sizeof(mark));
                expect(mark == expected_marks[static_cast<std::size_t>(j)],
                    "final temporary mark differs");
            }
            expect(screen_dialog_frame_file == prior_global_descriptor,
                "descriptor publication happened before temporary release");
            expect(screen_dialog_frame_cursor == prior_cursor,
                "cursor publication happened before temporary release");
            expect(position(observed_lookup->data_file) == expected_data_position,
                "real descriptor position differs at temporary release");
        }
        else
        {
            necessary(phase == Phase::destruction, "unexpected deallocation phase");
        }
        a.live = false;
        std::free(a.raw);
        a.raw = nullptr;
        ++totals.releases;
        return;
    }
    // The real palette owner was constructed during the unobserved bootstrap.
    // Its exact saved pointer, not a size guess, is the sole extra game release.
    if (phase == Phase::destruction && pointer == bootstrap_palette)
    {
        bootstrap_palette = nullptr;
        ++totals.owner_palette_releases;
        std::free(pointer);
        return;
    }
    fail("untracked or duplicate game scalar delete");
}

void request(Role role, std::size_t bytes, int record = -1)
{
    necessary(request_count < requests.size(), "request ledger full");
    requests[request_count++] = {role, bytes, record};
}

struct Colour
{
    unsigned char red, green, blue;
    unsigned short rgb565, rgb555;
};
constexpr std::array<Colour, 6> colours = {{{0, 0, 0, 0, 0},
    {0, 0, 8, 1, 1},
    {0, 0, 16, 2, 2},
    {0, 0, 24, 3, 3},
    {248, 252, 248, 65535, 32767},
    {0, 252, 0, 2016, 992}}};
struct Palette
{
    unsigned char fill = 0;
    int exception = -1;
    unsigned char replacement = 0;
};
struct RecordPlan
{
    const char* name = "plain";
    unsigned int format = 1;
    int palette_count = 0;
    std::array<Palette, 2> palettes{};
    bool frame = false;
    unsigned int classification = 0;
};
constexpr std::array<unsigned char, 4> format5_bytes = {0x8a, 0, 0xff, 0x31};
// Accepted R140/R137 literal Frame: empty non-NUL name allocation, two planes,
// 320x200, opaque word 0x1234, signed -3, offset 0x10203040, size 128000.
constexpr std::array<unsigned char, 22> frame_literal = {0,
    2,
    0,
    2,
    0x40,
    1,
    0xc8,
    0,
    0x34,
    0x12,
    0xfd,
    0xff,
    0x40,
    0x30,
    0x20,
    0x10,
    0,
    0xf4,
    1,
    0,
    4,
    0};

void word32(std::vector<unsigned char>& bytes, unsigned int value)
{
    for (unsigned int shift : {0U, 8U, 16U, 24U})
    {
        bytes.push_back(static_cast<unsigned char>(value >> shift));
    }
}

const Colour& colour_at(const Palette& p, int index)
{
    const unsigned char which = index == p.exception ? p.replacement : p.fill;
    necessary(which < colours.size(), "literal colour outside table");
    return colours[which];
}

std::vector<unsigned char> metadata(std::span<const RecordPlan> records)
{
    std::vector<unsigned char> bytes;
    for (unsigned int i = 0; i < 20; ++i)
    {
        bytes.push_back(static_cast<unsigned char>(0x81U + i));
    }
    word32(bytes, 0xdeadbeefU);
    word32(bytes, static_cast<unsigned int>(records.size()));
    for (const RecordPlan& r : records)
    {
        const std::size_t length = std::strlen(r.name) + 1;
        necessary(length < 256 && r.palette_count >= 0 && r.palette_count <= 2,
            "record literal outside bounded domain");
        necessary(
            r.format == 1 || r.format == 2 || r.format == 3 || r.format == 5 || r.format == 11,
            "unsupported positive-palette format");
        bytes.push_back(static_cast<unsigned char>(length));
        bytes.insert(bytes.end(), r.name, r.name + length);
        word32(bytes, r.format);
        if (r.format == 5)
        {
            bytes.insert(bytes.end(), format5_bytes.begin(), format5_bytes.end());
        }
        word32(bytes, static_cast<unsigned int>(r.palette_count));
        for (int palette = 0; palette < r.palette_count; ++palette)
        {
            for (int index = 0; index < 256; ++index)
            {
                const Colour& c = colour_at(r.palettes[static_cast<std::size_t>(palette)], index);
                bytes.insert(bytes.end(), {c.red, c.green, c.blue});
            }
        }
        word32(bytes, r.frame ? 1U : 0U);
        if (r.frame)
        {
            bytes.insert(bytes.end(), frame_literal.begin(), frame_literal.end());
        }
    }
    return bytes;
}

void prepare_requests(std::span<const RecordPlan> plans, bool data_exists)
{
    request_count = request_next = 0;
    stream_captured = false;
    expected_record_count = static_cast<int>(plans.size());
    necessary(plans.size() <= expected_marks.size(), "record domain exceeds fixed ledger");
    request(Role::record_table, plans.size() * sizeof(Record*));
    for (std::size_t i = 0; i < plans.size(); ++i)
    {
        const RecordPlan& r = plans[i];
        request(Role::record, sizeof(Record), static_cast<int>(i));
        request(Role::name, std::strlen(r.name) + 1, static_cast<int>(i));
        request(Role::palette_table, static_cast<std::size_t>(r.palette_count) * sizeof(void*));
        for (int palette = 0; palette < r.palette_count; ++palette)
        {
            request(Role::palette, r.format == 3 ? 128 : 512);
        }
        request(Role::frame_table, r.frame ? sizeof(Frame*) : 0);
        if (r.frame)
        {
            request(Role::frame, sizeof(Frame));
            request(Role::frame_name, 0);
        }
        expected_marks[i] = r.palette_count ? 1 : 0;
    }
    if (data_exists)
    {
        request(Role::marks, plans.size() * sizeof(int));
    }
}

void write_file(const fs::path& path, std::span<const unsigned char> bytes)
{
    FILE* stream = std::fopen(path.string().c_str(), "wb");
    necessary(stream, "generated file open failed");
    if (!bytes.empty())
    {
        necessary(std::fwrite(bytes.data(), 1, bytes.size(), stream) == bytes.size(),
            "generated file write failed");
    }
    necessary(std::fclose(stream) == 0, "generated file close failed");
}

void unchanged_file(const fs::path& path, const std::vector<unsigned char>& expected)
{
    expect(fs::file_size(path) == expected.size(), "generated file size changed");
    FILE* stream = std::fopen(path.string().c_str(), "rb");
    necessary(stream, "generated file verification open failed");
    std::vector<unsigned char> bytes(expected.size());
    if (!bytes.empty())
    {
        expect(std::fread(bytes.data(), 1, bytes.size(), stream) == bytes.size(),
            "generated file verification read failed");
    }
    expect(bytes == expected, "generated file bytes changed");
    necessary(std::fclose(stream) == 0, "verification FILE close failed");
}

struct Files
{
    fs::path directory;
    std::vector<unsigned char> header, data;
    bool has_data;
};

Files make_files(const fs::path& directory,
    std::span<const RecordPlan> records,
    std::size_t data_length,
    bool data_exists = true)
{
    fs::create_directories(directory / "data/sprites");
    Files files{directory, metadata(records), {}, data_exists};
    for (std::size_t i = 0; i < data_length; ++i)
    {
        files.data.push_back(static_cast<unsigned char>(0x40U + i));
    }
    write_file(directory / "data/sprites/BLSprite.hdr", files.header);
    if (data_exists)
    {
        write_file(directory / "data/sprites/BLSprite.Dat", files.data);
    }
    return files;
}

void unchanged_files(const Files& files)
{
    unchanged_file(files.directory / "data/sprites/BLSprite.hdr", files.header);
    if (files.has_data)
    {
        unchanged_file(files.directory / "data/sprites/BLSprite.Dat", files.data);
    }
    else
    {
        expect(!fs::exists(files.directory / "data/sprites/BLSprite.Dat"),
            "Load created a missing data file");
    }
}

// A fixture routing boundary only: every open is forwarded to a genuine
// FolderSource and returns its genuine SourceFile unchanged. Changing the
// manifest is the mounted equivalent of the former current-directory switch.
class MountedSource final : public data::FileSource
{
  public:
    void select(const fs::path& directory)
    {
        std::vector<data::SourceEntry> entries;
        for (const char* name : {"data/sprites/BLSprite.hdr",
                 "data/sprites/BLSprite.Dat",
                 "data/sprites/PaletteModifiers.bin",
                 "held.bin"})
        {
            if (fs::exists(directory / name))
            {
                entries.push_back({name, fs::file_size(directory / name)});
            }
        }
        current_ = std::make_shared<data::FolderSource>(directory, std::move(entries));
    }
    std::unique_ptr<data::SourceFile> open(std::string_view path) const override
    {
        necessary(bool(current_), "mounted source has no selected generated manifest");
        return current_->open(path);
    }

  private:
    std::shared_ptr<const data::FileSource> current_;
};

// This backing is alive before real ring construction and after ReturnPool
// starts its new PoolChunkLink lifetime. No ended Node is inspected.
struct PoolBacking
{
    using Node = ScreenDialogResourceNode;
    std::array<unsigned char, 16> before{};
    alignas(Node) unsigned char storage[(sizeof(Node) + 7u) & ~7u]{};
    std::array<unsigned char, 16> after{};
    static constexpr unsigned bucket = (sizeof(storage) - 1u) / 8u;
    PoolBacking()
    {
        necessary(pool_lock_word == 0 && pool_chunk_begin == nullptr && pool_chunk_end == nullptr &&
                      pool_heap_bytes == 0,
            "bootstrap pool is not empty");
        for (auto* root : pool_chunk_buckets)
        {
            necessary(root == nullptr, "bootstrap pool has an existing free root");
        }
        before.fill(0xa7);
        after.fill(0x7a);
        pool_chunk_buckets[bucket] = ::new (static_cast<void*>(storage)) PoolChunkLink{nullptr};
    }
    ~PoolBacking()
    {
        pool_chunk_buckets[bucket] = nullptr;
    }
    void restored() const
    {
        auto* link = pool_chunk_buckets[bucket];
        expect(static_cast<const void*>(link) == storage && link->next == nullptr,
            "real owner ring did not return its sentinel");
        expect(pool_lock_word == 0 && pool_chunk_begin == nullptr && pool_chunk_end == nullptr &&
                   pool_heap_bytes == 0,
            "real owner ring changed bounded pool state");
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            expect(before[i] == 0xa7 && after[i] == 0x7a, "ring backing guards changed");
        }
    }
};

struct GuardedLookup
{
    std::array<unsigned char, 16> before{};
    Lookup value;
    std::array<unsigned char, 16> after{};
    ScreenDialogPaletteModifiers* palette;
    ScreenDialogResourceNode* sentinel;

    GuardedLookup()
        : palette(value.palette_modifiers)
        , sentinel(value.loaded_resources.head)
    {
        before.fill(0xa7);
        after.fill(0x7a);
        for (unsigned char& byte : value.unknown_18)
        {
            byte = 0x39;
        }
        // The old unknown_08 prefix is now a genuine typed palette owner.
        // Do not pattern-fill that pointer or the real ring.
    }
    void guards() const
    {
        for (unsigned char byte : before)
        {
            expect(byte == 0xa7, "lookup prefix guard changed");
        }
        for (unsigned char byte : after)
        {
            expect(byte == 0x7a, "lookup suffix guard changed");
        }
        for (unsigned char byte : value.unknown_18)
        {
            expect(byte == 0x39, "lookup opaque member changed");
        }
        expect(value.palette_modifiers == palette && value.loaded_resources.head == sentinel &&
                   sentinel->next == sentinel && sentinel->previous == sentinel &&
                   value.word_1c == 0,
            "Load changed genuine owner members outside its fields");
        for (const auto& name : value.recent_names)
        {
            expect(name[0] == 0, "Load changed the initial recent-name prefix");
        }
    }
};

void check_frame(Frame& frame)
{
    owner(&frame, Role::frame, sizeof(Frame));
    owner(frame.name, Role::frame_name, 0);
    expect(frame.loaded_08 == 0 && frame.count_0a == 2 && frame.plane_count_0c == 2,
        "frame count/state differs");
    expect(frame.width_0e == 320 && frame.height_10 == 200 && frame.unknown_12[0] == 0x34 &&
               frame.unknown_12[1] == 0x12 && frame.word_14 == -3,
        "frame geometry literal differs");
    expect(frame.index_ready_16 == 0 && frame.data_size_18 == 128000 &&
               frame.file_offset_1c == 0x10203040U && frame.offset_count_40 == 4,
        "frame data metadata differs");
    expect(!frame.indices_44 && !frame.data_4c && !frame.offsets_70 && frame.whole_data_48 == 0 &&
               frame.flag_74 == 0,
        "frame owned data was not null");
    for (std::size_t i = 0; i < 8; ++i)
    {
        expect(frame.plane_offsets_20[i] == 0 && !frame.plane_data_50[i],
            "frame bank constructor state differs");
    }
    // These actual member representations were deliberately patterned before
    // construction. No typed floating read or padding comparison is made.
    const auto* raw = reinterpret_cast<const unsigned char*>(&frame);
    const std::array<std::pair<std::size_t, std::size_t>, 6> untouched = {
        {{offsetof(Frame, time_04), sizeof(Frame::time_04)},
            {offsetof(Frame, unknown_09), 1},
            {offsetof(Frame, unknown_0d), 1},
            {offsetof(Frame, unknown_17), 1},
            {offsetof(Frame, unknown_42), 2},
            {offsetof(Frame, unknown_49), 3}}};
    for (const auto& [offset, count] : untouched)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            expect(raw[offset + i] == 0x6b, "untouched Frame member changed");
        }
    }
}

struct Graph
{
    Record** table;
    std::array<Record*, 4> records;
    std::size_t count;
    int descriptor;
};

Graph inspect_graph(Lookup& lookup, std::span<const RecordPlan> plans, bool renderer_changes)
{
    necessary(
        lookup.record_count == static_cast<int>(plans.size()), "published record count differs");
    owner(lookup.records, Role::record_table, plans.size() * sizeof(Record*));
    Graph graph{lookup.records, {}, plans.size(), lookup.data_file};
    for (std::size_t i = 0; i < plans.size(); ++i)
    {
        necessary(lookup.records[i], "null record in owned table");
        Record& r = *lookup.records[i];
        const RecordPlan& p = plans[i];
        graph.records[i] = &r;
        owner(&r, Role::record, sizeof(Record));
        owner(r.name, Role::name, std::strlen(p.name) + 1);
        expect(std::memcmp(r.name, p.name, std::strlen(p.name) + 1) == 0, "owned name differs");
        expect(r.reference_count == 0 && r.format_18 == p.format && r.word_1c == p.classification &&
                   r.palette_count == p.palette_count && r.frame_count == (p.frame ? 1 : 0),
            "parsed record members differ");
        owner(r.palettes,
            Role::palette_table,
            static_cast<std::size_t>(p.palette_count) * sizeof(void*));
        owner(r.frames, Role::frame_table, p.frame ? sizeof(Frame*) : 0);
        for (std::size_t j = 0; j < 4; ++j)
        {
            expect(r.unknown_20[j] == (p.format == 5 ? format5_bytes[j] : 0x6b),
                "record opaque member differs");
        }
        const int words = p.format == 3 ? 64 : 256;
        for (int palette = 0; palette < p.palette_count; ++palette)
        {
            owner(r.palettes[palette], Role::palette, static_cast<std::size_t>(words) * 2);
            for (int word = 0; word < words; ++word)
            {
                const Colour& c = colour_at(p.palettes[static_cast<std::size_t>(palette)], word);
                const unsigned short expected = renderer_changes && i > 0 ? c.rgb555 : c.rgb565;
                expect(r.palettes[palette][word] == expected, "literal palette word differs");
            }
        }
        if (p.frame)
        {
            necessary(r.frames[0], "real Frame was not published");
            check_frame(*r.frames[0]);
        }
    }
    return graph;
}

void destroy_graph(const Graph& graph)
{
    // Only a graph actually overwritten by repeated Load reaches this path.
    // Saved live receipts drive real Record/Frame destruction; the final graph
    // remains owned by the genuine Lookup destructor.
    {
        Trace trace(Phase::destruction);
        for (std::size_t i = 0; i < graph.count; ++i)
        {
            delete graph.records[i];
        }
        ::operator delete(graph.table);
    }
    if (graph.descriptor >= 0)
    {
        close_descriptor(graph.descriptor);
    }
    ++totals.graph_cleanups;
}

void begin(const char* name)
{
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        necessary(!allocations[i].live, "prior graph owner remains live");
    }
    allocation_count = request_count = request_next = 0;
    observed_lookup = nullptr;
    first_error = nullptr;
    second_renderer = nullptr;
    std::snprintf(label, sizeof(label), "%s", name);
    ++totals.sessions;
}

void finish()
{
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        necessary(!allocations[i].live, "graph cleanup missed owner");
    }
    screen_dialog_frame_file = -1;
    screen_dialog_frame_cursor = 0;
    screen_request_renderer = nullptr;
    observed_lookup = nullptr;
    if (first_error)
    {
        // All game owners are already released. Unwind the host vectors,
        // paths and deques before main reports the expected control failure.
        throw FixtureFailure{};
    }
}

int invoke(GuardedLookup& lookup,
    std::span<const RecordPlan> plans,
    const Files& files,
    MountedSource& mounted,
    ScreenRequestRenderer& renderer,
    ScreenRequestRenderer* alternate = nullptr)
{
    mounted.select(files.directory);
    prepare_requests(plans, files.has_data);
    observed_lookup = &lookup.value;
    prior_global_descriptor = screen_dialog_frame_file;
    prior_cursor = screen_dialog_frame_cursor;
    expected_data_position =
        static_cast<long long>(files.data.size() < 24 ? files.data.size() : 24);
    second_renderer = alternate;
    screen_request_renderer = &renderer;
    int result;
    {
        Trace trace(Phase::load);
        result = lookup.value.Load();
    }
    ++totals.calls;
    totals.metadata_bytes += files.header.size();
    totals.data_bytes += files.has_data ? static_cast<std::size_t>(expected_data_position) : 0;
    expect(request_next == request_count, "scalar request sequence incomplete");
    necessary(stream_captured, "live metadata stream was not observed");
    // The mounted token remains live but closed until Session teardown. Keep
    // the exact retained-pointer representation oracle without a FILE cast.
    expect(std::memcmp(&lookup.value.metadata_file,
               closed_stream_representation.data(),
               sizeof(files::Stream*)) == 0,
        "closed metadata pointer member was cleared or changed");
    lookup.guards();
    if (files.has_data)
    {
        expect(result == 0, "Load success result differs");
        necessary(lookup.value.data_file >= 0, "Load did not retain a real descriptor");
        expect(position(lookup.value.data_file) == expected_data_position,
            "real data descriptor cursor differs");
        expect(screen_dialog_frame_cursor == static_cast<unsigned int>(expected_data_position),
            "published cursor differs");
        expect(
            screen_dialog_frame_file ==
                (prior_global_descriptor == -1 ? lookup.value.data_file : prior_global_descriptor),
            "shared descriptor publication differs");
    }
    else
    {
        expect(result == -1 && lookup.value.data_file < 0, "data-open failure result differs");
        expect(screen_dialog_frame_file == prior_global_descriptor &&
                   screen_dialog_frame_cursor == prior_cursor,
            "data-open failure changed globals");
    }
    expect(screen_request_renderer == (alternate ? alternate : &renderer),
        "renderer publication differs");
    unchanged_files(files);
    return result;
}

struct Renderers
{
    ScreenRequestRenderer first{}, second{}, expected_first{}, expected_second{};
    Renderers()
    {
        renderer_queue_test::initialize(first, 31);
        renderer_queue_test::initialize(second, 57);
        first.format_44 = 0;
        second.format_44 = 1;
        expected_first = first;
        expected_second = second;
    }
    void unchanged() const
    {
        expect(renderer_queue_test::equal(first, expected_first), "first renderer state changed");
        expect(
            renderer_queue_test::equal(second, expected_second), "second renderer state changed");
    }
};

// One real owner spans every observed Load in a case. Its initial empty graph
// is reclaimed before the fixture graph is installed; its final graph is freed
// by the actual Lookup destructor. The Session outlives all retained tokens.
struct OwnerScope
{
    std::shared_ptr<MountedSource> mounted = std::make_shared<MountedSource>();
    files::Session session{mounted};
    PoolBacking backing;
    Renderers renderers;
    std::optional<GuardedLookup> lookup;

    explicit OwnerScope(const fs::path& root)
    {
        necessary(
            !bootstrap_palette && !screen_dialog_resources && !screen_dialog_palette_modifiers,
            "another genuine resource owner is live");
        const Files initial = make_files(root / "bootstrap", {}, 24);
        const std::array<unsigned char, 12 * 64 * 3> palette{};
        write_file(initial.directory / "data/sprites/PaletteModifiers.bin", palette);
        mounted->select(initial.directory);
        screen_request_renderer = &renderers.first;
        lookup.emplace();
        ++totals.owner_constructions;
        auto& value = lookup->value;
        necessary(screen_dialog_resources == &value && value.record_count == 0 && value.records &&
                      value.data_file >= 0 && value.palette_modifiers,
            "genuine bootstrap owner construction failed");
        bootstrap_palette = value.palette_modifiers;
        std::uint64_t cursor = 0x81;
        expect(!files::stream_position(value.metadata_file, cursor) && cursor == 0x81,
            "closed bootstrap metadata observer changed its output");
        // The actual constructor owns this empty table and descriptor. Release
        // them before later fixture Loads overwrite their owning fields.
        ::operator delete(value.records);
        necessary(files::close_descriptor(value.data_file) == 0,
            "bootstrap owned descriptor cleanup failed");
        value.records = nullptr;
        value.record_count = 0;
        value.data_file = -1;
        ++totals.bootstrap_cleanups;
        screen_dialog_frame_file = -1;
        screen_dialog_frame_cursor = 0;
        unchanged_files(initial);
    }
    OwnerScope(const OwnerScope&) = delete;
    OwnerScope& operator=(const OwnerScope&) = delete;
    ~OwnerScope()
    {
        close();
        screen_dialog_frame_file = -1;
        screen_dialog_frame_cursor = 0;
        screen_request_renderer = nullptr;
    }
    void close()
    {
        if (!lookup)
        {
            return;
        }
        lookup->guards();
        const auto table = reinterpret_cast<std::uintptr_t>(lookup->value.records);
        bool traced_graph = false;
        for (std::size_t i = 0; i < allocation_count; ++i)
        {
            traced_graph |= allocations[i].live && allocations[i].role == Role::record_table &&
                            allocations[i].address == table;
        }
        const int descriptor = lookup->value.data_file;
        {
            Trace trace(Phase::destruction);
            lookup.reset();
        }
        ++totals.owner_destructions;
        totals.graph_cleanups += traced_graph;
        expect(!screen_dialog_resources && !screen_dialog_palette_modifiers && !bootstrap_palette,
            "genuine owner destructor left a registration or palette owner");
        if (descriptor >= 0)
        {
            expect(position(descriptor) == -1, "genuine owner did not close its final descriptor");
            ++totals.closed_descriptors;
        }
        backing.restored();
        renderers.unchanged();
    }
};

void single_case(const fs::path& root,
    const char* name,
    std::span<const RecordPlan> plans,
    std::size_t data_length,
    bool data_exists = true,
    bool held_descriptor = false,
    bool renderer_changes = false)
{
    begin(name);
    const Files files = make_files(root / name, plans, data_length, data_exists);
    OwnerScope context(root / name);
    auto& lookup = *context.lookup;
    auto& renderers = context.renderers;
    int held = -1;
    if (held_descriptor)
    {
        const std::array<unsigned char, 3> held_bytes = {0x23, 0x45, 0x67};
        write_file(files.directory / "held.bin", held_bytes);
        context.mounted->select(files.directory);
        held = open_readonly("held.bin");
        necessary(held >= 0, "held descriptor open failed");
    }
    screen_dialog_frame_file = held;
    screen_dialog_frame_cursor = 0x12345678U;
    invoke(lookup,
        plans,
        files,
        *context.mounted,
        renderers.first,
        renderer_changes ? &renderers.second : nullptr);
    const Graph graph = inspect_graph(lookup.value, plans, renderer_changes);
    if (held >= 0)
    {
        expect(position(held) == 0 && held != graph.descriptor,
            "existing shared descriptor was consumed or reused");
    }
    renderers.unchanged();
    lookup.guards();
    context.close();
    if (held >= 0)
    {
        close_descriptor(held);
    }
    finish();
}

void metadata_missing(const fs::path& root)
{
    begin("metadata-missing");
    const fs::path directory = root / label;
    fs::create_directories(directory);
    const std::array<unsigned char, 3> contents = {7, 11, 13};
    write_file(directory / "held.bin", contents);
    OwnerScope context(directory);
    context.mounted->select(directory);
    const int held = open_readonly("held.bin");
    necessary(held >= 0, "held descriptor open failed");
    files::Stream* previous_stream = files::open_stream("held.bin", "rb");
    necessary(previous_stream, "prior borrowed stream open failed");
    // Genuine Records with no owned children. The actual owner temporarily
    // borrows this table only for the unchanged metadata-open failure oracle.
    std::array<Record, 7> previous_records;
    std::array<Record*, 7> previous_table;
    for (std::size_t i = 0; i < previous_table.size(); ++i)
    {
        previous_table[i] = &previous_records[i];
    }
    auto& lookup = *context.lookup;
    lookup.value.metadata_file = previous_stream;
    lookup.value.data_file = held;
    lookup.value.record_count = 7;
    lookup.value.records = previous_table.data();
    screen_dialog_frame_file = held;
    screen_dialog_frame_cursor = 91;
    observed_lookup = &lookup.value;
    int result;
    {
        Trace trace(Phase::load);
        result = lookup.value.Load();
    }
    ++totals.calls;
    expect(
        result == -1 && lookup.value.metadata_file == nullptr, "metadata failure result differs");
    expect(lookup.value.record_count == 7 && lookup.value.records == previous_table.data() &&
               lookup.value.data_file == held,
        "metadata failure changed prior prefix");
    expect(
        screen_dialog_frame_file == held && screen_dialog_frame_cursor == 91 && position(held) == 0,
        "metadata failure changed prior descriptor/cursor");
    unsigned char byte = 0;
    expect(files::read_stream(&byte, 1, 1, previous_stream) == 1 && byte == 7,
        "overwritten borrowed FILE was closed");
    necessary(files::close_stream(previous_stream) == 0, "prior borrowed stream cleanup failed");
    lookup.guards();
    // No borrowed stack Record may reach the owning destructor.
    lookup.value.records = nullptr;
    lookup.value.record_count = 0;
    context.close();
    finish();
}

void repeated_load(const fs::path& root)
{
    begin("repeated-load");
    const std::array<RecordPlan, 1> first = {{{"fat_bloke", 1, 1, {{{2, -1, 0}, {}}}, true, 1}}};
    const std::array<RecordPlan, 1> second = {{{"gail", 3, 1, {{{3, 63, 0}, {}}}, false, 3}}};
    const Files a = make_files(root / label / "first", first, 29);
    const Files b = make_files(root / label / "second", second, 7);
    OwnerScope context(root / label);
    auto& lookup = *context.lookup;
    auto& renderers = context.renderers;
    screen_dialog_frame_file = -1;
    screen_dialog_frame_cursor = 0;
    invoke(lookup, first, a, *context.mounted, renderers.first);
    const Graph first_graph = inspect_graph(lookup.value, first, false);
    const std::size_t retained_owners = allocation_count;
    invoke(lookup, second, b, *context.mounted, renderers.first);
    const Graph second_graph = inspect_graph(lookup.value, second, false);
    expect(first_graph.table != second_graph.table &&
               first_graph.descriptor != second_graph.descriptor,
        "repeated load reused an unreleased owner");
    for (std::size_t i = 0; i < retained_owners; ++i)
    {
        if (allocations[i].role != Role::marks)
        {
            necessary(allocations[i].live, "repeated load released the old graph");
            allocation_guards(allocations[i]);
        }
    }
    expect(position(first_graph.descriptor) == 24 && position(second_graph.descriptor) == 7 &&
               screen_dialog_frame_file == first_graph.descriptor &&
               screen_dialog_frame_cursor == 7,
        "repeated load descriptor/cursor asymmetry differs");
    // Reclaim only the genuinely overwritten graph from its live snapshot.
    // The final graph stays attached to the real owner until its destructor.
    destroy_graph(first_graph);
    renderers.unchanged();
    lookup.guards();
    context.close();
    finish();
}

void palette_cases(const fs::path& root)
{
    struct Case
    {
        const char* name;
        unsigned int format;
        Palette reference, candidate;
    };
    const std::array<Case, 7> cases = {{{"palette-nonzero", 1, {2, -1, 0}, {3, -1, 0}},
        {"palette-stop-first", 1, {2, -1, 0}, {3, 0, 0}},
        {"palette-stop-last-supported", 1, {0, -1, 0}, {2, 255, 1}},
        {"palette-zero-supported", 1, {0, -1, 0}, {0, -1, 0}},
        {"palette64-nonzero", 3, {2, -1, 0}, {3, -1, 0}},
        {"palette64-last", 3, {2, -1, 0}, {3, 63, 0}},
        {"palette64-discarded-input", 3, {2, -1, 0}, {3, 64, 0}}}};
    for (const Case& c : cases)
    {
        const std::array<RecordPlan, 2> records = {
            {{"plain", c.format, 1, {{c.reference, {}}}, false, 0},
                {"plain", c.format, 1, {{c.candidate, {}}}, false, 0}}};
        single_case(root, c.name, records, 24);
    }
}

void run(const fs::path& root)
{
    necessary(std::setlocale(LC_ALL, "C"), "C locale unavailable");
    // The sole Load control increments the published cursor. Cleanup of all
    // owners/descriptors still completes before case-0 reports that difference.
    single_case(root, "case-0", {}, 24);
    for (std::size_t length : {0U, 7U, 20U, 22U, 29U})
    {
        char name[40];
        std::snprintf(name, sizeof(name), "data-length-%zu", length);
        single_case(root, name, {}, length);
    }
    metadata_missing(root);
    single_case(root, "data-missing", {}, 0, false);
    single_case(root, "retained-global", {}, 24, true, true);
    palette_cases(root);

    const std::array<RecordPlan, 3> formats = {
        {{"FaT_BlOkE", 2, 2, {{{4, -1, 0}, {5, -1, 0}}}, true, 1},
            {"plain", 5, 1, {{{2, -1, 0}, {}}}, false, 0},
            {"plain", 11, 1, {{{3, -1, 0}, {}}}, false, 0}}};
    single_case(root, "supported-formats-frames", formats, 29);

    const std::array<RecordPlan, 2> reloaded = {{{"plain", 1, 1, {{{4, -1, 0}, {}}}, false, 0},
        {"plain", 1, 1, {{{4, -1, 0}, {}}}, false, 0}}};
    single_case(root, "live-renderer-reload", reloaded, 24, true, false, true);
    single_case(root, "data-missing-owned-graph", reloaded, 0, false);
    repeated_load(root);
}
} // namespace

// Transparent test-link observations of the actual resource I/O boundary.
// Names are taken from its qualified Linux ELF object. Host allocations/frees
// cannot consume the game's scalar ledger; all arguments/results/side effects
// still belong to the real function. No wrapper is linked into gameplay.
extern "C" files::Stream* __real__ZN9beachlife7runtime14resource_files11open_streamEPKcS3_(
    const char* path, const char* mode) noexcept;
extern "C" files::Stream* __wrap__ZN9beachlife7runtime14resource_files11open_streamEPKcS3_(
    const char* path, const char* mode) noexcept
{
    Trace host(Phase::outside);
    ++totals.io_forwardings;
    return __real__ZN9beachlife7runtime14resource_files11open_streamEPKcS3_(path, mode);
}

extern "C" std::size_t __real__ZN9beachlife7runtime14resource_files11read_streamEPvmmPNS1_6StreamE(
    void* buffer, std::size_t size, std::size_t count, files::Stream* stream) noexcept;
extern "C" std::size_t __wrap__ZN9beachlife7runtime14resource_files11read_streamEPvmmPNS1_6StreamE(
    void* buffer, std::size_t size, std::size_t count, files::Stream* stream) noexcept
{
    Trace host(Phase::outside);
    ++totals.io_forwardings;
    return __real__ZN9beachlife7runtime14resource_files11read_streamEPvmmPNS1_6StreamE(
        buffer, size, count, stream);
}

extern "C" int __real__ZN9beachlife7runtime14resource_files12close_streamEPNS1_6StreamE(
    files::Stream* stream) noexcept;
extern "C" int __wrap__ZN9beachlife7runtime14resource_files12close_streamEPNS1_6StreamE(
    files::Stream* stream) noexcept
{
    Trace host(Phase::outside);
    ++totals.io_forwardings;
    return __real__ZN9beachlife7runtime14resource_files12close_streamEPNS1_6StreamE(stream);
}

extern "C" int __real__ZN9beachlife7runtime14resource_files15open_descriptorEPKci(
    const char* path, int flags) noexcept;
extern "C" int __wrap__ZN9beachlife7runtime14resource_files15open_descriptorEPKci(
    const char* path, int flags) noexcept
{
    Trace host(Phase::outside);
    ++totals.io_forwardings;
    return __real__ZN9beachlife7runtime14resource_files15open_descriptorEPKci(path, flags);
}

extern "C" int __real__ZN9beachlife7runtime14resource_files15read_descriptorEiPvj(
    int descriptor, void* buffer, unsigned int bytes) noexcept;
extern "C" int __wrap__ZN9beachlife7runtime14resource_files15read_descriptorEiPvj(
    int descriptor, void* buffer, unsigned int bytes) noexcept
{
    Trace host(Phase::outside);
    ++totals.io_forwardings;
    return __real__ZN9beachlife7runtime14resource_files15read_descriptorEiPvj(
        descriptor, buffer, bytes);
}

extern "C" int __real__ZN9beachlife7runtime14resource_files15seek_descriptorEiji(
    int descriptor, unsigned int displacement, int origin) noexcept;
extern "C" int __wrap__ZN9beachlife7runtime14resource_files15seek_descriptorEiji(
    int descriptor, unsigned int displacement, int origin) noexcept
{
    Trace host(Phase::outside);
    ++totals.io_forwardings;
    return __real__ZN9beachlife7runtime14resource_files15seek_descriptorEiji(
        descriptor, displacement, origin);
}

extern "C" int __real__ZN9beachlife7runtime14resource_files16close_descriptorEi(
    int descriptor) noexcept;
extern "C" int __wrap__ZN9beachlife7runtime14resource_files16close_descriptorEi(
    int descriptor) noexcept
{
    Trace host(Phase::outside);
    ++totals.io_forwardings;
    return __real__ZN9beachlife7runtime14resource_files16close_descriptorEi(descriptor);
}

void* operator new(std::size_t bytes)
{
    return allocate(bytes);
}

void operator delete(void* pointer) noexcept
{
    release(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept
{
    release(pointer);
}

int main(int argc, char** argv)
{
    necessary(argc == 2, "one fresh generated-files directory is required");
    necessary(screen_dialog_frame_file == -1 && screen_dialog_frame_cursor == 0,
        "file-state global initializers differ");
    const fs::path previous = fs::current_path();
    const fs::path root = fs::absolute(argv[1]);
    necessary(!fs::exists(root), "generated-files directory already exists");
    fs::create_directories(root);
    bool failed = false;
    try
    {
        run(root);
    }
    catch (const FixtureFailure&)
    {
        failed = true;
    }
    fs::current_path(previous);
    fs::remove_all(root);
    if (failed)
    {
        std::fprintf(stderr, "resource file producer %s: %s\n", label, first_error);
        return 2;
    }
    std::printf("{\"suite\":\"resource-file-producer\",\"sessions\":%zu,\"calls\":%zu,"
                "\"records\":%zu,\"frames\":%zu,\"palettes\":%zu,\"allocations\":%zu,"
                "\"releases\":%zu,\"graph_cleanups\":%zu,\"closed_descriptors\":%zu,"
                "\"metadata_bytes\":%zu,\"data_bytes\":%zu,\"checks\":%zu,"
                "\"owner_constructions\":%zu,\"owner_destructions\":%zu,"
                "\"bootstrap_cleanups\":%zu,\"owner_palette_releases\":%zu,"
                "\"io_forwardings\":%zu}\n",
        totals.sessions,
        totals.calls,
        totals.records,
        totals.frames,
        totals.palettes,
        totals.allocations,
        totals.releases,
        totals.graph_cleanups,
        totals.closed_descriptors,
        totals.metadata_bytes,
        totals.data_bytes,
        totals.checks,
        totals.owner_constructions,
        totals.owner_destructions,
        totals.bootstrap_cleanups,
        totals.owner_palette_releases,
        totals.io_forwardings);
    return 0;
}
