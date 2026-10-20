#include "startup_screen_dialog_resource.h"

#include <array>
#include <bit>
#include <climits>
#include <clocale>
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
static_assert(sizeof(short) == 2 && sizeof(int) == 4 && sizeof(float) == 4);
static_assert(std::endian::native == std::endian::little);
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::is_standard_layout_v<Record> && !std::is_polymorphic_v<Record>);
static_assert(std::is_standard_layout_v<Frame> && !std::is_polymorphic_v<Frame>);
static_assert(!std::is_aggregate_v<Record> && !std::is_trivially_destructible_v<Record>);
static_assert(sizeof(void*) == 4 || sizeof(void*) == 8);
static_assert(sizeof(Record) == (sizeof(void*) == 8 ? 56 : 36));
static_assert(sizeof(Frame) == (sizeof(void*) == 8 ? 176 : 120));
static_assert(offsetof(Record, unknown_20) == (sizeof(void*) == 8 ? 48 : 32));
static_assert(offsetof(Frame, flag_74) == (sizeof(void*) == 8 ? 168 : 116));

namespace
{
constexpr std::size_t guard = alignof(std::max_align_t);
using RecordBytes = std::array<unsigned char, sizeof(Record)>;
using FrameBytes = std::array<unsigned char, sizeof(Frame)>;
struct Totals
{
    std::size_t sessions = 0, constructors = 0, destructors = 0, calls = 0;
    std::size_t frames = 0, palettes = 0, consumed = 0;
    std::size_t allocations = 0, releases = 0, orphan_releases = 0, checks = 0;
} totals;
char label[64];
const char* first_error = nullptr;

[[noreturn]] void fail(const char* reason)
{
    std::fprintf(stderr, "resource record metadata %s: %s\n", label, reason);
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
    method,
    destruction,
    cleanup
};
Phase phase = Phase::outside;
struct Trace
{
    Phase saved;
    explicit Trace(Phase next)
        : saved(phase)
    {
        phase = next;
    }
    ~Trace()
    {
        phase = saved;
    }
};

enum class Role
{
    record_name,
    palette_table,
    palette,
    frame_table,
    frame,
    frame_name
};
struct Request
{
    Role role;
    std::size_t bytes;
    long position;
};
struct Allocation
{
    unsigned char* raw = nullptr;
    std::uintptr_t address = 0;
    std::size_t bytes = 0, capacity = 0;
    Role role = Role::record_name;
    bool live = false;
};
std::array<Request, 16> requests;
std::size_t request_count = 0, request_next = 0;
std::array<Allocation, 32> allocations;
std::size_t allocation_count = 0;
std::array<std::size_t, 32> release_ids;
std::size_t release_count = 0;
FILE* active_stream = nullptr;

void allocation_guards(const Allocation& a)
{
    necessary(a.live, "allocation already dead");
    for (std::size_t i = 0; i < guard; ++i)
    {
        expect(a.raw[i] == 0xa5, "allocation prefix changed");
        expect(a.raw[guard + a.capacity + i] == 0x5a, "allocation suffix changed");
    }
    if (a.bytes == 0)
    {
        expect(a.raw[guard] == 0x6b, "zero-size allocation was written");
    }
}

// Fixture-only scalar allocation observation; real malloc/free supply storage.
// No failure injection, file-reader replacement or game implementation is here.
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
    necessary(phase == Phase::method && active_stream, "unexpected allocation phase");
    necessary(request_next < request_count, "unexpected scalar allocation");
    const Request& r = requests[request_next++];
    necessary(bytes == r.bytes, "scalar allocation size differs");
    expect(std::ftell(active_stream) == r.position, "allocation stream position differs");
    necessary(allocation_count < allocations.size(), "allocation ledger full");
    const std::size_t capacity = bytes ? bytes : 1;
    necessary(capacity <= 4096, "allocation outside bounded fixture domain");
    auto* raw = static_cast<unsigned char*>(std::malloc(capacity + 2 * guard));
    if (!raw)
    {
        throw std::bad_alloc();
    }
    std::memset(raw, 0xa5, guard);
    volatile unsigned char* bytes_before_construction = raw + guard;
    for (std::size_t i = 0; i < capacity; ++i)
    {
        bytes_before_construction[i] = 0x6b;
    }
    std::memset(raw + guard + capacity, 0x5a, guard);
    allocations[allocation_count++] = {
        raw, reinterpret_cast<std::uintptr_t>(raw + guard), bytes, capacity, r.role, true};
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
        necessary(phase == Phase::destruction || phase == Phase::cleanup,
            "parser unexpectedly released an owner");
        if (phase == Phase::destruction)
        {
            necessary(release_count < release_ids.size(), "release ledger full");
            release_ids[release_count++] = i;
        }
        else
        {
            necessary(a.role != Role::frame, "orphan cleanup cannot replace a Frame destructor");
            ++totals.orphan_releases;
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

void begin(const char* family, std::size_t number)
{
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        necessary(!allocations[i].live, "prior owner remained live");
    }
    allocation_count = request_count = request_next = release_count = 0;
    active_stream = nullptr;
    first_error = nullptr;
    std::snprintf(label, sizeof(label), "%s-%zu", family, number);
    ++totals.sessions;
}

void finish(std::size_t expected_orphans)
{
    std::size_t remaining = 0;
    {
        Trace trace(Phase::cleanup);
        for (std::size_t i = 0; i < allocation_count; ++i)
        {
            if (allocations[i].live)
            {
                ++remaining;
                ::operator delete(reinterpret_cast<void*>(allocations[i].address));
            }
        }
    }
    expect(remaining == expected_orphans, "orphan allocation count differs");
    if (first_error)
    {
        fail(first_error);
    }
}

struct Span
{
    std::size_t offset, size;
};
#define MEMBER(T, name)                                                                            \
    Span                                                                                           \
    {                                                                                              \
        offsetof(T, name), sizeof(T::name)                                                         \
    }
constexpr auto record_members = std::to_array<Span>({MEMBER(Record, name),
    MEMBER(Record, reference_count),
    MEMBER(Record, frame_count),
    MEMBER(Record, frames),
    MEMBER(Record, palette_count),
    MEMBER(Record, palettes),
    MEMBER(Record, format_18),
    MEMBER(Record, word_1c),
    MEMBER(Record, unknown_20)});
constexpr auto frame_members = std::to_array<Span>({MEMBER(Frame, name),
    MEMBER(Frame, time_04),
    MEMBER(Frame, loaded_08),
    MEMBER(Frame, unknown_09),
    MEMBER(Frame, count_0a),
    MEMBER(Frame, plane_count_0c),
    MEMBER(Frame, unknown_0d),
    MEMBER(Frame, width_0e),
    MEMBER(Frame, height_10),
    MEMBER(Frame, unknown_12),
    MEMBER(Frame, word_14),
    MEMBER(Frame, index_ready_16),
    MEMBER(Frame, unknown_17),
    MEMBER(Frame, data_size_18),
    MEMBER(Frame, file_offset_1c),
    MEMBER(Frame, plane_offsets_20),
    MEMBER(Frame, offset_count_40),
    MEMBER(Frame, unknown_42),
    MEMBER(Frame, indices_44),
    MEMBER(Frame, whole_data_48),
    MEMBER(Frame, unknown_49),
    MEMBER(Frame, data_4c),
    MEMBER(Frame, plane_data_50),
    MEMBER(Frame, offsets_70),
    MEMBER(Frame, flag_74)});
#undef MEMBER

template <std::size_t N, std::size_t M>
void same_members(const std::array<unsigned char, N>& actual,
    const std::array<unsigned char, N>& expected,
    const std::array<Span, M>& members)
{
    for (const Span& span : members)
    {
        for (std::size_t i = 0; i < span.size; ++i)
        {
            expect(actual[span.offset + i] == expected[span.offset + i],
                "member representation differs");
        }
    }
}

template <std::size_t N, class T>
void put(std::array<unsigned char, N>& bytes, std::size_t offset, const T& value)
{
    necessary(offset + sizeof(value) <= N, "expected member out of bounds");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

RecordBytes constructed_record(RecordBytes initial)
{
#define ZERO(name) put(initial, offsetof(Record, name), decltype(Record::name){})
    ZERO(name);
    ZERO(reference_count);
    ZERO(frame_count);
    ZERO(frames);
    ZERO(palette_count);
    ZERO(palettes);
    ZERO(word_1c);
#undef ZERO
    put(initial, offsetof(Record, format_18), 1U);
    return initial;
}

FrameBytes constructed_frame()
{
    FrameBytes expected;
    expected.fill(0x6b);
#define ZERO(name) put(expected, offsetof(Frame, name), decltype(Frame::name){})
    ZERO(name);
    ZERO(loaded_08);
    ZERO(count_0a);
    ZERO(plane_count_0c);
    ZERO(width_0e);
    ZERO(height_10);
    ZERO(word_14);
    ZERO(index_ready_16);
    ZERO(data_size_18);
    ZERO(file_offset_1c);
    ZERO(offset_count_40);
    ZERO(indices_44);
    ZERO(whole_data_48);
    ZERO(data_4c);
    ZERO(offsets_70);
    ZERO(flag_74);
#undef ZERO
    put(expected, offsetof(Frame, unknown_12), std::array<unsigned char, 2>{});
    for (std::size_t i = 0; i < 8; ++i)
    {
        put(expected, offsetof(Frame, plane_offsets_20) + i * sizeof(unsigned int), 0U);
        put(expected,
            offsetof(Frame, plane_data_50) + i * sizeof(unsigned char*),
            static_cast<unsigned char*>(nullptr));
    }
    return expected;
}

struct Storage
{
    std::array<unsigned char, guard> before;
    alignas(Record) RecordBytes bytes;
    std::array<unsigned char, guard> after;
    Record* object = nullptr;

    Storage()
    {
        before.fill(0x39);
        after.fill(0x93);
        volatile unsigned char* raw = bytes.data();
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            raw[i] = 0x35;
        }
        const RecordBytes expected = constructed_record(bytes);
        {
            Trace trace(Phase::method);
            object = ::new (bytes.data()) Record;
        }
        ++totals.constructors;
        same_members(bytes, expected, record_members);
        object->reference_count = 0x10203040U;
        object->word_1c = 0x11223344U;
        object->unknown_20[0] = 0xde;
        object->unknown_20[1] = 0xad;
        object->unknown_20[2] = 0xbe;
        object->unknown_20[3] = 0xef;
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

unsigned int digit(char c)
{
    necessary((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'), "invalid literal hex digit");
    return c <= '9' ? static_cast<unsigned int>(c - '0') : static_cast<unsigned int>(c - 'a' + 10);
}

std::size_t hex_size(const char* hex)
{
    const std::size_t length = std::strlen(hex);
    necessary(length % 2 == 0, "odd literal byte count");
    return length / 2;
}

void check_bytes(const void* pointer, const char* hex)
{
    const auto* actual = static_cast<const unsigned char*>(pointer);
    const std::size_t count = hex_size(hex);
    for (std::size_t i = 0; i < count; ++i)
    {
        expect(
            actual[i] == ((digit(hex[2 * i]) << 4) | digit(hex[2 * i + 1])), "name byte differs");
    }
}

struct File
{
    std::array<unsigned char, 4096> input{};
    std::size_t size = 0;
    FILE* stream = nullptr;

    void byte(unsigned char value)
    {
        necessary(size < input.size(), "generated file capacity exceeded");
        input[size++] = value;
    }
    void hex(const char* value)
    {
        const std::size_t count = hex_size(value);
        for (std::size_t i = 0; i < count; ++i)
        {
            byte(static_cast<unsigned char>((digit(value[2 * i]) << 4) | digit(value[2 * i + 1])));
        }
    }
    void open(long position)
    {
        stream = std::tmpfile();
        necessary(stream != nullptr, "real tmpfile failed");
        necessary(
            std::fwrite(input.data(), 1, size, stream) == size, "generated file write failed");
        necessary(std::fseek(stream, position, SEEK_SET) == 0, "generated file seek failed");
    }
    void verify_and_close()
    {
        expect(std::ferror(stream) == 0 && std::feof(stream) == 0, "full read set a stream error");
        necessary(std::fseek(stream, 0, SEEK_SET) == 0, "file verification seek failed");
        std::array<unsigned char, 4096> actual{};
        expect(std::fread(actual.data(), 1, size, stream) == size, "file size changed");
        expect(std::memcmp(actual.data(), input.data(), size) == 0, "file contents changed");
        expect(std::fclose(stream) == 0, "caller stream close failed");
        stream = nullptr;
    }
};

struct FramePlan
{
    const char* encoded;
    const char* name;
    unsigned short count, width, height, opaque, offsets;
    unsigned char planes;
    short word;
    unsigned int position;
    int data_size;
};
struct Plan
{
    const char* prefix;
    const char* tail;
    const char* name;
    unsigned int format, surface, classification;
    unsigned int palette_count, palette_words, frame_count;
    bool reverse_second;
    std::array<unsigned int, 2> frame_ids;
    const std::array<unsigned short, 8>* palette_pattern;
    long begin, consumed;
    std::array<unsigned char, 4> opaque;
};

constexpr std::array<std::array<unsigned char, 3>, 8> rgb = {{
    {0, 0, 0},
    {255, 255, 255},
    {248, 0, 0},
    {0, 252, 0},
    {0, 0, 248},
    {0, 4, 0},
    {123, 45, 67},
    {7, 3, 7},
}};
constexpr std::array<unsigned short, 8> words565 = {
    0x0000, 0xffff, 0xf800, 0x07e0, 0x001f, 0x0020, 0x7968, 0x0000};
constexpr std::array<unsigned short, 8> words555 = {
    0x0000, 0x7fff, 0x7c00, 0x03e0, 0x001f, 0x0000, 0x3ca8, 0x0000};

// BEGIN D261 LITERALS
constexpr std::array<FramePlan, 4> frame_plans = {{
    {"000200024001c8003412fdff4030201000f401000400",
        "",
        2,
        320,
        200,
        4660,
        4,
        2,
        -3,
        270544960U,
        128000},
    {"034142000200024001c8003412fdff4030201000f401000400",
        "414200",
        2,
        320,
        200,
        4660,
        4,
        2,
        -3,
        270544960U,
        128000},
    {"034142430200024001c8003412fdff4030201000f401000400",
        "414243",
        2,
        320,
        200,
        4660,
        4,
        2,
        -3,
        270544960U,
        128000},
    {"025a00ffffffffffffffcdab0080ffffffff00000080ffff",
        "5a00",
        65535,
        65535,
        65535,
        43981,
        65535,
        255,
        -32768,
        4294967295U,
        -2147483648},
}};
constexpr std::array<Plan, 38> plans = {{
    {"0a4641545f424c4f4b45000100000000000000",
        "00000000",
        "4641545f424c4f4b4500",
        1U,
        0U,
        1U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        0,
        23,
        {222, 173, 190, 239}},
    {"0a4669745f426c6f4b65000100000000000000",
        "00000000",
        "4669745f426c6f4b6500",
        1U,
        0U,
        2U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        1,
        23,
        {222, 173, 190, 239}},
    {"054761496c000100000000000000",
        "00000000",
        "4761496c00",
        1U,
        0U,
        3U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        2,
        18,
        {222, 173, 190, 239}},
    {"057348615a000100000000000000",
        "00000000",
        "7348615a00",
        1U,
        0U,
        4U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        3,
        18,
        {222, 173, 190, 239}},
    {"08756e6b6e6f776e000100000000000000",
        "00000000",
        "756e6b6e6f776e00",
        1U,
        0U,
        0U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        0,
        21,
        {222, 173, 190, 239}},
    {"01000100000000000000",
        "00000000",
        "00",
        1U,
        0U,
        0U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        1,
        14,
        {222, 173, 190, 239}},
    {"074761496c0080ff0100000000000000",
        "00000000",
        "4761496c0080ff",
        1U,
        0U,
        3U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        2,
        20,
        {222, 173, 190, 239}},
    {"0870616c65747465000100000001000000",
        "00000000",
        "70616c6574746500",
        1U,
        0U,
        0U,
        1,
        256,
        0,
        false,
        {0, 0},
        &words565,
        3,
        789,
        {222, 173, 190, 239}},
    {"0870616c65747465000100000001000000",
        "00000000",
        "70616c6574746500",
        1U,
        1U,
        0U,
        1,
        256,
        0,
        false,
        {0, 0},
        &words555,
        0,
        789,
        {222, 173, 190, 239}},
    {"0870616c65747465000100000001000000",
        "00000000",
        "70616c6574746500",
        1U,
        4294967295U,
        0U,
        1,
        256,
        0,
        false,
        {0, 0},
        &words555,
        1,
        789,
        {222, 173, 190, 239}},
    {"0870616c65747465000200000001000000",
        "00000000",
        "70616c6574746500",
        2U,
        0U,
        0U,
        1,
        256,
        0,
        false,
        {0, 0},
        &words565,
        2,
        789,
        {222, 173, 190, 239}},
    {"0870616c65747465000200000001000000",
        "00000000",
        "70616c6574746500",
        2U,
        1U,
        0U,
        1,
        256,
        0,
        false,
        {0, 0},
        &words555,
        3,
        789,
        {222, 173, 190, 239}},
    {"0870616c65747465000200000001000000",
        "00000000",
        "70616c6574746500",
        2U,
        4294967295U,
        0U,
        1,
        256,
        0,
        false,
        {0, 0},
        &words555,
        0,
        789,
        {222, 173, 190, 239}},
    {"0870616c65747465000300000001000000",
        "00000000",
        "70616c6574746500",
        3U,
        0U,
        0U,
        1,
        64,
        0,
        false,
        {0, 0},
        &words565,
        1,
        789,
        {222, 173, 190, 239}},
    {"0870616c65747465000300000001000000",
        "00000000",
        "70616c6574746500",
        3U,
        1U,
        0U,
        1,
        64,
        0,
        false,
        {0, 0},
        &words555,
        2,
        789,
        {222, 173, 190, 239}},
    {"0870616c65747465000300000001000000",
        "00000000",
        "70616c6574746500",
        3U,
        4294967295U,
        0U,
        1,
        64,
        0,
        false,
        {0, 0},
        &words555,
        3,
        789,
        {222, 173, 190, 239}},
    {"0870616c657474650005000000007f80ff01000000",
        "00000000",
        "70616c6574746500",
        5U,
        0U,
        0U,
        1,
        256,
        0,
        false,
        {0, 0},
        &words565,
        0,
        793,
        {0, 127, 128, 255}},
    {"0870616c657474650005000000007f80ff01000000",
        "00000000",
        "70616c6574746500",
        5U,
        1U,
        0U,
        1,
        256,
        0,
        false,
        {0, 0},
        &words555,
        1,
        793,
        {0, 127, 128, 255}},
    {"0870616c657474650005000000007f80ff01000000",
        "00000000",
        "70616c6574746500",
        5U,
        4294967295U,
        0U,
        1,
        256,
        0,
        false,
        {0, 0},
        &words555,
        2,
        793,
        {0, 127, 128, 255}},
    {"0870616c65747465000600000001000000",
        "00000000",
        "70616c6574746500",
        6U,
        0U,
        0U,
        1,
        64,
        0,
        false,
        {0, 0},
        &words565,
        3,
        789,
        {222, 173, 190, 239}},
    {"0870616c65747465000600000001000000",
        "00000000",
        "70616c6574746500",
        6U,
        1U,
        0U,
        1,
        64,
        0,
        false,
        {0, 0},
        &words555,
        0,
        789,
        {222, 173, 190, 239}},
    {"0870616c65747465000600000001000000",
        "00000000",
        "70616c6574746500",
        6U,
        4294967295U,
        0U,
        1,
        64,
        0,
        false,
        {0, 0},
        &words555,
        1,
        789,
        {222, 173, 190, 239}},
    {"0870616c65747465000b00000001000000",
        "00000000",
        "70616c6574746500",
        11U,
        0U,
        0U,
        1,
        256,
        0,
        false,
        {0, 0},
        &words565,
        2,
        789,
        {222, 173, 190, 239}},
    {"0870616c65747465000b00000001000000",
        "00000000",
        "70616c6574746500",
        11U,
        1U,
        0U,
        1,
        256,
        0,
        false,
        {0, 0},
        &words555,
        3,
        789,
        {222, 173, 190, 239}},
    {"0870616c65747465000b00000001000000",
        "00000000",
        "70616c6574746500",
        11U,
        4294967295U,
        0U,
        1,
        256,
        0,
        false,
        {0, 0},
        &words555,
        0,
        789,
        {222, 173, 190, 239}},
    {"027a000000000000000000",
        "00000000",
        "7a00",
        0U,
        0U,
        0U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        1,
        15,
        {222, 173, 190, 239}},
    {"027a000400000000000000",
        "00000000",
        "7a00",
        4U,
        0U,
        0U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        2,
        15,
        {222, 173, 190, 239}},
    {"027a000700000000000000",
        "00000000",
        "7a00",
        7U,
        0U,
        0U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        3,
        15,
        {222, 173, 190, 239}},
    {"027a000800000000000000",
        "00000000",
        "7a00",
        8U,
        0U,
        0U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        0,
        15,
        {222, 173, 190, 239}},
    {"027a000900000000000000",
        "00000000",
        "7a00",
        9U,
        0U,
        0U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        1,
        15,
        {222, 173, 190, 239}},
    {"027a000a00000000000000",
        "00000000",
        "7a00",
        10U,
        0U,
        0U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        2,
        15,
        {222, 173, 190, 239}},
    {"027a000c00000000000000",
        "00000000",
        "7a00",
        12U,
        0U,
        0U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        3,
        15,
        {222, 173, 190, 239}},
    {"027a00ffffffff00000000",
        "00000000",
        "7a00",
        4294967295U,
        0U,
        0U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words565,
        0,
        15,
        {222, 173, 190, 239}},
    {"027a0005000000007f80ff00000000",
        "00000000",
        "7a00",
        5U,
        1U,
        0U,
        0,
        0,
        0,
        false,
        {0, 0},
        &words555,
        1,
        19,
        {0, 127, 128, 255}},
    {"0870616c657474650005000000007f80ff02000000",
        "00000000",
        "70616c6574746500",
        5U,
        0U,
        0U,
        2,
        256,
        0,
        true,
        {0, 0},
        &words565,
        2,
        1561,
        {0, 127, 128, 255}},
    {"077265636f7264000100000000000000",
        "01000000000200024001c8003412fdff4030201000f401000400",
        "7265636f726400",
        1U,
        0U,
        0U,
        0,
        0,
        1,
        false,
        {0, 0},
        &words565,
        3,
        42,
        {222, 173, 190, 239}},
    {"077265636f7264000200000000000000",
        "02000000034142000200024001c8003412fdff4030201000f401000400025a00ffffffffffffffcdab0080ffff"
        "ffff00000080ffff",
        "7265636f726400",
        2U,
        4294967295U,
        0U,
        0,
        0,
        2,
        false,
        {1, 3},
        &words555,
        0,
        69,
        {222, 173, 190, 239}},
    {"077265636f7264000300000001000000",
        "01000000034142430200024001c8003412fdff4030201000f401000400",
        "7265636f726400",
        3U,
        0U,
        0U,
        1,
        64,
        1,
        false,
        {2, 0},
        &words565,
        1,
        813,
        {222, 173, 190, 239}},
}};
// END D261 LITERALS

void append_record(File& file, const Plan& p)
{
    const std::size_t initial = file.size;
    file.hex(p.prefix);
    for (unsigned int palette = 0; palette < p.palette_count; ++palette)
    {
        for (unsigned int i = 0; i < p.palette_words; ++i)
        {
            const auto& value = rgb[p.reverse_second && palette == 1 ? 7 - i % 8 : i % 8];
            for (unsigned char byte : value)
            {
                file.byte(byte);
            }
        }
        for (unsigned int i = p.palette_words; i < 256; ++i)
        {
            file.byte(17);
            file.byte(34);
            file.byte(51);
        }
    }
    file.hex(p.tail);
    necessary(file.size - initial == static_cast<std::size_t>(p.consumed),
        "literal record length differs");
}

struct Graph
{
    std::size_t name, palette_table, frame_table;
    std::array<std::size_t, 2> palettes{}, frames{}, frame_names{};
};

std::size_t request(Role role, std::size_t bytes, long position)
{
    necessary(request_count < requests.size(), "request protocol capacity");
    const std::size_t id = allocation_count + request_count;
    requests[request_count++] = {role, bytes, position};
    return id;
}

Graph protocol(const Plan& p, long begin)
{
    request_count = request_next = 0;
    Graph g{};
    g.name = request(Role::record_name, hex_size(p.name), begin + 1);
    const long palette_begin = begin + static_cast<long>(hex_size(p.prefix));
    g.palette_table =
        request(Role::palette_table, p.palette_count * sizeof(unsigned short*), palette_begin);
    for (unsigned int i = 0; i < p.palette_count; ++i)
    {
        g.palettes[i] = request(Role::palette, 2 * p.palette_words, palette_begin + 768 * i);
    }
    long frame_begin = palette_begin + 768 * p.palette_count + 4;
    g.frame_table = request(Role::frame_table, p.frame_count * sizeof(Frame*), frame_begin);
    for (unsigned int i = 0; i < p.frame_count; ++i)
    {
        const FramePlan& f = frame_plans[p.frame_ids[i]];
        g.frames[i] = request(Role::frame, sizeof(Frame), frame_begin);
        g.frame_names[i] = request(Role::frame_name, hex_size(f.name), frame_begin + 1);
        frame_begin += static_cast<long>(hex_size(f.encoded));
    }
    necessary(frame_begin == begin + p.consumed, "allocation cursor plan differs");
    return g;
}

void* live(std::size_t id)
{
    necessary(id < allocation_count && allocations[id].live, "expected owner is absent");
    return reinterpret_cast<void*>(allocations[id].address);
}

void check_frame(const FramePlan& p, Frame* object, std::size_t id, std::size_t name_id)
{
    necessary(object == live(id), "actual Frame owner differs");
    necessary(object->name == live(name_id), "actual Frame name owner differs");
    FrameBytes expected = constructed_frame();
    put(expected, offsetof(Frame, name), static_cast<const char*>(live(name_id)));
    put(expected, offsetof(Frame, count_0a), p.count);
    put(expected, offsetof(Frame, plane_count_0c), p.planes);
    put(expected, offsetof(Frame, width_0e), p.width);
    put(expected, offsetof(Frame, height_10), p.height);
    put(expected,
        offsetof(Frame, unknown_12),
        std::array<unsigned char, 2>{
            static_cast<unsigned char>(p.opaque), static_cast<unsigned char>(p.opaque >> 8)});
    put(expected, offsetof(Frame, word_14), p.word);
    put(expected, offsetof(Frame, file_offset_1c), p.position);
    put(expected, offsetof(Frame, data_size_18), p.data_size);
    put(expected, offsetof(Frame, offset_count_40), p.offsets);
    FrameBytes actual;
    std::memcpy(actual.data(), object, sizeof(Frame));
    same_members(actual, expected, frame_members);
    check_bytes(object->name, p.name);
    ++totals.frames;
}

Graph call(Storage& storage, File& file, const Plan& p, long begin)
{
    Record& r = *storage.object;
    RecordBytes expected = storage.bytes;
    const std::size_t released = totals.releases;
    const Graph g = protocol(p, begin);
    active_stream = file.stream;
    {
        Trace trace(Phase::method);
        r.ReadMetadata(file.stream, p.surface);
    }
    active_stream = nullptr;
    ++totals.calls;
    expect(request_next == request_count, "scalar allocation protocol incomplete");
    expect(totals.releases == released, "parser repaired an original leak");
    const long final_position = std::ftell(file.stream);
    expect(final_position == begin + p.consumed, "parser stream position differs");
    necessary(final_position >= begin, "stream position moved backwards");
    totals.consumed += static_cast<std::size_t>(final_position - begin);
    expect(r.word_1c == p.classification, "record classification differs");
    necessary(r.name == live(g.name), "record name owner differs");
    necessary(r.palettes == live(g.palette_table), "palette table owner differs");
    necessary(r.frames == live(g.frame_table), "frame table owner differs");
    put(expected, offsetof(Record, name), static_cast<const char*>(live(g.name)));
    put(expected, offsetof(Record, palette_count), static_cast<int>(p.palette_count));
    put(expected, offsetof(Record, palettes), static_cast<unsigned short**>(live(g.palette_table)));
    put(expected, offsetof(Record, frame_count), static_cast<int>(p.frame_count));
    put(expected, offsetof(Record, frames), static_cast<Frame**>(live(g.frame_table)));
    put(expected, offsetof(Record, format_18), p.format);
    put(expected, offsetof(Record, word_1c), p.classification);
    put(expected, offsetof(Record, unknown_20), p.opaque);
    same_members(storage.bytes, expected, record_members);
    check_bytes(r.name, p.name);
    for (unsigned int palette = 0; palette < p.palette_count; ++palette)
    {
        necessary(r.palettes[palette] == live(g.palettes[palette]), "palette owner differs");
        for (unsigned int i = 0; i < p.palette_words; ++i)
        {
            const std::size_t index = p.reverse_second && palette == 1 ? 7 - i % 8 : i % 8;
            expect(r.palettes[palette][i] == (*p.palette_pattern)[index], "palette word differs");
        }
        ++totals.palettes;
    }
    for (unsigned int frame = 0; frame < p.frame_count; ++frame)
    {
        check_frame(frame_plans[p.frame_ids[frame]],
            r.frames[frame],
            g.frames[frame],
            g.frame_names[frame]);
    }
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        if (allocations[i].live)
        {
            allocation_guards(allocations[i]);
        }
    }
    storage.guards();
    return g;
}

void destroy(Storage& storage, const Plan& p, const Graph& g)
{
    std::array<std::size_t, 16> order{};
    std::size_t count = 0;
    order[count++] = g.name;
    for (unsigned int i = 0; i < p.palette_count; ++i)
    {
        order[count++] = g.palettes[i];
    }
    order[count++] = g.palette_table;
    for (unsigned int i = 0; i < p.frame_count; ++i)
    {
        order[count++] = g.frame_names[i];
        order[count++] = g.frames[i];
    }
    order[count++] = g.frame_table;
    const RecordBytes before = storage.bytes;
    storage.destroy();
    expect(release_count == count, "Record destructor release count differs");
    for (std::size_t i = 0; i < count && i < release_count; ++i)
    {
        expect(release_ids[i] == order[i], "Record destructor release order differs");
    }
    same_members(storage.bytes, before, record_members); // Raw retained storage only.
    storage.guards();
}

void independent_cases()
{
    constexpr std::array<unsigned char, 3> prefix = {0x91, 0x2b, 0xa5};
    for (std::size_t i = 0; i < plans.size(); ++i)
    {
        begin("case", i);
        const Plan& p = plans[i];
        File file;
        for (long n = 0; n < p.begin; ++n)
        {
            file.byte(prefix[static_cast<std::size_t>(n)]);
        }
        append_record(file, p);
        file.hex("8100ff2a");
        file.open(p.begin);
        Storage storage;
        const Graph g = call(storage, file, p, p.begin);
        destroy(storage, p, g);
        file.verify_and_close();
        finish(0);
    }
}

void repeated_call()
{
    begin("replacement", 0);
    File file;
    append_record(file, plans[0]);
    append_record(file, plans[1]);
    file.hex("8100ff2a");
    file.open(0);
    Storage storage;
    const Graph first = call(storage, file, plans[0], 0);
    const Graph second = call(storage, file, plans[1], 23);
    expect(allocations[first.name].live && allocations[first.palette_table].live &&
               allocations[first.frame_table].live,
        "replacement lost an overwritten owner");
    check_bytes(live(first.name), plans[0].name);
    destroy(storage, plans[1], second);
    expect(allocations[first.name].live && allocations[first.palette_table].live &&
               allocations[first.frame_table].live,
        "destructor freed an overwritten owner");
    file.verify_and_close();
    finish(3); // Only saved first-call receipts; no dead Record or table access.
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
    necessary(std::setlocale(LC_ALL, "C") != nullptr, "C locale unavailable");
    independent_cases();
    repeated_call();
    necessary(totals.sessions == 39 && totals.constructors == 39 && totals.destructors == 39,
        "record lifetime totals");
    necessary(
        totals.calls == 40 && totals.frames == 4 && totals.palettes == 21, "parser output totals");
    necessary(totals.consumed == 17021 && totals.allocations == 149 && totals.releases == 149 &&
                  totals.orphan_releases == 3,
        "ownership/stream totals");
    std::printf("{\"sessions\":%zu,\"constructors\":%zu,\"destructors\":%zu,\"parser_calls\":%zu,"
                "\"frames\":%zu,\"palettes\":%zu,\"consumed\":%zu,\"allocations\":%zu,"
                "\"releases\":%zu,\"orphan_releases\":%zu,\"checks\":%zu}\n",
        totals.sessions,
        totals.constructors,
        totals.destructors,
        totals.calls,
        totals.frames,
        totals.palettes,
        totals.consumed,
        totals.allocations,
        totals.releases,
        totals.orphan_releases,
        totals.checks);
}
