#include "startup_screen_dialog_frame_file.h"
#include "startup_screen_dialog_resource.h"

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

using Frame = ScreenDialogResourceFrame;
static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(sizeof(int) == 4 && sizeof(float) == 4);
static_assert(std::endian::native == std::endian::little);
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::is_standard_layout_v<Frame> && !std::is_polymorphic_v<Frame>);
static_assert(sizeof(Frame) == (sizeof(void*) == 8 ? 176 : 120));

namespace
{
constexpr std::size_t guard = alignof(std::max_align_t);
constexpr std::size_t max_file = 128034;
using Bytes = std::array<unsigned char, sizeof(Frame)>;
enum class Phase
{
    outside,
    setup,
    method,
    destruction,
    orphan_cleanup
};
Phase phase = Phase::outside;
struct Totals
{
    std::size_t sessions = 0, constructors = 0, destructors = 0, whole_calls = 0, plane_calls = 0;
    std::size_t exceptions = 0, attempts = 0, allocations = 0, releases = 0, orphans = 0;
    std::size_t byte_checks = 0, checks = 0;
} totals;
const char* label = nullptr;
const char* first_error = nullptr;

[[noreturn]] void fail(const char* reason)
{
    std::fprintf(stderr, "resource raw loaders %s: %s\n", label ? label : "startup", reason);
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
#if defined(_WIN32)
    return ::_lseek(descriptor, 0, SEEK_CUR);
#else
    return ::lseek(descriptor, 0, SEEK_CUR);
#endif
}

// These are literal publication masks, not a second implementation of a loader.
constexpr unsigned int index_published = 1;
constexpr unsigned int offsets_published = 2;
constexpr unsigned int data_published = 4;
constexpr unsigned int plane_published = 8;
constexpr unsigned int bases_published = 16;
struct State
{
    unsigned int cursor;
    long long position;
    unsigned char whole, ready;
    unsigned int publications;
};
struct AllocationPlan
{
    std::size_t bytes;
    State before;
    std::size_t file_offset, read_bytes;
};
enum class Data
{
    literal,
    modulo251,
    ascending,
    explicit_offset
};
struct Case
{
    const char* name;
    bool whole;
    unsigned short count;
    unsigned char planes;
    unsigned short offsets;
    int data_size;
    unsigned int file_offset;
    int plane;
    unsigned int initial_cursor;
    long long initial_position;
    unsigned char initial_ready;
    std::size_t metadata_start, file_bytes;
    std::array<unsigned short, 8> indices;
    std::array<unsigned int, 8> offset_words;
    Data data;
    std::size_t data_bytes, explicit_offset;
    std::array<unsigned char, 8> literal;
    std::array<unsigned int, 8> bases;
    unsigned int base_count;
    std::array<AllocationPlan, 3> allocations;
    std::size_t allocation_count;
    State final;
};
struct ExceptionCase
{
    const char* name;
    std::size_t case_index, allocation;
    State final;
};

// BEGIN RETAINED D252 LITERALS
constexpr std::array<Case, 13> cases = {{{"W0-complete-and-replace",
                                             true,
                                             2,
                                             2,
                                             4,
                                             8,
                                             10U,
                                             0,
                                             0U,
                                             0,
                                             3,
                                             10,
                                             42,
                                             {0, 1, 2, 3},
                                             {0, 2, 4, 6},
                                             Data::literal,
                                             8,
                                             0,
                                             {64, 65, 66, 67, 68, 69, 70, 71},
                                             {},
                                             0,
                                             {{{8, {10U, 10, 2, 3, 0}, 10, 8},
                                                 {16, {18U, 18, 2, 3, 1}, 18, 16},
                                                 {8, {34U, 34, 1, 3, 3}, 34, 8}}},
                                             3,
                                             {42U, 42, 1, 3, 7}},
    {"W1-short-final-data",
        true,
        2,
        2,
        4,
        8,
        10U,
        0,
        0U,
        0,
        0,
        10,
        37,
        {0, 1, 2, 3},
        {0, 2, 4, 6},
        Data::literal,
        8,
        0,
        {64, 65, 66, 67, 68, 69, 70, 71},
        {},
        0,
        {{{8, {10U, 10, 2, 0, 0}, 10, 8},
            {16, {18U, 18, 2, 0, 1}, 18, 16},
            {8, {34U, 34, 1, 0, 3}, 34, 3}}},
        3,
        {37U, 37, 1, 0, 7}},
    {"W2-short-index-table-no-dereference",
        true,
        2,
        2,
        4,
        8,
        10U,
        0,
        0U,
        0,
        0,
        10,
        13,
        {0, 1, 2, 3},
        {0, 2, 4, 6},
        Data::literal,
        8,
        0,
        {64, 65, 66, 67, 68, 69, 70, 71},
        {},
        0,
        {{{8, {10U, 10, 2, 0, 0}, 10, 3},
            {16, {13U, 13, 2, 0, 1}, 13, 0},
            {8, {13U, 13, 1, 0, 3}, 13, 0}}},
        3,
        {13U, 13, 1, 0, 7}},
    {"W3-tracked-cursor-does-not-equal-OS",
        true,
        2,
        2,
        4,
        8,
        10U,
        0,
        4U,
        0,
        0,
        6,
        38,
        {0, 1, 2, 3},
        {0, 2, 4, 6},
        Data::literal,
        8,
        0,
        {64, 65, 66, 67, 68, 69, 70, 71},
        {},
        0,
        {{{8, {6U, 6, 2, 0, 0}, 6, 8},
            {16, {14U, 14, 2, 0, 1}, 14, 16},
            {8, {30U, 30, 1, 0, 3}, 30, 8}}},
        3,
        {38U, 38, 1, 0, 7}},
    {"W4-valid-file-negative-seek-failure",
        true,
        2,
        2,
        4,
        8,
        4294967295U,
        0,
        0U,
        0,
        0,
        0,
        32,
        {0, 1, 2, 3},
        {0, 2, 4, 6},
        Data::literal,
        8,
        0,
        {64, 65, 66, 67, 68, 69, 70, 71},
        {},
        0,
        {{{8, {4294967295U, 0, 2, 0, 0}, 0, 8},
            {16, {7U, 8, 2, 0, 1}, 8, 16},
            {8, {23U, 24, 1, 0, 3}, 24, 8}}},
        3,
        {31U, 32, 1, 0, 7}},
    {"W5-zero-length-blocks",
        true,
        0,
        0,
        0,
        0,
        10U,
        0,
        0U,
        0,
        0,
        10,
        10,
        {},
        {},
        Data::literal,
        0,
        0,
        {},
        {},
        0,
        {{{0, {10U, 10, 2, 0, 0}, 10, 0},
            {0, {10U, 10, 2, 0, 1}, 10, 0},
            {0, {10U, 10, 1, 0, 3}, 10, 0}}},
        3,
        {10U, 10, 1, 0, 7}},
    {"P0-build-metadata-last-plane",
        false,
        2,
        2,
        4,
        128000,
        10U,
        1,
        0U,
        0,
        0,
        10,
        128034,
        {0, 1, 2, 3},
        {0, 5, 64000, 64007},
        Data::modulo251,
        128000,
        0,
        {},
        {0, 64000},
        2,
        {{{8, {10U, 10, 0, 0, 0}, 10, 8},
            {16, {18U, 18, 0, 0, 1}, 18, 16},
            {64000, {64034U, 64034, 0, 1, 19}, 64034, 64000}}},
        3,
        {128034U, 128034, 0, 1, 27}},
    {"P1-ready-two-nonlast-plane",
        false,
        2,
        2,
        4,
        128000,
        10U,
        0,
        128034U,
        128034,
        2,
        10,
        128034,
        {0, 1, 2, 3},
        {0, 5, 64000, 64007},
        Data::modulo251,
        128000,
        0,
        {},
        {0, 64000},
        2,
        {{{64000, {34U, 34, 0, 2, 0}, 34, 64000}}},
        1,
        {64034U, 64034, 0, 2, 8}},
    {"P2-short-last-plane",
        false,
        2,
        2,
        4,
        128000,
        10U,
        1,
        0U,
        0,
        1,
        10,
        64037,
        {0, 1, 2, 3},
        {0, 5, 64000, 64007},
        Data::modulo251,
        128000,
        0,
        {},
        {0, 64000},
        2,
        {{{64000, {64034U, 64034, 0, 1, 0}, 64034, 3}}},
        1,
        {64037U, 64037, 0, 1, 8}},
    {"P3-eight-permuted-bases-nonlast",
        false,
        1,
        8,
        8,
        32,
        10U,
        6,
        0U,
        0,
        0,
        10,
        90,
        {7, 0, 5, 2, 6, 1, 4, 3},
        {4, 20, 12, 28, 24, 8, 16, 0},
        Data::ascending,
        32,
        0,
        {},
        {0, 4, 8, 12, 16, 20, 24, 28},
        8,
        {{{16, {10U, 10, 0, 0, 0}, 10, 16},
            {32, {26U, 26, 0, 0, 1}, 26, 32},
            {4, {82U, 82, 0, 1, 19}, 82, 4}}},
        3,
        {86U, 86, 0, 1, 27}},
    {"P4-eight-permuted-bases-last",
        false,
        1,
        8,
        8,
        32,
        10U,
        7,
        0U,
        0,
        0,
        10,
        90,
        {7, 0, 5, 2, 6, 1, 4, 3},
        {4, 20, 12, 28, 24, 8, 16, 0},
        Data::ascending,
        32,
        0,
        {},
        {0, 4, 8, 12, 16, 20, 24, 28},
        8,
        {{{16, {10U, 10, 0, 0, 0}, 10, 16},
            {32, {26U, 26, 0, 0, 1}, 26, 32},
            {4, {86U, 86, 0, 1, 19}, 86, 4}}},
        3,
        {90U, 90, 0, 1, 27}},
    {"P5-one-empty-plane",
        false,
        1,
        1,
        1,
        0,
        10U,
        0,
        0U,
        0,
        0,
        10,
        16,
        {0},
        {0},
        Data::literal,
        0,
        0,
        {},
        {0},
        1,
        {{{2, {10U, 10, 0, 0, 0}, 10, 2},
            {4, {12U, 12, 0, 0, 1}, 12, 4},
            {0, {16U, 16, 0, 1, 19}, 16, 0}}},
        3,
        {16U, 16, 0, 1, 27}},
    {"P6-ready-relative-seek-mismatch",
        false,
        1,
        2,
        2,
        8,
        0U,
        1,
        2U,
        0,
        1,
        0,
        18,
        {0, 1},
        {0, 4},
        Data::explicit_offset,
        4,
        14,
        {9, 8, 7, 6},
        {0, 4},
        2,
        {{{4, {14U, 14, 0, 1, 0}, 14, 4}}},
        1,
        {18U, 18, 0, 1, 8}}}};

constexpr std::array<ExceptionCase, 7> exceptions = {{{"W-new-I", 0, 1, {10U, 10, 2, 3, 0}},
    {"W-new-O", 0, 2, {18U, 18, 2, 3, 1}},
    {"W-new-D", 0, 3, {34U, 34, 1, 3, 3}},
    {"P-new-I", 6, 1, {10U, 10, 0, 0, 0}},
    {"P-new-O", 6, 2, {18U, 18, 0, 0, 1}},
    {"P-new-plane", 6, 3, {64034U, 64034, 0, 1, 19}},
    {"P-ready-new-plane", 7, 1, {34U, 34, 0, 2, 0}}}};
// END RETAINED D252 LITERALS

struct File
{
    std::array<unsigned char, max_file> bytes{};
    FILE* stream = nullptr;
    int descriptor = -1;

