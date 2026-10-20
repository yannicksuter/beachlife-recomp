#include "startup_screen_dialog_resource.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>

using Record = ScreenDialogResourceRecord;
using Frame = ScreenDialogResourceFrame;
static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(sizeof(int) == 4 && (sizeof(void*) == 4 || sizeof(void*) == 8));
static_assert(std::is_standard_layout_v<Record> && !std::is_polymorphic_v<Record>);
static_assert(std::is_standard_layout_v<Frame> && !std::is_polymorphic_v<Frame>);
static_assert(!std::is_aggregate_v<Record> && !std::is_trivially_destructible_v<Record>);
static_assert(sizeof(Record) == (sizeof(void*) == 8 ? 56 : 36));
static_assert(offsetof(Record, unknown_20) == (sizeof(void*) == 8 ? 48 : 32));
static_assert(offsetof(Record, unknown_20) == offsetof(Record, word_1c) + 4);
static_assert(sizeof(Frame) == (sizeof(void*) == 8 ? 176 : 120));

namespace
{
constexpr std::size_t guard = alignof(std::max_align_t);
using Bytes = std::array<unsigned char, sizeof(Record)>;

struct Totals
{
    std::size_t sessions = 0, constructors = 0, destructors = 0, frames = 0;
    std::size_t allocations = 0, releases = 0, checks = 0, member_bytes = 0;
} totals;
char label[80];
const char* first_error = nullptr;

[[noreturn]] void fail(const char* reason)
{
    std::fprintf(stderr, "resource record %s: %s\n", label, reason);
    std::exit(2);
}

void expect(bool condition, const char* reason)
{
    ++totals.checks;
    if (!condition && !first_error)
    {
        first_error = reason;
    }
}

void necessary(bool condition, const char* reason)
{
    if (!condition)
    {
        fail(reason);
    }
}

enum class Phase
{
    outside,
    setup,
    method,
    destruction,
    cleanup
};
Phase phase = Phase::outside;
struct Trace
{
    Phase saved;
    explicit Trace(Phase value)
        : saved(phase)
    {
        phase = value;
    }
    ~Trace()
    {
        phase = saved;
    }
};

enum class Tag
{
    none,
    name,
    palette_table,
    palette0,
    palette1,
    palette2,
    frame_table,
    frame0,
    frame1,
    frame2,
    frame_name,
    frame_data,
    frame_plane7
};
struct Allocation
{
    unsigned char* raw = nullptr;
    std::uintptr_t address = 0;
    std::size_t bytes = 0;
    Tag tag = Tag::none;
    bool live = false;
};
std::array<Allocation, 16> allocations;
std::size_t allocation_count = 0;
std::array<Tag, 16> release_tags;
std::size_t release_count = 0;
Tag next_tag = Tag::none;

void allocation_guards(const Allocation& a)
{
    necessary(a.live, "allocation already dead");
    for (std::size_t i = 0; i < guard; ++i)
    {
        expect(a.raw[i] == 0xa5, "allocation prefix changed");
        expect(a.raw[guard + a.bytes + i] == 0x5a, "allocation suffix changed");
    }
}

// Fixture-only scalar allocation observation, paired with real malloc/free.
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
    necessary(phase == Phase::setup && next_tag != Tag::none,
        "unexpected allocation in a lifetime method");
    necessary(bytes > 0 && bytes <= std::numeric_limits<std::size_t>::max() - 2 * guard,
        "allocation size outside fixture domain");
    necessary(allocation_count < allocations.size(), "allocation ledger full");
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        necessary(allocations[i].tag != next_tag, "duplicate allocation tag");
    }
    auto* raw = static_cast<unsigned char*>(std::malloc(bytes + 2 * guard));
    if (!raw)
    {
        throw std::bad_alloc();
    }
    std::memset(raw, 0xa5, guard);
    std::memset(raw + guard, 0x6b, bytes);
    std::memset(raw + guard + bytes, 0x5a, guard);
    allocations[allocation_count++] = {
        raw, reinterpret_cast<std::uintptr_t>(raw + guard), bytes, next_tag, true};
    next_tag = Tag::none;
    ++totals.allocations;
    return raw + guard;
}

