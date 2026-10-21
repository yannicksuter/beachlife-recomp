#include "data/folder_source.h"
#include "pool_chunk_acquire.h"
#include "pool_lock_guard.h"
#include "renderer_queue_test_state.h"
#include "runtime/resource_files.h"
#include "startup_screen_dialog_frame_file.h"
#include "startup_screen_dialog_resource.h"
#include "startup_screen_palette_modifiers.h"

#include <array>
#include <climits>
#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <vector>

namespace
{
namespace fs = std::filesystem;
namespace files = beachlife::runtime::resource_files;
namespace data = beachlife::data;
using Lookup = ScreenDialogResourceLookup;
using Node = ScreenDialogResourceNode;
static_assert(sizeof(unsigned int) == 4);
static_assert(std::is_standard_layout_v<ScreenDialogResourceRecord>);
static_assert(std::is_standard_layout_v<ScreenDialogResourceLookup>);
static_assert(!std::is_polymorphic_v<ScreenDialogResource>);
static_assert(sizeof(void*) == 4 || sizeof(void*) == 8);
static_assert(sizeof(ScreenDialogResourceRecord) == (sizeof(void*) == 8 ? 56 : 36));
static_assert(!std::is_aggregate_v<Lookup> && !std::is_trivially_destructible_v<Lookup>);
static_assert(sizeof(Lookup) == (sizeof(void*) == 8 ? 456 : 432));
static_assert(offsetof(Lookup, record_count) == (sizeof(void*) == 8 ? 24 : 12));
static_assert(offsetof(Lookup, records) == (sizeof(void*) == 8 ? 32 : 16));
static_assert(offsetof(Lookup, loaded_resources) == (sizeof(void*) == 8 ? 40 : 20));
static_assert(offsetof(Lookup, recent_names) == (sizeof(void*) == 8 ? 56 : 32));

constexpr std::array<std::uint32_t, 4> before_guard{
    0x12345678U, 0x9abcdef0U, 0x16543210U, 0xfedcba98U};
constexpr std::array<std::uint32_t, 4> after_guard{
    0x87654321U, 0x0fedcba9U, 0x01234561U, 0x89abcdefU};

template <class T> struct Guarded
{
    std::array<std::uint32_t, 4> before = before_guard;
    T value{};
    std::array<std::uint32_t, 4> after = after_guard;

    bool intact() const
    {
        return before == before_guard && after == after_guard;
    }
};

using Name = std::array<char, 32>;

struct Call
{
    const char* request;
    bool through_lookup;
    std::size_t expected_record;
    std::array<unsigned int, 3> expected_counts;
    const char* expected_name;
};

struct Case
{
    const char* label;
    std::array<const char*, 3> names;
    std::array<unsigned int, 3> counts;
    std::array<std::size_t, 3> record_indices;
    int count;
    std::array<Call, 2> calls;
    std::size_t call_count;
};

// Fixed literals from D228. No scanner, search or allocation oracle is implemented.
constexpr std::array<Case, 7> cases{{
    {"exact-first",
        {"Alpha", "Beta", "unused"},
        {20, 7, 90},
        {0, 1, 2},
        2,
        {{{"Alpha", true, 0, {21, 7, 90}, "Alpha"}, {}}},
        1},
    {"first-duplicate-casefold",
        {"Alpha", "aLPHa", "dummy"},
        {3, 8, 4},
        {0, 1, 2},
        3,
        {{{"ALPHA", false, 0, {4, 8, 4}, "ALPHA"}, {}}},
        1},
    {"later-casefold",
        {"skip", "Target", "unused"},
        {9, 12, 90},
        {0, 1, 2},
        2,
        {{{"tArGeT", true, 1, {9, 13, 90}, "tArGeT"}, {}}},
        1},
    {"recursive-fallback",
        {"other", "dummy", "unused"},
        {1, 3, 90},
        {0, 1, 2},
        2,
        {{{"missing", false, 1, {1, 4, 90}, "dummy"}, {}}},
        1},
    {"empty-request",
        {"", "dummy", "unused"},
        {0, 4, 90},
        {0, 1, 2},
        2,
        {{{"", true, 0, {1, 4, 90}, ""}, {}}},
        1},
    {"same-record-two-calls",
        {"shared", "unused", "unused"},
        {5, 90, 91},
        {0, 0, 2},
        2,
        {{{"shared", false, 0, {6, 90, 91}, "shared"}, {"SHARED", true, 0, {7, 90, 91}, "SHARED"}}},
        2},
    {"count-wrap",
        {"wrap", "unused", "unused"},
        {UINT_MAX, 90, 91},
        {0, 1, 2},
        1,
        {{{"wrap", false, 0, {0, 90, 91}, "wrap"}, {}}},
        1},
}};

std::size_t checks = 0;
std::size_t calls = 0;
std::size_t direct_calls = 0;
std::size_t lookup_calls = 0;
std::size_t record_lifetimes = 0;
std::size_t owned_names = 0;

bool check(bool value)
{
    ++checks;
    return value;
}

void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

// These empty generated files let the genuine owner establish all of its
// lifetime dependencies. They do not replace any game constructor or loader.
struct OwnerFiles
{
    fs::path root;
    explicit OwnerFiles(const fs::path& directory)
        : root(directory)
    {
        require(!fs::exists(root), "generated owner directory already exists");
        fs::create_directories(root / "Data/Sprites");
        const std::array<unsigned char, 28> metadata{}; // Full header, word, zero records.
        const std::array<unsigned char, 24> raw{};
        const std::array<unsigned char, 2304> palette{};
        write("Data/Sprites/BLSprite.hdr", metadata);
        write("Data/Sprites/BLSprite.Dat", raw);
        write("Data/Sprites/PaletteModifiers.bin", palette);
    }
    ~OwnerFiles()
    {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    void write(const char* name, std::span<const unsigned char> bytes) const
    {
        std::ofstream output(root / name, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        output.close();
        require(bool(output), "generated owner file write failed");
    }
    std::shared_ptr<const data::FileSource> mount() const
    {
        return std::make_shared<data::FolderSource>(root,
            std::vector<data::SourceEntry>{{"Data/Sprites/BLSprite.hdr", 28},
                {"Data/Sprites/BLSprite.Dat", 24},
                {"Data/Sprites/PaletteModifiers.bin", 2304}});
    }
};

// A real free pool slot becomes the resource ring's real sentinel and returns
// to PoolChunkLink lifetime after genuine owner destruction. No refill path.
struct PoolBacking
{
    std::array<unsigned char, 16> before{};
    alignas(Node) unsigned char storage[(sizeof(Node) + 7u) & ~7u]{};
    std::array<unsigned char, 16> after{};
    static constexpr unsigned bucket = (sizeof(storage) - 1u) / 8u;

    PoolBacking()
    {
        require(pool_lock_word == 0 && pool_chunk_begin == nullptr && pool_chunk_end == nullptr &&
                    pool_heap_bytes == 0,
            "pool state must start empty");
        for (auto* entry : pool_chunk_buckets)
        {
            require(entry == nullptr, "pool free root must start empty");
        }
        before.fill(0x39);
        after.fill(0xc6);
        pool_chunk_buckets[bucket] = ::new (static_cast<void*>(storage)) PoolChunkLink{nullptr};
    }
    ~PoolBacking()
    {
        pool_chunk_buckets[bucket] = nullptr;
    }
    bool restored() const
    {
        const auto* link = pool_chunk_buckets[bucket];
        bool okay = check(static_cast<const void*>(link) == storage);
        if (okay)
        {
            okay &= check(link->next == nullptr);
        }
        okay &= check(pool_lock_word == 0 && pool_chunk_begin == nullptr &&
                      pool_chunk_end == nullptr && pool_heap_bytes == 0);
        for (unsigned i = 0; i < 16; ++i)
        {
            okay &= check(before[i] == 0x39 && after[i] == 0xc6);
            if (i != bucket)
            {
                okay &= check(pool_chunk_buckets[i] == nullptr);
            }
        }
        return okay;
    }
};

struct Environment
{
    renderer_owner_test::Bootstrap renderer_bootstrap;
    ScreenRequestRenderer renderer{renderer_bootstrap.video(), 8, 4, 1};
    renderer_owner_test::RestoreOwnedState renderer_restore{renderer, renderer_bootstrap};
    Environment()
    {
        require(screen_request_renderer == nullptr && screen_dialog_resources == nullptr &&
                    screen_dialog_palette_modifiers == nullptr && screen_dialog_frame_file == -1 &&
                    screen_dialog_frame_cursor == 0,
            "resource owner globals must start empty");
        renderer_queue_test::initialize(renderer, 31);
        renderer.format_44 = 0;
        screen_request_renderer = &renderer;
    }
    ~Environment()
    {
        // The actual owner intentionally retains its closed descriptor globally.
        screen_dialog_frame_file = -1;
        screen_dialog_frame_cursor = 0;
        screen_request_renderer = nullptr;
    }
};

struct BorrowedRecords
{
    Lookup& owner;
    int count;
    ScreenDialogResourceRecord** table;
    BorrowedRecords(Lookup& value, int borrowed_count, ScreenDialogResourceRecord** borrowed)
        : owner(value)
        , count(value.record_count)
        , table(value.records)
    {
        require(count == 0 && table != nullptr, "bootstrap owner is not empty");
        owner.record_count = borrowed_count;
        owner.records = borrowed;
    }
    ~BorrowedRecords()
    {
        // Restore before any borrowed Record ends, including control failures.
        owner.record_count = count;
        owner.records = table;
    }
};

void set_name(Name& destination, const char* source)
{
    destination.fill('\x6d');
    const auto count = std::strlen(source) + 1;
    std::memcpy(destination.data(), source, count);
}

bool initialized_members(const ScreenDialogResource& resource)
{
    // Read only the constructor's initialized typed members. Unknown bytes and
    // ABI padding are not inspected, and no pre-lifetime pattern is assumed.
    return resource.word_04 == 0 && resource.word_08 == 0 && resource.word_0c == 18 &&
           resource.word_10 == 0 && resource.word_14 == 0 && resource.word_18 == 0 &&
           resource.word_1c == 0 && resource.flag_20 == 0 && resource.word_24 == 0 &&
           resource.flag_28 == 0 && resource.flag_29 == 0 && resource.flag_2a == 0 &&
           resource.word_2c == 0 && resource.flag_34 == 0;
}

bool record_members(const ScreenDialogResourceRecord& record)
{
    return record.frame_count == 0 && record.frames == nullptr && record.palette_count == 0 &&
           record.palettes == nullptr && record.format_18 == 1 && record.word_1c == 0;
}

bool run_case(const Case& example, Guarded<Lookup>& lookup)
{
    std::array<Guarded<Name>, 3> names;
    std::array<Guarded<Name>, 2> requests;
    std::array<Guarded<ScreenDialogResourceRecord>, 3> records;
    std::array<const char*, 3> record_names{};
    Guarded<std::array<ScreenDialogResourceRecord*, 3>> entries;
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        set_name(names[i].value, example.names[i]);
        // Guarded::value invokes the real Record constructor. Each record owns
        // a distinct scalar name, even when the literal string contents agree.
        char* owned = static_cast<char*>(::operator new(names[i].value.size()));
        std::memcpy(owned, names[i].value.data(), names[i].value.size());
        records[i].value.name = record_names[i] = owned;
        records[i].value.reference_count = example.counts[i];
        for (std::size_t n = 0; n < sizeof(records[i].value.unknown_20); ++n)
        {
            records[i].value.unknown_20[n] = static_cast<unsigned char>(0x70 + i + n);
        }
        ++record_lifetimes;
        ++owned_names;
        entries.value[i] = &records[example.record_indices[i]].value;
    }
    for (std::size_t i = 0; i < example.call_count; ++i)
    {
        set_name(requests[i].value, example.calls[i].request);
    }
    const auto* metadata = lookup.value.metadata_file;
    const int descriptor = lookup.value.data_file;
    const auto* palette = lookup.value.palette_modifiers;
    const auto* head = lookup.value.loaded_resources.head;
    const unsigned word = lookup.value.word_1c;
    require(metadata != nullptr && descriptor >= 3 && palette != nullptr && head != nullptr &&
                head->next == head && head->previous == head,
        "genuine owner dependencies are missing");
    std::array<unsigned char, 4> opaque;
    for (std::size_t i = 0; i < opaque.size(); ++i)
    {
        lookup.value.unknown_18[i] = opaque[i] = static_cast<unsigned char>(0x30 + i);
    }
    for (auto& recent : lookup.value.recent_names)
    {
        std::memset(recent, 0x6d, sizeof(recent));
        recent[0] = '\0';
    }
    std::array<unsigned char, sizeof(Lookup::recent_names)> recent{};
    std::memcpy(recent.data(), lookup.value.recent_names, recent.size());
    const BorrowedRecords borrowed(lookup.value, example.count, entries.value.data());
    const auto owner_unchanged = [&]
    {
        return lookup.value.metadata_file == metadata && lookup.value.data_file == descriptor &&
               lookup.value.palette_modifiers == palette &&
               lookup.value.loaded_resources.head == head && head->next == head &&
               head->previous == head && lookup.value.word_1c == word &&
               screen_dialog_resources == &lookup.value &&
               screen_dialog_palette_modifiers == palette &&
               screen_dialog_frame_file == descriptor && screen_dialog_frame_cursor == 24 &&
               std::memcmp(recent.data(), lookup.value.recent_names, recent.size()) == 0;
    };
    const auto saved_names = names;
    const auto saved_requests = requests;
    const auto saved_entries = entries.value;
    std::array<ScreenDialogResource*, 2> results{};
    bool okay = true;
    for (std::size_t i = 0; i < example.call_count; ++i)
    {
        const auto& expected = example.calls[i];
        char* request = requests[i].value.data();
        if (expected.through_lookup)
        {
            ++lookup_calls;
            results[i] = lookup.value.Lookup(request);
        }
        else
        {
            ++direct_calls;
            results[i] = lookup.value.Find(request);
        }
        ++calls;
        ScreenDialogResource* result = results[i];
        okay &= check(result != nullptr);
        if (result)
        {
            okay &= check(result->record == &records[expected.expected_record].value);
            okay &= check(initialized_members(*result));
            okay &= check(result->name != nullptr);
            if (result->name)
            {
                okay &= check(std::strcmp(result->name, expected.expected_name) == 0);
                okay &= check(result->name != request);
                for (const auto& name : names)
                {
                    okay &= check(result->name != name.value.data());
                }
                for (const char* name : record_names)
                {
                    okay &= check(result->name != name);
                }
            }
        }
        for (std::size_t n = 0; n < records.size(); ++n)
        {
            okay &= check(records[n].value.reference_count == expected.expected_counts[n]);
            okay &= check(records[n].value.name == record_names[n]);
            okay &= check(records[n].intact());
            okay &= check(names[n].intact() && names[n].value == saved_names[n].value);
            okay &= check(std::memcmp(record_names[n],
                              saved_names[n].value.data(),
                              saved_names[n].value.size()) == 0);
            okay &= check(record_members(records[n].value));
            for (std::size_t b = 0; b < sizeof(records[n].value.unknown_20); ++b)
            {
                okay &= check(
                    records[n].value.unknown_20[b] == static_cast<unsigned char>(0x70 + n + b));
            }
        }
        for (std::size_t n = 0; n < requests.size(); ++n)
        {
            okay &= check(requests[n].intact() && requests[n].value == saved_requests[n].value);
        }
        okay &= check(lookup.intact() && entries.intact());
        okay &= check(lookup.value.record_count == example.count);
        okay &= check(owner_unchanged());
        okay &=
            check(lookup.value.records == entries.value.data() && entries.value == saved_entries);
        for (std::size_t n = 0; n < opaque.size(); ++n)
        {
            okay &= check(lookup.value.unknown_18[n] == opaque[n]);
        }
    }
    if (example.call_count == 2 && results[0] && results[1])
    {
        okay &= check(results[0] != results[1]);
        okay &= check(results[0]->name != results[1]->name);
        okay &= check(results[0]->record == results[1]->record);
    }
    // Both wrappers stay live until all pointer-identity observations finish.
    // The real destructor frees its scalar name; delete also releases the wrapper.
    // It deliberately does not decrement the borrowed record's count.
    for (ScreenDialogResource* resource : results)
    {
        delete resource;
    }
    const auto& final_counts = example.calls[example.call_count - 1].expected_counts;
    for (std::size_t i = 0; i < records.size(); ++i)
    {
        okay &= check(records[i].value.reference_count == final_counts[i]);
        okay &= check(records[i].value.name == record_names[i]);
        okay &= check(records[i].intact() && names[i].intact());
        okay &= check(names[i].value == saved_names[i].value);
        okay &= check(
            std::memcmp(
                record_names[i], saved_names[i].value.data(), saved_names[i].value.size()) == 0);
        okay &= check(record_members(records[i].value));
        for (std::size_t b = 0; b < sizeof(records[i].value.unknown_20); ++b)
        {
            okay &=
                check(records[i].value.unknown_20[b] == static_cast<unsigned char>(0x70 + i + b));
        }
    }
    okay &= check(lookup.intact() && entries.intact());
    okay &= check(owner_unchanged());
    // Real Record destructors now release the independently owned record names.
    // All borrowed wrapper observations ended above; no record/name read follows
    // destruction, and the preserved nonzero reference counts do not block it.
    return okay;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2 || !std::setlocale(LC_CTYPE, "C"))
    {
        return 3;
    }
    try
    {
        OwnerFiles inputs{fs::path(argv[1])};
        Environment environment;
        files::Session session(inputs.mount());
        PoolBacking pool;
        {
            Guarded<Lookup> lookup;
            require(lookup.value.record_count == 0 && lookup.value.records != nullptr,
                "genuine bootstrap graph differs");
            for (std::size_t i = 0; i < cases.size(); ++i)
            {
                if (!run_case(cases[i], lookup))
                {
                    std::fprintf(stderr,
                        "case %zu (%s): resource lookup result differs\n",
                        i,
                        cases[i].label);
                    return 2;
                }
            }
        }
        require(screen_dialog_resources == nullptr && screen_dialog_palette_modifiers == nullptr,
            "owner registration survived destruction");
        if (!pool.restored())
        {
            std::fprintf(stderr, "resource lookup owner did not return its ring storage\n");
            return 2;
        }
        std::printf("{\"sessions\":%zu,\"calls\":%zu,\"direct_find\":%zu,\"lookup\":%zu,"
                    "\"record_lifetimes\":%zu,\"owned_names\":%zu,\"checks\":%zu,"
                    "\"record_size\":%zu,\"lookup_size\":%zu,\"count_offset\":%zu,\"records_"
                    "offset\":%zu}\n",
            cases.size(),
            calls,
            direct_calls,
            lookup_calls,
            record_lifetimes,
            owned_names,
            checks,
            sizeof(ScreenDialogResourceRecord),
            sizeof(ScreenDialogResourceLookup),
            offsetof(ScreenDialogResourceLookup, record_count),
            offsetof(ScreenDialogResourceLookup, records));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "resource lookup setup: %s\n", error.what());
        return 3;
    }
}