    explicit File(const Case& c)
    {
        std::size_t at = c.metadata_start;
        const std::size_t indices = static_cast<std::size_t>(c.count) * c.planes;
        necessary(indices <= c.indices.size() && c.offsets <= c.offset_words.size(),
            "file recipe table overflow");
        for (std::size_t i = 0; i < indices; ++i)
        {
            bytes[at++] = static_cast<unsigned char>(c.indices[i]);
            bytes[at++] = static_cast<unsigned char>(c.indices[i] >> 8);
        }
        for (std::size_t i = 0; i < c.offsets; ++i)
        {
            for (unsigned int shift = 0; shift < 32; shift += 8)
            {
                bytes[at++] = static_cast<unsigned char>(c.offset_words[i] >> shift);
            }
        }
        if (c.data == Data::explicit_offset)
        {
            at = c.explicit_offset;
        }
        necessary(at + c.data_bytes <= bytes.size() && c.file_bytes <= bytes.size(),
            "file recipe data overflow");
        for (std::size_t i = 0; i < c.data_bytes; ++i)
        {
            bytes[at + i] = c.data == Data::modulo251   ? static_cast<unsigned char>(i % 251)
                            : c.data == Data::ascending ? static_cast<unsigned char>(64 + i)
                                                        : c.literal[i];
        }
        stream = std::tmpfile();
        necessary(stream != nullptr, "real temporary file creation");
#if defined(_WIN32)
        descriptor = ::_fileno(stream);
        necessary(
            descriptor >= 0 && ::_setmode(descriptor, _O_BINARY) != -1, "binary descriptor mode");
#else
        descriptor = ::fileno(stream);
        necessary(descriptor >= 0, "real file descriptor");
#endif
        necessary(std::fwrite(bytes.data(), 1, c.file_bytes, stream) == c.file_bytes,
            "generated file write");
        necessary(std::fflush(stream) == 0, "generated file flush");
#if defined(_WIN32)
        const long actual = ::_lseek(descriptor, static_cast<long>(c.initial_position), SEEK_SET);
#else
        const off_t actual = ::lseek(descriptor, static_cast<off_t>(c.initial_position), SEEK_SET);
#endif
        necessary(actual == c.initial_position, "initial real file position");
    }
    ~File()
    {
        close();
    }
    void close()
    {
        if (stream)
        {
            FILE* closing = stream;
            stream = nullptr;
            necessary(std::fclose(closing) == 0, "real file close");
        }
    }
};

struct Allocation
{
    unsigned char* raw = nullptr;
    std::uintptr_t address = 0;
    std::size_t bytes = 0, capacity = 0;
    bool live = false;
};
std::array<Allocation, 32> allocations;
std::size_t allocation_count = 0;
std::array<std::size_t, 16> release_ids{};
std::size_t release_count = 0;
std::array<std::size_t, 3> method_ids{};
std::size_t method_allocations = 0, method_attempts = 0, throw_at = 0;
const Case* active_case = nullptr;
const File* active_file = nullptr;
Frame* active_frame = nullptr;
Bytes entry{};

struct Span
{
    std::size_t offset, bytes;
};
#define MEMBER(name)                                                                               \
    Span                                                                                           \
    {                                                                                              \
        offsetof(Frame, name), sizeof(Frame::name)                                                 \
    }
constexpr auto members = std::to_array<Span>({MEMBER(name),
    MEMBER(time_04),
    MEMBER(loaded_08),
    MEMBER(unknown_09),
    MEMBER(count_0a),
    MEMBER(plane_count_0c),
    MEMBER(unknown_0d),
    MEMBER(width_0e),
    MEMBER(height_10),
    MEMBER(unknown_12),
    MEMBER(word_14),
    MEMBER(index_ready_16),
    MEMBER(unknown_17),
    MEMBER(data_size_18),
    MEMBER(file_offset_1c),
    MEMBER(plane_offsets_20),
    MEMBER(offset_count_40),
    MEMBER(unknown_42),
    MEMBER(indices_44),
    MEMBER(whole_data_48),
    MEMBER(unknown_49),
    MEMBER(data_4c),
    MEMBER(plane_data_50),
    MEMBER(offsets_70),
    MEMBER(flag_74)});
#undef MEMBER

template <class T> void put(Bytes& bytes, std::size_t offset, const T& value)
{
    necessary(offset + sizeof(value) <= bytes.size(), "expected member bounds");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void* method_pointer(std::size_t index)
{
    necessary(index < method_allocations, "publication before allocation");
    return reinterpret_cast<void*>(allocations[method_ids[index]].address);
}

void state(const State& s, unsigned char loaded)
{
    Bytes expected = entry;
    put(expected, offsetof(Frame, loaded_08), loaded);
    put(expected, offsetof(Frame, whole_data_48), s.whole);
    put(expected, offsetof(Frame, index_ready_16), s.ready);
    if (s.publications & index_published)
    {
        put(expected, offsetof(Frame, indices_44), static_cast<unsigned short*>(method_pointer(0)));
    }
    if (s.publications & offsets_published)
    {
        put(expected, offsetof(Frame, offsets_70), static_cast<unsigned int*>(method_pointer(1)));
    }
    if (s.publications & data_published)
    {
        put(expected, offsetof(Frame, data_4c), static_cast<unsigned char*>(method_pointer(2)));
    }
    if (s.publications & plane_published)
    {
        const std::size_t index = active_case->allocation_count - 1;
        put(expected,
            offsetof(Frame, plane_data_50) +
                static_cast<std::size_t>(active_case->plane) * sizeof(unsigned char*),
            static_cast<unsigned char*>(method_pointer(index)));
    }
    if (s.publications & bases_published)
    {
        for (std::size_t i = 0; i < active_case->base_count; ++i)
        {
            put(expected,
                offsetof(Frame, plane_offsets_20) + i * sizeof(unsigned int),
                active_case->bases[i]);
        }
    }
    Bytes actual{};
    std::memcpy(actual.data(), static_cast<const void*>(active_frame), actual.size());
    for (const Span& span : members)
    {
        for (std::size_t n = 0; n < span.bytes; ++n)
        {
            ++totals.byte_checks;
            expect(actual[span.offset + n] == expected[span.offset + n],
                "literal member/publication state");
        }
    }
    expect(screen_dialog_frame_file == active_file->descriptor, "descriptor publication changed");
    expect(screen_dialog_frame_cursor == s.cursor, "literal tracked cursor");
    expect(position(active_file->descriptor) == s.position, "literal real descriptor position");
}

void allocation_guards(const Allocation& a)
{
    necessary(a.live, "read of retired allocation");
    for (std::size_t i = 0; i < guard; ++i)
    {
        totals.byte_checks += 2;
        expect(a.raw[i] == 0xa5 && a.raw[guard + a.capacity + i] == 0x5a,
            "scalar allocation guard changed");
    }
    if (a.bytes == 0)
    {
        ++totals.byte_checks;
        expect(a.raw[guard] == 0x6b, "zero-size allocation changed");
    }
}

void* allocate(std::size_t bytes)
{
    if (phase == Phase::outside)
    {
        void* p = std::malloc(bytes ? bytes : 1);
        if (!p)
        {
            throw std::bad_alloc();
        }
        return p;
    }
    necessary(phase == Phase::setup || phase == Phase::method, "unexpected scalar allocation");
    ++totals.attempts;
    if (phase == Phase::method)
    {
        necessary(method_attempts < active_case->allocation_count, "extra loader allocation");
        const AllocationPlan& plan = active_case->allocations[method_attempts++];
        necessary(bytes == plan.bytes, "literal loader allocation size");
        state(plan.before, 7);
        if (method_attempts == throw_at)
        {
            throw std::bad_alloc();
        }
    }
    necessary(allocation_count < allocations.size(), "allocation ledger full");
    const std::size_t capacity = bytes ? bytes : 1;
    necessary(capacity <= max_file, "allocation outside fixture domain");
    auto* raw = static_cast<unsigned char*>(std::malloc(capacity + 2 * guard));
    if (!raw)
    {
        throw std::bad_alloc();
    }
    std::memset(raw, 0xa5, guard);
    std::memset(raw + guard, 0x6b, capacity);
    std::memset(raw + guard + capacity, 0x5a, guard);
    const std::size_t id = allocation_count++;
    allocations[id] = {raw, reinterpret_cast<std::uintptr_t>(raw + guard), bytes, capacity, true};
    if (phase == Phase::method)
    {
        method_ids[method_allocations++] = id;
    }
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
        necessary(a.live, "duplicate scalar release");
        necessary(phase == Phase::destruction || phase == Phase::orphan_cleanup,
            "loader released an existing owner");
        allocation_guards(a);
        if (phase == Phase::destruction)
        {
            necessary(release_count < release_ids.size(), "release ledger full");
            release_ids[release_count++] = i;
        }
        else
        {
            ++totals.orphans;
        }
        a.live = false;
        std::free(a.raw);
        a.raw = nullptr;
        ++totals.releases;
        return;
    }
    necessary(phase == Phase::outside, "untracked game scalar release");
    std::free(pointer);
}

struct Storage
{
    std::array<unsigned char, guard> before{};
    alignas(Frame) Bytes bytes;
    std::array<unsigned char, guard> after{};
    Frame* object = nullptr;