void release(void* pointer) noexcept
{
    if (!pointer)
    {
        return;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        Allocation& a = allocations[i];
        if (a.address != address)
        {
            continue;
        }
        necessary(a.live, "duplicate scalar delete");
        allocation_guards(a);
        if (phase == Phase::destruction)
        {
            necessary(release_count < release_tags.size(), "release ledger full");
            release_tags[release_count++] = a.tag;
        }
        a.live = false;
        std::free(a.raw);
        a.raw = nullptr;
        ++totals.releases;
        return;
    }
    necessary(phase == Phase::outside, "untracked game scalar delete");
    std::free(pointer);
}

void* request(Tag tag, std::size_t bytes)
{
    Trace trace(Phase::setup);
    necessary(next_tag == Tag::none, "pending allocation tag");
    next_tag = tag;
    void* result = ::operator new(bytes);
    necessary(next_tag == Tag::none, "allocation provider not selected");
    return result;
}

template <class T> T* table(Tag tag, std::size_t count)
{
    necessary(count > 0 && count <= 3, "table capacity outside fixture domain");
    void* raw = request(tag, count * sizeof(T));
    T* result = ::new (raw) T[count]{};
    necessary(static_cast<void*>(result) == raw, "nonallocating table placement changed address");
    return result;
}

void begin(const char* family, std::size_t number)
{
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        necessary(!allocations[i].live, "prior allocation remained live");
    }
    allocation_count = release_count = 0;
    next_tag = Tag::none;
    first_error = nullptr;
    std::snprintf(label, sizeof(label), "%s-%zu", family, number);
    ++totals.sessions;
}

void finish()
{
    // A safe changed-source control may leave a palette. Reclaim only from the
    // saved allocation receipt, never from a dead record or freed pointer table.
    {
        Trace trace(Phase::cleanup);
        for (std::size_t i = 0; i < allocation_count; ++i)
        {
            if (allocations[i].live)
            {
                ::operator delete(reinterpret_cast<void*>(allocations[i].address));
            }
        }
    }
    if (first_error)
    {
        fail(first_error);
    }
}

struct Span
{
    std::size_t offset, size;
};
#define MEMBER(name)                                                                               \
    Span                                                                                           \
    {                                                                                              \
        offsetof(Record, name), sizeof(Record::name)                                               \
    }
constexpr auto members = std::to_array<Span>({MEMBER(name),
    MEMBER(reference_count),
    MEMBER(frame_count),
    MEMBER(frames),
    MEMBER(palette_count),
    MEMBER(palettes),
    MEMBER(format_18),
    MEMBER(word_1c),
    MEMBER(unknown_20)});
#undef MEMBER

void same_members(const Bytes& actual, const Bytes& expected)
{
    for (const Span& span : members)
    {
        for (std::size_t i = 0; i < span.size; ++i)
        {
            ++totals.member_bytes;
            expect(actual[span.offset + i] == expected[span.offset + i],
                "record member representation changed");
        }
    }
}

struct Storage
{
    std::array<unsigned char, guard> before;
    alignas(Record) Bytes bytes;
    std::array<unsigned char, guard> after;
    Record* object = nullptr;

    explicit Storage(unsigned char pattern)
    {
        before.fill(0x39);
        after.fill(0x93);
        volatile unsigned char* raw = bytes.data();
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            raw[i] = pattern;
        }
    }
    void construct()
    {
        Trace trace(Phase::method);
        object = ::new (bytes.data()) Record;
        ++totals.constructors;
    }
    void destroy()
    {
        Trace trace(Phase::destruction);
        object->~Record();
        object = nullptr;
        ++totals.destructors;
    }
    void guards() const
    {
        for (std::size_t i = 0; i < guard; ++i)
        {
            expect(before[i] == 0x39 && after[i] == 0x93, "record outside guard changed");
        }
    }
};
static_assert(offsetof(Storage, bytes) == guard);
static_assert(offsetof(Storage, after) == guard + sizeof(Record));