    void construct()
    {
        before.fill(0x39);
        after.fill(0x93);
        object = ::new (bytes.data()) Frame;
        ++totals.constructors;
    }
    void guards()
    {
        for (std::size_t i = 0; i < guard; ++i)
        {
            totals.byte_checks += 2;
            expect(before[i] == 0x39 && after[i] == 0x93, "Frame outside guard changed");
        }
    }
    void destroy()
    {
        phase = Phase::destruction;
        object->~Frame();
        object = nullptr;
        ++totals.destructors;
        phase = Phase::outside;
    }
};
static_assert(offsetof(Storage, bytes) == guard);
static_assert(offsetof(Storage, after) == guard + sizeof(Frame));

std::size_t allocation_id(const void* pointer)
{
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        if (allocations[i].address == address && allocations[i].live)
        {
            return i;
        }
    }
    fail("owner missing from live ledger");
}

void initialize(Frame& f, const Case& c)
{
    // Every typed member is assigned during a genuine constructor-established
    // lifetime. ABI padding is neither an oracle nor read as a member.
    f.time_04 = 17.25F;
    f.loaded_08 = 7;
    f.unknown_09 = 0x39;
    f.count_0a = c.count;
    f.plane_count_0c = c.planes;
    f.unknown_0d = 0x5b;
    f.width_0e = 11;
    f.height_10 = 13;
    f.unknown_12[0] = 0x76;
    f.unknown_12[1] = 0x2e;
    f.word_14 = -17;
    f.index_ready_16 = c.initial_ready;
    f.unknown_17 = 0xce;
    f.data_size_18 = c.data_size;
    f.file_offset_1c = c.file_offset;
    for (std::size_t i = 0; i < 8; ++i)
    {
        f.plane_offsets_20[i] = c.initial_ready && !c.whole && i < c.base_count
                                    ? c.bases[i]
                                    : 0x12340000U + static_cast<unsigned int>(i);
    }
    f.offset_count_40 = c.offsets;
    f.unknown_42[0] = 0xab;
    f.unknown_42[1] = 0x54;
    f.whole_data_48 = 2;
    f.unknown_49[0] = 0xe7;
    f.unknown_49[1] = 0x31;
    f.unknown_49[2] = 0x6a;
    f.flag_74 = 0xe3;
    phase = Phase::setup;
    auto* name = static_cast<char*>(::operator new(5));
    std::memcpy(name, "raw", 4);
    f.name = name;
    f.data_4c = static_cast<unsigned char*>(::operator new(12));
    f.offsets_70 = static_cast<unsigned int*>(::operator new(32));
    f.indices_44 = static_cast<unsigned short*>(::operator new(16));
    for (std::size_t i = 0; i < 8; ++i)
    {
        f.offsets_70[i] = c.offset_words[i];
        f.indices_44[i] = c.indices[i];
        f.plane_data_50[i] = static_cast<unsigned char*>(::operator new(8));
    }
    phase = Phase::outside;
}

void run(const Case& c, const ExceptionCase* exception)
{
    label = exception ? exception->name : c.name;
    first_error = nullptr;
    necessary(
        phase == Phase::outside && screen_dialog_frame_file == -1, "prior descriptor state leaked");
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        necessary(!allocations[i].live, "prior allocation leaked");
    }
    allocation_count = release_count = method_allocations = method_attempts = 0;
    throw_at = exception ? exception->allocation : 0;
    active_case = &c;
    ++totals.sessions;
    File file(c);
    active_file = &file;
    Storage storage;
    storage.construct();
    Frame& f = *storage.object;
    active_frame = &f;
    initialize(f, c);
    necessary(allocation_count == 12, "fixture old ownership ledger");
    std::array<std::array<unsigned char, 32>, 12> old_bytes{};
    for (std::size_t i = 0; i < 12; ++i)
    {
        std::memcpy(old_bytes[i].data(), allocations[i].raw + guard, allocations[i].bytes);
    }
    std::memcpy(entry.data(), static_cast<const void*>(&f), entry.size());
    screen_dialog_frame_file = file.descriptor;
    screen_dialog_frame_cursor = c.initial_cursor;
    bool caught = false;
    phase = Phase::method;
    try
    {
        if (c.whole)
        {
            ++totals.whole_calls;
            f.LoadAllData();
        }
        else
        {
            ++totals.plane_calls;
            f.LoadPlaneData(c.plane);
        }
    }
    catch (const std::bad_alloc&)
    {
        caught = true;
        ++totals.exceptions;
    }
    phase = Phase::outside;
    expect(caught == (exception != nullptr), "host allocation exception boundary");
    expect(method_attempts == (exception ? exception->allocation : c.allocation_count),
        "allocation attempt count");
    expect(method_allocations == (exception ? exception->allocation - 1 : c.allocation_count),
        "successful allocation count");
    expect(f.loaded_08 == (exception ? 7 : 1),
        c.whole ? "whole loaded publication" : "plane loaded publication");
    state(exception ? exception->final : c.final, exception ? 7 : 1);
    expect(release_count == 0, "loader released storage");
    for (std::size_t i = 0; i < 12; ++i)
    {
        necessary(allocations[i].live, "old owner retired by loader");
        for (std::size_t b = 0; b < allocations[i].bytes; ++b)
        {
            ++totals.byte_checks;
            expect(allocations[i].raw[guard + b] == old_bytes[i][b], "old owner payload changed");
        }
    }
    for (std::size_t i = 0; i < method_allocations; ++i)
    {
        const AllocationPlan& plan = c.allocations[i];
        const Allocation& a = allocations[method_ids[i]];
        necessary(plan.read_bytes <= a.bytes && plan.file_offset + plan.read_bytes <= c.file_bytes,
            "literal initialized read prefix bounds");
        for (std::size_t b = 0; b < a.bytes; ++b)
        {
            const unsigned char expected =
                b < plan.read_bytes ? file.bytes[plan.file_offset + b] : 0x6b;
            ++totals.byte_checks;
            expect(a.raw[guard + b] == expected, "read bytes or untouched scalar storage");
        }
    }
    // Capture live owner identities before destruction; no dead Frame reads.
    std::array<std::size_t, 12> expected_release = {allocation_id(f.name),
        allocation_id(f.data_4c),
        allocation_id(f.offsets_70),
        allocation_id(f.indices_44)};
    for (std::size_t i = 0; i < 8; ++i)
    {
        expected_release[4 + i] = allocation_id(f.plane_data_50[i]);
    }
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        allocation_guards(allocations[i]);
    }
    storage.guards();
    storage.destroy();
    expect(release_count == expected_release.size(), "actual destructor release count");
    for (std::size_t i = 0; i < release_count && i < expected_release.size(); ++i)
    {
        expect(release_ids[i] == expected_release[i], "actual destructor ownership order");
    }
    storage.guards();
    std::size_t abandoned = 0;
    phase = Phase::orphan_cleanup;
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        if (allocations[i].live)
        {
            necessary(i < 12, "new allocation was not owned by Frame");
            ++abandoned;
            ::operator delete(reinterpret_cast<void*>(allocations[i].address));
        }
    }
    phase = Phase::outside;
    expect(abandoned == method_allocations, "retained overwritten-owner leak count");
    screen_dialog_frame_file = -1;
    screen_dialog_frame_cursor = 0;
    active_frame = nullptr;
    active_file = nullptr;
    file.close();
    if (first_error)
    {
        fail(first_error);
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
    necessary(screen_dialog_frame_file == -1 && screen_dialog_frame_cursor == 0,
        "real file-state global initialization");
    for (const Case& c : cases)
    {
        run(c, nullptr);
    }
    for (const ExceptionCase& exception : exceptions)
    {
        run(cases[exception.case_index], &exception);
    }
    expect(totals.sessions == 20 && totals.constructors == 20 && totals.destructors == 20,
        "genuine Frame lifetime totals");
    expect(totals.whole_calls == 9 && totals.plane_calls == 11 && totals.exceptions == 7,
        "loader call and exception totals");
    expect(totals.attempts == 286 && totals.allocations == 279 && totals.releases == 279 &&
               totals.orphans == 39,
        "scalar ownership totals");
    if (first_error)
    {
        fail(first_error);
    }
    std::printf("resource raw loaders: %zu sessions, %zu frames, %zu whole calls, %zu plane calls, "
                "%zu host exceptions, %zu allocation attempts, %zu allocations, %zu releases, "
                "%zu orphan releases, %zu byte checks, %zu checks\n",
        totals.sessions,
        totals.constructors,
        totals.whole_calls,
        totals.plane_calls,
        totals.exceptions,
        totals.attempts,
        totals.allocations,
        totals.releases,
        totals.orphans,
        totals.byte_checks,
        totals.checks);
    return 0;
}