void initialized(const Record& record, unsigned char pattern)
{
    expect(record.name == nullptr, "constructor name");
    expect(record.reference_count == 0, "constructor reference count");
    expect(record.frame_count == 0 && record.frames == nullptr, "constructor frames");
    expect(record.palette_count == 0 && record.palettes == nullptr, "constructor palettes");
    expect(record.format_18 == 1, "constructor format");
    expect(record.word_1c == 0, "constructor word1c");
    for (unsigned char value : record.unknown_20)
    {
        expect(value == pattern, "constructor opaque tail");
    }
}

void construction_cases()
{
    constexpr std::array<unsigned char, 4> patterns{0, 0x5a, 0xa5, 0xff};
    for (std::size_t i = 0; i < patterns.size(); ++i)
    {
        begin("constructor", i);
        Storage storage(patterns[i]);
        storage.construct();
        initialized(*storage.object, patterns[i]);
        storage.guards();
        Bytes before;
        std::memcpy(before.data(), storage.bytes.data(), before.size());
        storage.destroy();
        same_members(storage.bytes, before);
        storage.guards();
        expect(release_count == 0 && allocation_count == 0, "empty record released an owner");
        finish();
    }
}

struct Case
{
    bool name;
    int palette_count;
    unsigned int palette_capacity;
    std::array<bool, 3> palette_slots;
    int frame_count;
    unsigned int frame_capacity;
    std::array<bool, 3> frame_slots;
    unsigned int frame0_owners;
    unsigned int reference_count;
    std::array<Tag, 8> releases;
    std::size_t release_count;
};

// D260's independent literal release order. Capacity and count intentionally
// differ for empty/negative counts; the two real tables are still owned.
constexpr std::array<Case, 12> cases{{
    {false, 1, 1, {true}, 0, 0, {}, 0, 77, {Tag::palette0, Tag::palette_table}, 2},
    {false, 0, 0, {}, 0, 0, {}, 0, 0, {}, 0},
    {true, 0, 0, {}, 0, 0, {}, 0, UINT_MAX, {Tag::name}, 1},
    {false,
        2,
        2,
        {true, true},
        0,
        0,
        {},
        0,
        0,
        {Tag::palette0, Tag::palette1, Tag::palette_table},
        3},
    {false,
        3,
        3,
        {true, false, true},
        0,
        0,
        {},
        0,
        0,
        {Tag::palette0, Tag::palette2, Tag::palette_table},
        3},
    {false, 0, 1, {}, 0, 1, {}, 0, 0, {Tag::palette_table, Tag::frame_table}, 2},
    {false, -3, 1, {}, -2, 1, {}, 0, 0, {Tag::palette_table, Tag::frame_table}, 2},
    {false, 0, 0, {}, 1, 1, {true}, 0, 0, {Tag::frame0, Tag::frame_table}, 2},
    {false,
        0,
        0,
        {},
        3,
        3,
        {true, false, true},
        0,
        0,
        {Tag::frame0, Tag::frame2, Tag::frame_table},
        3},
    {false,
        0,
        0,
        {},
        1,
        1,
        {true},
        7,
        0,
        {Tag::frame_name, Tag::frame_data, Tag::frame_plane7, Tag::frame0, Tag::frame_table},
        5},
    {true,
        2,
        2,
        {true, true},
        1,
        1,
        {true},
        5,
        123,
        {Tag::name,
            Tag::palette0,
            Tag::palette1,
            Tag::palette_table,
            Tag::frame_name,
            Tag::frame_plane7,
            Tag::frame0,
            Tag::frame_table},
        8},
    {false, 2, 2, {}, 2, 2, {}, 0, 0, {Tag::palette_table, Tag::frame_table}, 2},
}};

void populate(Record& record, const Case& example)
{
    record.reference_count = example.reference_count;
    record.format_18 = 3;
    record.word_1c = 4;
    if (example.name)
    {
        char* name = static_cast<char*>(request(Tag::name, 7));
        std::memcpy(name, "record", 7);
        record.name = name;
    }
    record.palette_count = example.palette_count;
    if (example.palette_capacity)
    {
        record.palettes = table<unsigned short*>(Tag::palette_table, example.palette_capacity);
        constexpr std::array<Tag, 3> tags{Tag::palette0, Tag::palette1, Tag::palette2};
        for (std::size_t i = 0; i < example.palette_capacity; ++i)
        {
            if (example.palette_slots[i])
            {
                void* raw = request(tags[i], 3 * sizeof(unsigned short));
                auto* palette = ::new (raw) unsigned short[3]{0x1234, 0xabcd, 0x00ff};
                necessary(static_cast<void*>(palette) == raw, "palette placement changed address");
                record.palettes[i] = palette;
            }
        }
    }
    record.frame_count = example.frame_count;
    if (example.frame_capacity)
    {
        record.frames = table<Frame*>(Tag::frame_table, example.frame_capacity);
        constexpr std::array<Tag, 3> tags{Tag::frame0, Tag::frame1, Tag::frame2};
        for (std::size_t i = 0; i < example.frame_capacity; ++i)
        {
            if (!example.frame_slots[i])
            {
                continue;
            }
            void* raw = request(tags[i], sizeof(Frame));
            Frame* frame;
            {
                Trace trace(Phase::method);
                frame = ::new (raw) Frame;
            }
            ++totals.frames;
            frame->time_04 = 17.25F;
            frame->loaded_08 = 1;
            frame->whole_data_48 = 1;
            frame->index_ready_16 = 1;
            frame->flag_74 = 1;
            record.frames[i] = frame;
            if (i == 0 && (example.frame0_owners & 1))
            {
                char* name = static_cast<char*>(request(Tag::frame_name, 6));
                std::memcpy(name, "frame", 6);
                frame->name = name;
            }
            if (i == 0 && (example.frame0_owners & 2))
            {
                frame->data_4c = static_cast<unsigned char*>(request(Tag::frame_data, 9));
            }
            if (i == 0 && (example.frame0_owners & 4))
            {
                frame->plane_data_50[7] =
                    static_cast<unsigned char*>(request(Tag::frame_plane7, 11));
            }
        }
    }
}

void ownership_cases()
{
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        begin("ownership", i);
        Storage storage(0x71);
        storage.construct();
        initialized(*storage.object, 0x71);
        populate(*storage.object, cases[i]);
        storage.guards();
        Bytes before;
        std::memcpy(before.data(), storage.bytes.data(), before.size());
        storage.destroy();
        expect(release_count == cases[i].release_count, "destructor release count");
        for (std::size_t n = 0; n < release_count && n < cases[i].release_count; ++n)
        {
            expect(release_tags[n] == cases[i].releases[n], "destructor release order");
        }
        for (std::size_t n = 0; n < allocation_count; ++n)
        {
            expect(!allocations[n].live, "record left an owned allocation live");
        }
        same_members(storage.bytes, before);
        storage.guards();
        finish();
    }
}
} // namespace

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

int main()
{
    construction_cases();
    ownership_cases();
    necessary(totals.sessions == 16 && totals.constructors == 16 && totals.destructors == 16,
        "literal record lifetime totals");
    necessary(totals.frames == 5 && totals.allocations == 33 && totals.releases == 33,
        "literal owned allocation totals");
    std::printf("{\"sessions\":%zu,\"constructors\":%zu,\"destructors\":%zu,\"frames\":%zu,"
                "\"allocations\":%zu,\"releases\":%zu,\"checks\":%zu,\"member_bytes\":%zu,"
                "\"record_size\":%zu,\"frame_size\":%zu,\"opaque_offset\":%zu}\n",
        totals.sessions,
        totals.constructors,
        totals.destructors,
        totals.frames,
        totals.allocations,
        totals.releases,
        totals.checks,
        totals.member_bytes,
        sizeof(Record),
        sizeof(Frame),
        offsetof(Record, unknown_20));
}
